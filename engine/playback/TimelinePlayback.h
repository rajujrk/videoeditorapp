#pragma once

#include "decode/VideoDecoder.h"
#include "model/Project.h"
#include "playback/FrameSource.h"
#include "playback/TimelineAudio.h"
#include "video/OfxFrames.h"

#include <atomic>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <thread>

namespace tve {

// A stretch of the timeline rendered to a file (the render cache): played from there, one
// picture of the whole canvas, instead of its layers. Its frames start at `from`.
struct RenderedStretch {
    Ticks from = 0;
    Ticks to = 0;
    std::string path;
};

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
    // `rendered`: stretches to play from their render files (made from this very project).
    void setProject(std::shared_ptr<const Project> project, std::vector<RenderedStretch> rendered = {});

    // Each frame's layers decoded side by side (the default): stacks of layers keep up in the
    // preview. An export turns it off: there it gains nothing and the decoding competes
    // with the encoder for the media engine.
    void setParallelDecoding(bool on) { m_parallelDecoding = on; }

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
    void run(std::shared_ptr<const Project> project, std::shared_ptr<const std::vector<RenderedStretch>> rendered,
             Ticks start);
    // The rendered stretch's picture at `time` as the one layer of `out`; false if its file
    // can't be read (then the layers are composited after all).
    bool renderedLayer(const RenderedStretch& stretch, std::size_t index, Ticks time, CompositeFrame& out);
    // The layers `tracks` show at `time`, bottom first, into `out`; a compound clip's from its
    // sequence, nested. `live` collects every clip shown (their decoders stay open).
    void buildLayers(const Project& project, const std::vector<Track>& tracks, Ticks time,
                     std::vector<VideoLayer>& out, int depth, std::set<Id>& live);
    static constexpr int kMaxNesting = 8; // compound clips inside compound clips
    // Rendered stretches' decoders, apart from clips' (ids count down from here).
    static constexpr Id kRenderedLayerId = ~Id(0);
    // The decoded frame on screen at `sourceTime` for this clip, decoding or seeking as
    // needed; nullptr if the media cannot be decoded.
    const DecodedFrame* frameAt(LayerState& state, const MediaItem& media, Ticks sourceTime);
    // A clip's picture at `sourceTime`: its processed copy where that covers the time, else
    // its media's; nullptr if it cannot be decoded.
    const DecodedFrame* pictureOf(LayerState& state, const Project& project, const Clip& clip, Ticks sourceTime);
    // Decodes, side by side, the frames buildLayers is about to ask for at `time` (each clip
    // has a decoder of its own), so a stack of layers takes about as long as its slowest.
    void prefetch(const Project& project, Ticks time);
    const DecodedFrame* reversedFrameAt(LayerState& state, const MediaItem& media, Ticks sourceTime);
    bool openDecoder(LayerState& state, const MediaItem& media);

    mutable std::mutex m_control; // serializes setProject/seek from the UI thread
    std::shared_ptr<const Project> m_project;
    std::shared_ptr<const std::vector<RenderedStretch>> m_rendered;

    std::shared_ptr<TimelineAudio> m_audio = std::make_shared<TimelineAudio>();
    FrameQueue m_queue{12};
    PlaybackClock m_clock;
    PlaybackCounters m_counters;
    std::atomic<bool> m_hardware{false};
    std::atomic<bool> m_stop{false};
    std::atomic<bool> m_finished{false};
    std::atomic<bool> m_parallelDecoding{true};
    std::thread m_producer;

    // Only touched by the producer thread; restarts join it first. Keyed by clip id, so
    // two clips of the same file decode independently.
    std::map<Id, LayerState> m_layers;
    // Clips' OpenFX effects, on their decoded pictures (producer thread only).
    OfxFrames m_ofx;
};

} // namespace tve
