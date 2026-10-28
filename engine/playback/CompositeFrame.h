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
    ColorGrade look; // the clip's look at its intensity, before `grade`
    std::string stillPath; // a still image: read from here, with no frame
    ChromaKey chromaKey;
    std::vector<ClipMask> masks;
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
    ColorGrade look;              // before `grade`
    // An adjustment layer: no picture of its own; its look, grade and effects apply to the
    // canvas below it, blended by its opacity.
    bool adjustment = false;
    std::string stillPath; // a still image: read from here, with no frame
    // After colour, before effects: the key, then the masks.
    ChromaKey chromaKey;
    std::vector<ClipMask> masks;
    // Uses the layer just above as its matte (that layer is then not drawn).
    MatteMode matte = MatteMode::None;
    // A title: how far into its clip this frame is, and the clip's length, in seconds (its
    // text animations and scrolling depend on them).
    double titleSeconds = 0.0;
    double titleDuration = 0.0;
};

// Everything visible at one timeline time, bottom layer first. No layers means black.
struct CompositeFrame {
    Ticks time = 0;
    std::vector<VideoLayer> layers;

    Ticks pts() const { return time; }
};

using FrameQueue = BasicFrameQueue<CompositeFrame>;

} // namespace tve
