#include "commands/CommandStack.h"
#include "commands/EditCommands.h"
#include "decode/VideoDecoder.h"
#include "playback/PlaybackClock.h"
#include "playback/TimelinePlayback.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <thread>

using namespace tve;

namespace {

constexpr Ticks kSec = kTicksPerSecond;
constexpr Ticks kFrame30 = kSec / 30;

std::string sample(const char* name)
{
    return (std::filesystem::path(TVE_SAMPLES_DIR) / name).string();
}

void requireSamples()
{
    for (const char* name : {"test_1080p30_h264.mp4", "test_720p30_mjpeg.mov"}) {
        if (!std::filesystem::exists(sample(name)))
            SKIP("sample missing: " << name << " (run tools/make_test_media.sh)");
    }
}

// Drains every frame the producer queues, in order. Returns (pts, isBlack) pairs.
std::vector<std::pair<Ticks, bool>> drain(TimelinePlayback& playback)
{
    std::vector<std::pair<Ticks, bool>> frames;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (std::chrono::steady_clock::now() < deadline) {
        if (auto front = playback.queue().frontPts()) {
            int skipped = 0;
            auto frame = playback.queue().takeLatestDue(*front, skipped);
            REQUIRE(skipped == 0);
            frames.emplace_back(frame->pts(), !frame->pixelBuffer());
            continue;
        }
        if (playback.producerFinished())
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return frames;
}

} // namespace

TEST_CASE("decoder seek lands on the frame on screen at the target")
{
    requireSamples();
    auto decoder = VideoDecoder::open(sample("test_1080p30_h264.mp4"));
    REQUIRE(decoder);

    REQUIRE(decoder->seek(37 * kFrame30));
    CHECK(decoder->nextFrame()->pts() == 37 * kFrame30);

    REQUIRE(decoder->seek(37 * kFrame30 + kFrame30 / 2)); // between frames 37 and 38
    CHECK(decoder->nextFrame()->pts() == 37 * kFrame30);

    REQUIRE(decoder->seek(5 * kFrame30)); // backwards
    CHECK(decoder->nextFrame()->pts() == 5 * kFrame30);
    CHECK(decoder->nextFrame()->pts() == 6 * kFrame30);
}

TEST_CASE("clock advances only while playing")
{
    using namespace std::chrono;
    PlaybackClock clock;
    const auto t0 = steady_clock::now();
    clock.seek(2 * kSec, t0);
    CHECK(clock.now(t0 + seconds(5)) == 2 * kSec); // paused
    clock.play(t0 + seconds(1));
    CHECK(clock.now(t0 + seconds(3)) == 4 * kSec);
    clock.pause(t0 + seconds(4));
    CHECK(clock.now(t0 + seconds(9)) == 5 * kSec);
    clock.seek(kSec, t0 + seconds(9));
    CHECK(clock.now(t0 + seconds(10)) == kSec);
}

TEST_CASE("timeline playback stitches clips, gaps and track priority")
{
    requireSamples();
    auto project = std::make_shared<Project>();
    CommandStack stack(*project);
    auto push = [&](std::unique_ptr<Command> c) { REQUIRE_FALSE(stack.push(std::move(c))); };

    auto v1 = std::make_unique<AddTrackCommand>(TrackKind::Video, "V1");
    auto* v1Cmd = v1.get();
    push(std::move(v1));
    auto v2 = std::make_unique<AddTrackCommand>(TrackKind::Video, "V2");
    auto* v2Cmd = v2.get();
    push(std::move(v2));
    auto h264 = std::make_unique<ImportMediaCommand>(sample("test_1080p30_h264.mp4"), 2 * kSec);
    auto* h264Cmd = h264.get();
    push(std::move(h264));
    auto mjpeg = std::make_unique<ImportMediaCommand>(sample("test_720p30_mjpeg.mov"), 2 * kSec);
    auto* mjpegCmd = mjpeg.get();
    push(std::move(mjpeg));

    // V1: h264 source 0.5–1.5 s at 0–1 s, gap 1–1.5 s, mjpeg source 0–0.5 s at 1.5–2 s.
    // V2: mjpeg covering 0.5–0.6 s (3 frames) on top of the first clip.
    push(std::make_unique<AddClipCommand>(v1Cmd->trackId(), h264Cmd->mediaId(), 0, kSec, kSec / 2));
    push(std::make_unique<AddClipCommand>(v1Cmd->trackId(), mjpegCmd->mediaId(), 3 * kSec / 2, kSec / 2, 0));
    push(std::make_unique<AddClipCommand>(v2Cmd->trackId(), mjpegCmd->mediaId(), 15 * kFrame30, 3 * kFrame30, 0));

    TimelinePlayback playback;
    playback.setProject(project);
    const auto frames = drain(playback);

    // 30 frames of clip 1 (incl. the 3 replaced by V2), 1 black, 15 frames of clip 2.
    REQUIRE(frames.size() == 30 + 1 + 15);
    for (int i = 0; i < 30; ++i) {
        INFO("frame " << i);
        CHECK(frames[std::size_t(i)] == std::pair{i * kFrame30, false});
    }
    CHECK(frames[30] == std::pair{kSec, true});
    CHECK(frames[31] == std::pair{3 * kSec / 2, false});
    CHECK(frames.back().first == 3 * kSec / 2 + 14 * kFrame30);
    CHECK(playback.hardwareAccelerated() == false); // the last clip is MJPEG (software)
}

TEST_CASE("seeking restarts the timeline at the requested frame")
{
    requireSamples();
    auto project = std::make_shared<Project>();
    CommandStack stack(*project);
    auto track = std::make_unique<AddTrackCommand>(TrackKind::Video, "V1");
    auto* trackCmd = track.get();
    REQUIRE_FALSE(stack.push(std::move(track)));
    auto media = std::make_unique<ImportMediaCommand>(sample("test_1080p30_h264.mp4"), 2 * kSec);
    auto* mediaCmd = media.get();
    REQUIRE_FALSE(stack.push(std::move(media)));
    REQUIRE_FALSE(stack.push(std::make_unique<AddClipCommand>(trackCmd->trackId(), mediaCmd->mediaId(), kSec, kSec, 0)));

    TimelinePlayback playback;
    playback.setProject(project);
    playback.seek(kSec + 10 * kFrame30 + 7); // just after frame 10 of the clip
    const auto frames = drain(playback);
    REQUIRE(frames.size() == 20);
    CHECK(frames.front() == std::pair{kSec + 10 * kFrame30 + 7, false}); // shown from the seek time
    CHECK(frames[1].first == kSec + 11 * kFrame30);
    CHECK(playback.clock().now() == kSec + 10 * kFrame30 + 7); // paused at the seek time
}
