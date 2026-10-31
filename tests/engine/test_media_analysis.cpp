#include "analysis/MediaAnalysis.h"
#include "decode/AudioDecoder.h"
#include "encode/MediaWriter.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <filesystem>
#include <numbers>
#include <random>

#include <unistd.h>

using namespace tve;
namespace fs = std::filesystem;

namespace {

constexpr Ticks kSec = kTicksPerSecond;

std::string tempFile(const std::string& name)
{
    return (fs::temp_directory_path() / ("tve_analysis_" + std::to_string(::getpid()) + "_" + name)).string();
}

double seconds(Ticks t)
{
    return toSeconds(t);
}

// A WAV of `pieces`: (seconds, loud?) — loud pieces a 300 Hz tone at -12 dBFS, the rest silence.
std::string writeSpeechLike(const std::vector<std::pair<double, bool>>& pieces, const std::string& name)
{
    const std::string path = tempFile(name);
    MediaSettings settings;
    settings.container = Container::Wav;
    settings.audioCodec = AudioCodec::Pcm16;
    auto writer = MediaWriter::open(path, settings);
    REQUIRE(writer);
    std::int64_t n = 0;
    for (const auto& [length, loud] : pieces) {
        std::vector<float> samples(std::size_t(length * audio::kSampleRate) * 2);
        for (std::size_t i = 0; i < samples.size() / 2; ++i, ++n) {
            const float v = loud ? 0.25f * float(std::sin(2.0 * std::numbers::pi * 300.0 * double(n) / audio::kSampleRate)) : 0.0f;
            samples[i * 2] = v;
            samples[i * 2 + 1] = v;
        }
        REQUIRE(writer->writeAudio(samples.data(), int(samples.size() / 2)));
    }
    REQUIRE(writer->finish());
    return path;
}

// A WAV of short noise clicks at `bpm`, starting at `offset` seconds, over a quiet hum.
std::string writeClicks(double bpm, double offset, double length, const std::string& name)
{
    const std::string path = tempFile(name);
    MediaSettings settings;
    settings.container = Container::Wav;
    settings.audioCodec = AudioCodec::Pcm16;
    auto writer = MediaWriter::open(path, settings);
    REQUIRE(writer);
    std::mt19937 random(7);
    std::uniform_real_distribution<float> noise(-1.0f, 1.0f);
    const std::size_t total = std::size_t(length * audio::kSampleRate);
    std::vector<float> samples(total * 2);
    const double period = 60.0 / bpm;
    for (std::size_t i = 0; i < total; ++i) {
        const double t = double(i) / audio::kSampleRate;
        float v = 0.01f * float(std::sin(2.0 * std::numbers::pi * 110.0 * t)); // a little hum
        const double since = t - offset;
        if (since >= 0) {
            const double into = std::fmod(since, period);
            if (into < 0.02)
                v += 0.6f * noise(random) * float(1.0 - into / 0.02);
        }
        samples[i * 2] = v;
        samples[i * 2 + 1] = v;
    }
    REQUIRE(writer->writeAudio(samples.data(), int(total)));
    REQUIRE(writer->finish());
    return path;
}

} // namespace

TEST_CASE("silences are found, padded, and short pauses kept")
{
    // 1 s sound, 1 s pause, 0.4 s sound, 0.3 s pause, 1 s sound, 1.2 s pause, 0.5 s sound.
    const std::string path = writeSpeechLike({{1.0, true}, {1.0, false}, {0.4, true}, {0.3, false}, {1.0, true},
                                              {1.2, false}, {0.5, true}},
                                             "silence.wav");
    std::string error;
    const auto features = analyzeAudio(path, {}, &error);
    INFO(error);
    REQUIRE(features);
    CHECK(seconds(features->duration) == Catch::Approx(5.4).margin(0.02));
    CHECK(features->loudness.size() == 540);
    CHECK(features->loudness[50] == Catch::Approx(-15.05).margin(0.2)); // a 0.25 sine: -15 dBFS RMS
    CHECK(features->loudness[150] <= -100.0f);

    const auto silences = findSilences(*features, {}, 0, features->duration);
    REQUIRE(silences.size() == 2);
    CHECK(seconds(silences[0].start) == Catch::Approx(1.1).margin(0.011));
    CHECK(seconds(silences[0].end) == Catch::Approx(1.9).margin(0.011));
    CHECK(seconds(silences[1].start) == Catch::Approx(3.8).margin(0.011));
    CHECK(seconds(silences[1].end) == Catch::Approx(4.8).margin(0.011));

    // Shorter pauses count with a smaller minimum; a range limits the search.
    CHECK(findSilences(*features, {.minDuration = 0.25}, 0, features->duration).size() == 3);
    const auto late = findSilences(*features, {.padding = 0}, 4 * kSec, features->duration);
    REQUIRE(late.size() == 1);
    CHECK(seconds(late[0].start) == Catch::Approx(4.0).margin(0.011));
    // Loud enough under a high threshold, nothing is silence.
    CHECK(findSilences(*features, {.thresholdDb = -100}, 0, features->duration).size() == 2);
    CHECK(findSilences(*features, {.thresholdDb = -10}, 0, features->duration).size() == 1); // all quieter: one long "pause"
    fs::remove(path);
}

TEST_CASE("speech spans for ducking, with short gaps bridged")
{
    const std::string path = writeSpeechLike({{0.5, false}, {1.0, true}, {0.2, false}, {0.8, true}, {1.5, false}, {0.05, true},
                                              {0.5, false}},
                                             "speech.wav");
    const auto features = analyzeAudio(path);
    REQUIRE(features);
    const auto speech = findSpeech(*features, {});
    REQUIRE(speech.size() == 1); // the 0.2 s gap is bridged; the 50 ms blip is too short
    CHECK(seconds(speech[0].start) == Catch::Approx(0.5).margin(0.011));
    CHECK(seconds(speech[0].end) == Catch::Approx(2.5).margin(0.011));
    CHECK(findSpeech(*features, {.hold = 0.1}).size() == 2);
    fs::remove(path);
}

TEST_CASE("beats and tempo of click tracks")
{
    for (const auto& [bpm, offset] : {std::pair{120.0, 0.25}, std::pair{90.0, 0.4}, std::pair{140.0, 0.1}}) {
        INFO(bpm << " BPM");
        const std::string path = writeClicks(bpm, offset, 12.0, "clicks.wav");
        const auto features = analyzeAudio(path);
        REQUIRE(features);
        const Beats beats = findBeats(*features);
        CHECK(beats.bpm == Catch::Approx(bpm).margin(1.5));
        // Nearly every click is a beat, within 20 ms.
        const double period = 60.0 / bpm;
        int hits = 0;
        int clicks = 0;
        for (double t = offset; t < 11.5; t += period, ++clicks) {
            for (Ticks beat : beats.times) {
                if (std::abs(seconds(beat) - t) <= 0.02) {
                    ++hits;
                    break;
                }
            }
        }
        CHECK(hits >= clicks - 2);
        CHECK(beats.times.size() <= std::size_t(clicks + 1)); // no extra beats between clicks
        fs::remove(path);
    }
    // Silence has no beat.
    const std::string silent = writeSpeechLike({{3.0, false}}, "silent.wav");
    CHECK(findBeats(*analyzeAudio(silent)).times.empty());
    fs::remove(silent);
}

TEST_CASE("scene cuts between shots, not within them")
{
    // Three one-second shots at 30 fps, each with movement inside it: a red field with a
    // bar sliding across, a blue gradient drifting, a green field with a growing disc.
    const std::string path = tempFile("scenes.mov");
    MediaSettings settings;
    settings.container = Container::Mov;
    settings.codec = VideoCodec::ProRes;
    settings.proRes = ProResProfile::Proxy;
    settings.width = 160;
    settings.height = 90;
    settings.audio = false;
    auto writer = MediaWriter::open(path, settings);
    REQUIRE(writer);
    std::vector<std::uint8_t> rgba(160 * 90 * 4);
    for (int f = 0; f < 90; ++f) {
        for (int y = 0; y < 90; ++y)
            for (int x = 0; x < 160; ++x) {
                std::uint8_t* p = &rgba[std::size_t((y * 160 + x) * 4)];
                const int shot = f / 30;
                const int t = f % 30;
                if (shot == 0) {
                    const bool bar = std::abs(x - t * 5) < 8;
                    p[0] = bar ? 250 : 200; p[1] = bar ? 220 : 30; p[2] = 30;
                } else if (shot == 1) {
                    p[0] = 20; p[1] = std::uint8_t((x + t * 2) % 160); p[2] = 200;
                } else {
                    const bool disc = std::hypot(x - 80, y - 45) < 10 + t;
                    p[0] = disc ? 240 : 30; p[1] = disc ? 240 : 170; p[2] = disc ? 240 : 60;
                }
                p[3] = 255;
            }
        REQUIRE(writer->writeVideoRgba(rgba.data(), 160 * 4));
    }
    REQUIRE(writer->finish());

    std::string error;
    const auto features = analyzeVideo(path, {}, &error);
    INFO(error);
    REQUIRE(features);
    REQUIRE(features->times.size() == 90);
    const auto cuts = findSceneCuts(*features, {});
    REQUIRE(cuts.size() == 2);
    CHECK(cuts[0] == features->times[30]);
    CHECK(cuts[1] == features->times[60]);
    // A long minimum scene keeps the first cut only; a threshold of 1 (total change) none.
    CHECK(findSceneCuts(*features, {.minScene = 1.5}).size() == 1);
    CHECK(findSceneCuts(*features, {.threshold = 1.0}).empty());

    // Cancelling stops the work.
    CHECK_FALSE(analyzeVideo(path, [](double) { return false; }));
    fs::remove(path);
}

TEST_CASE("features are cached by fingerprint and version")
{
    const fs::path dir = fs::temp_directory_path() / ("tve_analysis_cache_" + std::to_string(::getpid()));
    fs::remove_all(dir);
    AnalysisCache cache(dir.string());
    const AudioFeatures audio{.loudness = {-20.0f, -120.0f, -30.5f}, .onset = {0.0f, 1.5f, 0.25f}, .duration = 3 * kSec / 100};
    const VideoFeatures video{.times = {0, 1000, 2000}, .change = {0.0f, 0.5f, 0.1f}};
    CHECK_FALSE(cache.loadAudio("v1:10:abc"));
    REQUIRE(cache.save("v1:10:abc", audio));
    REQUIRE(cache.save("v1:10:abc", video));
    CHECK(cache.loadAudio("v1:10:abc") == audio);
    CHECK(cache.loadVideo("v1:10:abc") == video);
    CHECK_FALSE(cache.loadAudio("v1:10:abd"));
    // A file that is not one of ours, or cut short, is ignored.
    for (const auto& entry : fs::directory_iterator(dir))
        fs::resize_file(entry.path(), 10);
    CHECK_FALSE(cache.loadAudio("v1:10:abc"));
    CHECK_FALSE(cache.loadVideo("v1:10:abc"));
    fs::remove_all(dir);
}
