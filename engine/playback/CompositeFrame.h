#pragma once

#include "decode/DecodedFrame.h"
#include "model/Project.h"
#include "playback/FrameQueue.h"

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace tve {

// The incoming clip of a layer that is in a transition, and how far the transition is.
struct CompositeFrame;

// What a clip does with the people in its picture at one frame: its portrait cutout, and
// the faces to cover there (with the cover).
struct LayerPeople {
    PortraitCutout cutout;
    std::vector<FaceBox> faces;
    FaceCover cover = FaceCover::Mosaic;
    float amount = 0.5f;

    bool any() const { return cutout.enabled || !faces.empty(); }
};

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
    std::string stillPath; // a still image or sticker: read from here, with no frame
    ChromaKey chromaKey;
    std::vector<ClipMask> masks;
    double clipSeconds = 0.0; // how far into the incoming clip
    LayerPeople people;
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
    std::string stillPath; // a still image or sticker (Lottie): read from here, with no frame
    // After colour, before effects: the key, then the masks.
    ChromaKey chromaKey;
    std::vector<ClipMask> masks;
    // Uses the layer just above as its matte (that layer is then not drawn).
    MatteMode matte = MatteMode::None;
    // How far into its clip this frame is, and the clip's length, in seconds (a title's text
    // animations and scrolling, a sticker's frame, depend on them).
    double clipSeconds = 0.0;
    double clipDuration = 0.0;
    // A compound clip: its sequence's layers at this moment, drawn as its picture.
    std::shared_ptr<CompositeFrame> nested;
    // After the masks, before the effects.
    LayerPeople people;
};

// Everything visible at one timeline time, bottom layer first. No layers means black.
struct CompositeFrame {
    Ticks time = 0;
    std::vector<VideoLayer> layers;

    Ticks pts() const { return time; }
};

using FrameQueue = BasicFrameQueue<CompositeFrame>;

} // namespace tve
