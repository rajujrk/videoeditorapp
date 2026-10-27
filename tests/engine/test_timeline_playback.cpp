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

// Drains every composite frame the producer queues, in order: (time, number of layers).
std::vector<std::pair<Ticks, std::size_t>> drain(TimelinePlayback& playback)
{
    std::vector<std::pair<Ticks, std::size_t>> frames;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (std::chrono::steady_clock::now() < deadline) {
        if (auto front = playback.queue().frontPts()) {
            int skipped = 0;
            auto frame = playback.queue().takeLatestDue(*front, skipped);
            REQUIRE(skipped == 0);
            frames.emplace_back(frame->pts(), frame->layers.size());
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

TEST_CASE("timeline playback composites layers on the project frame grid")
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
    // V2: mjpeg over frames 15–17 of the first clip, with a picture setting and an effect.
    push(std::make_unique<AddClipCommand>(v1Cmd->trackId(), h264Cmd->mediaId(), 0, kSec, kSec / 2));
    push(std::make_unique<AddClipCommand>(v1Cmd->trackId(), mjpegCmd->mediaId(), 3 * kSec / 2, kSec / 2, 0));
    auto overlay = std::make_unique<AddClipCommand>(v2Cmd->trackId(), mjpegCmd->mediaId(), 15 * kFrame30, 3 * kFrame30, 0);
    auto* overlayCmd = overlay.get();
    push(std::move(overlay));
    ClipVisual pip;
    pip.transform.scale = 0.3;
    pip.opacity = 0.8;
    push(std::make_unique<SetClipVisualCommand>(overlayCmd->clipId(), pip));
    push(std::make_unique<SetClipEffectsCommand>(overlayCmd->clipId(),
                                                 std::vector<ClipEffect>{{"tve.color.adjust", true, {}}}, "Add Effect"));

    TimelinePlayback playback;
    playback.setProject(project);
    const auto frames = drain(playback);

    // 30 frames of clip 1 (3 of them with the overlay on top), 1 black, 15 frames of clip 2.
    REQUIRE(frames.size() == 30 + 1 + 15);
    for (int i = 0; i < 30; ++i) {
        INFO("frame " << i);
        const std::size_t layers = i >= 15 && i < 18 ? 2 : 1;
        CHECK(frames[std::size_t(i)] == std::pair{i * kFrame30, layers});
    }
    CHECK(frames[30] == std::pair{kSec, std::size_t(0)});
    CHECK(frames[31] == std::pair{3 * kSec / 2, std::size_t(1)});
    CHECK(frames.back().first == 3 * kSec / 2 + 14 * kFrame30);
    CHECK(playback.hardwareAccelerated() == false); // the last clip is MJPEG (software)
}

TEST_CASE("layers carry their clip's picture settings and effects, top last")
{
    requireSamples();
    auto project = std::make_shared<Project>();
    CommandStack stack(*project);
    auto v1 = std::make_unique<AddTrackCommand>(TrackKind::Video, "V1");
    auto* v1Cmd = v1.get();
    REQUIRE_FALSE(stack.push(std::move(v1)));
    auto v2 = std::make_unique<AddTrackCommand>(TrackKind::Video, "V2");
    auto* v2Cmd = v2.get();
    REQUIRE_FALSE(stack.push(std::move(v2)));
    auto media = std::make_unique<ImportMediaCommand>(sample("test_1080p30_h264.mp4"), 2 * kSec);
    auto* mediaCmd = media.get();
    REQUIRE_FALSE(stack.push(std::move(media)));
    REQUIRE_FALSE(stack.push(std::make_unique<AddClipCommand>(v1Cmd->trackId(), mediaCmd->mediaId(), 0, kSec, 0)));
    auto top = std::make_unique<AddClipCommand>(v2Cmd->trackId(), mediaCmd->mediaId(), 0, kSec, kSec / 2);
    auto* topCmd = top.get();
    REQUIRE_FALSE(stack.push(std::move(top)));
    ClipVisual visual;
    visual.blend = BlendMode::Screen;
    visual.transform.rotation = 45;
    REQUIRE_FALSE(stack.push(std::make_unique<SetClipVisualCommand>(topCmd->clipId(), visual)));

    TimelinePlayback playback;
    playback.setProject(project);
    playback.seek(10 * kFrame30);
    for (int attempt = 0; attempt < 200 && !playback.queue().frontPts(); ++attempt)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    int skipped = 0;
    auto frame = playback.queue().takeLatestDue(10 * kFrame30, skipped);
    REQUIRE(frame);
    REQUIRE(frame->layers.size() == 2);
    CHECK(frame->layers[1].clipId == topCmd->clipId());
    CHECK(frame->layers[1].visual == visual);
    // Same file, different in-points: each layer shows its own source frame.
    CHECK(frame->layers[0].frame.pts() == 10 * kFrame30);
    CHECK(frame->layers[1].frame.pts() == kSec / 2 + 10 * kFrame30);
}

TEST_CASE("a 60 fps project shows each frame of a 30 fps clip twice")
{
    requireSamples();
    auto project = std::make_shared<Project>();
    project->settings.frameRate = {60, 1};
    CommandStack stack(*project);
    auto track = std::make_unique<AddTrackCommand>(TrackKind::Video, "V1");
    auto* trackCmd = track.get();
    REQUIRE_FALSE(stack.push(std::move(track)));
    auto media = std::make_unique<ImportMediaCommand>(sample("test_1080p30_h264.mp4"), 2 * kSec);
    auto* mediaCmd = media.get();
    REQUIRE_FALSE(stack.push(std::move(media)));
    REQUIRE_FALSE(stack.push(std::make_unique<AddClipCommand>(trackCmd->trackId(), mediaCmd->mediaId(), 0, kSec / 2, 0)));

    TimelinePlayback playback;
    playback.setProject(project);
    const auto frames = drain(playback);
    REQUIRE(frames.size() == 30); // 0.5 s at 60 fps
    CHECK(frames[1].first == kSec / 60);
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
    CHECK(frames.front() == std::pair{kSec + 10 * kFrame30 + 7, std::size_t(1)}); // shown from the seek time
    CHECK(frames[1].first == kSec + 11 * kFrame30);
    CHECK(playback.clock().now() == kSec + 10 * kFrame30 + 7); // paused at the seek time
}
