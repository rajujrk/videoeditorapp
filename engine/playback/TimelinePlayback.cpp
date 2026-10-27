#include "playback/TimelinePlayback.h"

#include "model/TimelineQuery.h"

#include <algorithm>

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
    if (state.decoder && state.path == media.path)
        return true;
    state = LayerState{VideoDecoder::open(media.path), media.path, std::nullopt, std::nullopt, {}};
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

void TimelinePlayback::run(std::shared_ptr<const Project> project, Ticks start)
{
    const Ticks end = project->duration();
    const Ticks frameTicks = frameDuration(project->settings.frameRate);
    Ticks time = start;

    while (!m_stop && time < end) {
        const auto visible = visibleVideoLayersAt(*project, time);
        CompositeFrame composite{time, {}};
        for (const VisibleClip& layer : visible) {
            const MediaItem* media = project->findMedia(layer.clip->mediaId);
            if (!media)
                continue;
            LayerState& state = m_layers[layer.clip->id];
            const DecodedFrame* frame = layer.clip->reversed ? reversedFrameAt(state, *media, layer.sourceTime)
                                                             : frameAt(state, *media, layer.sourceTime);
            if (frame)
                composite.layers.push_back(
                    VideoLayer{layer.clip->id, frame->share(), layer.clip->visual, layer.clip->effects});
        }
        if (!m_queue.push(std::move(composite)))
            return;

        if (visible.empty()) {
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

        // Release decoders of clips that are no longer on screen.
        std::erase_if(m_layers, [&](const auto& entry) {
            return std::ranges::none_of(visible, [&](const VisibleClip& v) { return v.clip->id == entry.first; });
        });
    }
    m_finished = true;
}

} // namespace tve
