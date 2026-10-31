#pragma once

#include "analysis/MediaAnalysis.h"
#include "model/Motion.h"

#include <optional>
#include <string>
#include <vector>

namespace tve {

// Follows what is inside `box` (picture fractions: centre and size, in the frame shown at
// `from`) through a file until `to` or until it is lost, with Apple's Vision: an object (it
// may change shape), or a flat surface — a rectangle found inside the box (a screen, a sign,
// a page), which also gives its turn. The first sample is at `from`.
std::optional<std::vector<TrackSample>> trackRegion(const std::string& path, Ticks from, Ticks to, const TrackSample& box,
                                                    bool surface, const AnalysisProgress& progress = {},
                                                    std::string* error = nullptr);

} // namespace tve
