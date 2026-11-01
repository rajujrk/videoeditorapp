#include "model/Paths.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace tve {

namespace {

constexpr double kDegrees = 180.0 / std::numbers::pi;

double segment(const std::array<double, 2>& a, const std::array<double, 2>& b, double aspect)
{
    return std::hypot((b[0] - a[0]) * aspect, b[1] - a[1]);
}

double easeInOut(double t)
{
    t = std::clamp(t, 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

// A smooth line through the points (Catmull–Rom, a few steps between each pair).
Polyline smoothed(const Polyline& points)
{
    if (points.size() < 3)
        return points;
    Polyline line;
    constexpr int kSteps = 4;
    for (std::size_t i = 0; i + 1 < points.size(); ++i) {
        const auto& p0 = points[i == 0 ? 0 : i - 1];
        const auto& p1 = points[i];
        const auto& p2 = points[i + 1];
        const auto& p3 = points[std::min(points.size() - 1, i + 2)];
        for (int s = 0; s < kSteps; ++s) {
            const double t = double(s) / kSteps, t2 = t * t, t3 = t2 * t;
            std::array<double, 2> p{};
            for (int k = 0; k < 2; ++k)
                p[std::size_t(k)] = 0.5 * (2 * p1[std::size_t(k)] + (-p0[std::size_t(k)] + p2[std::size_t(k)]) * t
                                           + (2 * p0[std::size_t(k)] - 5 * p1[std::size_t(k)] + 4 * p2[std::size_t(k)] - p3[std::size_t(k)]) * t2
                                           + (-p0[std::size_t(k)] + 3 * p1[std::size_t(k)] - 3 * p2[std::size_t(k)] + p3[std::size_t(k)]) * t3);
            line.push_back(p);
        }
    }
    line.push_back(points.back());
    return line;
}

} // namespace

Polyline pathLine(const DrawnPath& path)
{
    const Polyline& p = path.points;
    switch (path.kind) {
    case PathKind::Brush:
        return smoothed(p);
    case PathKind::Line:
    case PathKind::Arrow:
        return p.size() >= 2 ? Polyline{p.front(), p.back()} : p;
    case PathKind::Rectangle: {
        if (p.size() < 2)
            return p;
        const auto [a, b] = std::pair{p.front(), p.back()};
        return {a, {b[0], a[1]}, b, {a[0], b[1]}, a};
    }
    case PathKind::Ellipse: {
        if (p.size() < 2)
            return p;
        const double cx = (p.front()[0] + p.back()[0]) / 2, cy = (p.front()[1] + p.back()[1]) / 2;
        const double rx = std::abs(p.back()[0] - p.front()[0]) / 2, ry = std::abs(p.back()[1] - p.front()[1]) / 2;
        Polyline line;
        constexpr int kSteps = 64;
        for (int i = 0; i <= kSteps; ++i) {
            // From the top, clockwise.
            const double a = -std::numbers::pi / 2 + 2 * std::numbers::pi * i / kSteps;
            line.push_back({cx + rx * std::cos(a), cy + ry * std::sin(a)});
        }
        return line;
    }
    }
    return p;
}

double lineLength(const Polyline& line, double aspect)
{
    double length = 0;
    for (std::size_t i = 1; i < line.size(); ++i)
        length += segment(line[i - 1], line[i], aspect);
    return length;
}

PathPoint pointAlong(const Polyline& line, double fraction, double aspect)
{
    if (line.empty())
        return {0.5, 0.5, 0.0};
    if (line.size() == 1)
        return {line[0][0], line[0][1], 0.0};
    const double total = lineLength(line, aspect);
    double wanted = std::clamp(fraction, 0.0, 1.0) * total;
    for (std::size_t i = 1; i < line.size(); ++i) {
        const double length = segment(line[i - 1], line[i], aspect);
        if (wanted <= length || i + 1 == line.size()) {
            const double u = length > 0 ? std::clamp(wanted / length, 0.0, 1.0) : 0.0;
            const auto& a = line[i - 1];
            const auto& b = line[i];
            return {a[0] + (b[0] - a[0]) * u, a[1] + (b[1] - a[1]) * u,
                    std::atan2(b[1] - a[1], (b[0] - a[0]) * aspect) * kDegrees};
        }
        wanted -= length;
    }
    return {line.back()[0], line.back()[1], 0.0};
}

Polyline lineUpTo(const Polyline& line, double fraction, double aspect)
{
    if (fraction >= 1.0 || line.size() < 2)
        return fraction <= 0.0 ? Polyline{} : line;
    if (fraction <= 0.0)
        return {};
    double wanted = fraction * lineLength(line, aspect);
    Polyline part{line.front()};
    for (std::size_t i = 1; i < line.size(); ++i) {
        const double length = segment(line[i - 1], line[i], aspect);
        if (wanted <= length) {
            const double u = length > 0 ? wanted / length : 0.0;
            part.push_back({line[i - 1][0] + (line[i][0] - line[i - 1][0]) * u, line[i - 1][1] + (line[i][1] - line[i - 1][1]) * u});
            return part;
        }
        part.push_back(line[i]);
        wanted -= length;
    }
    return part;
}

std::vector<double> drawnAmounts(const ClipDrawing& drawing, double seconds, double duration, double aspect)
{
    const std::size_t n = drawing.paths.size();
    std::vector<double> amounts(n, 1.0);
    // How far drawn on (1 when all is there).
    double progress = 1.0;
    if (drawing.drawIn > 0 && seconds < drawing.drawIn)
        progress = easeInOut(seconds / drawing.drawIn);
    if (drawing.drawOut > 0 && duration > 0 && seconds > duration - drawing.drawOut)
        progress = std::min(progress, easeInOut((duration - seconds) / drawing.drawOut));
    if (progress >= 1.0)
        return amounts;
    if (!drawing.oneByOne) {
        std::ranges::fill(amounts, progress);
        return amounts;
    }
    // One after another: the drawn length shared out in order.
    std::vector<double> lengths(n);
    double total = 0;
    for (std::size_t i = 0; i < n; ++i)
        total += lengths[i] = std::max(1e-6, lineLength(pathLine(drawing.paths[i]), aspect));
    double left = progress * total;
    for (std::size_t i = 0; i < n; ++i) {
        amounts[i] = std::clamp(left / lengths[i], 0.0, 1.0);
        left = std::max(0.0, left - lengths[i]);
    }
    return amounts;
}

Polyline presetPath(const std::string& name)
{
    Polyline line;
    constexpr int kSteps = 48;
    if (name == "arc" || name == "dip") {
        // A shallow arch across the middle (or its upside-down).
        const double bend = name == "arc" ? -0.12 : 0.12;
        for (int i = 0; i <= kSteps; ++i) {
            const double t = double(i) / kSteps;
            line.push_back({0.15 + 0.7 * t, 0.5 + bend * (1.0 - std::pow(2.0 * t - 1.0, 2.0))});
        }
    } else if (name == "wave") {
        for (int i = 0; i <= kSteps; ++i) {
            const double t = double(i) / kSteps;
            line.push_back({0.1 + 0.8 * t, 0.5 + 0.06 * std::sin(t * 2 * std::numbers::pi * 1.5)});
        }
    } else if (name == "circle") {
        // Around the middle, clockwise from the bottom: its middle (where centred text sits) at
        // the top, reading left to right.
        for (int i = 0; i <= kSteps * 2; ++i) {
            const double a = std::numbers::pi / 2 + 2 * std::numbers::pi * i / (kSteps * 2);
            line.push_back({0.5 + 0.18 * std::cos(a), 0.5 + 0.3 * std::sin(a)});
        }
    }
    return line;
}

FollowKeyframes followLine(const Polyline& line, double aspect, Ticks duration, bool turn)
{
    FollowKeyframes keys;
    if (line.size() < 2 || duration <= 0)
        return keys;
    // Ten a second, at least the two ends.
    const int count = std::max(2, int(std::lround(double(duration) / double(kTicksPerSecond) * 10.0)) + 1);
    double lastAngle = 0;
    for (int i = 0; i < count; ++i) {
        const double fraction = double(i) / (count - 1);
        const PathPoint p = pointAlong(line, fraction, aspect);
        const Ticks at = Ticks(std::llround(fraction * double(duration)));
        keys.x.push_back({.time = at, .value = p.x - 0.5});
        keys.y.push_back({.time = at, .value = p.y - 0.5});
        if (turn) {
            // The nearest turn to the last (no spin through ±180°).
            double angle = p.angle;
            while (angle - lastAngle > 180)
                angle -= 360;
            while (angle - lastAngle < -180)
                angle += 360;
            keys.rotation.push_back({.time = at, .value = angle});
            lastAngle = angle;
        }
    }
    return keys;
}

} // namespace tve
