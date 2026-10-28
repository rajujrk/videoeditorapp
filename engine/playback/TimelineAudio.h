#pragma once

#include "audio/AudioEffects.h"
#include "audio/dsp/Loudness.h"
#include "audio/dsp/TimeStretch.h"
#include "decode/AudioDecoder.h"
#include "model/Project.h"

#include <array>
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
    // Never blocks for long (retries a briefly held lock); nullopt when empty, or busy for
    // longer than a push takes.
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

// Peak and RMS (0..1) of the left and right channels over one chunk.
struct ChannelLevels {
    float peak[2] = {0.0f, 0.0f};
    float rms[2] = {0.0f, 0.0f};

    void measure(const float* stereo, int frames);
};

// What the meters show for one chunk of the mix: each track that carries sound (after its
// volume and balance) and the master, with the master's loudness so far in LUFS (-infinity
// while unknown; integrated since playback started there).
struct MixLevels {
    Ticks pts = 0;
    std::vector<std::pair<Id, ChannelLevels>> tracks;
    ChannelLevels master;
    double momentary = 0.0;
    double shortTerm = 0.0;
    double integrated = 0.0;
};

// Mixes a project's timeline into AudioChunks on a background thread. Audible clips are
// those on audio tracks plus video clips whose sound has not been detached, on tracks
// that are not muted (and, while any track is soloed, soloed); each gets its own decoder so
// overlapping clips of one file work. Per clip: volume and fades, then its audio effects
// (whose echoes ring on past its end); played faster or slower it keeps its pitch unless
// set not to. Per track: volume and balance. A compound clip plays its sequence's tracks,
// mixed the same way, as its sound.
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
    // The levels of the chunk holding `time`, if it was mixed recently (any thread).
    std::optional<MixLevels> levelsAt(Ticks time) const;

private:
    struct ClipStream {
        std::unique_ptr<AudioDecoder> decoder;
        Ticks nextSourceTime = -1;
        std::unique_ptr<AudioEffectChain> effects;
        std::unique_ptr<dsp::TimeStretcher> stretcher;
        double stretchNext = -1.0; // source sample the stretcher continues from
    };

    void stopProducer();
    void run(std::shared_ptr<const Project> project, Ticks start, std::uint64_t generation);
    // How long the clip's effects ring on after its end (0 without effects).
    Ticks tailOf(const Clip& clip);
    // Mixes `frames` samples of a clip from `chunkStart` (its track's time) into `mix`;
    // `keepUntil` is the timeline time its streams (and those inside it) stay open until.
    void mixClip(const Project& project, const Track& track, const Clip& clip, Ticks chunkStart, int frames,
                 Ticks tail, Ticks keepUntil, float* mix, std::vector<float>& scratch, int depth);
    // Mixes a compound clip's sequence from `start` (its own time) into `mix`.
    void mixTracks(const Project& project, const std::vector<Track>& tracks, Ticks start, int frames, Ticks keepUntil,
                   float* mix, std::vector<float>& scratch, int depth);

    static constexpr int kMaxNesting = 8;

    std::mutex m_control;
    std::shared_ptr<const Project> m_project;
    AudioChunkQueue m_queue{24}; // ~0.5 s ahead
    std::atomic<std::uint64_t> m_generation{0};
    std::atomic<bool> m_stop{false};
    std::atomic<bool> m_finished{false};
    std::thread m_producer;

    // Only touched by the producer thread; restarts join it first.
    std::map<Id, ClipStream> m_streams;
    std::array<std::vector<float>, kMaxNesting + 2> m_clipBuffers;  // a clip's sound, per nesting depth
    std::array<std::vector<float>, kMaxNesting + 2> m_trackBuffers; // a nested track's mix, per depth
    std::map<Id, Ticks> m_clipEnds; // when each stream used this chunk can close
    dsp::LoudnessMeter m_meter;

    mutable std::mutex m_levelsMutex;
    std::deque<MixLevels> m_levels; // the last few seconds mixed
};

// Integrated loudness (LUFS) of a project's mix from `from` to `to`, mixed as fast as it
// decodes; -infinity when silent.
double measureLoudness(std::shared_ptr<const Project> project, Ticks from, Ticks to);

} // namespace tve
