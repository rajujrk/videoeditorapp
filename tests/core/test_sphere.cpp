#include "commands/EditCommands.h"
#include "io/ProjectSerializer.h"
#include "model/Keyframes.h"
#include "model/Sphere.h"

#include <nlohmann/json.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace tve;
using Catch::Approx;

TEST_CASE("equirectangular points and directions: the front in the middle, the top first")
{
    const auto front = equirectDirection(0.5, 0.5);
    CHECK(front[2] == Approx(1.0));
    CHECK(equirectDirection(0.75, 0.5)[0] == Approx(1.0));        // 90° right
    CHECK(equirectDirection(0.5, 0.0)[1] == Approx(1.0));         // straight up
    CHECK(equirectDirection(0.0, 0.5)[2] == Approx(-1.0));        // behind
    for (const auto& [u, v] : {std::pair{0.3, 0.2}, std::pair{0.9, 0.7}, std::pair{0.5, 0.5}}) {
        const auto back = equirectPoint(equirectDirection(u, v));
        CHECK(back[0] == Approx(u));
        CHECK(back[1] == Approx(v));
    }
}

TEST_CASE("a reframed view looks where its direction says, as wide as its field of view")
{
    ClipSphereView view;
    // The middle of the view is the direction looked in.
    auto centre = reframedSource(view, 16.0 / 9.0, 0.5, 0.5);
    CHECK(centre[0] == Approx(0.5));
    CHECK(centre[1] == Approx(0.5));
    view.yaw = 90;
    CHECK(reframedSource(view, 16.0 / 9.0, 0.5, 0.5)[0] == Approx(0.75));
    view.yaw = -45;
    view.pitch = 30;
    centre = reframedSource(view, 16.0 / 9.0, 0.5, 0.5);
    CHECK(centre[0] == Approx(0.375));
    CHECK(centre[1] == Approx(0.5 - 30.0 / 180.0));
    // The edges: half the field of view either side.
    view = {};
    view.fov = 90;
    CHECK(reframedSource(view, 16.0 / 9.0, 1.0, 0.5)[0] == Approx(0.625));
    CHECK(reframedSource(view, 16.0 / 9.0, 0.0, 0.5)[0] == Approx(0.375));
    // Upright: a point above the middle is above it in the source.
    CHECK(reframedSource(view, 16.0 / 9.0, 0.5, 0.2)[1] < 0.5);
    // Rolled clockwise a quarter: what was to the right is now above.
    view.roll = 90;
    const auto rolled = reframedSource(view, 1.0, 0.5, 0.2);
    CHECK(rolled[0] > 0.5);
    CHECK(rolled[1] == Approx(0.5).margin(1e-9));
}

TEST_CASE("a turned sphere puts the view's direction at its front")
{
    ClipSphereView view;
    view.yaw = 90;
    CHECK(turnedSource(view, 0.5, 0.5)[0] == Approx(0.75));
    view = {};
    view.pitch = -45;
    CHECK(turnedSource(view, 0.5, 0.5)[1] == Approx(0.75));
    CHECK_FALSE(ClipSphereView{}.turned());
    CHECK(view.turned());
}

TEST_CASE("360° clips' direction and field of view animate, are checked, and are saved")
{
    Clip clip;
    clip.duration = 10 * kTicksPerSecond;
    clip.keyframes[std::string(property::kYaw)] = {{0, 0.0}, {10 * kTicksPerSecond, 180.0}};
    clip.keyframes[std::string(property::kFov)] = {{0, 60.0}, {10 * kTicksPerSecond, 120.0}};
    const ClipVisual half = clip.visualAt(5 * kTicksPerSecond);
    CHECK(half.sphere.yaw == Approx(90.0));
    CHECK(half.sphere.fov == Approx(90.0));
    CHECK(findAnimatable("pitch")->max == 90.0);
    CHECK(validateKeyframes("fov", {{0, 10.0}}));

    Project project;
    project.tracks.push_back(Track{.id = 1, .kind = TrackKind::Video});
    project.tracks[0].clips.push_back(Clip{.id = 2, .mediaId = 3, .duration = kTicksPerSecond});
    ClipVisual v;
    v.sphere.fov = 200;
    SetClipVisualCommand tooWide(2, v);
    CHECK(tooWide.redo(project) == "the field of view must be between 20° and 150°");
    v.sphere = {.yaw = 30, .pitch = 95};
    SetClipVisualCommand tooHigh(2, v);
    CHECK(tooHigh.redo(project));

    project.settings.spherical = true;
    project.media.push_back({.id = 3, .path = "/m/360.mp4", .duration = kTicksPerSecond, .spherical = true});
    project.tracks[0].clips[0].visual.sphere = {.yaw = 45, .pitch = -10, .roll = 5, .fov = 100};
    auto json = projectToJson(project);
    const Project back = projectFromJson(json);
    CHECK(back.settings.spherical);
    CHECK(back.media[0].spherical);
    CHECK(back.tracks[0].clips[0].visual.sphere == ClipSphereView{45, -10, 5, 100});
    // Older files: flat, looking ahead.
    json["settings"].erase("spherical");
    json["media"][0].erase("spherical");
    json["tracks"][0]["clips"][0]["visual"].erase("sphere");
    const Project old = projectFromJson(json);
    CHECK_FALSE(old.settings.spherical);
    CHECK_FALSE(old.media[0].spherical);
    CHECK(old.tracks[0].clips[0].visual.sphere == ClipSphereView{});
}
