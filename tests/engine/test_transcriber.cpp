#include "decode/AudioDecoder.h"
#include "encode/MediaWriter.h"
#include "model/Captions.h"
#include "speech/Transcriber.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <numbers>

#include <unistd.h>

using namespace tve;
namespace fs = std::filesystem;

namespace {

std::string sample(const char* name)
{
    return (fs::path(TVE_SAMPLES_DIR) / name).string();
}

void requireSpeech()
{
    if (!fs::exists(sample("speech_en.wav")))
        SKIP("speech_en.wav missing (run tools/make_test_media.sh)");
    if (!fs::exists(sample("models/ggml-base.en-q5_1.bin")))
        SKIP("speech model missing (run tools/fetch_models.sh)");
}

std::string normalized(std::string word)
{
    std::erase_if(word, [](unsigned char c) { return !std::isalnum(c); });
    std::ranges::transform(word, word.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return word;
}

} // namespace

TEST_CASE("speech is resampled to 16 kHz mono with its pitch kept")
{
    const std::string path = (fs::temp_directory_path() / ("tve_speech_tone_" + std::to_string(::getpid()) + ".wav")).string();
    MediaSettings settings;
    settings.container = Container::Wav;
    settings.audioCodec = AudioCodec::Pcm16;
    auto writer = MediaWriter::open(path, settings);
    REQUIRE(writer);
    std::vector<float> tone(std::size_t(audio::kSampleRate) * 2);
    for (std::size_t i = 0; i < tone.size() / 2; ++i)
        tone[i * 2] = tone[i * 2 + 1] = 0.5f * float(std::sin(2.0 * std::numbers::pi * 1000.0 * double(i) / audio::kSampleRate));
    REQUIRE(writer->writeAudio(tone.data(), int(tone.size() / 2)));
    REQUIRE(writer->finish());

    const auto samples = speechSamples(path);
    REQUIRE(samples);
    CHECK(samples->size() == 16000);
    // A 1 kHz tone at 16 kHz: 16 samples per cycle, amplitude kept.
    float peak = 0.0f;
    int crossings = 0;
    for (std::size_t i = 200; i < 15800; ++i) {
        peak = std::max(peak, std::abs((*samples)[i]));
        crossings += ((*samples)[i - 1] < 0.0f) != ((*samples)[i] < 0.0f);
    }
    CHECK(peak == Catch::Approx(0.5f).margin(0.02f));
    CHECK(crossings == Catch::Approx(2 * 1000 * 15600 / 16000).margin(4));
    fs::remove(path);
}

TEST_CASE("speech becomes words with their times")
{
    requireSpeech();
    const auto started = std::chrono::steady_clock::now();
    std::string error;
    const auto transcript = transcribe(sample("speech_en.wav"), sample("models/ggml-base.en-q5_1.bin"), {.language = "en"},
                                       {}, &error);
    INFO(error);
    REQUIRE(transcript);
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    WARN("7 s of speech transcribed in " << seconds << " s");
    CHECK(transcript->language == "en");

    std::string said;
    for (const TranscriptWord& w : transcript->words)
        said += normalized(w.text) + " ";
    INFO(said);
    CHECK(said.find("quick brown fox jumps over the lazy dog") != std::string::npos);
    CHECK(said.find("edit our videos on the timeline every day") != std::string::npos);

    // Each sentence on its side of the pause (2.62–4.33 s in the sample), the words drawn in to
    // where the sound is: the pause belongs to no word.
    const auto find = [&](const char* text) {
        return std::ranges::find_if(transcript->words, [&](const TranscriptWord& w) { return normalized(w.text) == text; });
    };
    REQUIRE(find("dog") != transcript->words.end());
    REQUIRE(find("we") != transcript->words.end());
    CHECK(toSeconds(find("dog")->end) == Catch::Approx(2.62).margin(0.1));
    CHECK(toSeconds(find("we")->start) == Catch::Approx(4.33).margin(0.1));
    CHECK(toSeconds(transcript->words.back().end) <= 7.3);
    for (std::size_t i = 1; i < transcript->words.size(); ++i)
        CHECK(transcript->words[i].start >= transcript->words[i - 1].start);
    float sure = 0.0f;
    for (const TranscriptWord& w : transcript->words)
        sure += w.probability;
    CHECK(sure / float(transcript->words.size()) > 0.6f);

    // Kept and read back.
    const fs::path dir = fs::temp_directory_path() / ("tve_transcripts_" + std::to_string(::getpid()));
    const AnalysisCache cache(dir.string());
    REQUIRE(saveTranscript(cache, "v1:1:ab", "base.en-q5_1.en", *transcript));
    CHECK(loadTranscript(cache, "v1:1:ab", "base.en-q5_1.en") == transcript);
    CHECK_FALSE(loadTranscript(cache, "v1:1:ab", "small.en"));
    fs::remove_all(dir);
}

TEST_CASE("transcription can be cancelled, and a missing model is an error")
{
    requireSpeech();
    std::string error = "unset";
    CHECK_FALSE(transcribe(sample("speech_en.wav"), sample("models/ggml-base.en-q5_1.bin"), {}, [](double p) { return p < 0.3; },
                           &error));
    CHECK(error.empty()); // cancelled, not failed
    CHECK_FALSE(transcribe(sample("speech_en.wav"), "/nonexistent/model.bin", {}, {}, &error));
    CHECK(error.find("cannot load the speech model") == 0);
}

TEST_CASE("fillers are written down, so they can be cut")
{
    requireSpeech();
    if (!fs::exists(sample("speech_fillers.wav")))
        SKIP("speech_fillers.wav missing (run tools/make_test_media.sh)");
    std::string error;
    const auto transcript = transcribe(sample("speech_fillers.wav"), sample("models/ggml-base.en-q5_1.bin"), {.language = "en"}, {},
                                       &error);
    INFO(error);
    REQUIRE(transcript);
    std::string said;
    int fillers = 0;
    for (const TranscriptWord& w : transcript->words) {
        said += w.text + " ";
        fillers += isFillerWord(w.text);
    }
    INFO(said);
    CHECK(fillers >= 1);
    CHECK(said.find("short clips") != std::string::npos);
}
