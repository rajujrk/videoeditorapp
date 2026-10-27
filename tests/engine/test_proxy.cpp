#include "decode/AudioDecoder.h"
#include "decode/Thumbnail.h"
#include "decode/VideoDecoder.h"
#include "encode/Proxy.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <vector>

using namespace tve;

namespace {

constexpr Ticks kSec = kTicksPerSecond;

std::string sample(const char* name)
{
    return (std::filesystem::path(TVE_SAMPLES_DIR) / name).string();
}

std::string tempFile(const char* name)
{
    return (std::filesystem::temp_directory_path() / "tve-proxy-tests" / name).string();
}

std::vector<Ticks> framePts(const std::string& path)
{
    std::vector<Ticks> pts;
    auto decoder = VideoDecoder::open(path);
    REQUIRE(decoder);
    while (auto frame = decoder->nextFrame())
        pts.push_back(frame->pts());
    return pts;
}

// The test pattern's first colour bar is red, its second green (at 1/16 and 3/16 across).
void checkBars(const std::string& path, Ticks time)
{
    auto thumb = extractThumbnail(path, time, 90);
    REQUIRE(thumb);
    const auto pixel = [&](double fx) {
        return &thumb->pixels[std::size_t((45 * thumb->width + int(fx * thumb->width)) * 4)];
    };
    CHECK(pixel(1.0 / 16)[0] > 180);
    CHECK(pixel(1.0 / 16)[1] < 80);
    CHECK(pixel(3.0 / 16)[1] > 150);
    CHECK(pixel(3.0 / 16)[0] < 80);
}

} // namespace

TEST_CASE("proxy sizes keep the shape and cap the short side")
{
    CHECK(proxySize(3840, 2160, 720) == std::pair{1280, 720});
    CHECK(proxySize(1080, 1920, 720) == std::pair{720, 1280}); // portrait
    CHECK(proxySize(1920, 1080, 360) == std::pair{640, 360});
    CHECK(proxySize(640, 360, 720) == std::pair{640, 360});    // already small
}

TEST_CASE("a proxy has the same frames at the same times, smaller, with frequent keyframes")
{
    const std::string source = sample("test_1080p30_h264.mp4");
    if (!std::filesystem::exists(source))
        SKIP("sample missing: " << source);
    const std::string proxy = tempFile("bars_360.mp4");
    std::string error;
    double last = 0;
    REQUIRE(makeProxy(source, proxy, {360, 10}, [&](double p) { last = p; return true; }, &error));
    CHECK(error.empty());
    CHECK(last == 1.0);

    auto decoder = VideoDecoder::open(proxy);
    REQUIRE(decoder);
    CHECK(decoder->info().width == 640);
    CHECK(decoder->info().height == 360);
    CHECK(framePts(proxy) == framePts(source));
    checkBars(proxy, kSec / 2);
    // Keyframes every 10 frames (1/3 s at 30 fps): cheap to seek and to play backwards.
    CHECK(decoder->keyframeBefore(kSec / 2) == kSec / 3);
    CHECK_FALSE(AudioDecoder::open(proxy)); // pictures only
    CHECK_FALSE(std::filesystem::exists(proxy + ".part"));
}

TEST_CASE("10-bit sources make proxies with the right colours")
{
    const std::string source = sample("test_hevc10_1080p30.mp4");
    if (!std::filesystem::exists(source))
        SKIP("sample missing: " << source);
    const std::string proxy = tempFile("hevc10_360.mp4");
    std::string error;
    REQUIRE(makeProxy(source, proxy, {360, 10}, nullptr, &error));
    checkBars(proxy, kSec / 2);
    CHECK(framePts(proxy).size() == 60);
}

TEST_CASE("cancelling or failing leaves no proxy behind")
{
    const std::string source = sample("test_1080p30_h264.mp4");
    if (!std::filesystem::exists(source))
        SKIP("sample missing: " << source);
    const std::string proxy = tempFile("cancelled.mp4");
    std::string error = "unchanged";
    int calls = 0;
    CHECK_FALSE(makeProxy(source, proxy, {360, 10}, [&](double) { return ++calls < 5; }, &error));
    CHECK(error.empty());
    CHECK_FALSE(std::filesystem::exists(proxy));
    CHECK_FALSE(std::filesystem::exists(proxy + ".part"));

    CHECK_FALSE(makeProxy("/nonexistent.mp4", proxy, {}, nullptr, &error));
    CHECK(error.find("/nonexistent.mp4") != std::string::npos);
}

#include "commands/CommandStack.h"
#include "commands/EditCommands.h"
#include "playback/TimelinePlayback.h"

#include <chrono>
#include <thread>

TEST_CASE("playback decodes a media item's proxy when it has one, at the same times")
{
    const std::string source = sample("test_1080p30_h264.mp4");
    if (!std::filesystem::exists(source))
        SKIP("sample missing: " << source);
    const std::string proxy = tempFile("bars_for_playback.mp4");
    REQUIRE(makeProxy(source, proxy, {360, 10}, nullptr, nullptr));

    auto project = std::make_shared<Project>();
    CommandStack stack(*project);
    auto track = std::make_unique<AddTrackCommand>(TrackKind::Video, "V1");
    const auto* trackCmd = track.get();
    REQUIRE_FALSE(stack.push(std::move(track)));
    auto media = std::make_unique<ImportMediaCommand>(source, 2 * kSec);
    const auto* mediaCmd = media.get();
    REQUIRE_FALSE(stack.push(std::move(media)));
    REQUIRE_FALSE(stack.push(std::make_unique<AddClipCommand>(trackCmd->trackId(), mediaCmd->mediaId(), 0, kSec, kSec / 2)));
    project->media[0].proxyPath = proxy;

    TimelinePlayback playback;
    playback.setProject(project);
    std::vector<std::pair<Ticks, int>> shown; // source pts, picture width
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (std::chrono::steady_clock::now() < deadline && !(playback.producerFinished() && !playback.queue().frontPts())) {
        if (auto front = playback.queue().frontPts()) {
            int skipped = 0;
            auto frame = playback.queue().takeLatestDue(*front, skipped);
            REQUIRE(frame->layers.size() == 1);
            shown.emplace_back(frame->layers[0].frame.pts(),
                               int(CVPixelBufferGetWidth(frame->layers[0].frame.pixelBuffer())));
            continue;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    REQUIRE(shown.size() == 30);
    for (std::size_t i = 0; i < shown.size(); ++i) {
        INFO("frame " << i);
        CHECK(shown[i].second == 640);                                   // the proxy's pictures…
        CHECK(shown[i].first == kSec / 2 + Ticks(i) * (kSec / 30));      // …at the source's times
    }
}
