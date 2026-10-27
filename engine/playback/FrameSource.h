#pragma once

#include "playback/FrameQueue.h"
#include "playback/PlaybackClock.h"

#include <atomic>
#include <cstdint>
#include <string>

namespace tve {

struct PlaybackCounters {
    std::atomic<std::int64_t> decoded{0};   // frames produced by the decoder
    std::atomic<std::int64_t> presented{0}; // distinct frames shown on screen
    std::atomic<std::int64_t> skipped{0};   // frames that became due but were never shown
};

// What the renderer pulls frames from: a queue of frames stamped with presentation times
// and the clock that says which one is due. A frame without a pixel buffer means "show
// black" (a gap in the timeline).
class FrameSource {
public:
    virtual ~FrameSource() = default;

    virtual FrameQueue& queue() = 0;
    virtual PlaybackClock& clock() = 0;
    virtual PlaybackCounters& counters() = 0;
    virtual bool hardwareAccelerated() const = 0;

    // A free-running source starts its clock when its first frame arrives; a controlled
    // source (the timeline) is started and stopped by the UI.
    virtual bool startsOnFirstFrame() const { return false; }
};

} // namespace tve
