#include "audio/WaveformCache.h"

#include "decode/AudioDecoder.h"

#include <algorithm>
#include <cmath>

namespace tve {

float WaveformPeaks::maxIn(Ticks from, Ticks to) const
{
    const auto index = [](Ticks t) { return std::int64_t(t * kPeaksPerSecond / kTicksPerSecond); };
    const std::int64_t first = std::max<std::int64_t>(index(from), 0);
    const std::int64_t last = std::min<std::int64_t>(std::max(index(to), first + 1), std::int64_t(peaks.size()));
    float value = 0.0f;
    for (std::int64_t i = first; i < last; ++i)
        value = std::max(value, peaks[std::size_t(i)]);
    return value;
}

WaveformCache::WaveformCache(ReadyCallback onReady) : m_onReady(std::move(onReady))
{
    m_worker = std::thread([this] { run(); });
}

WaveformCache::~WaveformCache()
{
    {
        std::lock_guard lock(m_mutex);
        m_stop = true;
    }
    m_wake.notify_all();
    m_worker.join();
}

std::shared_ptr<const WaveformPeaks> WaveformCache::peaks(const std::string& path)
{
    std::lock_guard lock(m_mutex);
    if (auto it = m_ready.find(path); it != m_ready.end())
        return it->second;
    if (!m_queued[path]) {
        m_queued[path] = true;
        m_pending.push_back(path);
        m_wake.notify_one();
    }
    return nullptr;
}

WaveformPeaks WaveformCache::compute(const std::string& path)
{
    WaveformPeaks result;
    auto decoder = AudioDecoder::open(path);
    if (!decoder)
        return result;
    constexpr int kBlock = audio::kSampleRate / WaveformPeaks::kPeaksPerSecond; // 480 frames
    std::vector<float> buffer(std::size_t(kBlock) * audio::kChannels);
    while (true) {
        const int read = decoder->read(buffer.data(), kBlock);
        if (read <= 0)
            break;
        float peak = 0.0f;
        for (int i = 0; i < read * audio::kChannels; ++i)
            peak = std::max(peak, std::abs(buffer[std::size_t(i)]));
        result.peaks.push_back(std::min(peak, 1.0f));
        if (read < kBlock)
            break;
    }
    return result;
}

void WaveformCache::run()
{
    while (true) {
        std::string path;
        {
            std::unique_lock lock(m_mutex);
            m_wake.wait(lock, [&] { return m_stop || !m_pending.empty(); });
            if (m_stop)
                return;
            path = std::move(m_pending.front());
            m_pending.pop_front();
        }
        auto peaks = std::make_shared<const WaveformPeaks>(compute(path));
        {
            std::lock_guard lock(m_mutex);
            m_ready[path] = std::move(peaks);
        }
        if (m_onReady)
            m_onReady(path);
    }
}

} // namespace tve
