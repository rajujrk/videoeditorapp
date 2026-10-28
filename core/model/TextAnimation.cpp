#include "model/TextAnimation.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace tve {

namespace {

constexpr double kPi = std::numbers::pi;

double easeOut(double u)
{
    return 1.0 - std::pow(1.0 - u, 3.0);
}

double backOut(double u)
{
    // Overshoots to about 110 % before settling.
    constexpr double c = 1.70158;
    const double v = u - 1.0;
    return 1.0 + (c + 1.0) * v * v * v + c * v * v;
}

double bounceOut(double u)
{
    // Lands, bounces twice, settles.
    if (u < 1.0 / 2.75)
        return 7.5625 * u * u;
    if (u < 2.0 / 2.75) {
        u -= 1.5 / 2.75;
        return 7.5625 * u * u + 0.75;
    }
    if (u < 2.5 / 2.75) {
        u -= 2.25 / 2.75;
        return 7.5625 * u * u + 0.9375;
    }
    u -= 2.625 / 2.75;
    return 7.5625 * u * u + 0.984375;
}

double hash(double a, double b)
{
    const double h = std::sin(a * 127.1 + b * 311.7) * 43758.5453;
    return h - std::floor(h);
}

// An in preset's state at unit progress u (0: not arrived, 1: settled).
TextUnitState arrive(std::string_view preset, double u, std::size_t index, std::size_t count)
{
    TextUnitState s;
    const double e = easeOut(u);
    if (preset == "fade") {
        s.opacity = e;
    } else if (preset == "rise") {
        s.dy = (1.0 - e) * 0.6;
        s.opacity = e;
    } else if (preset == "drop") {
        s.dy = -(1.0 - e) * 0.8;
        s.opacity = e;
    } else if (preset == "slide_left") {
        s.dx = (1.0 - e) * 1.5;
        s.opacity = e;
    } else if (preset == "slide_right") {
        s.dx = -(1.0 - e) * 1.5;
        s.opacity = e;
    } else if (preset == "zoom") {
        s.scale = 0.3 + 0.7 * e;
        s.opacity = e;
    } else if (preset == "pop") {
        s.scale = std::max(0.0, backOut(u));
        s.opacity = std::min(1.0, u * 3.0);
    } else if (preset == "spin") {
        s.rotation = -(1.0 - e) * 180.0;
        s.scale = 0.5 + 0.5 * e;
        s.opacity = e;
    } else if (preset == "flip") {
        s.scaleX = std::sin(e * kPi / 2.0);
        s.opacity = u > 0.0 ? 1.0 : 0.0;
    } else if (preset == "bounce") {
        s.dy = -(1.0 - bounceOut(u)) * 1.2;
        s.opacity = std::min(1.0, u * 4.0);
    } else if (preset == "typewriter") {
        s.opacity = u > 0.0 ? 1.0 : 0.0;
    } else if (preset == "blur") {
        s.blur = (1.0 - e) * 0.25;
        s.scale = 1.0 + 0.15 * (1.0 - e);
        s.opacity = e;
    } else if (preset == "wipe") {
        s.reveal = e;
    } else if (preset == "tracking") {
        s.dx = (double(index) - double(count - 1) / 2.0) * (1.0 - e) * 0.6;
        s.opacity = e;
    }
    return s;
}

// A loop preset's state, `phase` cycles in.
TextUnitState cycle(std::string_view preset, double phase, std::size_t index)
{
    TextUnitState s;
    const double turn = 2.0 * kPi * phase;
    if (preset == "wave") {
        s.dy = std::sin(turn - double(index) * 0.6) * 0.12;
    } else if (preset == "pulse") {
        s.scale = 1.0 + 0.08 * std::sin(turn);
    } else if (preset == "flicker") {
        // Changes about twelve times a cycle; dims now and then.
        s.opacity = hash(double(index), std::floor(phase * 12.0)) > 0.18 ? 1.0 : 0.35;
    } else if (preset == "shake") {
        const double step = std::floor(phase * 20.0);
        s.dx = (hash(double(index), step) - 0.5) * 0.08;
        s.dy = (hash(double(index) + 17.0, step) - 0.5) * 0.08;
    } else if (preset == "swing") {
        s.rotation = std::sin(turn - double(index) * 0.4) * 8.0;
    }
    return s;
}

// Progress of unit `index` of `count` `time` seconds into an animation of `duration`.
double unitProgress(const TextAnimation& a, std::size_t index, std::size_t count, double time)
{
    const double n = double(std::max<std::size_t>(count, 1));
    if (a.preset == "typewriter") // units appear one by one, evenly through the duration
        return time >= a.duration * double(index) / n ? 1.0 : 0.0;
    const double length = a.duration / (1.0 + a.stagger * (n - 1.0));
    const double start = double(index) * a.stagger * length;
    return std::clamp((time - start) / std::max(length, 1e-9), 0.0, 1.0);
}

} // namespace

bool TextUnitState::identity() const
{
    return *this == TextUnitState{};
}

const std::vector<TextAnimationPreset>& textAnimationPresets()
{
    static const std::vector<TextAnimationPreset> presets{
        {"fade", "Fade", false},         {"rise", "Rise", false},          {"drop", "Drop", false},
        {"slide_left", "Slide left", false}, {"slide_right", "Slide right", false}, {"zoom", "Zoom", false},
        {"pop", "Pop", false},           {"spin", "Spin", false},          {"flip", "Flip", false},
        {"bounce", "Bounce", false},     {"typewriter", "Typewriter", false}, {"blur", "Focus", false},
        {"wipe", "Wipe", false},         {"tracking", "Tracking", false},  {"wave", "Wave", true},
        {"pulse", "Pulse", true},        {"flicker", "Flicker", true},     {"shake", "Shake", true},
        {"swing", "Swing", true},
    };
    return presets;
}

const TextAnimationPreset* findTextAnimationPreset(std::string_view id)
{
    const auto& all = textAnimationPresets();
    const auto it = std::ranges::find(all, id, &TextAnimationPreset::id);
    return it == all.end() ? nullptr : &*it;
}

TextUnitState animateTextUnit(const TextAnimation& a, TextAnimationRole role, std::size_t index, std::size_t count,
                              double time, double clipDuration)
{
    if (!a.active() || a.duration <= 0.0)
        return {};
    switch (role) {
    case TextAnimationRole::In:
        if (time >= a.duration)
            return {};
        return arrive(a.preset, unitProgress(a, index, count, std::max(time, 0.0)), index, count);
    case TextAnimationRole::Out: {
        // Played backwards so that it ends as the clip does: the first unit leaves first.
        const double into = time - (clipDuration - a.duration);
        if (into <= 0.0)
            return {};
        return arrive(a.preset, 1.0 - unitProgress(a, index, count, into), index, count);
    }
    case TextAnimationRole::Loop:
        return cycle(a.preset, time / a.duration, index);
    }
    return {};
}

std::optional<std::string> validateTextAnimation(const TextAnimation& a, TextAnimationRole role)
{
    if (!a.active())
        return std::nullopt;
    const TextAnimationPreset* preset = findTextAnimationPreset(a.preset);
    if (!preset)
        return "unknown text animation \"" + a.preset + "\"";
    if (preset->loop != (role == TextAnimationRole::Loop))
        return preset->loop ? "\"" + a.preset + "\" is a loop, not an in or out animation"
                            : "\"" + a.preset + "\" is not a loop";
    if (!std::isfinite(a.duration) || a.duration < kMinTextAnimationDuration || a.duration > kMaxTextAnimationDuration)
        return "text animation duration is out of range";
    if (!std::isfinite(a.stagger) || a.stagger < 0.0 || a.stagger > 1.0)
        return "text animation stagger must be between 0 and 1";
    return std::nullopt;
}

} // namespace tve
