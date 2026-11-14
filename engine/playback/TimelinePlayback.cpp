#include "playback/TimelinePlayback.h"

#include "playback/AutoreleasePool.h"

#include "model/TimelineQuery.h"

#include <dispatch/dispatch.h>

#include <algorithm>
#include <functional>
#include <set>

namespace tve {

TimelinePlayback::~TimelinePlayback()
{
    stopProducer();
}

void TimelinePlayback::setProject(std::shared_ptr<const Project> project, std::vector<RenderedStretch> rendered)
{
    std::lock_guard lock(m_control);
    m_project = std::move(project);
    m_rendered = std::make_shared<const std::vector<RenderedStretch>>(std::move(rendered));
    restartAt(m_clock.now());
}

void TimelinePlayback::seek(Ticks time)
{
    std::lock_guard lock(m_control);
    restartAt(std::max<Ticks>(time, 0));
}

Ticks TimelinePlayback::duration() const
{
    std::lock_guard lock(m_control);
    return m_project ? m_project->duration() : 0;
}

void TimelinePlayback::stopProducer()
{
    m_stop = true;
    m_queue.stop();
    if (m_producer.joinable())
        m_producer.join();
}

void TimelinePlayback::restartAt(Ticks time)
{
    stopProducer();
    m_queue.restart();
    m_clock.seek(time);
    m_stop = false;
    m_finished = false;
    if (m_project)
        m_producer = std::thread([this, project = m_project, rendered = m_rendered, time] { run(project, rendered, time); });
    m_audio->restart(m_project, time);
}

TimelinePlayback::Size TimelinePlayback::canvasSize() const
{
    std::lock_guard lock(m_control);
    if (!m_project)
        return {1920, 1080};
    return {m_project->settings.width, m_project->settings.height};
}

bool TimelinePlayback::openDecoder(LayerState& state, const MediaItem& media)
{
    // The proxy when there is one: the same frames at the same times, cheaper to decode.
    const std::string& path = media.proxyPath.empty() ? media.path : media.proxyPath;
    if (state.decoder && state.path == path)
        return true;
    state = LayerState{VideoDecoder::open(path), path, std::nullopt, std::nullopt, {}};
    return state.decoder != nullptr; // missing or unreadable media is left out (shows what is below)
}

const DecodedFrame* TimelinePlayback::reversedFrameAt(LayerState& state, const MediaItem& media, Ticks sourceTime)
{
    if (!openDecoder(state, media))
        return nullptr;
    constexpr std::size_t kMaxFrames = 32; // bounds memory for 4K sources (~400 MB)
    const Ticks frameTicks = frameDuration(state.decoder->info().frameRate);
    const bool covered = !state.window.empty() && state.window.front().pts() <= sourceTime
                      && sourceTime < state.window.back().pts() + frameTicks;
    if (!covered) {
        // Decode from the keyframe before the wanted time up to it, keeping the last
        // kMaxFrames. Playing backwards, each group of pictures is decoded once and then
        // served from this window until the playhead passes its start.
        state.window.clear();
        // From one frame earlier, so a wanted frame that is itself a keyframe brings the
        // group before it along instead of making a window of one.
        if (!state.decoder->seekToKeyframe(sourceTime - frameTicks))
            return nullptr;
        while (auto frame = state.decoder->nextFrame()) {
            ++m_counters.decoded;
            if (frame->pts() > sourceTime && !state.window.empty())
                break;
            state.window.push_back(std::move(*frame));
            if (state.window.size() > kMaxFrames)
                state.window.pop_front();
            if (state.window.back().pts() > sourceTime)
                break;
        }
    }
    if (state.window.empty())
        return nullptr;
    m_hardware = state.decoder->info().hardwareAccelerated;
    // The last frame starting at or before the wanted time.
    for (auto it = state.window.rbegin(); it != state.window.rend(); ++it) {
        if (it->pts() <= sourceTime)
            return &*it;
    }
    return &state.window.front();
}

const DecodedFrame* TimelinePlayback::frameAt(LayerState& state, const MediaItem& media, Ticks sourceTime)
{
    if (!openDecoder(state, media))
        return nullptr;
    // Seek when going backwards, or when jumping ahead past a keyframe (a seek decodes from
    // there, which is less work); otherwise decode forward. Small steps (fast play) always
    // decode forward. Without an index, seek when more than a second ahead.
    constexpr Ticks kMaxDecodeAhead = kTicksPerSecond;
    const Ticks kMinSeekGap = 4 * frameDuration(state.decoder->info().frameRate);
    const bool behind = !state.current || sourceTime < state.current->pts();
    bool farAhead = false;
    if (state.current && sourceTime - state.current->pts() > kMinSeekGap) {
        const auto keyframe = state.decoder->keyframeBefore(sourceTime);
        farAhead = keyframe ? *keyframe > state.current->pts() : sourceTime - state.current->pts() > kMaxDecodeAhead;
    }
    if (behind || farAhead) {
        if (!state.decoder->seek(sourceTime))
            return nullptr;
        state.current = state.decoder->nextFrame();
        state.next = state.decoder->nextFrame();
    }
    while (state.next && state.next->pts() <= sourceTime) {
        state.current = std::move(state.next);
        state.next = state.decoder->nextFrame();
        if (state.next)
            ++m_counters.decoded;
    }
    if (state.current)
        m_hardware = state.decoder->info().hardwareAccelerated;
    return state.current ? &*state.current : nullptr;
}

const DecodedFrame* TimelinePlayback::pictureOf(LayerState& state, const Project& project, const Clip& clip,
                                                Ticks sourceTime)
{
    const MediaItem* media = project.findMedia(clip.mediaId);
    if (!media)
        return nullptr;
    // Its processed copy where it covers this time (its own time starts at `from`); the
    // media's pictures where it does not, or if it cannot be read.
    if (const auto& processed = clip.processedVideo;
        processed && sourceTime >= processed->from && sourceTime < processed->to) {
        MediaItem copy;
        copy.path = processed->path;
        const Ticks at = sourceTime - processed->from;
        if (const DecodedFrame* frame = clip.reversed ? reversedFrameAt(state, copy, at) : frameAt(state, copy, at))
            return frame;
    }
    return clip.reversed ? reversedFrameAt(state, *media, sourceTime) : frameAt(state, *media, sourceTime);
}

void TimelinePlayback::prefetch(const Project& project, Ticks time)
{
    // The decoded pictures buildLayers will want: each visible clip's (both of a
    // transition's), inside compound clips too.
    struct Job {
        const Clip* clip;
        Ticks sourceTime;
        LayerState* state = nullptr;
    };
    std::vector<Job> jobs;
    const std::function<void(const std::vector<Track>&, Ticks, int)> collect = [&](const std::vector<Track>& tracks,
                                                                                   Ticks at, int depth) {
        const auto add = [&](const Clip& clip, Ticks sourceTime) {
            const MediaItem* media = project.findMedia(clip.mediaId);
            if (media && !media->still && !clip.text && !clip.drawing && !clip.adjustment)
                jobs.push_back({&clip, sourceTime});
        };
        const auto side = [&](const Clip& clip, Ticks sourceTime) {
            if (!clip.compound()) {
                add(clip, sourceTime);
                return;
            }
            if (const Sequence* sequence = project.findSequence(clip.sequenceId); sequence && depth < kMaxNesting)
                collect(sequence->tracks, sourceTime, depth + 1);
        };
        for (const VisibleClip& layer : visibleVideoLayersAt(project, tracks, at)) {
            side(*layer.clip, layer.sourceTime);
            if (layer.transition)
                side(*layer.transition->to, layer.transition->toSourceTime);
        }
    };
    collect(project.tracks, time, 0);
    if (!m_parallelDecoding || jobs.size() < 2)
        return; // one layer: nothing to overlap
    // Their states made here, first: the decoding below only touches its own.
    for (Job& job : jobs)
        job.state = &m_layers[job.clip->id];
    struct Context {
        TimelinePlayback* self;
        const Project* project;
        std::vector<Job>* jobs;
    } context{this, &project, &jobs};
    dispatch_apply_f(jobs.size(), dispatch_get_global_queue(QOS_CLASS_USER_INTERACTIVE, 0), &context,
                     [](void* data, std::size_t index) {
                         auto* c = static_cast<Context*>(data);
                         Job& job = (*c->jobs)[index];
                         c->self->pictureOf(*job.state, *c->project, *job.clip, job.sourceTime);
                     });
}

void TimelinePlayback::buildLayers(const Project& project, const std::vector<Track>& tracks, Ticks time,
                                   std::vector<VideoLayer>& out, int depth, std::set<Id>& live)
{
    const std::size_t firstNew = out.size();
    buildLayersOf(project, tracks, time, out, depth, live);
    // Motion blur: a moving clip's placements across the shutter, drawn and averaged.
    const Ticks frame = frameDuration(project.settings.frameRate);
    for (std::size_t i = firstNew; i < out.size(); ++i) {
        VideoLayer& layer = out[i];
        if (layer.transition || layer.adjustment)
            continue;
        const Clip* clip = nullptr;
        for (const Track& track : tracks)
            if (!clip)
                clip = track.findClip(layer.clipId);
        if (clip)
            layer.blurVisuals = motionBlurVisuals(*clip, time, frame);
    }
}

void TimelinePlayback::buildLayersOf(const Project& project, const std::vector<Track>& tracks, Ticks time,
                                     std::vector<VideoLayer>& out, int depth, std::set<Id>& live)
{
    const auto pictureOf = [&](const Clip& clip, Ticks sourceTime) {
        return this->pictureOf(m_layers[clip.id], project, clip, sourceTime);
    };
    const auto lookOf = [](const Clip& clip) { return clip.look ? clip.look->effective() : ColorGrade{}; };
    const auto motionOf = [](const Clip& clip, Ticks sourceTime) {
        LayerMotion m;
        m.sourceTime = sourceTime;
        m.denoise = float(clip.denoise);
        if (clip.stabilization.enabled && !clip.stabilization.samples.empty()) {
            const SteadySample s = clip.stabilization.at(sourceTime);
            m.steady = true;
            m.zoom = float(clip.stabilization.zoom);
            m.dx = float(s.dx);
            m.dy = float(s.dy);
            m.angle = float(s.angle);
            m.scale = float(s.scale);
        }
        if (clip.deflicker.enabled)
            m.gain = clip.deflicker.gainAt(sourceTime);
        return m;
    };
    const auto cursorOf = [](const Clip& clip, Ticks sourceTime) {
        LayerCursor c;
        const CursorEffects& e = clip.cursor;
        if (!e.shows())
            return c;
        const auto at = e.at(sourceTime);
        c.show = true;
        c.x = float(at->x);
        c.y = float(at->y);
        c.size = float(e.size);
        c.highlight = e.highlight;
        c.spotlight = e.spotlight;
        if (e.rings)
            if (const auto since = e.sinceClick(sourceTime, kClickRingSeconds))
                c.ring = float(*since / kClickRingSeconds);
        return c;
    };
    const auto peopleOf = [](const Clip& clip, Ticks sourceTime) {
        LayerPeople people{clip.cutout, std::nullopt, clip.faceMosaic.boxesAt(sourceTime), clip.faceMosaic.style,
                           float(clip.faceMosaic.amount)};
        people.cutout.subject.clear(); // (the box at this frame is enough)
        people.beauty = clip.beauty;
        people.night = clip.night;
        people.depth = clip.depthOfField;
        if (clip.cutout.enabled)
            people.subject = clip.cutout.subjectAt(sourceTime);
        return people;
    };
    for (const VisibleClip& layer : visibleVideoLayersAt(project, tracks, time)) {
        const Clip& top = *layer.clip;
        live.insert(top.id);
        if (layer.transition)
            live.insert(layer.transition->to->id);
        const double clipSeconds = toSeconds(time - top.start);
        const double clipDuration = toSeconds(top.duration);
        if (top.adjustment) {
            out.push_back(VideoLayer{top.id, DecodedFrame{}, top.visualAt(time), top.effectsAt(time), std::nullopt,
                                     std::nullopt, top.color, lookOf(top), true, {}, ChromaKey{}, {}, MatteMode::None,
                                     clipSeconds, clipDuration, nullptr, {}, {}, {}, {}});
            continue;
        }
        if (top.text) {
            out.push_back(VideoLayer{top.id, DecodedFrame{}, top.visualAt(time), top.effectsAt(time), std::nullopt,
                                     top.text, ColorGrade{}, ColorGrade{}, false, {}, ChromaKey{}, top.masks,
                                     top.matte, // titles are not graded
                                     clipSeconds, clipDuration, nullptr, {}, {}, {}, {}});
            continue;
        }
        if (top.drawing) {
            out.push_back(VideoLayer{top.id, DecodedFrame{}, top.visualAt(time), top.effectsAt(time), std::nullopt,
                                     std::nullopt, top.color, lookOf(top), false, {}, ChromaKey{}, top.masks, top.matte,
                                     clipSeconds, clipDuration, nullptr, {}, {}, {}, top.drawing});
            continue;
        }
        // Each side is a decoded frame, a still image's path (nothing to decode), or a compound
        // clip's sequence at the time it plays, as one picture.
        struct Side {
            const Clip* clip = nullptr;
            const DecodedFrame* frame = nullptr;
            std::string stillPath;
            std::shared_ptr<CompositeFrame> nested;
            bool shown() const { return frame || !stillPath.empty() || nested; }
            DecodedFrame picture() const { return frame ? frame->share() : DecodedFrame{}; }
        };
        const auto sideOf = [&](const Clip& clip, Ticks sourceTime) {
            Side side{&clip, nullptr, {}, nullptr};
            if (clip.compound()) {
                side.nested = std::make_shared<CompositeFrame>(CompositeFrame{sourceTime, {}});
                if (const Sequence* sequence = project.findSequence(clip.sequenceId); sequence && depth < kMaxNesting)
                    buildLayers(project, sequence->tracks, sourceTime, side.nested->layers, depth + 1, live);
                return side;
            }
            const MediaItem* media = project.findMedia(clip.mediaId);
            if (media && media->still)
                side.stillPath = media->path;
            else
                side.frame = pictureOf(clip, sourceTime);
            return side;
        };
        if (top.compound() && !layer.transition) {
            Side side = sideOf(top, layer.sourceTime);
            out.push_back(VideoLayer{top.id, DecodedFrame{}, top.visualAt(time), top.effectsAt(time), std::nullopt,
                                     std::nullopt, top.color, lookOf(top), false, {}, top.chromaKey, top.masks,
                                     top.matte, clipSeconds, clipDuration, std::move(side.nested), {}, {}, {}, {}});
            continue;
        }
        const auto spherical = [&](const Clip& clip) {
            const MediaItem* media = project.findMedia(clip.mediaId);
            return media && media->spherical;
        };
        const Side outgoing = sideOf(top, layer.sourceTime);
        const Side incoming = layer.transition ? sideOf(*layer.transition->to, layer.transition->toSourceTime) : Side{};
        if (outgoing.shown() && incoming.shown()) {
            const TransitionLayer& t = *layer.transition;
            out.push_back(VideoLayer{
                top.id, outgoing.picture(), top.visualAt(time), top.effectsAt(time),
                LayerTransition{t.spec->transitionId, t.spec->values, float(t.progress), t.to->id, incoming.picture(),
                                t.to->visualAt(time), t.to->effectsAt(time), t.to->color, lookOf(*t.to),
                                incoming.stillPath, t.to->chromaKey, t.to->masks, toSeconds(time - t.to->start),
                                peopleOf(*t.to, t.toSourceTime), motionOf(*t.to, t.toSourceTime),
                                cursorOf(*t.to, t.toSourceTime), spherical(*t.to), incoming.nested},
                std::nullopt, top.color, lookOf(top), false, outgoing.stillPath, top.chromaKey, top.masks,
                MatteMode::None, // no matte during a transition
                clipSeconds, clipDuration, outgoing.nested, peopleOf(top, layer.sourceTime), motionOf(top, layer.sourceTime),
                cursorOf(top, layer.sourceTime), {}});
            out.back().spherical = spherical(top);
        } else if (outgoing.shown() || incoming.shown()) {
            // One side unreadable: show the other alone.
            const Side& side = outgoing.shown() ? outgoing : incoming;
            const Clip& clip = *side.clip;
            out.push_back(VideoLayer{clip.id, side.picture(), clip.visualAt(time), clip.effectsAt(time), std::nullopt,
                                     std::nullopt, clip.color, lookOf(clip), false, side.stillPath, clip.chromaKey,
                                     clip.masks, clip.matte, toSeconds(time - clip.start), toSeconds(clip.duration), side.nested,
                                     peopleOf(clip, &side == &outgoing ? layer.sourceTime : layer.transition->toSourceTime),
                                     motionOf(clip, &side == &outgoing ? layer.sourceTime : layer.transition->toSourceTime),
                                     cursorOf(clip, &side == &outgoing ? layer.sourceTime : layer.transition->toSourceTime), {}});
            out.back().spherical = spherical(clip);
        }
    }
}

bool TimelinePlayback::renderedLayer(const RenderedStretch& stretch, std::size_t index, Ticks time, CompositeFrame& out)
{
    MediaItem file;
    file.path = stretch.path;
    // (Its own state, apart from the clips': ids count down from the last one.)
    LayerState& state = m_layers[kRenderedLayerId - Id(index)];
    const DecodedFrame* frame = frameAt(state, file, time - stretch.from);
    if (!frame)
        return false;
    VideoLayer layer;
    layer.clipId = kRenderedLayerId - Id(index);
    layer.frame = frame->share();
    out.layers.push_back(std::move(layer));
    return true;
}

void TimelinePlayback::run(std::shared_ptr<const Project> project,
                           std::shared_ptr<const std::vector<RenderedStretch>> rendered, Ticks start)
{
    const Ticks end = project->duration();
    const Ticks frameTicks = frameDuration(project->settings.frameRate);
    Ticks time = start;

    while (!m_stop && time < end) {
        const AutoreleasePool pool; // what decoding this frame autoreleased goes with it
        CompositeFrame composite{time, {}, project->settings.colour, project->settings.spherical};
        std::set<Id> live;
        // A rendered stretch: its file's picture; else the layers, composited.
        bool fromRender = false;
        for (std::size_t i = 0; rendered && i < rendered->size() && !fromRender; ++i) {
            const RenderedStretch& stretch = (*rendered)[i];
            if (time >= stretch.from && time < stretch.to && renderedLayer(stretch, i, time, composite)) {
                fromRender = true;
                live.insert(kRenderedLayerId - Id(i));
            }
        }
        if (!fromRender) {
            prefetch(*project, time);
            buildLayers(*project, project->tracks, time, composite.layers, 0, live);
        }
        for (VideoLayer& layer : composite.layers)
            m_ofx.apply(layer);
        const bool visible = !composite.layers.empty() || !live.empty();
        if (!m_queue.push(std::move(composite)))
            return;

        if (!visible) {
            // Gap: the black frame stays up until the next clip starts.
            const auto next = nextVideoClipStart(*project, time);
            if (!next)
                break;
            time = *next;
        } else {
            time = (time / frameTicks + 1) * frameTicks; // next point on the project's frame grid
        }
        // Behind the clock (a slow start, a costly stretch such as fast reverse): drop the
        // stale frames, which the presenter would skip anyway, and restart a little ahead so
        // the next frames are ready in time instead of every one arriving late.
        if (m_clock.playing()) {
            const Ticks now = m_clock.now();
            if (time < now - kTicksPerSecond / 10)
                time = ((now + kTicksPerSecond / 5) / frameTicks) * frameTicks;
        }

        // Release decoders of clips that are no longer on screen (inside compound clips too).
        std::erase_if(m_layers, [&](const auto& entry) { return !live.contains(entry.first); });
        m_ofx.keepOnly(live);
    }
    m_finished = true;
}

} // namespace tve
