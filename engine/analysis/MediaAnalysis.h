#pragma once

#include "model/Time.h"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace tve {

// Media analysis without models: what the sound and pictures of a file do over time,
// measured once (slow: decodes the whole file) and kept as features; the detectors below
// turn features into silences, speech, beats and scene cuts instantly, for any settings.

struct TimeRange {
    Ticks start = 0;
    Ticks end = 0;

    Ticks length() const { return end - start; }
    friend bool operator==(const TimeRange&, const TimeRange&) = default;
};

// Every 10 ms of a file's sound (mixed to mono): its loudness and how much new sound starts.
struct AudioFeatures {
    static constexpr int kVersion = 1;
    static constexpr double kHop = 0.01; // seconds per value

    std::vector<float> loudness; // RMS level, dBFS (-120 for digital silence)
    std::vector<float> onset;    // onset strength (spectral flux of the log spectrum), ≥ 0
    Ticks duration = 0;

    friend bool operator==(const AudioFeatures&, const AudioFeatures&) = default;
};

// Every frame of a file's pictures: when it is shown and how different it is from the one
// before (0: the same … 1: nothing in common), from colour histograms and pixel differences.
struct VideoFeatures {
    static constexpr int kVersion = 1;

    std::vector<Ticks> times;
    std::vector<float> change; // change[0] is 0

    friend bool operator==(const VideoFeatures&, const VideoFeatures&) = default;
};

// `progress` gets 0..1 now and then; returning false cancels (the result is then nullopt).
using AnalysisProgress = std::function<bool(double)>;

std::optional<AudioFeatures> analyzeAudio(const std::string& path, const AnalysisProgress& progress = {},
                                          std::string* error = nullptr);
std::optional<VideoFeatures> analyzeVideo(const std::string& path, const AnalysisProgress& progress = {},
                                          std::string* error = nullptr);

struct SilenceOptions {
    double thresholdDb = -40.0; // quieter than this is silence
    double minDuration = 0.5;   // seconds; shorter pauses stay
    double padding = 0.1;       // seconds of the pause kept next to the sound on each side
};
// Pauses (media time) within `from`–`to`, each already shrunk by the padding.
std::vector<TimeRange> findSilences(const AudioFeatures& features, const SilenceOptions& options, Ticks from, Ticks to);

struct SpeechOptions {
    double thresholdDb = -35.0; // louder than this is someone talking
    double hold = 0.3;          // seconds: gaps shorter than this join the speech around them
    double minDuration = 0.1;   // seconds: shorter bursts are not speech
};
// When there is sound to duck under (media time).
std::vector<TimeRange> findSpeech(const AudioFeatures& features, const SpeechOptions& options);

struct Beats {
    double bpm = 0.0;
    std::vector<Ticks> times; // media time of each beat
};
// The tempo (searched between 60 and 200 BPM) and a beat grid that follows the music.
Beats findBeats(const AudioFeatures& features);

// How two recordings of the same sound line up: `other`'s media time t shows the same sound
// as `reference`'s at t + offset. From their onset strength (what starts when), correlated
// over lags up to ±maxLag seconds; confidence 0..1 (how far the best match stands out).
struct AudioOffset {
    double offset = 0.0; // seconds
    double confidence = 0.0;
};
std::optional<AudioOffset> findAudioOffset(const AudioFeatures& reference, const AudioFeatures& other, double maxLag = 30.0);

struct SceneOptions {
    double threshold = 0.35; // change above this (and well above the shot's own movement) is a cut
    double minScene = 0.5;   // seconds between cuts
};
// Media times of the first frames of new scenes.
std::vector<Ticks> findSceneCuts(const VideoFeatures& features, const SceneOptions& options);

// Features kept on disk, one file per media fingerprint and kind. A file from another
// version of the analysis is ignored.
class AnalysisCache {
public:
    explicit AnalysisCache(std::string directory) : m_directory(std::move(directory)) {}

    std::optional<AudioFeatures> loadAudio(const std::string& fingerprint) const;
    std::optional<VideoFeatures> loadVideo(const std::string& fingerprint) const;
    bool save(const std::string& fingerprint, const AudioFeatures& features) const;
    bool save(const std::string& fingerprint, const VideoFeatures& features) const;

    const std::string& directory() const { return m_directory; }

private:
    std::string pathFor(const std::string& fingerprint, const char* kind) const;
    std::string m_directory;
};

} // namespace tve
