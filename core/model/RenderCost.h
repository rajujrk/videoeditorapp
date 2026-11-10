#pragma once

#include "model/Project.h"

#include <functional>
#include <vector>

namespace tve {

// How hard each stretch of the timeline is to play in real time: its load, the share of a
// frame's time its pictures take to decode and draw, 1 being all of it. Measured on an Apple
// M5 (tve_perf_tests "[stress]"): a 4K30 layer, decoded and placed, takes about 0.07 (twelve
// play in real time); a blur pass over a 4K picture 0.11; most effects' passes far less.
// Effects run at the layer's own size, so a 1080p layer's cost a quarter of a 4K one's.
inline constexpr double kRealTimeLoad = 1.0;

struct PlaybackLoad {
    Ticks from = 0;
    Ticks to = 0;
    double load = 0.0;
};

// An effect's cost per 4K picture (the app knows each effect's passes; see effectLoad).
using EffectWeight = std::function<double(const ClipEffect&)>;
// Without knowing the effects: 0.02 each, 0.15 for a plug-in (they work on the CPU).
double defaultEffectWeight(const ClipEffect& effect);

// The timeline cut where what plays changes (clips, transitions, compound clips' insides),
// each stretch with its load. Proxies (media.proxyPath) count at their size.
std::vector<PlaybackLoad> playbackLoad(const Project& project, const EffectWeight& weight = defaultEffectWeight);

// The stretches whose load is over `limit`, neighbours joined.
std::vector<PlaybackLoad> heavyStretches(const Project& project, double limit = kRealTimeLoad,
                                         const EffectWeight& weight = defaultEffectWeight);

} // namespace tve
