#pragma once

#include "decode/VideoDecoder.h"
#include "playback/FrameSource.h"

#include <atomic>
#include <memory>
#include <string>
#include <thread>

namespace tve {

// Decodes one file on a background thread into a FrameQueue, looping at the end.
// Presentation timestamps keep increasing across loops so the consumer's clock never
// has to rewind. Used for single-file preview and benchmarking.
class PlaybackSession final : public FrameSource {
public:
    static std::shared_ptr<PlaybackSession> open(const std::string& path, std::string* error = nullptr);
    ~PlaybackSession() override;

    PlaybackSession(const PlaybackSession&) = delete;
    PlaybackSession& operator=(const PlaybackSession&) = delete;

    void start();
    void stop();

    // Decoder info is fixed after open(), except `hardwareAccelerated`, which is
    // confirmed once the first frame is decoded.
    VideoStreamInfo info() const;
    bool hardwareAccelerated() const override { return m_hardware.load(); }

    FrameQueue& queue() override { return m_queue; }
    PlaybackClock& clock() override { return m_clock; }
    PlaybackCounters& counters() override { return m_counters; }
    bool startsOnFirstFrame() const override { return true; }
    Size canvasSize() const override { return {m_info.width, m_info.height}; }

private:
    explicit PlaybackSession(std::unique_ptr<VideoDecoder> decoder);
    void run();

    std::unique_ptr<VideoDecoder> m_decoder;
    VideoStreamInfo m_info;
    FrameQueue m_queue{8};
    PlaybackClock m_clock;
    PlaybackCounters m_counters;
    std::atomic<bool> m_hardware{false};
    std::atomic<bool> m_stopRequested{false};
    std::thread m_thread;
};

} // namespace tve
