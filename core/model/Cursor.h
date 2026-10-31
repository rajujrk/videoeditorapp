#pragma once

#include "model/Time.h"

#include <optional>
#include <vector>

namespace tve {

// The mouse during a screen recording — where it was and when it clicked — and the effects
// drawn from it on the recording's clip: rings where it clicks, a highlight around it, a
// spotlight that dims the rest.

struct CursorSample {
    Ticks time = 0; // media time
    double x = 0.0; // picture fractions, origin top left
    double y = 0.0;

    friend bool operator==(const CursorSample&, const CursorSample&) = default;
};

struct CursorEffects {
    std::vector<CursorSample> path; // in time order
    std::vector<Ticks> clicks;      // media times, in order
    bool rings = true;      // an expanding ring at each click
    bool highlight = false; // a soft disc around the cursor
    bool spotlight = false; // everything but around the cursor dimmed
    double size = 0.05;     // the highlight's / spotlight's radius, a fraction of the picture's height

    bool hasLog() const { return !path.empty(); }
    bool shows() const { return hasLog() && (rings || highlight || spotlight); }
    // Where the cursor was at a media time (between samples: mixed; held at the ends).
    std::optional<CursorSample> at(Ticks mediaTime) const;
    // How long ago (seconds) the latest click at or before `mediaTime` was, if within `within`.
    std::optional<double> sinceClick(Ticks mediaTime, double within) const;

    friend bool operator==(const CursorEffects&, const CursorEffects&) = default;
};

inline constexpr double kClickRingSeconds = 0.45;

// Keeps only the samples needed to follow the path to within `tolerance` (picture fractions).
std::vector<CursorSample> thinCursorPath(const std::vector<CursorSample>& path, double tolerance = 0.002);

} // namespace tve
