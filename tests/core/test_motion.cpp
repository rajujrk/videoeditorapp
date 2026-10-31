#include "commands/CommandStack.h"
#include "commands/EditCommands.h"
#include "io/ProjectSerializer.h"
#include "model/Motion.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <cmath>
#include <numbers>
#include <random>

using namespace tve;
using Catch::Approx;

namespace {

constexpr Ticks kFrame = kTicksPerSecond / 30;

// Smooth random texture (sums of blobs), as a function of a continuous point.
struct Scene {
    std::vector<std::array<double, 4>> blobs; // x, y, radius, sign
    explicit Scene(unsigned seed = 3)
    {
        std::mt19937 random(seed);
        std::uniform_real_distribution<double> u(0.0, 1.0);
        for (int i = 0; i < 160; ++i)
            blobs.push_back({u(random) * 420 - 50, u(random) * 280 - 50, 4 + 10 * u(random), u(random) < 0.5 ? -1.0 : 1.0});
    }
    float at(double x, double y) const
    {
        double v = 0.0;
        for (const auto& b : blobs) {
            const double d = (x - b[0]) * (x - b[0]) + (y - b[1]) * (y - b[1]);
            if (d < 9 * b[2] * b[2])
                v += b[3] * std::exp(-d / (2 * b[2] * b[2]));
        }
        return float(0.5 + 0.4 * std::tanh(v));
    }
};

// The scene as seen after the content moved by a similarity about the centre (320 × 180):
// shown = scale · R(angle) · original + (dx, dy). Optionally a square moving on its own.
GrayImage view(const Scene& scene, double dx, double dy, double angle = 0.0, double scale = 1.0, double squareX = -1)
{
    GrayImage image{320, 180, std::vector<float>(320 * 180)};
    const double c = std::cos(angle), s = std::sin(angle);
    for (int y = 0; y < 180; ++y)
        for (int x = 0; x < 320; ++x) {
            const double px = x - 160 - dx, py = y - 90 - dy;
            const double ox = (c * px + s * py) / scale + 160, oy = (-s * px + c * py) / scale + 90;
            float v = scene.at(ox, oy);
            if (squareX >= 0 && x >= squareX && x < squareX + 40 && y >= 70 && y < 110)
                v = ((x / 4 + y / 4) % 2) ? 0.95f : 0.05f;
            image.pixels[std::size_t(y * 320 + x)] = v;
        }
    return image;
}

} // namespace

TEST_CASE("block matching finds the camera's shift, turn and zoom to a fraction of a pixel")
{
    const Scene scene;
    const GrayImage still = view(scene, 0, 0);

    const FrameMotion none = estimateMotion(still, still);
    CHECK(none.dx == Approx(0.0).margin(0.05));
    CHECK(none.dy == Approx(0.0).margin(0.05));
    CHECK(none.confidence > 0.9);

    const FrameMotion shift = estimateMotion(still, view(scene, 5.3, -2.7));
    CHECK(shift.dx == Approx(5.3).margin(0.2));
    CHECK(shift.dy == Approx(-2.7).margin(0.2));
    CHECK(shift.angle == Approx(0.0).margin(0.003));
    CHECK(shift.scale == Approx(1.0).margin(0.003));

    // A big jolt: past the fine search, found by the coarse one.
    const FrameMotion jolt = estimateMotion(still, view(scene, -14.0, 9.0));
    CHECK(jolt.dx == Approx(-14.0).margin(0.3));
    CHECK(jolt.dy == Approx(9.0).margin(0.3));

    const double turn = 1.0 * std::numbers::pi / 180.0;
    const FrameMotion turned = estimateMotion(still, view(scene, 1.0, 0.5, turn, 1.01));
    CHECK(turned.angle == Approx(turn).margin(0.002));
    CHECK(turned.scale == Approx(1.01).margin(0.003));
    CHECK(turned.dx == Approx(1.0).margin(0.3));

    // Something moving by itself (a square crossing the frame) does not pull the camera.
    const FrameMotion apart = estimateMotion(view(scene, 0, 0, 0, 1, 100), view(scene, 2.0, 1.0, 0, 1, 130));
    CHECK(apart.dx == Approx(2.0).margin(0.3));
    CHECK(apart.dy == Approx(1.0).margin(0.3));
    CHECK(apart.confidence < 1.0);

    // A flat picture (nothing to match): no motion, no confidence.
    const GrayImage flat{320, 180, std::vector<float>(320 * 180, 0.5f)};
    CHECK(estimateMotion(flat, flat).confidence == 0.0);
}

TEST_CASE("stabilizing keeps the smooth part of the path and zooms just enough")
{
    // A slow pan right (2 px a frame) with ±3 px of shake up and down, 3 s at 30 fps.
    MotionAnalysis a;
    a.width = 320;
    a.height = 180;
    double shakeBefore = 0.0;
    for (int k = 0; k < 90; ++k) {
        const double shake = 3.0 * std::sin(k * 1.7);
        a.times.push_back(k * kFrame);
        a.motion.push_back(k == 0 ? FrameMotion{} : FrameMotion{2.0, shake - shakeBefore, 0, 1, 1.0});
        a.brightness.push_back(0.5f);
        shakeBefore = shake;
    }
    const Stabilization s = stabilize(a, 0.3, false);
    REQUIRE(s.samples.size() == 90);
    CHECK(s.enabled);
    // The shake is taken out (correction = −shake, about its mean), the pan kept (little x).
    for (int k = 20; k < 70; ++k) {
        const double shake = 3.0 * std::sin(k * 1.7);
        CHECK(s.samples[std::size_t(k)].dy * 180 == Approx(-shake).margin(0.6));
        CHECK(std::abs(s.samples[std::size_t(k)].dx * 320) < 0.5);
    }
    // Zoomed just enough to hide the ±3 px edges: about 1 + 2·3/180.
    CHECK(s.zoom > 1.0 + 2.0 * 2.5 / 180.0);
    CHECK(s.zoom < 1.0 + 2.0 * 4.5 / 180.0);

    // Locked: the whole pan is taken out (centred on its middle), so it needs far more zoom —
    // more than allowed, so it stops at maxZoom.
    const Stabilization locked = stabilize(a, 0.3, true, 1.2);
    CHECK(locked.lock);
    CHECK(locked.zoom == Approx(1.2));
    CHECK(locked.samples.front().dx * 320 == Approx(89.0).margin(3.0));
    CHECK(locked.samples.back().dx * 320 == Approx(-89.0).margin(3.0));

    // Between frames the corrections are mixed; beyond the ends, held.
    const SteadySample mid = s.at(kFrame / 2);
    CHECK(mid.dy == Approx((s.samples[0].dy + s.samples[1].dy) / 2));
    CHECK(s.at(100 * kFrame).dy == s.samples.back().dy);
    CHECK(Stabilization{}.at(kFrame).scale == 1.0);
}

TEST_CASE("flicker is evened out, slow changes in light are kept")
{
    MotionAnalysis a;
    a.width = 320;
    a.height = 180;
    for (int k = 0; k < 120; ++k) {
        a.times.push_back(k * kFrame);
        a.motion.push_back({});
        // Light rising slowly from 0.3 to 0.6, with ±10 % flicker every other frame.
        const double base = 0.3 + 0.3 * k / 119.0;
        a.brightness.push_back(float(base * (k % 2 ? 1.1 : 0.9)));
    }
    const Deflicker d = deflicker(a, 0.2);
    REQUIRE(d.samples.size() == 120);
    for (int k = 20; k < 100; ++k) {
        const double base = 0.3 + 0.3 * k / 119.0;
        CHECK(a.brightness[std::size_t(k)] * d.samples[std::size_t(k)].gain == Approx(base).epsilon(0.03));
    }
    CHECK(d.gainAt(-kFrame) == d.samples.front().gain);
    CHECK(Deflicker{}.gainAt(kFrame) == 1.0f);
    // Near black: left alone.
    a.brightness.assign(120, 0.01f);
    for (const GainSample& g : deflicker(a, 0.5).samples)
        CHECK(g.gain == 1.0f);
}

TEST_CASE("motion settings and tracks are commands, kept by splits and saved compactly")
{
    Project project;
    CommandStack stack{project};
    const auto push = [&](std::unique_ptr<Command> c) {
        const auto error = stack.push(std::move(c));
        INFO(error.value_or(""));
        REQUIRE_FALSE(error);
    };
    auto track = std::make_unique<AddTrackCommand>(TrackKind::Video, "V1");
    auto* t = track.get();
    push(std::move(track));
    auto media = std::make_unique<ImportMediaCommand>(MediaItem{.path = "/shaky.mp4", .duration = 10 * kTicksPerSecond,
                                                                .width = 1920, .height = 1080});
    auto* m = media.get();
    push(std::move(media));
    auto add = std::make_unique<AddClipCommand>(t->trackId(), m->mediaId(), 0, 6 * kTicksPerSecond, 0);
    auto* a = add.get();
    push(std::move(add));
    const Id clip = a->clipId();

    Stabilization steady;
    steady.enabled = true;
    steady.zoom = 1.08;
    steady.samples = {{0, 0.01, -0.02, 0.003, 1.0}, {kFrame, -0.01, 0.0, 0.0, 1.001}};
    Deflicker even;
    even.enabled = true;
    even.samples = {{0, 1.1f}, {kFrame, 0.9f}};
    push(std::make_unique<SetClipMotionCommand>(clip, steady, even, 0.4, "Stabilize"));
    const Clip& c = *project.locateClip(clip)->clip;
    CHECK(c.stabilization == steady);
    CHECK(c.deflicker == even);
    CHECK(c.denoise == 0.4);
    stack.undo();
    CHECK_FALSE(project.locateClip(clip)->clip->stabilization.enabled);
    CHECK(project.locateClip(clip)->clip->denoise == 0.0);
    stack.redo();

    const std::vector<MotionTrack> tracks{{1, true, {{0, 0.4, 0.5, 0.2, 0.3, 0.0}, {kFrame * 30, 0.6, 0.5, 0.2, 0.3, 0.1}}}};
    push(std::make_unique<SetClipTracksCommand>(clip, tracks, "Track Motion"));
    CHECK(project.locateClip(clip)->clip->motionTracks == tracks);
    const auto mid = tracks[0].at(kFrame * 15);
    REQUIRE(mid);
    CHECK(mid->x == Approx(0.5));
    CHECK(mid->angle == Approx(0.05));
    CHECK_FALSE(tracks[0].at(kTicksPerSecond * 5));

    auto split = std::make_unique<SplitClipCommand>(clip, 3 * kTicksPerSecond);
    auto* sp = split.get();
    push(std::move(split));
    const Clip& right = *project.locateClip(sp->rightClipId())->clip;
    CHECK(right.stabilization == steady);
    CHECK(right.motionTracks == tracks);

    // Saved: each per-frame sample as a short array.
    nlohmann::json json = projectToJson(project);
    const auto& saved = json["tracks"][0]["clips"][0];
    CHECK(saved["stabilization"]["samples"][0].is_array());
    CHECK(saved["deflicker"]["samples"][1][1].get<float>() == Approx(0.9f));
    CHECK(saved["motionTracks"][0]["samples"][1].size() == 6);
    Project loaded = projectFromJson(json);
    CHECK(loaded.locateClip(clip)->clip->stabilization == steady);
    CHECK(loaded.locateClip(clip)->clip->motionTracks == tracks);

    // Out of range, or not a video file: refused.
    Stabilization tooMuch = steady;
    tooMuch.zoom = 3.0;
    CHECK(stack.push(std::make_unique<SetClipMotionCommand>(clip, tooMuch, std::nullopt, std::nullopt, "x")).value()
          == "stabilization zoom out of range");
    CHECK(stack.push(std::make_unique<SetClipMotionCommand>(clip, std::nullopt, std::nullopt, 1.5, "x")).value()
          == "denoise strength out of range");
}

TEST_CASE("a recording's takes go on new tracks in one step, with the cursor and click sounds")
{
    Project project;
    CommandStack stack{project};
    const auto push = [&](std::unique_ptr<Command> c) {
        const auto error = stack.push(std::move(c));
        INFO(error.value_or(""));
        REQUIRE_FALSE(error);
    };
    push(std::make_unique<AddTrackCommand>(TrackKind::Video, "V1"));
    CursorEffects cursor;
    cursor.path = {{0, 0.1, 0.1}, {kTicksPerSecond * 2, 0.9, 0.5}};
    cursor.clicks = {kTicksPerSecond / 2, kTicksPerSecond};
    std::vector<RecordedTake> takes{
        {MediaItem{.path = "/s.mov", .duration = 3 * kTicksPerSecond, .width = 1280, .height = 720}, TrackKind::Video, "Screen", {}, cursor},
        {MediaItem{.path = "/c.mov", .duration = 3 * kTicksPerSecond, .width = 640, .height = 360}, TrackKind::Video, "Camera", {}, {}},
        {MediaItem{.path = "/m.wav", .duration = 3 * kTicksPerSecond, .hasVideo = false, .hasAudio = true}, TrackKind::Audio, "Microphone", {}, {}},
    };
    auto add = std::make_unique<AddRecordingCommand>(takes, 2 * kTicksPerSecond, "Record Screen");
    auto* raw = add.get();
    push(std::move(add));
    REQUIRE(project.tracks.size() == 4);
    CHECK(project.tracks.front().name == "Microphone");                // sound at the bottom
    CHECK(project.tracks[2].name == "Screen");
    CHECK(project.tracks.back().name == "Camera");                     // the camera on top
    const Clip& screen = *project.locateClip(raw->clipIds()[0])->clip;
    CHECK(screen.start == 2 * kTicksPerSecond);
    CHECK(screen.cursor == cursor);
    CHECK(screen.cursor.at(kTicksPerSecond)->x == Approx(0.5));
    CHECK(screen.cursor.sinceClick(kTicksPerSecond + kTicksPerSecond / 10, kClickRingSeconds).value() == Approx(0.1));
    stack.undo();
    CHECK(project.tracks.size() == 1);
    CHECK(project.media.empty());
    stack.redo();
    CHECK(project.locateClip(raw->clipIds()[0])->clip->cursor == cursor); // the same ids again

    CursorEffects spot = cursor;
    spot.spotlight = true;
    push(std::make_unique<SetClipCursorCommand>(raw->clipIds()[0], spot));
    CHECK(project.locateClip(raw->clipIds()[0])->clip->cursor.spotlight);
    spot.size = 0.5;
    CHECK(stack.push(std::make_unique<SetClipCursorCommand>(raw->clipIds()[0], spot)).value() == "cursor effect size out of range");
    CHECK(stack.push(std::make_unique<SetClipCursorCommand>(raw->clipIds()[1], spot)).value() == "this clip has no cursor recorded");

    // Two clicks closer than the sound: one after the other.
    const Ticks tick = kTicksPerSecond / 25;
    push(std::make_unique<AddClickSoundsCommand>(MediaItem{.path = "/click.wav", .duration = tick, .hasVideo = false, .hasAudio = true},
                                                 std::vector<Ticks>{kTicksPerSecond, kTicksPerSecond + tick / 2}));
    const Track& clicks = project.tracks.front();
    CHECK(clicks.name == "Clicks");
    REQUIRE(clicks.clips.size() == 2);
    CHECK(clicks.clips[1].start == kTicksPerSecond + tick);

    const Project loaded = projectFromJson(projectToJson(project));
    CHECK(const_cast<Project&>(loaded).locateClip(raw->clipIds()[0])->clip->cursor.spotlight);
    CHECK(thinCursorPath({{0, 0, 0}, {1, 0.5, 0.5}, {2, 1, 1}}).size() == 2);
}
