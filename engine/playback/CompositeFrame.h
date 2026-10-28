#pragma once

#include "decode/DecodedFrame.h"
#include "model/Project.h"
#include "playback/FrameQueue.h"

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace tve {

// The incoming clip of a layer that is in a transition, and how far the transition is.
struct LayerTransition {
    std::string transitionId;
    std::map<std::string, std::vector<float>> values;
    float progress = 0.0f; // 0 = all outgoing clip, 1 = all incoming clip
    Id clipId = kNoId;
    DecodedFrame frame;
    ClipVisual visual;
    std::vector<ClipEffect> effects;
    ColorGrade grade;
};

// One clip's picture at a moment of the timeline, with how to show it. During a
// transition this is the outgoing clip and `transition` holds the incoming one. A title
// has `text` and an empty frame.
struct VideoLayer {
    Id clipId = kNoId;
    DecodedFrame frame;
    ClipVisual visual;
    std::vector<ClipEffect> effects;
    std::optional<LayerTransition> transition;
    std::optional<ClipText> text; // a title: drawn from this, with no frame
    ColorGrade grade;             // before the effects
};

// Everything visible at one timeline time, bottom layer first. No layers means black.
struct CompositeFrame {
    Ticks time = 0;
    std::vector<VideoLayer> layers;

    Ticks pts() const { return time; }
};

using FrameQueue = BasicFrameQueue<CompositeFrame>;

} // namespace tve
