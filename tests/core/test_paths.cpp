#include "commands/CommandStack.h"
#include "commands/DrawingCommands.h"
#include "commands/EditCommands.h"
#include "io/ProjectSerializer.h"
#include "model/Paths.h"

#include <nlohmann/json.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <numbers>

using namespace tve;
using Catch::Approx;

namespace {

DrawnPath line(double x0, double y0, double x1, double y1, PathKind kind = PathKind::Line)
{
    return DrawnPath{.kind = kind, .points = {{x0, y0}, {x1, y1}}, .stroke = {1, 0, 0, 1}};
}

} // namespace

TEST_CASE("a path's line by kind")
{
    // A line or arrow: its two ends, whatever was drawn between.
    DrawnPath arrow{.kind = PathKind::Arrow, .points = {{0.1, 0.1}, {0.3, 0.9}, {0.5, 0.5}}};
    CHECK(pathLine(arrow) == Polyline{{0.1, 0.1}, {0.5, 0.5}});
    // A rectangle: its corners, closed.
    const Polyline box = pathLine(line(0.2, 0.2, 0.6, 0.4, PathKind::Rectangle));
    REQUIRE(box.size() == 5);
    CHECK(box.front() == box.back());
    CHECK(box[2] == std::array<double, 2>{0.6, 0.4});
    // An ellipse: traced from the top, closed.
    const Polyline round = pathLine(line(0.2, 0.2, 0.6, 0.6, PathKind::Ellipse));
    CHECK(round.size() == 65);
    CHECK(round.front()[0] == Approx(0.4));
    CHECK(round.front()[1] == Approx(0.2));
    CHECK(round.back()[0] == Approx(round.front()[0]));
    // A brush: smoothed through its points, ending where it ended.
    DrawnPath brush{.kind = PathKind::Brush, .points = {{0.1, 0.5}, {0.3, 0.3}, {0.5, 0.5}, {0.7, 0.3}}};
    const Polyline smooth = pathLine(brush);
    CHECK(smooth.size() > brush.points.size());
    CHECK(smooth.front() == brush.points.front());
    CHECK(smooth.back() == brush.points.back());
}

TEST_CASE("lengths, points and directions along a line are as seen on the canvas")
{
    const Polyline across{{0.0, 0.5}, {1.0, 0.5}};
    // On a 16:9 canvas a full width is 16/9 heights.
    CHECK(lineLength(across, 16.0 / 9) == Approx(16.0 / 9));
    const PathPoint middle = pointAlong(across, 0.5, 16.0 / 9);
    CHECK(middle.x == Approx(0.5));
    CHECK(middle.angle == Approx(0.0));
    // Down and right at 45° as seen: x moves 9/16 as far as y.
    const Polyline slope{{0.0, 0.0}, {9.0 / 16, 1.0}};
    CHECK(pointAlong(slope, 0.5, 16.0 / 9).angle == Approx(45.0));
    // Up the canvas is -90°.
    CHECK(pointAlong(Polyline{{0.5, 0.9}, {0.5, 0.1}}, 0.3, 1.0).angle == Approx(-90.0));
    // Part of a line, along its length.
    const Polyline corner{{0.0, 0.0}, {1.0, 0.0}, {1.0, 1.0}};
    const Polyline part = lineUpTo(corner, 0.75, 1.0);
    REQUIRE(part.size() == 3);
    CHECK(part.back()[0] == Approx(1.0));
    CHECK(part.back()[1] == Approx(0.5));
    CHECK(lineUpTo(corner, 0.0, 1.0).empty());
    CHECK(lineUpTo(corner, 1.0, 1.0) == corner);
}

TEST_CASE("a drawing comes on one path after another and goes the same way")
{
    ClipDrawing drawing;
    drawing.paths = {line(0.0, 0.5, 0.5, 0.5), line(0.0, 0.6, 0.5, 0.6)}; // the same length each
    drawing.drawIn = 2.0;
    drawing.drawOut = 1.0;
    // Half way through drawing on: the first done, the second not begun.
    auto amounts = drawnAmounts(drawing, 1.0, 10.0, 1.0);
    CHECK(amounts[0] == Approx(1.0));
    CHECK(amounts[1] == Approx(0.0));
    // A quarter in (eased): part of the first.
    amounts = drawnAmounts(drawing, 0.5, 10.0, 1.0);
    CHECK(amounts[0] > 0.0);
    CHECK(amounts[0] < 1.0);
    // All there in the middle; going in the last second.
    CHECK(drawnAmounts(drawing, 5.0, 10.0, 1.0) == std::vector<double>{1.0, 1.0});
    amounts = drawnAmounts(drawing, 9.5, 10.0, 1.0);
    CHECK(amounts[0] == Approx(1.0));
    CHECK(amounts[1] == Approx(0.0));
    // Together: both alike.
    drawing.oneByOne = false;
    amounts = drawnAmounts(drawing, 1.0, 10.0, 1.0);
    CHECK(amounts[0] == Approx(0.5));
    CHECK(amounts[1] == Approx(0.5));
    // No timing: all there from the start.
    CHECK(drawnAmounts(ClipDrawing{.paths = drawing.paths}, 0.0, 10.0, 1.0) == std::vector<double>{1.0, 1.0});
}

TEST_CASE("text path presets")
{
    const Polyline arc = presetPath("arc");
    REQUIRE(arc.size() > 10);
    // Bending up: the middle higher on the canvas than the ends.
    CHECK(arc[arc.size() / 2][1] < arc.front()[1]);
    CHECK(presetPath("dip")[arc.size() / 2][1] > presetPath("dip").front()[1]);
    // A circle's middle at its top.
    const Polyline circle = presetPath("circle");
    REQUIRE(circle.size() > 10);
    CHECK(circle[circle.size() / 2][0] == Approx(0.5));
    CHECK(circle[circle.size() / 2][1] < 0.25);
    CHECK(presetPath("wave").size() > 10);
    CHECK(presetPath("spiral").empty());
}

TEST_CASE("keyframes that follow a line at an even speed, turning with it")
{
    const Polyline corner{{0.25, 0.25}, {0.75, 0.25}, {0.75, 0.75}};
    const FollowKeyframes keys = followLine(corner, 1.0, 2 * kTicksPerSecond, true);
    REQUIRE(keys.x.size() == 21); // ten a second, both ends
    CHECK(keys.x.front().time == 0);
    CHECK(keys.x.back().time == 2 * kTicksPerSecond);
    // Offsets from the centre.
    CHECK(keys.x.front().value == Approx(-0.25));
    CHECK(keys.y.front().value == Approx(-0.25));
    CHECK(keys.x.back().value == Approx(0.25));
    CHECK(keys.y.back().value == Approx(0.25));
    // Half way in time is the corner, half way along.
    CHECK(keys.x[10].value == Approx(0.25));
    CHECK(keys.y[10].value == Approx(-0.25));
    // Rightwards, then down.
    CHECK(keys.rotation.front().value == Approx(0.0));
    CHECK(keys.rotation.back().value == Approx(90.0));
    // Without turning, no rotation; too short a line, nothing.
    CHECK(followLine(corner, 1.0, kTicksPerSecond, false).rotation.empty());
    CHECK(followLine(Polyline{{0.5, 0.5}}, 1.0, kTicksPerSecond, true).x.empty());

    // Round and round: the turn keeps going, no jump back through 360°.
    Polyline circle;
    for (int i = 0; i <= 64; ++i)
        circle.push_back({0.5 + 0.3 * std::cos(i * 2 * std::numbers::pi / 64), 0.5 + 0.3 * std::sin(i * 2 * std::numbers::pi / 64)});
    const FollowKeyframes round = followLine(circle, 1.0, 4 * kTicksPerSecond, true);
    for (std::size_t i = 1; i < round.rotation.size(); ++i)
        CHECK(std::abs(round.rotation[i].value - round.rotation[i - 1].value) < 45.0);
    CHECK(round.rotation.back().value - round.rotation.front().value > 300.0);
}

TEST_CASE("drawings are checked, added and changed as undo steps")
{
    CHECK(!validateDrawing(ClipDrawing{.paths = {line(0.1, 0.1, 0.9, 0.9)}}));
    CHECK(validateDrawing(ClipDrawing{.paths = {DrawnPath{.points = {{0.5, 0.5}}}}}));
    DrawnPath wide = line(0.1, 0.1, 0.9, 0.9);
    wide.width = 0.5;
    CHECK(validateDrawing(ClipDrawing{.paths = {wide}}));
    CHECK(validateDrawing(ClipDrawing{.paths = {line(0.1, 0.1, 5.0, 0.9)}}));
    ClipDrawing many;
    many.paths.assign(kMaxDrawnPaths + 1, line(0.1, 0.1, 0.9, 0.9));
    CHECK(validateDrawing(many));
    CHECK(validateDrawing(ClipDrawing{.paths = {line(0.1, 0.1, 0.9, 0.9)}, .drawIn = -1}));

    Project project;
    CommandStack stack(project);
    // No track given: a new one on top, undone with it.
    auto add = std::make_unique<AddDrawingCommand>(kNoId, kTicksPerSecond, 5 * kTicksPerSecond,
                                                   ClipDrawing{.paths = {line(0.1, 0.1, 0.9, 0.9)}});
    auto* raw = add.get();
    REQUIRE_FALSE(stack.push(std::move(add)));
    REQUIRE(project.tracks.size() == 1);
    CHECK(project.tracks[0].kind == TrackKind::Video);
    const Clip* clip = project.locateClip(raw->clipId())->clip;
    REQUIRE(clip->drawing);
    CHECK(clip->generated());
    CHECK_FALSE(clip->audioEnabled);
    CHECK(stack.undoLabel() == "Add Drawing");

    ClipDrawing changed = *clip->drawing;
    changed.paths.push_back(line(0.2, 0.8, 0.8, 0.2, PathKind::Arrow));
    changed.drawIn = 1.5;
    REQUIRE_FALSE(stack.push(std::make_unique<SetClipDrawingCommand>(raw->clipId(), changed, "Draw")));
    CHECK(project.locateClip(raw->clipId())->clip->drawing->paths.size() == 2);
    // Not a drawing, or a bad one: refused.
    CHECK(stack.push(std::make_unique<SetClipDrawingCommand>(raw->clipId(), many, "Draw")));
    stack.undo();
    CHECK(project.locateClip(raw->clipId())->clip->drawing->paths.size() == 1);
    stack.undo();
    CHECK(project.tracks.empty());
}

TEST_CASE("an overlay clip goes over others, blended, its media brought in once")
{
    Project project;
    CommandStack stack(project);
    REQUIRE_FALSE(stack.push(std::make_unique<AddTrackCommand>(TrackKind::Video, "Video 1")));
    const MediaItem leak{.path = "/cache/leak_warm-v1.mov", .duration = 36 * kTicksPerSecond / 30, .hasVideo = true,
                         .width = 1280, .height = 720, .alpha = true};
    ClipVisual screen;
    screen.blend = BlendMode::Screen;
    auto first = std::make_unique<AddOverlayClipCommand>(leak, kNoId, kTicksPerSecond, leak.duration, screen, "Overlays", "Add Overlay");
    auto* raw = first.get();
    REQUIRE_FALSE(stack.push(std::move(first)));
    REQUIRE(project.tracks.size() == 2);
    CHECK(project.tracks[1].name == "Overlays");
    CHECK(project.media.size() == 1);
    const Clip* clip = project.locateClip(raw->clipId())->clip;
    CHECK(clip->visual.blend == BlendMode::Screen);
    CHECK_FALSE(clip->audioEnabled);
    CHECK(clip->mediaId == project.media[0].id);
    // The same overlay again on that track: its media used again.
    REQUIRE_FALSE(stack.push(std::make_unique<AddOverlayClipCommand>(leak, project.tracks[1].id, 5 * kTicksPerSecond, leak.duration,
                                                                     screen, "Overlays", "Add Overlay")));
    CHECK(project.media.size() == 1);
    CHECK(project.tracks[1].clips.size() == 2);
    // Over a clip already there, or longer than its media: refused.
    CHECK(stack.push(std::make_unique<AddOverlayClipCommand>(leak, project.tracks[1].id, 5 * kTicksPerSecond, leak.duration,
                                                             screen, "Overlays", "Add Overlay")));
    CHECK(stack.push(std::make_unique<AddOverlayClipCommand>(leak, kNoId, 0, leak.duration * 2, screen, "Overlays", "Add Overlay")));
    stack.undo();
    stack.undo();
    CHECK(project.tracks.size() == 1);
    CHECK(project.media.empty());
}

TEST_CASE("drawings and text on a path are saved and read back")
{
    Project project;
    Track track{.id = project.allocateId(), .kind = TrackKind::Video, .name = "Video 2"};
    Clip drawn{.id = project.allocateId(), .start = 0, .duration = 5 * kTicksPerSecond};
    DrawnPath box = line(0.2, 0.2, 0.6, 0.5, PathKind::Rectangle);
    box.fill = {0.1, 0.2, 0.3, 0.4};
    box.width = 0.02;
    drawn.drawing = ClipDrawing{.paths = {line(0.1, 0.1, 0.9, 0.9, PathKind::Arrow), box}, .drawIn = 1.5, .drawOut = 0.5,
                                .oneByOne = false};
    Clip title{.id = project.allocateId(), .start = 5 * kTicksPerSecond, .duration = 5 * kTicksPerSecond};
    title.text = ClipText{};
    title.text->text = "Along";
    title.text->path = presetPath("arc");
    track.clips = {drawn, title};
    project.tracks.push_back(track);

    const Project back = projectFromJson(projectToJson(project));
    const Clip& d = back.tracks[0].clips[0];
    REQUIRE(d.drawing);
    REQUIRE(d.drawing->paths.size() == 2);
    CHECK(d.drawing->paths[0].kind == PathKind::Arrow);
    CHECK(d.drawing->paths[1].kind == PathKind::Rectangle);
    CHECK(d.drawing->paths[1].points == box.points);
    CHECK(d.drawing->paths[1].fill.a == Approx(0.4));
    CHECK(d.drawing->paths[1].width == Approx(0.02));
    CHECK(d.drawing->drawIn == Approx(1.5));
    CHECK(d.drawing->drawOut == Approx(0.5));
    CHECK_FALSE(d.drawing->oneByOne);
    REQUIRE(back.tracks[0].clips[1].text);
    CHECK(back.tracks[0].clips[1].text->path == title.text->path);
    // Clips without them have none.
    CHECK(projectToJson(Project{}).dump().find("drawing") == std::string::npos);
}
