#include "model/Sphere.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace tve {

namespace {

constexpr double kRadians = std::numbers::pi / 180.0;

SphereVector rotated(const SphereMatrix& m, const SphereVector& d)
{
    return {m[0] * d[0] + m[1] * d[1] + m[2] * d[2], m[3] * d[0] + m[4] * d[1] + m[5] * d[2],
            m[6] * d[0] + m[7] * d[1] + m[8] * d[2]};
}

SphereMatrix multiply(const SphereMatrix& a, const SphereMatrix& b)
{
    SphereMatrix out{};
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c)
            for (int k = 0; k < 3; ++k)
                out[std::size_t(r * 3 + c)] += a[std::size_t(r * 3 + k)] * b[std::size_t(k * 3 + c)];
    return out;
}

} // namespace

SphereMatrix sphereRotation(const ClipSphereView& view)
{
    const double y = view.yaw * kRadians, p = view.pitch * kRadians, r = view.roll * kRadians;
    // Forward (0, 0, 1) to (sin yaw, 0, cos yaw); to (0, sin pitch, cos pitch); up (0, 1, 0)
    // to (sin roll, cos roll, 0).
    const SphereMatrix yaw{std::cos(y), 0, std::sin(y), 0, 1, 0, -std::sin(y), 0, std::cos(y)};
    const SphereMatrix pitch{1, 0, 0, 0, std::cos(p), std::sin(p), 0, -std::sin(p), std::cos(p)};
    const SphereMatrix roll{std::cos(r), std::sin(r), 0, -std::sin(r), std::cos(r), 0, 0, 0, 1};
    return multiply(multiply(yaw, pitch), roll);
}

SphereVector equirectDirection(double u, double v)
{
    const double lon = (u - 0.5) * 2.0 * std::numbers::pi;
    const double lat = (0.5 - v) * std::numbers::pi;
    return {std::cos(lat) * std::sin(lon), std::sin(lat), std::cos(lat) * std::cos(lon)};
}

std::array<double, 2> equirectPoint(const SphereVector& d)
{
    const double length = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    const double lon = std::atan2(d[0], d[2]);
    const double lat = std::asin(std::clamp(d[1] / length, -1.0, 1.0));
    return {lon / (2.0 * std::numbers::pi) + 0.5, 0.5 - lat / std::numbers::pi};
}

std::array<double, 2> reframedSource(const ClipSphereView& view, double aspect, double u, double v)
{
    const double tanX = std::tan(view.fov * kRadians / 2.0);
    const double tanY = tanX / aspect;
    const SphereVector ray{(2.0 * u - 1.0) * tanX, (1.0 - 2.0 * v) * tanY, 1.0};
    return equirectPoint(rotated(sphereRotation(view), ray));
}

std::array<double, 2> turnedSource(const ClipSphereView& view, double u, double v)
{
    return equirectPoint(rotated(sphereRotation(view), equirectDirection(u, v)));
}

} // namespace tve
