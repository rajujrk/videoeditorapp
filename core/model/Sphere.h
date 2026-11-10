#pragma once

#include "model/Project.h"

#include <array>

namespace tve {

// 360° video: directions on the sphere and the equirectangular pictures of it (sphere.frag
// does the same per pixel). An equirectangular picture's u (0..1, left to right) is the
// longitude, -180° to 180° with the front (0°) in the middle; v (0..1, top to bottom) the
// latitude, 90° to -90°. Directions: x right, y up, z forward.

using SphereVector = std::array<double, 3>;
using SphereMatrix = std::array<double, 9>; // row-major

// Turns the front towards the view's direction: yaw about y (right), then pitch about x
// (up), then roll about z (clockwise).
SphereMatrix sphereRotation(const ClipSphereView& view);
SphereVector equirectDirection(double u, double v);
std::array<double, 2> equirectPoint(const SphereVector& direction);

// Where in the equirectangular source a reframed picture of `aspect` (width / height) shows
// its point (u, v) (0..1 from the top left): a rectilinear view in the view's direction with
// its horizontal field of view.
std::array<double, 2> reframedSource(const ClipSphereView& view, double aspect, double u, double v);
// Where in the source a turned sphere shows its point (u, v): the view's direction at the front.
std::array<double, 2> turnedSource(const ClipSphereView& view, double u, double v);

} // namespace tve
