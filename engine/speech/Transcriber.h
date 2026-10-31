#pragma once

#include "analysis/MediaAnalysis.h"

#include <optional>
#include <string>
#include <vector>

namespace tve {

// Speech to text, on this computer: whisper.cpp (MIT) running a Whisper model (MIT) on the
// GPU (Metal) where it can.

struct TranscriptWord {
    Ticks start = 0; // media time
    Ticks end = 0;
    std::string text;      // as written, with its punctuation
    float probability = 0; // how sure the model is, 0..1

    friend bool operator==(const TranscriptWord&, const TranscriptWord&) = default;
};

struct Transcript {
    static constexpr int kVersion = 2; // 2: verbatim (fillers kept)
    std::string language; // detected (or as asked), e.g. "en"
    std::vector<TranscriptWord> words;

    friend bool operator==(const Transcript&, const Transcript&) = default;
};

struct TranscribeOptions {
    std::string language = "auto"; // a language code ("en", "de"…) or "auto" to detect it
    int threads = 0;                // 0: as many as make sense here
};

// Transcribes the sound of a media file with the model at `modelPath`. Sounds that are not
// speech ("[music]", "♪") are left out. `progress` (0..1) may cancel; nullopt with `error`
// on failure (empty when cancelled).
std::optional<Transcript> transcribe(const std::string& mediaPath, const std::string& modelPath,
                                     const TranscribeOptions& options = {}, const AnalysisProgress& progress = {},
                                     std::string* error = nullptr);

// The sound of a media file as 16 kHz mono (what the model listens to).
std::optional<std::vector<float>> speechSamples(const std::string& mediaPath, const AnalysisProgress& progress = {},
                                                std::string* error = nullptr);

// Kept beside the other analysis (AnalysisCache's folder), per media and per model + language.
std::optional<Transcript> loadTranscript(const AnalysisCache& cache, const std::string& fingerprint, const std::string& variant);
bool saveTranscript(const AnalysisCache& cache, const std::string& fingerprint, const std::string& variant,
                    const Transcript& transcript);

} // namespace tve
