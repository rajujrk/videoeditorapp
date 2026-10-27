#pragma once

#include "model/Time.h"

#include <memory>
#include <string>

namespace tve {

// The engine's one internal audio format: 48 kHz, stereo, interleaved 32-bit float.
// At 48 kHz one sample is exactly 14 700 ticks, so timeline times map to whole samples.
namespace audio {
inline constexpr int kSampleRate = 48000;
inline constexpr int kChannels = 2;
inline constexpr Ticks kTicksPerSample = kTicksPerSecond / kSampleRate;
static_assert(kTicksPerSecond % kSampleRate == 0);

constexpr std::int64_t samplesFromTicks(Ticks ticks)
{
    return ticks / kTicksPerSample;
}
constexpr Ticks ticksFromSamples(std::int64_t samples)
{
    return samples * kTicksPerSample;
}
} // namespace audio

struct AudioStreamInfo {
    int sourceSampleRate = 0;
    int sourceChannels = 0;
    Ticks duration = 0;
    std::string codecName;
};

// Decodes the best audio stream of a file and converts it to the engine format.
class AudioDecoder {
public:
    // Returns nullptr if the file cannot be opened or has no audio stream.
    static std::unique_ptr<AudioDecoder> open(const std::string& path, std::string* error = nullptr);
    ~AudioDecoder();

    AudioDecoder(const AudioDecoder&) = delete;
    AudioDecoder& operator=(const AudioDecoder&) = delete;

    const AudioStreamInfo& info() const;

    // Sample-accurate: the next sample read is the one at `target` (media time).
    bool seek(Ticks target);

    // Reads up to `frames` stereo frames into `out`. Returns the number read; fewer than
    // asked means the stream ended.
    int read(float* out, int frames);

    // Media time of the next sample read() returns.
    Ticks position() const;

private:
    struct Impl;
    explicit AudioDecoder(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> m_impl;
};

} // namespace tve
