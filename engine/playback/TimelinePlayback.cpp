#include "playback/TimelinePlayback.h"

#include "model/TimelineQuery.h"

#include <algorithm>
#include <set>

namespace tve {

TimelinePlayback::~TimelinePlayback()
{
    stopProducer();
}

void TimelinePlayback::setProject(std::shared_ptr<const Project> project)
{
    std::lock_guard lock(m_control);
    m_project = std::move(project);
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
        m_producer = std::thread([this, project = m_project, time] { run(project, time); });
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

void TimelinePlayback::buildLayers(const Project& project, const std::vector<Track>& tracks, Ticks time,
                                   std::vector<VideoLayer>& out, int depth, std::set<Id>& live)
{
    const auto pictureOf = [&](const Clip& clip, Ticks sourceTime) -> const DecodedFrame* {
        const MediaItem* media = project.findMedia(clip.mediaId);
        if (!media)
            return nullptr;
        LayerState& state = m_layers[clip.id];
        return clip.reversed ? reversedFrameAt(state, *media, sourceTime) : frameAt(state, *media, sourceTime);
    };
    const auto lookOf = [](const Clip& clip) { return clip.look ? clip.look->effective() : ColorGrade{}; };
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
                                     clipSeconds, clipDuration, nullptr});
            continue;
        }
        if (top.text) {
            out.push_back(VideoLayer{top.id, DecodedFrame{}, top.visualAt(time), top.effectsAt(time), std::nullopt,
                                     top.text, ColorGrade{}, ColorGrade{}, false, {}, ChromaKey{}, top.masks,
                                     top.matte, // titles are not graded
                                     clipSeconds, clipDuration, nullptr});
            continue;
        }
        if (top.compound()) {
            // Its sequence at the time it plays, as one picture.
            auto nested = std::make_shared<CompositeFrame>(CompositeFrame{layer.sourceTime, {}});
            if (const Sequence* sequence = project.findSequence(top.sequenceId); sequence && depth < kMaxNesting)
                buildLayers(project, sequence->tracks, layer.sourceTime, nested->layers, depth + 1, live);
            out.push_back(VideoLayer{top.id, DecodedFrame{}, top.visualAt(time), top.effectsAt(time), std::nullopt,
                                     std::nullopt, top.color, lookOf(top), false, {}, top.chromaKey, top.masks,
                                     top.matte, clipSeconds, clipDuration, std::move(nested)});
            continue;
        }
        // Each side is a decoded frame, or a still image's path (nothing to decode).
        struct Side {
            const Clip* clip = nullptr;
            const DecodedFrame* frame = nullptr;
            std::string stillPath;
            bool shown() const { return frame || !stillPath.empty(); }
            DecodedFrame picture() const { return frame ? frame->share() : DecodedFrame{}; }
        };
        const auto sideOf = [&](const Clip& clip, Ticks sourceTime) {
            Side side{&clip, nullptr, {}};
            const MediaItem* media = project.findMedia(clip.mediaId);
            if (media && media->still)
                side.stillPath = media->path;
            else
                side.frame = pictureOf(clip, sourceTime);
            return side;
        };
        const Side outgoing = sideOf(top, layer.sourceTime);
        const Side incoming = layer.transition ? sideOf(*layer.transition->to, layer.transition->toSourceTime) : Side{};
        if (outgoing.shown() && incoming.shown()) {
            const TransitionLayer& t = *layer.transition;
            out.push_back(VideoLayer{
                top.id, outgoing.picture(), top.visualAt(time), top.effectsAt(time),
                LayerTransition{t.spec->transitionId, t.spec->values, float(t.progress), t.to->id, incoming.picture(),
                                t.to->visualAt(time), t.to->effectsAt(time), t.to->color, lookOf(*t.to),
                                incoming.stillPath, t.to->chromaKey, t.to->masks, toSeconds(time - t.to->start)},
                std::nullopt, top.color, lookOf(top), false, outgoing.stillPath, top.chromaKey, top.masks,
                MatteMode::None, // no matte during a transition
                clipSeconds, clipDuration, nullptr});
        } else if (outgoing.shown() || incoming.shown()) {
            // One side unreadable: show the other alone.
            const Side& side = outgoing.shown() ? outgoing : incoming;
            const Clip& clip = *side.clip;
            out.push_back(VideoLayer{clip.id, side.picture(), clip.visualAt(time), clip.effectsAt(time), std::nullopt,
                                     std::nullopt, clip.color, lookOf(clip), false, side.stillPath, clip.chromaKey,
                                     clip.masks, clip.matte, toSeconds(time - clip.start), toSeconds(clip.duration), nullptr});
        }
    }
}

void TimelinePlayback::run(std::shared_ptr<const Project> project, Ticks start)
{
    const Ticks end = project->duration();
    const Ticks frameTicks = frameDuration(project->settings.frameRate);
    Ticks time = start;

    while (!m_stop && time < end) {
        CompositeFrame composite{time, {}};
        std::set<Id> live;
        buildLayers(*project, project->tracks, time, composite.layers, 0, live);
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
    }
    m_finished = true;
}

} // namespace tve
