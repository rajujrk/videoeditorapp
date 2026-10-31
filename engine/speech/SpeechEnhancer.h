#pragma once

#include "analysis/MediaAnalysis.h"

#include <complex>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace tve {

// Speech enhancement, on this computer: DeepFilterNet3 (MIT / Apache-2.0) run by ONNX
// Runtime (MIT). Noise, hum and room sound are taken out of voices; the result keeps the
// timing of the original sample for sample. 48 kHz, one channel at a time.

// The model's settings (its config.ini, [df] and [deepfilternet]).
struct DeepFilterConfig {
    int sampleRate = 48000;
    int fftSize = 960;
    int hopSize = 480;
    int nbErb = 32;
    int nbDf = 96;
    int minNbErbFreqs = 2;
    int dfOrder = 5;
    int dfLookahead = 2;
    int convLookahead = 2;
    double normTau = 1.0;

    static std::optional<DeepFilterConfig> parse(const std::string& ini, std::string* error = nullptr);
    // How fast the feature normalisation follows (per frame).
    float normAlpha() const;
    // The bins (of fftSize / 2 + 1) in each of the nbErb bands.
    std::vector<int> erbWidths() const;
};

// The short-time spectrum DeepFilterNet works on: a Vorbis window, half overlap, scaled so
// that analysis then synthesis gives back the signal, fftSize - hopSize samples late.
class DeepFilterStft {
public:
    explicit DeepFilterStft(const DeepFilterConfig& config);
    ~DeepFilterStft();
    DeepFilterStft(const DeepFilterStft&) = delete;
    DeepFilterStft& operator=(const DeepFilterStft&) = delete;

    int bins() const { return m_fft / 2 + 1; }
    // hopSize new samples → bins() values.
    void analysis(const float* input, std::complex<float>* spectrum);
    // bins() values → hopSize samples.
    void synthesis(const std::complex<float>* spectrum, float* output);

private:
    struct Fft;
    int m_fft;
    int m_hop;
    std::vector<float> m_window;
    std::vector<float> m_analysisMemory;
    std::vector<float> m_synthesisMemory;
    std::vector<float> m_buffer;
    std::unique_ptr<Fft> m_transform;
};

class SpeechEnhancer {
public:
    // The model in `directory`: enc.onnx, erb_dec.onnx, df_dec.onnx and config.ini.
    static std::unique_ptr<SpeechEnhancer> load(const std::string& directory, std::string* error = nullptr);
    ~SpeechEnhancer();
    SpeechEnhancer(const SpeechEnhancer&) = delete;
    SpeechEnhancer& operator=(const SpeechEnhancer&) = delete;

    const DeepFilterConfig& config() const { return m_config; }

    // One channel at 48 kHz, cleaned, the same length. nullopt when `progress` cancels or
    // the model fails (then `error` says why).
    std::optional<std::vector<float>> enhance(const std::vector<float>& samples, const AnalysisProgress& progress = {},
                                              std::string* error = nullptr);

    // The whole sound of a media file, cleaned, as a 48 kHz WAV of the same length (one
    // channel when the file's channels are alike, else two).
    bool enhanceFile(const std::string& mediaPath, const std::string& outputPath, const AnalysisProgress& progress = {},
                     std::string* error = nullptr);

private:
    struct Models;
    SpeechEnhancer(DeepFilterConfig config, std::unique_ptr<Models> models);

    DeepFilterConfig m_config;
    std::unique_ptr<Models> m_models;
};

// `original` and its cleaned copy mixed so that noise is lowered by at most `limitDb`
// (≥ 100: all of it, the cleaned copy as it is), as a 48 kHz WAV of the cleaned copy's
// channels. Both must have the same timing (enhanceFile's output does).
bool limitEnhancement(const std::string& originalPath, const std::string& enhancedPath, double limitDb,
                      const std::string& outputPath, const AnalysisProgress& progress = {}, std::string* error = nullptr);

} // namespace tve
