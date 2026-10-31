#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace tve::dsp {

inline constexpr double kSampleRate = 48000.0; // the engine format (audio::kSampleRate)

// Every setting of a processor, by id (see ProcessorInfo::params).
using Settings = std::map<std::string, float, std::less<>>;

// One built-in sound processor working in place on interleaved stereo at kSampleRate. An
// audio effect package (effects/audio) is a chain of these with fixed settings.
class Processor {
public:
    virtual ~Processor() = default;
    // Applies settings (all of them, in range). Keeps the running state where it can, so a
    // setting can change mid-stream.
    virtual void configure(const Settings& settings) = 0;
    virtual void process(float* samples, int frames) = 0;
    virtual void reset() = 0;
    // How long it keeps sounding after its input falls silent (echoes, reverb), in seconds.
    virtual double tail() const { return 0.0; }
};

struct ParamInfo {
    std::string id;
    float min = 0.0f;
    float max = 1.0f;
    float value = 0.0f; // the default
};

struct ProcessorInfo {
    std::string id;
    std::vector<ParamInfo> params;
    std::function<std::unique_ptr<Processor>()> make;

    const ParamInfo* find(std::string_view paramId) const;
    // `settings` completed with defaults and clamped to the ranges.
    Settings complete(const Settings& settings) const;
};

// eq, compressor, limiter, gate, reverb, delay, chorus, widener, autopan, pitch,
// distortion, ringmod, noise, denoise (RNNoise).
const std::vector<ProcessorInfo>& processors();
const ProcessorInfo* findProcessor(std::string_view id);

} // namespace tve::dsp
