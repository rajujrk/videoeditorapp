#pragma once

#include "playback/CompositeFrame.h"
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

// What the renderer pulls frames from: a queue of composite frames stamped with
// presentation times, the clock that says which one is due, and the canvas they are
// composed on.
class FrameSource {
public:
    virtual ~FrameSource() = default;

    virtual FrameQueue& queue() = 0;
    virtual PlaybackClock& clock() = 0;
    virtual PlaybackCounters& counters() = 0;
    virtual bool hardwareAccelerated() const = 0;

    struct Size {
        int width = 0;
        int height = 0;
    };
    virtual Size canvasSize() const = 0;

    // A free-running source starts its clock when its first frame arrives; a controlled
    // source (the timeline) is started and stopped by the UI.
    virtual bool startsOnFirstFrame() const { return false; }
};

} // namespace tve
