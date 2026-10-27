#include "commands/CommandStack.h"
#include "commands/EditCommands.h"
#include "decode/VideoDecoder.h"
#include "playback/PlaybackClock.h"
#include "playback/TimelinePlayback.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cmath>
#include <filesystem>
#include <map>
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

TEST_CASE("animated clips are placed and filtered as their keyframes say at each frame")
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
    auto add = std::make_unique<AddClipCommand>(trackCmd->trackId(), mediaCmd->mediaId(), 0, kSec, 0);
    auto* addCmd = add.get();
    REQUIRE_FALSE(stack.push(std::move(add)));
    REQUIRE_FALSE(stack.push(std::make_unique<SetClipKeyframesCommand>(
        addCmd->clipId(),
        std::map<std::string, Keyframes>{{"x", {{0, 0.0}, {30 * kFrame30, 0.3}}}, {"opacity", {{15 * kFrame30, 1.0}, {30 * kFrame30, 0.0}}}},
        "Add Keyframe")));
    ClipEffect blur{"tve.blur.gaussian", true, {}};
    blur.keyframes["radius"] = {{0, 0.0}, {kSec, 30.0}};
    REQUIRE_FALSE(stack.push(std::make_unique<SetClipEffectsCommand>(addCmd->clipId(), std::vector<ClipEffect>{blur}, "Add Effect")));

    TimelinePlayback playback;
    playback.setProject(project);
    std::map<Ticks, ClipVisual> visuals;
    std::map<Ticks, float> radii;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (std::chrono::steady_clock::now() < deadline && !(playback.producerFinished() && !playback.queue().frontPts())) {
        if (auto front = playback.queue().frontPts()) {
            int skipped = 0;
            auto frame = playback.queue().takeLatestDue(*front, skipped);
            if (frame && !frame->layers.empty()) {
                visuals[frame->pts()] = frame->layers[0].visual;
                radii[frame->pts()] = frame->layers[0].effects.at(0).values.at("radius").at(0);
                CHECK(frame->layers[0].effects.at(0).keyframes.empty());
            }
            continue;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    REQUIRE(visuals.size() == 30);
    CHECK(radii.at(0) == 0.0f);
    CHECK(radii.at(15 * kFrame30) == Catch::Approx(15.0f));
    CHECK(visuals.at(0).transform.x == Catch::Approx(0.0));
    CHECK(visuals.at(10 * kFrame30).transform.x == Catch::Approx(0.1));
    CHECK(visuals.at(10 * kFrame30).opacity == Catch::Approx(1.0));
    CHECK(visuals.at(20 * kFrame30).opacity == Catch::Approx(2.0 / 3.0));
    CHECK(visuals.at(29 * kFrame30).transform.x == Catch::Approx(0.29));
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

namespace {

// A one-clip project of the 2 s 1080p30 sample with the given speed settings.
std::shared_ptr<Project> speedProject(double speed, bool reversed, bool freezeAtOneSecond = false,
                                      const char* file = "test_1080p30_h264.mp4", Ticks length = 2 * kSec)
{
    auto project = std::make_shared<Project>();
    CommandStack stack(*project);
    auto track = std::make_unique<AddTrackCommand>(TrackKind::Video, "V1");
    auto* trackCmd = track.get();
    REQUIRE_FALSE(stack.push(std::move(track)));
    auto media = std::make_unique<ImportMediaCommand>(sample(file), length);
    auto* mediaCmd = media.get();
    REQUIRE_FALSE(stack.push(std::move(media)));
    auto clip = std::make_unique<AddClipCommand>(trackCmd->trackId(), mediaCmd->mediaId(), 0, length, 0);
    auto* clipCmd = clip.get();
    REQUIRE_FALSE(stack.push(std::move(clip)));
    if (freezeAtOneSecond)
        REQUIRE_FALSE(stack.push(std::make_unique<FreezeFrameCommand>(clipCmd->clipId(), kSec, kSec / 2)));
    else
        REQUIRE_FALSE(stack.push(std::make_unique<SetClipSpeedCommand>(clipCmd->clipId(), speed, reversed)));
    return project;
}

// Source frame times shown by each composite frame, in order.
std::vector<Ticks> shownSourceFrames(TimelinePlayback& playback)
{
    std::vector<Ticks> shown;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (std::chrono::steady_clock::now() < deadline) {
        if (auto front = playback.queue().frontPts()) {
            int skipped = 0;
            auto frame = playback.queue().takeLatestDue(*front, skipped);
            shown.push_back(frame->layers.empty() ? -1 : frame->layers.front().frame.pts());
            continue;
        }
        if (playback.producerFinished())
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return shown;
}

} // namespace

TEST_CASE("double speed shows every other source frame")
{
    requireSamples();
    TimelinePlayback playback;
    playback.setProject(speedProject(2.0, false));
    const auto shown = shownSourceFrames(playback);
    REQUIRE(shown.size() == 30); // 1 s of timeline at 30 fps
    for (std::size_t i = 0; i < shown.size(); ++i)
        CHECK(shown[i] == Ticks(2 * i) * kFrame30);
}

TEST_CASE("a speed ramp shows source frames ever further apart")
{
    requireSamples();
    auto project = speedProject(1.0, false);
    const Id clip = project->tracks.front().clips.front().id;
    CommandStack stack(*project);
    // 2 s of footage from 1× up to 3×: 1 s long, u + u² seconds in at fraction u.
    REQUIRE_FALSE(stack.push(std::make_unique<SetClipSpeedRampCommand>(clip, SpeedRamp{{0.0, 1.0}, {1.0, 3.0}})));
    TimelinePlayback playback;
    playback.setProject(project);
    const auto shown = shownSourceFrames(playback);
    REQUIRE(shown.size() == 30);
    for (std::size_t i = 0; i < shown.size(); ++i) {
        const double u = double(i) / 30.0;
        const Ticks expected = Ticks(std::floor((u + u * u) * 30.0 + 1e-9)) * kFrame30;
        INFO("frame " << i);
        CHECK(std::llabs(shown[i] - expected) <= kFrame30);
    }
    CHECK(shown[1] - shown[0] <= kFrame30);       // about 1× at the start
    CHECK(shown[29] - shown[28] >= 2 * kFrame30); // about 3× at the end
}

TEST_CASE("reversed clips show the source frames backwards")
{
    requireSamples();
    TimelinePlayback playback;
    playback.setProject(speedProject(1.0, true));
    const auto shown = shownSourceFrames(playback);
    REQUIRE(shown.size() == 60);
    for (std::size_t i = 0; i < shown.size(); ++i) {
        INFO("frame " << i);
        CHECK(shown[i] == Ticks(59 - i) * kFrame30);
    }
}

TEST_CASE("half speed holds each source frame for two project frames")
{
    requireSamples();
    TimelinePlayback playback;
    playback.setProject(speedProject(0.5, false));
    const auto shown = shownSourceFrames(playback);
    REQUIRE(shown.size() == 120);
    CHECK(shown[0] == 0);
    CHECK(shown[1] == 0);
    CHECK(shown[2] == kFrame30);
    CHECK(shown[119] == 59 * kFrame30);
}

TEST_CASE("a freeze frame holds one source frame, then playback resumes")
{
    requireSamples();
    TimelinePlayback playback;
    playback.setProject(speedProject(1.0, false, true));
    const auto shown = shownSourceFrames(playback);
    REQUIRE(shown.size() == 60 + 15); // 2 s clip + 0.5 s still
    for (std::size_t i = 30; i < 45; ++i)
        CHECK(shown[i] == 30 * kFrame30); // the frame at 1 s, held
    CHECK(shown[45] == 30 * kFrame30);    // the clip resumes at 1 s
    CHECK(shown[46] == 31 * kFrame30);
}

TEST_CASE("during a transition each composite carries both clips' frames and the progress")
{
    requireSamples();
    if (!std::filesystem::exists(sample("4k30_h264.mp4")))
        SKIP("sample missing: 4k30_h264.mp4");
    auto project = std::make_shared<Project>();
    CommandStack stack(*project);
    auto push = [&](std::unique_ptr<Command> command) { REQUIRE_FALSE(stack.push(std::move(command))); };
    auto track = std::make_unique<AddTrackCommand>(TrackKind::Video, "V1");
    const auto* trackCmd = track.get();
    push(std::move(track));
    auto media = std::make_unique<ImportMediaCommand>(sample("4k30_h264.mp4"), 20 * kSec);
    const auto* mediaCmd = media.get();
    push(std::move(media));
    // A: source 5–6 s at 0–1 s; B: source 10–11 s at 1–2 s; a 0.5 s transition at the cut.
    push(std::make_unique<AddClipCommand>(trackCmd->trackId(), mediaCmd->mediaId(), 0, kSec, 5 * kSec));
    auto b = std::make_unique<AddClipCommand>(trackCmd->trackId(), mediaCmd->mediaId(), kSec, kSec, 10 * kSec);
    const auto* bCmd = b.get();
    push(std::move(b));
    push(std::make_unique<SetClipTransitionCommand>(bCmd->clipId(), ClipTransition{"tve.transition.wipe", kSec / 2}));

    TimelinePlayback playback;
    playback.setProject(project);
    std::map<Ticks, CompositeFrame> frames;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (std::chrono::steady_clock::now() < deadline && !(playback.producerFinished() && !playback.queue().frontPts())) {
        if (auto front = playback.queue().frontPts()) {
            int skipped = 0;
            auto frame = playback.queue().takeLatestDue(*front, skipped);
            frames.emplace(frame->time, std::move(*frame));
            continue;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    REQUIRE(frames.size() == 60);

    const auto check = [&](int frameIndex, Ticks fromPts, Ticks toPts, double progress) {
        INFO("frame " << frameIndex);
        const CompositeFrame& frame = frames.at(Ticks(frameIndex) * kFrame30);
        REQUIRE(frame.layers.size() == 1);
        const VideoLayer& layer = frame.layers[0];
        CHECK(layer.frame.pts() == fromPts);
        REQUIRE(layer.transition);
        CHECK(layer.transition->transitionId == "tve.transition.wipe");
        CHECK(layer.transition->frame.pts() == toPts);
        CHECK(layer.transition->progress == Catch::Approx(progress).margin(1e-4));
    };
    check(24, 5 * kSec + 24 * kFrame30, 9 * kSec + 24 * kFrame30, 0.1);  // t = 0.8 s, B in its head handle
    check(36, 6 * kSec + 6 * kFrame30, 10 * kSec + 6 * kFrame30, 0.9);  // t = 1.2 s, A in its tail handle
    CHECK_FALSE(frames.at(22 * kFrame30).layers[0].transition);         // t = 0.733 s, before the window
    CHECK_FALSE(frames.at(38 * kFrame30).layers[0].transition);         // t = 1.267 s, after it
}

TEST_CASE("titles play as text layers above the video, with no media")
{
    requireSamples();
    auto project = std::make_shared<Project>();
    CommandStack stack(*project);
    auto push = [&](std::unique_ptr<Command> command) { REQUIRE_FALSE(stack.push(std::move(command))); };
    auto track = std::make_unique<AddTrackCommand>(TrackKind::Video, "V1");
    const auto* trackCmd = track.get();
    push(std::move(track));
    auto media = std::make_unique<ImportMediaCommand>(sample("test_1080p30_h264.mp4"), 2 * kSec);
    const auto* mediaCmd = media.get();
    push(std::move(media));
    push(std::make_unique<AddClipCommand>(trackCmd->trackId(), mediaCmd->mediaId(), 0, kSec, 0));
    // A title over the clip's second half and on past it, alone, to 1.5 s.
    ClipText text;
    text.text = "Hello";
    auto title = std::make_unique<AddTitleCommand>(kNoId, kSec / 2, kSec, text);
    const auto* titleCmd = title.get();
    push(std::move(title));

    TimelinePlayback playback;
    playback.setProject(project);
    std::vector<CompositeFrame> frames;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (std::chrono::steady_clock::now() < deadline && !(playback.producerFinished() && !playback.queue().frontPts())) {
        if (auto front = playback.queue().frontPts()) {
            int skipped = 0;
            frames.push_back(std::move(*playback.queue().takeLatestDue(*front, skipped)));
            continue;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    REQUIRE(frames.size() == 45); // 1.5 s at 30 fps
    CHECK(frames[10].layers.size() == 1);
    CHECK_FALSE(frames[10].layers[0].text);

    const CompositeFrame& both = frames[20]; // 0.67 s: clip and title
    REQUIRE(both.layers.size() == 2);
    REQUIRE(both.layers[1].text);
    CHECK(both.layers[1].clipId == titleCmd->clipId());
    CHECK(both.layers[1].text->text == "Hello");
    CHECK_FALSE(both.layers[1].frame.pixelBuffer());

    REQUIRE(frames[40].layers.size() == 1); // 1.33 s: the title alone
    CHECK(frames[40].layers[0].text);
}

// Not run by default: `tve_engine_tests "[perf]"`. Unpaced producer throughput, in project
// frames per second, for a 4K60 clip played forwards and in reverse at 60 fps, and the
// longest wait for a frame.
TEST_CASE("reverse playback throughput", "[.][perf]")
{
    requireSamples();
    for (const auto& [speed, reversed] : {std::pair{1.0, false}, std::pair{1.0, true}, std::pair{2.0, true}}) {
        TimelinePlayback playback;
        auto project = speedProject(speed, reversed, false, "4k60_h264.mp4", 4 * kSec);
        project->settings.frameRate = {60, 1};
        const auto begin = std::chrono::steady_clock::now();
        playback.setProject(project);
        std::size_t frames = 0;
        std::size_t empty = 0;
        double longestWait = 0;
        auto last = begin;
        while (!playback.producerFinished() || playback.queue().frontPts()) {
            if (auto front = playback.queue().frontPts()) {
                int skipped = 0;
                const auto frame = playback.queue().takeLatestDue(*front, skipped);
                ++frames;
                empty += frame->layers.empty() ? 1 : 0;
                const auto now = std::chrono::steady_clock::now();
                longestWait = std::max(longestWait, std::chrono::duration<double>(now - last).count());
                last = now;
                continue;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
        WARN(speed << "x " << (reversed ? "reverse" : "forward") << ": " << frames << " frames, " << frames / seconds
                                                << " fps, " << empty << " empty, longest wait " << longestWait * 1000 << " ms, decoded "
                                                << playback.counters().decoded.load());
    }
}

// Keyframe seeks are cheap with hardware decoding (no frame-thread pipeline to refill):
// reverse play depends on it.
TEST_CASE("keyframe seek cost", "[.][perf]")
{
    requireSamples();
    auto decoder = VideoDecoder::open(sample("4k60_h264.mp4"));
    REQUIRE(decoder);
    decoder->nextFrame();
    const auto begin = std::chrono::steady_clock::now();
    int frames = 0;
    for (int i = 0; i < 20; ++i) {
        REQUIRE(decoder->seekToKeyframe((19 - i) * kSec / 5 + kSec / 10));
        for (int f = 0; f < 12; ++f)
            frames += decoder->nextFrame() ? 1 : 0;
    }
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
    WARN("20 keyframe seeks + " << frames << " frames: " << seconds * 1000 << " ms");
    const auto linear = std::chrono::steady_clock::now();
    for (int f = 0; f < 240; ++f)
        decoder->nextFrame();
    WARN("240 frames straight: " << std::chrono::duration<double>(std::chrono::steady_clock::now() - linear).count() * 1000 << " ms");
}
