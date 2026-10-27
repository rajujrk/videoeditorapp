#include "model/Keyframes.h"

#include <algorithm>
#include <cmath>

namespace tve {

double ease(Easing easing, double t)
{
    t = std::clamp(t, 0.0, 1.0);
    switch (easing) {
    case Easing::Linear:
        return t;
    case Easing::EaseIn:
        return t * t * t;
    case Easing::EaseOut:
        return 1.0 - std::pow(1.0 - t, 3.0);
    case Easing::EaseInOut:
        return t < 0.5 ? 4.0 * t * t * t : 1.0 - std::pow(-2.0 * t + 2.0, 3.0) / 2.0;
    case Easing::Hold:
        return 0.0;
    case Easing::Curve:
        break;
    }
    return t;
}

EasingCurve curveFor(Easing easing)
{
    // Cubic-Bezier stand-ins for the named easings (close to their exact curves).
    switch (easing) {
    case Easing::EaseIn:
        return {0.32, 0.0, 0.67, 0.0};
    case Easing::EaseOut:
        return {0.33, 1.0, 0.68, 1.0};
    case Easing::EaseInOut:
        return {0.65, 0.0, 0.35, 1.0};
    case Easing::Linear:
    case Easing::Hold:
    case Easing::Curve:
        break;
    }
    return {0.0, 0.0, 1.0, 1.0};
}

double evaluateCurve(const EasingCurve& c, double t)
{
    t = std::clamp(t, 0.0, 1.0);
    const auto bezier = [](double p1, double p2, double s) {
        const double u = 1.0 - s;
        return 3.0 * u * u * s * p1 + 3.0 * u * s * s * p2 + s * s * s;
    };
    // x(s) rises monotonically (x1, x2 in 0..1): bisect for the s where x(s) = t.
    double lo = 0.0;
    double hi = 1.0;
    for (int i = 0; i < 40; ++i) {
        const double mid = (lo + hi) / 2.0;
        (bezier(c.x1, c.x2, mid) < t ? lo : hi) = mid;
    }
    return bezier(c.y1, c.y2, (lo + hi) / 2.0);
}

double ease(const Keyframe& from, double t)
{
    return from.easing == Easing::Curve ? evaluateCurve(from.curve, t) : ease(from.easing, t);
}

double evaluate(const Keyframes& keyframes, Ticks time, double fallback)
{
    if (keyframes.empty())
        return fallback;
    if (time <= keyframes.front().time)
        return keyframes.front().value;
    if (time >= keyframes.back().time)
        return keyframes.back().value;
    const auto next = std::ranges::upper_bound(keyframes, time, {}, &Keyframe::time);
    const Keyframe& a = *(next - 1);
    const Keyframe& b = *next;
    const double t = double(time - a.time) / double(b.time - a.time);
    return a.value + (b.value - a.value) * ease(a, t);
}

const Keyframe* keyframeAt(const Keyframes& keyframes, Ticks time)
{
    const auto it = std::ranges::lower_bound(keyframes, time, {}, &Keyframe::time);
    return it != keyframes.end() && it->time == time ? &*it : nullptr;
}

const std::vector<AnimatableProperty>& animatableProperties()
{
    // The same ranges as the static values (see SetClipVisualCommand and SetClipAudioCommand).
    static const std::vector<AnimatableProperty> properties{
        {property::kX, -10.0, 10.0, true},
        {property::kY, -10.0, 10.0, true},
        {property::kScale, 0.01, 20.0, true},
        {property::kRotation, -3600.0, 3600.0, true},
        {property::kOpacity, 0.0, 1.0, true},
        {property::kCropLeft, 0.0, 0.9, true},
        {property::kCropTop, 0.0, 0.9, true},
        {property::kCropRight, 0.0, 0.9, true},
        {property::kCropBottom, 0.0, 0.9, true},
        {property::kVolume, -60.0, 12.0, false},
    };
    return properties;
}

const AnimatableProperty* findAnimatable(std::string_view id)
{
    const auto& all = animatableProperties();
    const auto it = std::ranges::find(all, id, &AnimatableProperty::id);
    return it != all.end() ? &*it : nullptr;
}

std::optional<std::string> validateKeyframes(std::string_view propertyId, const Keyframes& keyframes)
{
    const AnimatableProperty* property = findAnimatable(propertyId);
    if (!property)
        return "\"" + std::string(propertyId) + "\" cannot be animated";
    for (const Keyframe& k : keyframes) {
        if (!std::isfinite(k.value) || k.value < property->min || k.value > property->max)
            return "a keyframe of " + std::string(propertyId) + " is out of range";
    }
    return validateKeyframeTimes(keyframes);
}

std::optional<std::string> validateKeyframeTimes(const Keyframes& keyframes)
{
    for (std::size_t i = 0; i < keyframes.size(); ++i) {
        const Keyframe& k = keyframes[i];
        if (!std::isfinite(k.value))
            return "a keyframe has no value";
        if (i > 0 && k.time <= keyframes[i - 1].time)
            return "keyframes must be in order, one per time";
        const EasingCurve& c = k.curve;
        const auto inside = [](double v, double lo, double hi) { return std::isfinite(v) && v >= lo && v <= hi; };
        if (!inside(c.x1, 0.0, 1.0) || !inside(c.x2, 0.0, 1.0) || !inside(c.y1, kMinCurveY, kMaxCurveY)
            || !inside(c.y2, kMinCurveY, kMaxCurveY))
            return "a keyframe's curve is out of range";
    }
    return std::nullopt;
}

} // namespace tve
