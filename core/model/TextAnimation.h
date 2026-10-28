#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace tve {

// What a title animation moves: the whole text at once, or its lines, words or characters
// one after another.
enum class TextUnit { Whole, Line, Word, Character };

// One of a title's three animations: how its text arrives (in, from the clip's start), leaves
// (out, ending at the clip's end) or keeps moving (loop). An empty preset is none.
struct TextAnimation {
    std::string preset;
    double duration = 0.8;          // seconds (a loop: one cycle)
    TextUnit unit = TextUnit::Character;
    double stagger = 0.6;           // 0: every unit together … 1: one after another

    bool active() const { return !preset.empty(); }
    friend bool operator==(const TextAnimation&, const TextAnimation&) = default;
};

inline constexpr double kMinTextAnimationDuration = 0.05;
inline constexpr double kMaxTextAnimationDuration = 20.0;

// A unit's look at one moment, relative to where the layout put it. Lengths are in ems
// (the font size); y points down.
struct TextUnitState {
    double opacity = 1.0;
    double dx = 0.0;
    double dy = 0.0;
    double scale = 1.0;
    double scaleX = 1.0;   // extra horizontal scale (flips)
    double rotation = 0.0; // degrees, clockwise
    double blur = 0.0;     // ems
    double reveal = 1.0;   // how much of the unit shows, from its left edge

    bool identity() const;
    friend bool operator==(const TextUnitState&, const TextUnitState&) = default;
};

enum class TextAnimationRole { In, Out, Loop };

struct TextAnimationPreset {
    std::string_view id;
    std::string_view title;
    bool loop; // a loop preset; the others serve as in and (played backwards) out animations
};

// fade, rise, drop, slide_left, slide_right, zoom, pop, spin, flip, bounce, typewriter,
// blur, wipe, tracking (in and out); wave, pulse, flicker, shake, swing (loops).
const std::vector<TextAnimationPreset>& textAnimationPresets();
const TextAnimationPreset* findTextAnimationPreset(std::string_view id);

// The state of unit `index` of `count` under `animation` in `role`, `time` seconds into a
// clip lasting `clipDuration` seconds. Identity outside the animation's window (an in
// animation after it ends, an out animation before it starts).
TextUnitState animateTextUnit(const TextAnimation& animation, TextAnimationRole role, std::size_t index,
                              std::size_t count, double time, double clipDuration);

// Nullopt if fine: a known preset of the right kind, a duration and stagger in range.
std::optional<std::string> validateTextAnimation(const TextAnimation& animation, TextAnimationRole role);

} // namespace tve
