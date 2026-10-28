#include "model/ColorGrade.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace tve {

namespace {

bool inRange(double v, double lo, double hi)
{
    return std::isfinite(v) && v >= lo && v <= hi;
}

std::optional<std::string> validateWheel(const ColorWheel& w, const char* name)
{
    if (!inRange(w.master, -1.0, 1.0) || !std::isfinite(w.x) || !std::isfinite(w.y) || std::hypot(w.x, w.y) > 1.0001)
        return std::string(name) + " is out of range";
    return std::nullopt;
}

// A wheel's push towards its hue: three channel amounts that add up to nothing, up to 1.
std::array<float, 3> wheelShift(const ColorWheel& w)
{
    const double strength = std::min(1.0, std::hypot(w.x, w.y));
    if (strength <= 0.0)
        return {0, 0, 0};
    const double angle = std::atan2(w.y, w.x);
    constexpr double third = 2.0 * std::numbers::pi / 3.0;
    return {float(strength * std::cos(angle)), float(strength * std::cos(angle - third)),
            float(strength * std::cos(angle + third))};
}

} // namespace

std::optional<std::string> validateCurve(const ColorCurve& curve)
{
    if (curve.empty())
        return std::nullopt;
    if (curve.size() < 2 || curve.size() > kMaxCurvePoints)
        return "a curve has 2 to " + std::to_string(kMaxCurvePoints) + " points";
    if (curve.front().x != 0.0 || curve.back().x != 1.0)
        return "a curve runs from input 0 to input 1";
    for (std::size_t i = 0; i < curve.size(); ++i) {
        if (!inRange(curve[i].x, 0.0, 1.0) || !inRange(curve[i].y, 0.0, 1.0))
            return "curve points are between 0 and 1";
        if (i > 0 && !(curve[i].x > curve[i - 1].x))
            return "curve points must be in order";
    }
    return std::nullopt;
}

std::optional<std::string> validateGrade(const ColorGrade& g)
{
    if (!inRange(g.exposure, -4.0, 4.0))
        return "exposure must be between -4 and +4 stops";
    for (double v : {g.contrast, g.saturation, g.temperature, g.tint}) {
        if (!inRange(v, -1.0, 1.0))
            return "colour adjustments must be between -100% and +100%";
    }
    for (const auto& [wheel, name] : {std::pair{&g.lift, "lift"}, std::pair{&g.gamma, "gamma"},
                                      std::pair{&g.gain, "gain"}, std::pair{&g.offset, "offset"}}) {
        if (auto error = validateWheel(*wheel, name))
            return error;
    }
    for (const ColorCurve* curve : {&g.master, &g.red, &g.green, &g.blue}) {
        if (auto error = validateCurve(*curve))
            return error;
    }
    if (!inRange(g.lutIntensity, 0.0, 1.0))
        return "LUT intensity must be between 0 and 100%";
    return std::nullopt;
}

double evaluateColorCurve(const ColorCurve& curve, double x)
{
    x = std::clamp(x, 0.0, 1.0);
    if (curve.size() < 2)
        return x;
    const std::size_t n = curve.size();
    std::size_t i = 1;
    while (i < n - 1 && x > curve[i].x)
        ++i;
    // Monotone cubic (Fritsch–Carlson): smooth, and never overshoots between points.
    const auto slope = [&](std::size_t k) { return (curve[k + 1].y - curve[k].y) / (curve[k + 1].x - curve[k].x); };
    const auto tangent = [&](std::size_t k) {
        if (k == 0)
            return slope(0);
        if (k == n - 1)
            return slope(n - 2);
        const double a = slope(k - 1);
        const double b = slope(k);
        if (a * b <= 0.0)
            return 0.0; // a peak or a dip: flat there
        return 2.0 / (1.0 / a + 1.0 / b); // harmonic mean of the neighbouring slopes
    };
    const CurvePoint& p0 = curve[i - 1];
    const CurvePoint& p1 = curve[i];
    const double h = p1.x - p0.x;
    const double t = (x - p0.x) / h;
    double m0 = tangent(i - 1);
    double m1 = tangent(i);
    // Keep the segment monotone where its ends are.
    const double d = slope(i - 1);
    if (d == 0.0) {
        m0 = m1 = 0.0;
    } else {
        m0 = std::clamp(m0 / d, 0.0, 3.0) * d;
        m1 = std::clamp(m1 / d, 0.0, 3.0) * d;
    }
    const double t2 = t * t;
    const double t3 = t2 * t;
    const double y = (2 * t3 - 3 * t2 + 1) * p0.y + (t3 - 2 * t2 + t) * h * m0 + (-2 * t3 + 3 * t2) * p1.y
                   + (t3 - t2) * h * m1;
    return std::clamp(y, 0.0, 1.0);
}

GradeCoefficients gradeCoefficients(const ColorGrade& g)
{
    GradeCoefficients c;
    const float exposure = float(std::exp2(g.exposure));
    const float warm = float(g.temperature) * 0.25f;
    const float magenta = float(g.tint) * 0.25f;
    c.linearGain = {exposure * (1.0f + warm), exposure * (1.0f - magenta), exposure * (1.0f - warm)};
    c.contrast = 1.0f + float(g.contrast);
    const auto lift = wheelShift(g.lift);
    const auto gamma = wheelShift(g.gamma);
    const auto gain = wheelShift(g.gain);
    const auto offset = wheelShift(g.offset);
    for (int i = 0; i < 3; ++i) {
        c.lift[i] = 0.25f * lift[i] + 0.5f * float(g.lift.master);
        c.gammaExponent[i] = std::exp2(-(0.5f * gamma[i] + float(g.gamma.master)));
        c.gain[i] = std::max(0.0f, 1.0f + 0.5f * gain[i] + float(g.gain.master));
        c.offset[i] = 0.25f * offset[i] + 0.25f * float(g.offset.master);
    }
    c.saturation = 1.0f + float(g.saturation);
    c.curves = !g.master.empty() || !g.red.empty() || !g.green.empty() || !g.blue.empty();
    return c;
}

std::array<std::vector<float>, 3> bakeCurves(const ColorGrade& g, int size)
{
    std::array<std::vector<float>, 3> tables;
    const ColorCurve* channels[3] = {&g.red, &g.green, &g.blue};
    for (int c = 0; c < 3; ++c) {
        tables[std::size_t(c)].resize(std::size_t(size));
        for (int i = 0; i < size; ++i) {
            const double x = double(i) / double(size - 1);
            tables[std::size_t(c)][std::size_t(i)] = float(evaluateColorCurve(g.master, evaluateColorCurve(*channels[c], x)));
        }
    }
    return tables;
}

std::array<float, 3> applyGrade(const GradeCoefficients& c, const std::array<std::vector<float>, 3>& curves,
                                std::array<float, 3> rgb)
{
    for (int i = 0; i < 3; ++i) {
        float v = std::pow(std::max(rgb[i], 0.0f), 2.2f) * c.linearGain[i];
        v = std::pow(v, 1.0f / 2.2f);
        v = (v - kMidGrey) * c.contrast + kMidGrey;
        v = c.gain[i] * (v + c.lift[i] * (1.0f - v));
        v = std::pow(std::max(v, 0.0f), c.gammaExponent[i]);
        rgb[i] = v + c.offset[i];
    }
    const float luma = 0.2126f * rgb[0] + 0.7152f * rgb[1] + 0.0722f * rgb[2];
    for (int i = 0; i < 3; ++i) {
        float v = std::clamp(luma + (rgb[i] - luma) * c.saturation, 0.0f, 1.0f);
        if (c.curves) {
            // Linear between the table's samples, like the GPU's filtered lookup.
            const auto& table = curves[std::size_t(i)];
            const float position = v * float(table.size() - 1);
            const std::size_t k = std::min(std::size_t(position), table.size() - 2);
            v = table[k] + (table[k + 1] - table[k]) * (position - float(k));
        }
        rgb[i] = v;
    }
    return rgb;
}

} // namespace tve
