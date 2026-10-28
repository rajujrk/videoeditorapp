#pragma once

#include <array>
#include <optional>
#include <string>
#include <vector>

namespace tve {

// A point of a tone curve: input to output, both 0..1.
struct CurvePoint {
    double x = 0.0;
    double y = 0.0;

    friend bool operator==(const CurvePoint&, const CurvePoint&) = default;
};
// Empty: unchanged. Otherwise at least two points, sorted by x, the first at x 0 and the
// last at x 1; smooth (monotone cubic) between them.
using ColorCurve = std::vector<CurvePoint>;
inline constexpr std::size_t kMaxCurvePoints = 16;

// A colour wheel: a puck in the unit disk (direction is the hue pushed towards, distance
// how strongly) and a master amount for all three channels (-1..1).
struct ColorWheel {
    double x = 0.0;
    double y = 0.0;
    double master = 0.0;

    friend bool operator==(const ColorWheel&, const ColorWheel&) = default;
};

// One of the HSL qualifier's eight hue bands: how colours of that hue change. Hue turns up
// to 30° either way; saturation and luminance scale by up to ±100 % / ±50 %. Colours between
// two bands blend their settings; greys are left alone.
struct HslBand {
    double hue = 0.0;        // -1..1
    double saturation = 0.0; // -1..1
    double luminance = 0.0;  // -1..1

    friend bool operator==(const HslBand&, const HslBand&) = default;
};
inline constexpr int kHslBands = 8;
// Band centres, in degrees: red, orange, yellow, green, aqua, blue, purple, magenta.
inline constexpr std::array<double, kHslBands> kHslBandHues{0, 30, 60, 120, 180, 240, 270, 300};

// A clip's colour correction, applied to its picture before its effects:
// exposure and white balance, contrast, lift/gamma/gain/offset wheels, saturation, the HSL
// bands and hue curves, the tone curves, then a 3D LUT.
struct ColorGrade {
    double exposure = 0.0;    // stops, -4..4
    double contrast = 0.0;    // -1..1
    double saturation = 0.0;  // -1..1 (-1: grey)
    double temperature = 0.0; // -1..1, warmer above 0
    double tint = 0.0;        // -1..1, towards magenta above 0
    ColorWheel lift;          // shadows
    ColorWheel gamma;         // midtones
    ColorWheel gain;          // highlights
    ColorWheel offset;        // everything
    ColorCurve master;
    ColorCurve red;
    ColorCurve green;
    ColorCurve blue;
    std::array<HslBand, kHslBands> hsl{};
    // Curves of an amount over hue (x 0..1 is 0°..360°) or luma, where y 0.5 changes
    // nothing: saturation ×0..×2 by hue, hue ±60° by hue, saturation ×0..×2 by luma.
    ColorCurve hueVsSat;
    ColorCurve hueVsHue;
    ColorCurve lumaVsSat;
    std::string lutPath;      // .cube or .3dl; empty: none
    double lutIntensity = 1.0;

    // True when it leaves every colour as it is.
    bool isNeutral() const { return *this == ColorGrade{}; }

    friend bool operator==(const ColorGrade&, const ColorGrade&) = default;
};

std::optional<std::string> validateGrade(const ColorGrade& grade);

// `grade` at `amount` of its strength (0: neutral, 1: as is): numbers and wheels scale,
// curves move towards their neutral line, the LUT's intensity scales.
ColorGrade scaledGrade(const ColorGrade& grade, double amount);
std::optional<std::string> validateCurve(const ColorCurve& curve);

// The curve's output at `x` (0..1); `x` itself for an empty curve.
double evaluateColorCurve(const ColorCurve& curve, double x);

// What the GPU needs, worked out once per grade: per-channel factors in render order.
struct GradeCoefficients {
    std::array<float, 3> linearGain{1, 1, 1}; // exposure × white balance, in linear light
    float contrast = 1.0f;                    // slope around mid grey
    std::array<float, 3> lift{0, 0, 0};
    std::array<float, 3> gammaExponent{1, 1, 1};
    std::array<float, 3> gain{1, 1, 1};
    std::array<float, 3> offset{0, 0, 0};
    float saturation = 1.0f;
    bool curves = false;
    // Per band: hue shift (turns), saturation factor, value factor.
    std::array<std::array<float, 3>, kHslBands> hsl{};
    bool hslActive = false;
    bool hueCurves = false;
};
GradeCoefficients gradeCoefficients(const ColorGrade& grade);

// Each channel's curve with the master curve after it, sampled at `size` evenly spaced
// inputs from 0 to 1: the lookup the shader reads.
std::array<std::vector<float>, 3> bakeCurves(const ColorGrade& grade, int size = 256);
// The hue curves the same way: hue vs saturation, hue vs hue, luma vs saturation (0.5 where
// a curve is empty).
std::array<std::vector<float>, 3> bakeHueCurves(const ColorGrade& grade, int size = 256);

// Hue (0..1 turns), saturation and value of an RGB colour, and back.
std::array<float, 3> rgbToHsv(std::array<float, 3> rgb);
std::array<float, 3> hsvToRgb(std::array<float, 3> hsv);

inline constexpr float kMidGrey = 0.435f; // 18 % grey, gamma-encoded

// The grade (without its LUT) applied to one gamma-encoded RGB colour, as the shader does
// it; for tests and previews.
std::array<float, 3> applyGrade(const GradeCoefficients& c, const std::array<std::vector<float>, 3>& curves,
                                std::array<float, 3> rgb,
                                const std::array<std::vector<float>, 3>& hueCurves = {});

} // namespace tve
