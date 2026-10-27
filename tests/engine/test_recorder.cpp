#include "audio/AudioRecorder.h"
#include "decode/AudioDecoder.h"
#include "encode/WavWriter.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cmath>
#include <filesystem>
#include <numbers>
#include <thread>
#include <vector>

using namespace tve;

namespace {

constexpr Ticks kSec = kTicksPerSecond;

std::string tempPath(const char* name)
{
    return (std::filesystem::temp_directory_path() / name).string();
}

float tone(std::int64_t sampleIndex)
{
    return 0.5f * float(std::sin(2.0 * std::numbers::pi * 440.0 * double(sampleIndex) / audio::kSampleRate));
}

std::vector<float> toneBlock(std::int64_t first, int frames)
{
    std::vector<float> block(static_cast<std::size_t>(frames));
    for (int i = 0; i < frames; ++i)
        block[std::size_t(i)] = tone(first + i);
    return block;
}

// The whole file as the engine decodes it (48 kHz stereo).
std::vector<float> decodeAll(const std::string& path)
{
    auto decoder = AudioDecoder::open(path);
    REQUIRE(decoder);
    std::vector<float> stereo;
    std::vector<float> buffer(4096 * 2);
    while (int n = decoder->read(buffer.data(), 4096))
        stereo.insert(stereo.end(), buffer.begin(), buffer.begin() + n * 2);
    return stereo;
}

} // namespace

TEST_CASE("a WAV file reads back as written", "[recorder]")
{
    const std::string path = tempPath("tve_wav_writer.wav");
    {
        auto writer = WavWriter::open(path, audio::kSampleRate, 1);
        REQUIRE(writer);
        for (int block = 0; block < 10; ++block)
            REQUIRE(writer->write(toneBlock(block * 4800, 4800).data(), 4800));
        CHECK(writer->frames() == 48000);
        REQUIRE(writer->finish());
    }
    CHECK(std::filesystem::file_size(path) == 58 + 48000 * 4);

    auto decoder = AudioDecoder::open(path);
    REQUIRE(decoder);
    CHECK(decoder->info().sourceSampleRate == 48000);
    CHECK(decoder->info().sourceChannels == 1);
    CHECK(decoder->info().duration == kSec);

    const std::vector<float> stereo = decodeAll(path);
    REQUIRE(stereo.size() == 48000 * 2);
    for (std::int64_t i : {0, 1000, 23999, 47999}) {
        // Mono plays at full level on both sides.
        CHECK(stereo[std::size_t(i) * 2] == Catch::Approx(tone(i)).margin(1e-4));
        CHECK(stereo[std::size_t(i) * 2 + 1] == Catch::Approx(tone(i)).margin(1e-4));
    }
    std::filesystem::remove(path);
}

TEST_CASE("a WAV file cut short after a flush still opens", "[recorder]")
{
    const std::string path = tempPath("tve_wav_partial.wav");
    const std::string copy = tempPath("tve_wav_partial_copy.wav");
    auto writer = WavWriter::open(path, audio::kSampleRate, 1);
    REQUIRE(writer);
    REQUIRE(writer->write(toneBlock(0, 24000).data(), 24000));
    REQUIRE(writer->flush());
    // What a crash would leave: the file as it is now, never finished.
    std::filesystem::copy_file(path, copy, std::filesystem::copy_options::overwrite_existing);
    auto decoder = AudioDecoder::open(copy);
    REQUIRE(decoder);
    CHECK(decoder->info().duration == kSec / 2);
    writer.reset();
    std::filesystem::remove(path);
    std::filesystem::remove(copy);
}

TEST_CASE("a WAV writer refuses bad formats and paths", "[recorder]")
{
    std::string error;
    CHECK_FALSE(WavWriter::open(tempPath("tve_bad.wav"), 0, 1, &error));
    CHECK_FALSE(error.empty());
    error.clear();
    CHECK_FALSE(WavWriter::open("/no/such/folder/take.wav", 48000, 1, &error));
    CHECK_FALSE(error.empty());
}

TEST_CASE("a recorder writes each take to its own file", "[recorder]")
{
    AudioRecorder recorder(AudioRecorder::Backend::None);
    REQUIRE(recorder.isOpen());
    CHECK(recorder.latency() == 0);

    std::string error;
    CHECK_FALSE(recorder.stop(&error));
    CHECK(error == "not recording");

    // Input before a take is metered but not kept.
    recorder.push(toneBlock(0, 480).data(), 480);
    CHECK(recorder.peak() == Catch::Approx(0.5f).margin(0.01));

    const std::string first = tempPath("tve_take_1.wav");
    REQUIRE(recorder.start(first, &error));
    CHECK(recorder.recording());
    CHECK_FALSE(recorder.start(first, &error));
    CHECK(error == "already recording");
    for (int block = 0; block < 100; ++block) // one second in 10 ms blocks
        recorder.push(toneBlock(block * 480, 480).data(), 480);
    CHECK(recorder.recordedDuration() == kSec);
    auto take = recorder.stop(&error);
    REQUIRE(take);
    CHECK_FALSE(recorder.recording());
    CHECK(take->path == first);
    CHECK(take->duration == kSec);
    CHECK(recorder.droppedFrames() == 0);

    const std::vector<float> stereo = decodeAll(first);
    REQUIRE(stereo.size() == 48000 * 2);
    for (std::int64_t i : {0, 4321, 47999})
        CHECK(stereo[std::size_t(i) * 2] == Catch::Approx(tone(i)).margin(1e-4));

    // Input between takes goes nowhere; the second take starts fresh.
    recorder.push(toneBlock(0, 4800).data(), 4800);
    const std::string second = tempPath("tve_take_2.wav");
    REQUIRE(recorder.start(second));
    recorder.push(toneBlock(0, 2400).data(), 2400);
    take = recorder.stop();
    REQUIRE(take);
    CHECK(take->duration == kSec / 20);
    CHECK(AudioDecoder::open(first)->info().duration == kSec); // untouched

    std::filesystem::remove(first);
    std::filesystem::remove(second);
}

TEST_CASE("a take with nothing in it leaves no file", "[recorder]")
{
    AudioRecorder recorder(AudioRecorder::Backend::None);
    const std::string path = tempPath("tve_take_empty.wav");
    REQUIRE(recorder.start(path));
    CHECK(std::filesystem::exists(path));
    std::string error;
    CHECK_FALSE(recorder.stop(&error));
    CHECK(error == "nothing was recorded");
    CHECK_FALSE(std::filesystem::exists(path));

    CHECK_FALSE(recorder.start("/no/such/folder/take.wav", &error));
    CHECK(error.starts_with("cannot create"));
    CHECK_FALSE(recorder.recording());
}

TEST_CASE("a recorder destroyed mid-take keeps what it recorded", "[recorder]")
{
    const std::string path = tempPath("tve_take_unfinished.wav");
    {
        AudioRecorder recorder(AudioRecorder::Backend::None);
        REQUIRE(recorder.start(path));
        recorder.push(toneBlock(0, 9600).data(), 9600);
    }
    auto decoder = AudioDecoder::open(path);
    REQUIRE(decoder);
    CHECK(decoder->info().duration == kSec / 5);
    std::filesystem::remove(path);
}

TEST_CASE("a recorder on a real-time device records for as long as it runs", "[recorder]")
{
    CHECK_FALSE(AudioRecorder::inputDevices(AudioRecorder::Backend::Null).empty());
    CHECK(AudioRecorder::inputDevices(AudioRecorder::Backend::None).empty());

    AudioRecorder recorder(AudioRecorder::Backend::Null);
    if (!recorder.isOpen())
        SKIP("no null input device: " << recorder.errorString());
    CHECK_FALSE(recorder.deviceName().empty());
    CHECK(recorder.latency() > 0);
    CHECK(recorder.latency() < kSec / 5);

    const std::string path = tempPath("tve_take_null.wav");
    REQUIRE(recorder.start(path));
    const auto begun = std::chrono::steady_clock::now();
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    auto take = recorder.stop();
    const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - begun).count();
    REQUIRE(take);
    // Real time, less the latency dropped from the start and a period or so in flight.
    const double seconds = double(take->duration) / kSec;
    CHECK(seconds > 0.25);
    CHECK(seconds <= elapsed + 0.01);
    CHECK(recorder.peak() == 0.0f); // the null device records silence
    CHECK(AudioDecoder::open(path)->info().duration == take->duration);
    std::filesystem::remove(path);
}
