#pragma once

#include "decode/DecodedFrame.h"
#include "model/Project.h"
#include "playback/FrameQueue.h"

#include <vector>

namespace tve {

// One clip's picture at a moment of the timeline, with how to show it.
struct VideoLayer {
    Id clipId = kNoId;
    DecodedFrame frame;
    ClipVisual visual;
    std::vector<ClipEffect> effects;
};

// Everything visible at one timeline time, bottom layer first. No layers means black.
struct CompositeFrame {
    Ticks time = 0;
    std::vector<VideoLayer> layers;

    Ticks pts() const { return time; }
};

using FrameQueue = BasicFrameQueue<CompositeFrame>;

} // namespace tve
