#pragma once

#include "analysis/MediaAnalysis.h"
#include "model/Captions.h"

#include <string>
#include <vector>

namespace tve {

// Highlights: a recording's moments worth keeping, found from what was measured of it (12a)
// and, when there is one, its transcript. The file is cut into half-second cells, each
// scored from its loudness, how much new sound starts in it, how much its picture moves and
// how much is said in it (each against the rest of the file); moments are runs of cells cut
// at natural points — scene changes, pauses and the ends of sentences — kept between a
// minimum and a maximum length, and scored by their cells.

struct Moment {
    Ticks start = 0; // media time
    Ticks end = 0;
    double score = 0;  // 0..1
    std::string why;   // what made it: "loud", "lively", "talk" or "calm"
    std::string text;  // what is said in it, if anything

    Ticks length() const { return end - start; }
    friend bool operator==(const Moment&, const Moment&) = default;
};

struct HighlightOptions {
    double minLength = 2.0; // seconds
    double maxLength = 6.0;
};

// Every moment of a recording (none overlapping), best first. Either kind of features may be
// missing (a silent video, a sound file); `words` are in media time.
std::vector<Moment> findMoments(const AudioFeatures* audio, const VideoFeatures* video, const std::vector<TimedWord>& words,
                                Ticks duration, const HighlightOptions& options = {});

// The best moments adding up to about `total` (never past it by more than one moment's
// shortest length), in time order.
std::vector<Moment> pickMoments(const std::vector<Moment>& moments, Ticks total);

} // namespace tve
