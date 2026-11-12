#include "model/ColourMatch.h"

#include <algorithm>
#include <cmath>

namespace tve {

namespace {

std::array<double, 5> quantilesOf(std::vector<float>& values)
{
    std::array<double, 5> q{};
    if (values.empty())
        return q;
    for (std::size_t i = 0; i < kColourQuantiles.size(); ++i) {
        const auto at = std::size_t(std::llround(kColourQuantiles[i] * double(values.size() - 1)));
        std::nth_element(values.begin(), values.begin() + std::ptrdiff_t(at), values.end());
        q[i] = std::clamp(double(values[at]), 0.0, 1.0);
    }
    return q;
}

// A rising curve through (s, t) pairs, reaching x 0 and x 1, points at least 0.02 apart.
ColorCurve curveThrough(const std::array<double, 5>& s, const std::array<double, 5>& t)
{
    std::vector<CurvePoint> points;
    const auto slope = [](double ds, double dt) { return ds > 1e-3 ? std::clamp(dt / ds, 0.0, 4.0) : 1.0; };
    const double low = slope(s[1] - s[0], t[1] - t[0]);
    const double high = slope(s[4] - s[3], t[4] - t[3]);
    points.push_back({0.0, std::clamp(t[0] - s[0] * low, 0.0, 1.0)});
    for (std::size_t i = 0; i < s.size(); ++i)
        points.push_back({s[i], std::clamp(t[i], 0.0, 1.0)});
    points.push_back({1.0, std::clamp(t[4] + (1.0 - s[4]) * high, 0.0, 1.0)});
    ColorCurve curve;
    for (const CurvePoint& p : points) {
        if (!curve.empty() && p.x - curve.back().x < 0.02) {
            if (p.x >= 1.0) // (the end stays at 1: the last point before it gives way)
                curve.back() = {1.0, std::max(curve.back().y, p.y)};
            continue;
        }
        curve.push_back({p.x, curve.empty() ? p.y : std::max(curve.back().y, p.y)});
    }
    if (curve.back().x < 1.0)
        curve.push_back({1.0, curve.back().y});
    return curve;
}

} // namespace

ColourStats colourStats(const std::vector<std::array<float, 3>>& pixels)
{
    ColourStats stats;
    if (pixels.empty())
        return stats;
    std::array<std::vector<float>, 3> channels;
    std::vector<float> luma;
    for (auto& c : channels)
        c.reserve(pixels.size());
    luma.reserve(pixels.size());
    for (const auto& p : pixels) {
        for (std::size_t i = 0; i < 3; ++i)
            channels[i].push_back(p[i]);
        luma.push_back(0.2126f * p[0] + 0.7152f * p[1] + 0.0722f * p[2]);
    }
    for (std::size_t i = 0; i < 3; ++i)
        stats.channels[i] = quantilesOf(channels[i]);
    stats.luma = quantilesOf(luma);
    stats.valid = true;
    return stats;
}

ColourStats autoColourTarget(const ColourStats& source)
{
    ColourStats target = source;
    const auto& l = source.luma;
    const double range = std::max(l[4] - l[0], 0.05);
    std::array<double, 5> neutral{};
    for (std::size_t i = 0; i < neutral.size(); ++i)
        neutral[i] = 0.04 + (l[i] - l[0]) / range * 0.92; // levels: 4 % to 96 %
    neutral[2] = neutral[2] + (0.45 - neutral[2]) * 0.3;  // the middle eased towards mid grey
    neutral[1] = std::min(neutral[1], neutral[2]);
    neutral[3] = std::max(neutral[3], neutral[2]);
    // Mostly neutral (the cast goes), a little of each channel's own spread (stretched as
    // well) so a warm scene stays a little warm.
    for (std::size_t c = 0; c < 3; ++c) {
        const auto& own = source.channels[c];
        const double ownRange = std::max(own[4] - own[0], 0.05);
        for (std::size_t i = 0; i < 5; ++i) {
            const double stretched = 0.04 + (own[i] - own[0]) / ownRange * 0.92;
            target.channels[c][i] = std::clamp(neutral[i] * 0.85 + stretched * 0.15, 0.0, 1.0);
        }
    }
    target.luma = neutral;
    return target;
}

ColorGrade withMatchingCurves(ColorGrade grade, const ColourStats& source, const ColourStats& target)
{
    if (!source.valid || !target.valid)
        return grade;
    grade.red = curveThrough(source.channels[0], target.channels[0]);
    grade.green = curveThrough(source.channels[1], target.channels[1]);
    grade.blue = curveThrough(source.channels[2], target.channels[2]);
    return grade;
}

} // namespace tve
