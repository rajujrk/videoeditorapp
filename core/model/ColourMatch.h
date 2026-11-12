#pragma once

#include "model/ColorGrade.h"

#include <array>
#include <vector>

// One-click colour: a picture's colours measured (each channel's spread, by quantiles), and
// tone curves that move them — towards a neutral, full-range picture (auto colour), or onto
// another picture's (colour match). No model, no cloud: statistics of the frame.
namespace tve {

inline constexpr std::array<double, 5> kColourQuantiles{0.02, 0.25, 0.5, 0.75, 0.98};

struct ColourStats {
    // Per channel (r, g, b) and for luma, the values at kColourQuantiles (gamma-encoded 0..1).
    std::array<std::array<double, 5>, 3> channels{};
    std::array<double, 5> luma{};
    bool valid = false;
};

// Of gamma-encoded RGB pixels (0..1).
ColourStats colourStats(const std::vector<std::array<float, 3>>& pixels);

// Where auto colour takes a picture: every channel to the same spread (no colour cast), its
// levels stretched to nearly the full range, its middle eased towards mid grey.
ColourStats autoColourTarget(const ColourStats& source);

// `grade` with red, green and blue curves that move `source`'s channels onto `target`'s
// (quantile to quantile, smooth and rising between them). Its other settings stay.
ColorGrade withMatchingCurves(ColorGrade grade, const ColourStats& source, const ColourStats& target);

} // namespace tve
