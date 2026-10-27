#pragma once

#include <optional>
#include <string>
#include <vector>

namespace tve {

// A speed that changes through a clip: points at fractions of the clip's length (0 = its
// start, 1 = its end), each with the speed there; between points the speed changes
// linearly. A ramped clip plays the same source span as at constant speed; its length on
// the timeline is that span divided by the ramp's average speed.
struct SpeedPoint {
    double at = 0.0;
    double speed = 1.0;

    friend bool operator==(const SpeedPoint&, const SpeedPoint&) = default;
};

// Sorted by `at`, the first at 0 and the last at 1; empty means no ramp.
using SpeedRamp = std::vector<SpeedPoint>;

inline constexpr std::size_t kMaxSpeedPoints = 16;

// Speed at fraction `u` (0..1) of the clip.
double rampSpeedAt(const SpeedRamp& ramp, double u);
// Average speed over the whole clip.
double rampAverage(const SpeedRamp& ramp);
// Fraction of the source span played by fraction `u` of the clip (0 at 0, 1 at 1).
double rampProgress(const SpeedRamp& ramp, double u);
// The fraction of the clip at which `progress` of the source span has played (inverse
// of rampProgress).
double rampFractionAt(const SpeedRamp& ramp, double progress);
// The part of the ramp from u0 to u1, stretched to 0..1: what each half keeps on a split.
SpeedRamp subRamp(const SpeedRamp& ramp, double u0, double u1);

// At least two points, first at 0, last at 1, rising, speeds 0.1×–10×; nullopt if fine.
std::optional<std::string> validateSpeedRamp(const SpeedRamp& ramp);

// Ready-made ramps, by name.
struct SpeedRampPreset {
    std::string id;
    std::string title;
    SpeedRamp ramp;
};
const std::vector<SpeedRampPreset>& speedRampPresets();

} // namespace tve
