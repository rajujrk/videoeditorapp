#include "capture/CaptureRecorder.h"
#include "decode/AudioDecoder.h"
#include "decode/Thumbnail.h"
#include "decode/VideoDecoder.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <thread>

#include <unistd.h>

using namespace tve;
using Catch::Approx;
namespace fs = std::filesystem;

TEST_CASE("a capture is recorded at a steady frame rate with its sound")
{
    const std::string path = (fs::temp_directory_path() / ("tve_capture_" + std::to_string(::getpid()) + ".mov")).string();
    CaptureRecorder recorder(makeTestSource(640, 360, 30, true), 30);
    std::string error;
    REQUIRE(recorder.start(path, &error));
    CHECK(recorder.recording());
    std::this_thread::sleep_for(std::chrono::milliseconds(2000));
    CHECK(toSeconds(recorder.elapsed()) == Approx(2.0).margin(0.2));
    const auto take = recorder.stop(&error);
    REQUIRE(take);
    INFO(error);
    CHECK(take->width == 640);
    CHECK(take->height == 360);
    CHECK(take->hasSound);
    CHECK(toSeconds(take->duration) == Approx(2.0).margin(0.15));

    auto video = VideoDecoder::open(path);
    REQUIRE(video);
    CHECK(video->info().width == 640);
    CHECK(video->info().frameRate.num == 30 * video->info().frameRate.den);
    std::vector<RgbaImage> frames;
    while (auto frame = video->nextFrame())
        if (auto p = pictureOf(*frame, 90))
            frames.push_back(std::move(*p));
    CHECK(int(frames.size()) == Approx(60).margin(5));
    // The bars move: frames a second apart differ.
    double change = 0.0;
    for (std::size_t i = 0; i < frames[10].pixels.size(); ++i)
        change += std::abs(int(frames[10].pixels[i]) - int(frames[40].pixels[i]));
    CHECK(change / double(frames[10].pixels.size()) > 5.0);
    // The tone is there, about as long as the pictures.
    auto sound = AudioDecoder::open(path);
    REQUIRE(sound);
    CHECK(toSeconds(sound->info().duration) == Approx(toSeconds(take->duration)).margin(0.25));
    std::vector<float> chunk(48000 * 2);
    sound->seek(kTicksPerSecond / 2);
    const int read = sound->read(chunk.data(), 24000);
    float peak = 0.0f;
    for (int i = 0; i < read * 2; ++i)
        peak = std::max(peak, std::abs(chunk[std::size_t(i)]));
    CHECK(peak == Approx(0.25f).margin(0.05));
    fs::remove(path);

    // Stopping when nothing records says so.
    CHECK_FALSE(recorder.stop(&error));
    CHECK_FALSE(error.empty());
}

TEST_CASE("the cursor log follows the mouse and its clicks")
{
    CursorLog log({0, 0, 1280, 720}, true);
    log.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(2300));
    const CursorEffects cursor = log.stop();
    REQUIRE(cursor.hasLog());
    // Clicks at the whole seconds (1 and 2).
    REQUIRE(cursor.clicks.size() == 2);
    CHECK(toSeconds(cursor.clicks[0]) == Approx(1.0).margin(0.05));
    CHECK(toSeconds(cursor.clicks[1]) == Approx(2.0).margin(0.05));
    // A quarter circle a second around the middle: at 1 s, at (0.5, 0.8).
    const auto at1 = cursor.at(kTicksPerSecond);
    REQUIRE(at1);
    CHECK(at1->x == Approx(0.5).margin(0.02));
    CHECK(at1->y == Approx(0.8).margin(0.02));
    CHECK(cursor.path.size() < 120); // thinned
    CHECK(cursor.sinceClick(kTicksPerSecond + kTicksPerSecond / 5, 0.45).value() == Approx(0.2).margin(0.05));
    CHECK_FALSE(cursor.sinceClick(kTicksPerSecond / 2, 0.45));
}

TEST_CASE("a paused capture writes nothing until resumed, in the same file")
{
    const std::string path = (fs::temp_directory_path() / ("tve_capture_paused_" + std::to_string(::getpid()) + ".mov")).string();
    CaptureRecorder recorder(makeTestSource(320, 180, 30, true), 30);
    std::string error;
    recorder.pause(); // (not recording: nothing to pause)
    CHECK_FALSE(recorder.paused());
    REQUIRE(recorder.start(path, &error));
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    recorder.pause();
    CHECK(recorder.paused());
    const double atPause = toSeconds(recorder.elapsed());
    std::this_thread::sleep_for(std::chrono::milliseconds(1200));
    // The time stands still while paused.
    CHECK(toSeconds(recorder.elapsed()) == Approx(atPause).margin(0.01));
    recorder.resume();
    CHECK_FALSE(recorder.paused());
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    CHECK(toSeconds(recorder.elapsed()) == Approx(2.0).margin(0.2));
    const auto take = recorder.stop(&error);
    REQUIRE(take);
    // Two seconds recorded, not the three and more that went by.
    CHECK(toSeconds(take->duration) == Approx(2.0).margin(0.2));
    auto video = VideoDecoder::open(path);
    REQUIRE(video);
    CHECK(toSeconds(video->info().duration) == Approx(2.0).margin(0.2));
    auto sound = AudioDecoder::open(path);
    REQUIRE(sound);
    CHECK(toSeconds(sound->info().duration) == Approx(toSeconds(take->duration)).margin(0.25));
    fs::remove(path);
}

TEST_CASE("a paused cursor log leaves the paused time out")
{
    CursorLog log({0, 0, 1280, 720}, true);
    log.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    log.pause();
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    log.resume();
    std::this_thread::sleep_for(std::chrono::milliseconds(800));
    const CursorEffects cursor = log.stop();
    REQUIRE(cursor.hasLog());
    // About 2.3 s logged: clicks at 1 and 2 of its own time, none from the second paused.
    REQUIRE(cursor.clicks.size() == 2);
    CHECK(toSeconds(cursor.clicks[1]) == Approx(2.0).margin(0.06));
    CHECK(toSeconds(cursor.path.back().time) == Approx(2.3).margin(0.15));
}
