#pragma once

#include "decode/VideoDecoder.h"
#include "model/Project.h"
#include "playback/FrameSource.h"
#include "playback/TimelineAudio.h"

#include <atomic>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <thread>

namespace tve {

// Plays a project's timeline. A producer thread walks the timeline on the project's frame
// grid from the requested time and, at each step, emits a CompositeFrame holding every
// visible video layer (bottom first) with its placement and effects; gaps produce one
// black frame. The UI drives the clock (play/pause/seek); every
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

    // The timeline's mixed sound; restarted together with the video on every seek/edit.
    std::shared_ptr<TimelineAudio> audio() const { return m_audio; }

    FrameQueue& queue() override { return m_queue; }
    PlaybackClock& clock() override { return m_clock; }
    PlaybackCounters& counters() override { return m_counters; }
    bool hardwareAccelerated() const override { return m_hardware.load(); }
    Size canvasSize() const override;

private:
    // Per visible clip: its decoder, the frame on screen and the next one decoded.
    struct LayerState {
        std::unique_ptr<VideoDecoder> decoder;
        std::string path;
        std::optional<DecodedFrame> current;
        std::optional<DecodedFrame> next;
        // Reversed clips: a window of frames decoded forward, served backwards.
        std::deque<DecodedFrame> window;
    };

    void restartAt(Ticks time);
    void stopProducer();
    void run(std::shared_ptr<const Project> project, Ticks start);
    // The layers `tracks` show at `time`, bottom first, into `out`; a compound clip's from its
    // sequence, nested. `live` collects every clip shown (their decoders stay open).
    void buildLayers(const Project& project, const std::vector<Track>& tracks, Ticks time,
                     std::vector<VideoLayer>& out, int depth, std::set<Id>& live);
    static constexpr int kMaxNesting = 8; // compound clips inside compound clips
    // The decoded frame on screen at `sourceTime` for this clip, decoding or seeking as
    // needed; nullptr if the media cannot be decoded.
    const DecodedFrame* frameAt(LayerState& state, const MediaItem& media, Ticks sourceTime);
    const DecodedFrame* reversedFrameAt(LayerState& state, const MediaItem& media, Ticks sourceTime);
    bool openDecoder(LayerState& state, const MediaItem& media);

    mutable std::mutex m_control; // serializes setProject/seek from the UI thread
    std::shared_ptr<const Project> m_project;

    std::shared_ptr<TimelineAudio> m_audio = std::make_shared<TimelineAudio>();
    FrameQueue m_queue{12};
    PlaybackClock m_clock;
    PlaybackCounters m_counters;
    std::atomic<bool> m_hardware{false};
    std::atomic<bool> m_stop{false};
    std::atomic<bool> m_finished{false};
    std::thread m_producer;

    // Only touched by the producer thread; restarts join it first. Keyed by clip id, so
    // two clips of the same file decode independently.
    std::map<Id, LayerState> m_layers;
};

} // namespace tve
