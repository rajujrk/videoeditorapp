#pragma once

#include "model/Time.h"

#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace tve {

// Peak levels of a media file's sound, for drawing waveforms: one value (0..1, the
// louder channel) per 10 ms.
struct WaveformPeaks {
    static constexpr int kPeaksPerSecond = 100;
    std::vector<float> peaks;

    // Largest peak in [from, to) media time; 0 outside the file.
    float maxIn(Ticks from, Ticks to) const;
};

// Computes peaks on a background thread, one file at a time, and keeps them in memory.
class WaveformCache {
public:
    // Called on the worker thread when a file's peaks are ready (or failed: empty peaks).
    using ReadyCallback = std::function<void(const std::string& path)>;

    explicit WaveformCache(ReadyCallback onReady = {});
    ~WaveformCache();

    WaveformCache(const WaveformCache&) = delete;
    WaveformCache& operator=(const WaveformCache&) = delete;

    // The peaks if computed; otherwise queues the file and returns nullptr.
    std::shared_ptr<const WaveformPeaks> peaks(const std::string& path);

    // Decodes the whole file synchronously (used by the worker and by tests).
    static WaveformPeaks compute(const std::string& path);

private:
    void run();

    ReadyCallback m_onReady;
    std::mutex m_mutex;
    std::condition_variable m_wake;
    std::map<std::string, std::shared_ptr<const WaveformPeaks>> m_ready;
    std::deque<std::string> m_pending;
    std::map<std::string, bool> m_queued;
    bool m_stop = false;
    std::thread m_worker;
};

} // namespace tve
