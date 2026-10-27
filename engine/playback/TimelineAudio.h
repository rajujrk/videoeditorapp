#pragma once

#include "decode/AudioDecoder.h"
#include "model/Project.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

namespace tve {

// A block of mixed timeline audio in the engine format, stamped with its timeline time.
struct AudioChunk {
    static constexpr int kFrames = 1024; // ~21 ms

    Ticks pts = 0;
    std::uint64_t generation = 0; // which producer run made it (stale after a seek)
    std::vector<float> samples;   // kFrames × audio::kChannels, interleaved

    Ticks end() const { return pts + audio::ticksFromSamples(kFrames); }
};

// Bounded hand-off from the mixing thread to the audio device callback. The consumer
// never blocks (it runs on the real-time audio thread).
class AudioChunkQueue {
public:
    explicit AudioChunkQueue(std::size_t capacity) : m_capacity(capacity) {}

    bool push(AudioChunk chunk);
    // Non-blocking; returns nullopt when empty or when the lock is busy.
    std::optional<AudioChunk> tryPop();
    void stop();
    void restart();
    std::size_t size() const;

private:
    const std::size_t m_capacity;
    mutable std::mutex m_mutex;
    std::condition_variable m_notFull;
    std::deque<AudioChunk> m_chunks;
    bool m_stopped = false;
};

// Mixes a project's timeline into AudioChunks on a background thread. Audible clips are
// those on audio tracks plus video clips whose sound has not been detached, on tracks
// that are not muted; each gets its own decoder so overlapping clips of one file work.
class TimelineAudio {
public:
    TimelineAudio() = default;
    ~TimelineAudio();

    TimelineAudio(const TimelineAudio&) = delete;
    TimelineAudio& operator=(const TimelineAudio&) = delete;

    // Restarts mixing at `time` with `project` (or the current project when null).
    void restart(std::shared_ptr<const Project> project, Ticks time);

    AudioChunkQueue& queue() { return m_queue; }
    std::uint64_t generation() const { return m_generation.load(); }
    bool producerFinished() const { return m_finished.load(); }

private:
    struct ClipStream {
        std::unique_ptr<AudioDecoder> decoder;
        Ticks nextSourceTime = -1;
    };

    void stopProducer();
    void run(std::shared_ptr<const Project> project, Ticks start, std::uint64_t generation);
    void mixClip(const Project& project, const Clip& clip, Ticks chunkStart, float* mix,
                 std::vector<float>& scratch);

    std::mutex m_control;
    std::shared_ptr<const Project> m_project;
    AudioChunkQueue m_queue{24}; // ~0.5 s ahead
    std::atomic<std::uint64_t> m_generation{0};
    std::atomic<bool> m_stop{false};
    std::atomic<bool> m_finished{false};
    std::thread m_producer;

    // Only touched by the producer thread; restarts join it first.
    std::map<Id, ClipStream> m_streams;
    std::vector<float> m_resampled;
};

} // namespace tve
