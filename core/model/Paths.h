#pragma once

#include "model/Keyframes.h"
#include "model/Project.h"

#include <array>
#include <string>
#include <vector>

namespace tve {

// The geometry of drawn paths: their lines, positions and directions along them (as seen,
// on a canvas `aspect` = width / height wide, so lengths and angles are not stretched), how
// much of each is drawn as a drawing comes and goes, lines for text on a path, and the
// keyframes that make a clip follow one.

using Polyline = std::vector<std::array<double, 2>>; // canvas fractions, origin top left

// A path's line: a brush stroke smoothed through its points, a line's or arrow's two ends, a
// rectangle's corners (closed), an ellipse traced (closed).
Polyline pathLine(const DrawnPath& path);

// Its length in canvas heights.
double lineLength(const Polyline& line, double aspect);

struct PathPoint {
    double x = 0.0;
    double y = 0.0;
    double angle = 0.0; // degrees clockwise from rightwards
};
// Where `fraction` (0..1) of the way along the line is, and which way it runs there.
PathPoint pointAlong(const Polyline& line, double fraction, double aspect);
// The line from its start to `fraction` of its length.
Polyline lineUpTo(const Polyline& line, double fraction, double aspect);

// How much of each path of a drawing shows (0..1 each) at `seconds` into a clip `duration`
// seconds long: drawn on (eased) over drawIn — one after another by length, or together — and
// taken off the same way over drawOut.
std::vector<double> drawnAmounts(const ClipDrawing& drawing, double seconds, double duration, double aspect);

// Lines for text on a path: "arc" (bending up), "dip" (bending down), "wave" and "circle";
// empty for anything else.
Polyline presetPath(const std::string& name);

// Keyframes that move a clip along a line at an even speed over `duration`: x and y as the
// clip's position (offsets from the canvas centre, in canvas widths and heights) and, when
// `turn`, its rotation following the line's direction.
struct FollowKeyframes {
    Keyframes x;
    Keyframes y;
    Keyframes rotation;
};
FollowKeyframes followLine(const Polyline& line, double aspect, Ticks duration, bool turn);

} // namespace tve
