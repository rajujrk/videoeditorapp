#pragma once

#include "model/Time.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace tve {

// How a value travels from one keyframe to the next (set on the earlier keyframe).
enum class Easing {
    Linear,
    EaseIn,    // starts slowly
    EaseOut,   // ends slowly
    EaseInOut, // both
    Hold,      // keeps this value until the next keyframe
    Curve,     // the keyframe's own cubic Bezier curve
};

// A timing curve from (0,0) to (1,1) through control points (x1,y1) and (x2,y2), as in CSS
// cubic-bezier(): x is time, y is progress. x stays in 0..1; y may overshoot.
struct EasingCurve {
    double x1 = 0.25;
    double y1 = 0.1;
    double x2 = 0.25;
    double y2 = 1.0;

    friend bool operator==(const EasingCurve&, const EasingCurve&) = default;
};
inline constexpr double kMinCurveY = -2.0;
inline constexpr double kMaxCurveY = 3.0;

// The curve that draws a named easing (Linear and the eases; Hold has none).
EasingCurve curveFor(Easing easing);
// Progress at time `t` (0..1) along `curve`.
double evaluateCurve(const EasingCurve& curve, double t);

// One value of an animated property at a time inside its clip (from the clip's start, on
// the timeline), so keyframes move with the clip.
struct Keyframe {
    Ticks time = 0;
    double value = 0.0;
    Easing easing = Easing::Linear;
    EasingCurve curve; // used when easing is Curve

    friend bool operator==(const Keyframe&, const Keyframe&) = default;
};

// A property's keyframes, sorted by time, at most one per time.
using Keyframes = std::vector<Keyframe>;

// Eased progress (0..1, or beyond for an overshooting curve) for a linear progress `t` (0..1).
double ease(Easing easing, double t);
double ease(const Keyframe& from, double t);

// The property's value at `time`: the first keyframe's value before it, the last one's after
// it, eased between them. `fallback` when there are no keyframes.
double evaluate(const Keyframes& keyframes, Ticks time, double fallback);

// The keyframe exactly at `time`, if any.
const Keyframe* keyframeAt(const Keyframes& keyframes, Ticks time);

// Clip properties that can be animated. Picture properties belong to clips on video tracks
// (and titles); volume to clips with sound.
struct AnimatableProperty {
    std::string_view id;
    double min;
    double max;
    bool picture;
};

namespace property {
inline constexpr std::string_view kX = "x";
inline constexpr std::string_view kY = "y";
inline constexpr std::string_view kScale = "scale";
inline constexpr std::string_view kRotation = "rotation";
inline constexpr std::string_view kOpacity = "opacity";
inline constexpr std::string_view kCropLeft = "cropLeft";
inline constexpr std::string_view kCropTop = "cropTop";
inline constexpr std::string_view kCropRight = "cropRight";
inline constexpr std::string_view kCropBottom = "cropBottom";
inline constexpr std::string_view kVolume = "volume"; // gain in dB
} // namespace property

const std::vector<AnimatableProperty>& animatableProperties();
const AnimatableProperty* findAnimatable(std::string_view id);

// Sorted by time, unique times, values in the property's range; nullopt if fine.
std::optional<std::string> validateKeyframes(std::string_view propertyId, const Keyframes& keyframes);
// The same without a range (effect parameters: their manifest knows it).
std::optional<std::string> validateKeyframeTimes(const Keyframes& keyframes);

} // namespace tve
