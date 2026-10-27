#pragma once

#include "decode/VideoDecoder.h"
#include "playback/FrameQueue.h"

#include <atomic>
#include <memory>
#include <string>
#include <thread>

namespace tve {

struct PlaybackCounters {
    std::atomic<std::int64_t> decoded{0};   // frames produced by the decoder
    std::atomic<std::int64_t> presented{0}; // distinct frames shown on screen
    std::atomic<std::int64_t> skipped{0};   // frames that became due but were never shown
};

// Decodes one file on a background thread into a FrameQueue, looping at the end.
// Presentation timestamps keep increasing across loops so the consumer's clock never
// has to rewind.
class PlaybackSession {
public:
    static std::shared_ptr<PlaybackSession> open(const std::string& path, std::string* error = nullptr);
    ~PlaybackSession();

    PlaybackSession(const PlaybackSession&) = delete;
    PlaybackSession& operator=(const PlaybackSession&) = delete;

    void start();
    void stop();

    // Decoder info is fixed after open(), except `hardwareAccelerated`, which is
    // confirmed once the first frame is decoded.
    VideoStreamInfo info() const;
    bool hardwareAccelerated() const { return m_hardware.load(); }

    FrameQueue& queue() { return m_queue; }
    PlaybackCounters& counters() { return m_counters; }

private:
    explicit PlaybackSession(std::unique_ptr<VideoDecoder> decoder);
    void run();

    std::unique_ptr<VideoDecoder> m_decoder;
    VideoStreamInfo m_info;
    FrameQueue m_queue{8};
    PlaybackCounters m_counters;
    std::atomic<bool> m_hardware{false};
    std::atomic<bool> m_stopRequested{false};
    std::thread m_thread;
};

} // namespace tve
