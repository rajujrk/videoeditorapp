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

const DecodedFrame* TimelinePlayback::frameAt(LayerState& state, const MediaItem& media, Ticks sourceTime)
{
    if (!state.decoder || state.path != media.path) {
        state = LayerState{VideoDecoder::open(media.path), media.path, std::nullopt, std::nullopt};
        if (!state.decoder)
            return nullptr; // missing or unreadable media is left out (shows what is below)
    }
    // Seek when going backwards or jumping far ahead; otherwise decode forward.
    constexpr Ticks kMaxDecodeAhead = kTicksPerSecond;
    const bool behind = !state.current || sourceTime < state.current->pts();
    const bool farAhead = state.current && sourceTime - state.current->pts() > kMaxDecodeAhead;
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
            if (const DecodedFrame* frame = frameAt(m_layers[layer.clip->id], *media, layer.sourceTime))
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

        // Release decoders of clips that are no longer on screen.
        std::erase_if(m_layers, [&](const auto& entry) {
            return std::ranges::none_of(visible, [&](const VisibleClip& v) { return v.clip->id == entry.first; });
        });
    }
    m_finished = true;
}

} // namespace tve
