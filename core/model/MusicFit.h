#pragma once

#include "model/Time.h"

#include <utility>
#include <vector>

namespace tve {

// Music fitted to a length, the way an editor would: whole bars taken out of the middle (or
// a run of bars played again) where the music sounds alike either side of the join, the
// start and the end kept; a last small difference made up by playing it a touch faster or
// slower (the pitch kept), or else the end trimmed and faded — a plan running over is
// preferred to one ending short, so the music lands on the length.

struct MusicFitInput {
    Ticks in = 0;               // the part of the media used (media time)
    Ticks out = 0;
    Ticks target = 0;           // the length wanted
    std::vector<Ticks> beats;   // media time; may be empty (no steady beat)
    int beatsPerBar = 4;
    // Loudness every `hop` seconds of the media (dBFS), to find joins that sound alike; may
    // be empty.
    std::vector<float> loudness;
    double hop = 0.01;
};

struct MusicFit {
    std::vector<std::pair<Ticks, Ticks>> pieces; // media ranges, played in order
    double speed = 1.0;  // all pieces played this much faster (> 1) or slower
    Ticks fadeOut = 0;   // the end faded (trimmed to fit)
    // How many bars went (< 0) or came again (> 0), and the length it now plays for.
    int barsChanged = 0;
    Ticks length = 0;
};

// Played at most this much faster or slower to land on the length exactly.
constexpr double kMostFitStretch = 0.04;

// The plan. Without beats the music is cut in two-second blocks. Too short a target for
// that (under four bars or blocks, or no join within 15 % of it): the start, trimmed and
// faded. A target within the stretch of the source: just the speed.
MusicFit planMusicFit(const MusicFitInput& input);

} // namespace tve
