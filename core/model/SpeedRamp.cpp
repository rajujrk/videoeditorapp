#include "model/SpeedRamp.h"

#include <algorithm>
#include <cmath>

namespace tve {

namespace {

constexpr double kMinRampSpeed = 0.1; // kMinSpeed / kMaxSpeed (Project.h includes this file)
constexpr double kMaxRampSpeed = 10.0;

// ∫ speed from 0 to u.
double integral(const SpeedRamp& ramp, double u)
{
    double sum = 0.0;
    for (std::size_t i = 1; i < ramp.size(); ++i) {
        const SpeedPoint& a = ramp[i - 1];
        const SpeedPoint& b = ramp[i];
        if (u <= a.at)
            break;
        const double end = std::min(u, b.at);
        const double width = b.at - a.at;
        if (width <= 0.0)
            continue;
        const double speedAtEnd = a.speed + (b.speed - a.speed) * (end - a.at) / width;
        sum += (end - a.at) * (a.speed + speedAtEnd) / 2.0; // trapezoid: exact for a line
    }
    return sum;
}

} // namespace

double rampSpeedAt(const SpeedRamp& ramp, double u)
{
    if (ramp.empty())
        return 1.0;
    u = std::clamp(u, 0.0, 1.0);
    for (std::size_t i = 1; i < ramp.size(); ++i) {
        const SpeedPoint& a = ramp[i - 1];
        const SpeedPoint& b = ramp[i];
        if (u <= b.at) {
            const double width = b.at - a.at;
            return width > 0.0 ? a.speed + (b.speed - a.speed) * (u - a.at) / width : b.speed;
        }
    }
    return ramp.back().speed;
}

double rampAverage(const SpeedRamp& ramp)
{
    return ramp.empty() ? 1.0 : integral(ramp, 1.0);
}

double rampProgress(const SpeedRamp& ramp, double u)
{
    if (ramp.empty())
        return std::clamp(u, 0.0, 1.0);
    const double total = integral(ramp, 1.0);
    return total > 0.0 ? integral(ramp, std::clamp(u, 0.0, 1.0)) / total : u;
}

double rampFractionAt(const SpeedRamp& ramp, double progress)
{
    progress = std::clamp(progress, 0.0, 1.0);
    double lo = 0.0;
    double hi = 1.0;
    for (int i = 0; i < 60; ++i) { // progress rises with u: bisect
        const double mid = (lo + hi) / 2.0;
        (rampProgress(ramp, mid) < progress ? lo : hi) = mid;
    }
    return (lo + hi) / 2.0;
}

SpeedRamp subRamp(const SpeedRamp& ramp, double u0, double u1)
{
    if (ramp.empty() || u1 <= u0)
        return ramp;
    SpeedRamp part{{0.0, rampSpeedAt(ramp, u0)}};
    for (const SpeedPoint& p : ramp) {
        if (p.at > u0 && p.at < u1)
            part.push_back({(p.at - u0) / (u1 - u0), p.speed});
    }
    part.push_back({1.0, rampSpeedAt(ramp, u1)});
    return part;
}

std::optional<std::string> validateSpeedRamp(const SpeedRamp& ramp)
{
    if (ramp.size() < 2)
        return "a speed ramp needs at least two points";
    if (ramp.size() > kMaxSpeedPoints)
        return "a speed ramp has at most " + std::to_string(kMaxSpeedPoints) + " points";
    if (ramp.front().at != 0.0 || ramp.back().at != 1.0)
        return "a speed ramp runs from the clip's start to its end";
    for (std::size_t i = 0; i < ramp.size(); ++i) {
        const SpeedPoint& p = ramp[i];
        if (!std::isfinite(p.speed) || p.speed < kMinRampSpeed || p.speed > kMaxRampSpeed)
            return "speed must be between 0.1× and 10×";
        if (i > 0 && !(p.at > ramp[i - 1].at))
            return "speed points must be in order";
    }
    return std::nullopt;
}

const std::vector<SpeedRampPreset>& speedRampPresets()
{
    // Our own shapes for the usual ramp moves.
    static const std::vector<SpeedRampPreset> presets{
        {"ramp-up", "Ramp up", {{0.0, 0.5}, {1.0, 3.0}}},
        {"ramp-down", "Ramp down", {{0.0, 3.0}, {1.0, 0.5}}},
        {"flash-in", "Flash in", {{0.0, 5.0}, {0.3, 5.0}, {0.5, 1.0}, {1.0, 1.0}}},
        {"flash-out", "Flash out", {{0.0, 1.0}, {0.5, 1.0}, {0.7, 5.0}, {1.0, 5.0}}},
        {"bullet-time", "Bullet time", {{0.0, 1.5}, {0.3, 1.5}, {0.4, 0.25}, {0.7, 0.25}, {0.8, 1.5}, {1.0, 1.5}}},
        {"hero-moment", "Hero moment", {{0.0, 1.0}, {0.4, 1.0}, {0.5, 0.2}, {0.6, 0.2}, {0.7, 3.0}, {1.0, 3.0}}},
        {"montage", "Montage", {{0.0, 2.5}, {0.25, 0.6}, {0.5, 2.5}, {0.75, 0.6}, {1.0, 2.5}}},
        {"jumper", "Jumper", {{0.0, 3.0}, {0.4, 3.0}, {0.5, 0.3}, {0.6, 3.0}, {1.0, 3.0}}},
    };
    return presets;
}

} // namespace tve
