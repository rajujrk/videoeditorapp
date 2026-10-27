#pragma once

#include "decode/VideoDecoder.h"
#include "model/Project.h"
#include "playback/FrameSource.h"

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <thread>

namespace tve {

// Plays a project's timeline. A producer thread walks the timeline from the requested
// time, decodes whichever video clip is visible, and stamps each frame with its timeline
// time; gaps produce one black frame. The UI drives the clock (play/pause/seek); every
// seek or project change restarts the producer at the clock's position.
class TimelinePlayback final : public FrameSource {
public:
    TimelinePlayback() = default;
    ~TimelinePlayback() override;

    TimelinePlayback(const TimelinePlayback&) = delete;
    TimelinePlayback& operator=(const TimelinePlayback&) = delete;

    // Replaces the project (an immutable snapshot) and restarts at the current time.
    void setProject(std::shared_ptr<const Project> project);

    void play() { m_clock.play(); }
    void pause() { m_clock.pause(); }
    // Moves the playhead and restarts decoding there.
    void seek(Ticks time);

    Ticks duration() const;
    // True once the producer has queued everything up to the end of the timeline.
    bool producerFinished() const { return m_finished.load(); }

    FrameQueue& queue() override { return m_queue; }
    PlaybackClock& clock() override { return m_clock; }
    PlaybackCounters& counters() override { return m_counters; }
    bool hardwareAccelerated() const override { return m_hardware.load(); }

private:
    void restartAt(Ticks time);
    void stopProducer();
    void run(std::shared_ptr<const Project> project, Ticks start);
    VideoDecoder* decoderFor(const MediaItem& media);

    mutable std::mutex m_control; // serializes setProject/seek from the UI thread
    std::shared_ptr<const Project> m_project;

    FrameQueue m_queue{12};
    PlaybackClock m_clock;
    PlaybackCounters m_counters;
    std::atomic<bool> m_hardware{false};
    std::atomic<bool> m_stop{false};
    std::atomic<bool> m_finished{false};
    std::thread m_producer;

    // Only touched by the producer thread; restarts join it first.
    std::map<Id, std::unique_ptr<VideoDecoder>> m_decoders;
    std::map<Id, std::string> m_decoderPaths;
};

} // namespace tve
