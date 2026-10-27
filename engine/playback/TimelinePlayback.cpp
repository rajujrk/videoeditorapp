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
}

VideoDecoder* TimelinePlayback::decoderFor(const MediaItem& media)
{
    auto it = m_decoders.find(media.id);
    if (it != m_decoders.end() && m_decoderPaths[media.id] == media.path)
        return it->second.get();
    auto decoder = VideoDecoder::open(media.path);
    if (!decoder)
        return nullptr; // missing or unreadable media plays as black
    m_decoderPaths[media.id] = media.path;
    return (m_decoders[media.id] = std::move(decoder)).get();
}

void TimelinePlayback::run(std::shared_ptr<const Project> project, Ticks start)
{
    const Ticks end = project->duration();
    const std::vector<Ticks> edits = editPoints(*project);
    Ticks time = start;

    while (!m_stop && time < end) {
        const auto visible = visibleVideoAt(*project, time);
        if (!visible) {
            // Gap: one black frame, then continue at the next clip.
            if (!m_queue.push(DecodedFrame(nullptr, time)))
                return;
            const auto next = nextVideoClipStart(*project, time);
            if (!next)
                break;
            time = *next;
            continue;
        }

        // Play this clip until the next edit anywhere, since an edit on another track can
        // change which clip is on top.
        const Clip clip = *visible->clip;
        const auto nextEdit = std::ranges::upper_bound(edits, time);
        const Ticks segmentEnd = std::min(clip.end(), nextEdit == edits.end() ? end : *nextEdit);
        const MediaItem* media = project->findMedia(clip.mediaId);
        VideoDecoder* decoder = media ? decoderFor(*media) : nullptr;
        if (!decoder || !decoder->seek(visible->sourceTime)) {
            if (!m_queue.push(DecodedFrame(nullptr, time)))
                return;
            time = segmentEnd;
            continue;
        }

        const Ticks sourceEnd = clip.sourceIn + (segmentEnd - clip.start);
        while (!m_stop) {
            auto frame = decoder->nextFrame();
            if (!frame || frame->pts() >= sourceEnd)
                break;
            // The first frame after a seek may have started before `time`; show it from `time`.
            frame->setPts(std::max(time, clip.start + (frame->pts() - clip.sourceIn)));
            m_hardware = decoder->info().hardwareAccelerated;
            ++m_counters.decoded;
            if (!m_queue.push(std::move(*frame)))
                return;
        }
        time = segmentEnd;
    }
    m_finished = true;
}

} // namespace tve
