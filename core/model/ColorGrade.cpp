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
    for (const HslBand& band : g.hsl) {
        if (!inRange(band.hue, -1.0, 1.0) || !inRange(band.saturation, -1.0, 1.0) || !inRange(band.luminance, -1.0, 1.0))
            return "HSL adjustments must be between -100% and +100%";
    }
    for (const ColorCurve* curve : {&g.master, &g.red, &g.green, &g.blue, &g.hueVsSat, &g.hueVsHue, &g.lumaVsSat}) {
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
    for (int b = 0; b < kHslBands; ++b) {
        const HslBand& band = g.hsl[std::size_t(b)];
        c.hsl[std::size_t(b)] = {float(band.hue) * 30.0f / 360.0f, 1.0f + float(band.saturation),
                                 1.0f + 0.5f * float(band.luminance)};
        c.hslActive = c.hslActive || band != HslBand{};
    }
    c.hueCurves = !g.hueVsSat.empty() || !g.hueVsHue.empty() || !g.lumaVsSat.empty();
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

std::array<std::vector<float>, 3> bakeHueCurves(const ColorGrade& g, int size)
{
    std::array<std::vector<float>, 3> tables;
    const ColorCurve* curves[3] = {&g.hueVsSat, &g.hueVsHue, &g.lumaVsSat};
    for (int c = 0; c < 3; ++c) {
        tables[std::size_t(c)].resize(std::size_t(size));
        for (int i = 0; i < size; ++i) {
            const double x = double(i) / double(size - 1);
            tables[std::size_t(c)][std::size_t(i)] = curves[c]->empty() ? 0.5f : float(evaluateColorCurve(*curves[c], x));
        }
    }
    return tables;
}

std::array<float, 3> rgbToHsv(std::array<float, 3> rgb)
{
    const float maxC = std::max({rgb[0], rgb[1], rgb[2]});
    const float minC = std::min({rgb[0], rgb[1], rgb[2]});
    const float delta = maxC - minC;
    float hue = 0.0f;
    if (delta > 1e-6f) {
        if (maxC == rgb[0])
            hue = std::fmod((rgb[1] - rgb[2]) / delta + 6.0f, 6.0f);
        else if (maxC == rgb[1])
            hue = (rgb[2] - rgb[0]) / delta + 2.0f;
        else
            hue = (rgb[0] - rgb[1]) / delta + 4.0f;
        hue /= 6.0f;
    }
    return {hue, maxC > 1e-6f ? delta / maxC : 0.0f, maxC};
}

std::array<float, 3> hsvToRgb(std::array<float, 3> hsv)
{
    const float h = (hsv[0] - std::floor(hsv[0])) * 6.0f;
    const float s = hsv[1];
    const float v = hsv[2];
    std::array<float, 3> rgb{};
    for (int i = 0; i < 3; ++i) {
        // The standard k-form: k = (n + h) mod 6 for n = 5, 3, 1.
        const float k = std::fmod(float(5 - 2 * i) + h, 6.0f);
        rgb[std::size_t(i)] = v - v * s * std::clamp(std::min(k, 4.0f - k), 0.0f, 1.0f);
    }
    return rgb;
}

namespace {

// Linear lookup in a baked table, like the GPU's filtered read.
float lookup(const std::vector<float>& table, float v)
{
    const float position = std::clamp(v, 0.0f, 1.0f) * float(table.size() - 1);
    const std::size_t k = std::min(std::size_t(position), table.size() - 2);
    return table[k] + (table[k + 1] - table[k]) * (position - float(k));
}

} // namespace

std::array<float, 3> applyGrade(const GradeCoefficients& c, const std::array<std::vector<float>, 3>& curves,
                                std::array<float, 3> rgb, const std::array<std::vector<float>, 3>& hueCurves)
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
    for (int i = 0; i < 3; ++i)
        rgb[i] = std::clamp(luma + (rgb[i] - luma) * c.saturation, 0.0f, 1.0f);

    if (c.hslActive || c.hueCurves) {
        std::array<float, 3> hsv = rgbToHsv(rgb);
        const float hue = hsv[0];
        if (c.hslActive) {
            // The two bands around this hue, blended by where it falls between their centres.
            const float degrees = hue * 360.0f;
            int a = kHslBands - 1;
            for (int b = 0; b < kHslBands; ++b) {
                if (degrees >= float(kHslBandHues[std::size_t(b)]))
                    a = b;
            }
            const int next = (a + 1) % kHslBands;
            const float from = float(kHslBandHues[std::size_t(a)]);
            const float to = next == 0 ? 360.0f : float(kHslBandHues[std::size_t(next)]);
            const float t = (degrees - from) / (to - from);
            const float strength = std::clamp(hsv[1] / 0.15f, 0.0f, 1.0f); // greys stay grey
            std::array<float, 3> adjust{};
            for (int k = 0; k < 3; ++k)
                adjust[std::size_t(k)] = c.hsl[std::size_t(a)][std::size_t(k)] * (1 - t) + c.hsl[std::size_t(next)][std::size_t(k)] * t;
            hsv[0] += adjust[0] * strength;
            hsv[1] *= 1.0f + (adjust[1] - 1.0f) * strength;
            hsv[2] *= 1.0f + (adjust[2] - 1.0f) * strength;
        }
        if (c.hueCurves) {
            hsv[1] *= 2.0f * lookup(hueCurves[0], hue);
            hsv[0] += (lookup(hueCurves[1], hue) - 0.5f) / 3.0f; // ±60°
            hsv[1] *= 2.0f * lookup(hueCurves[2], std::clamp(luma, 0.0f, 1.0f));
        }
        hsv[1] = std::clamp(hsv[1], 0.0f, 1.0f);
        hsv[2] = std::clamp(hsv[2], 0.0f, 1.0f);
        rgb = hsvToRgb(hsv);
    }

    if (c.curves) {
        for (int i = 0; i < 3; ++i)
            rgb[i] = lookup(curves[std::size_t(i)], rgb[i]);
    }
    return rgb;
}

} // namespace tve
