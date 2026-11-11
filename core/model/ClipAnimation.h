#pragma once

#include "model/Project.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Ready-made movement for a video or picture clip, written as keyframes on its position, size,
// turn and opacity (so it can be changed afterwards like any animation): how it arrives (in),
// how it leaves (out), both or a movement through it (combo); and pan and zoom (Ken Burns)
// from one framing to another over the whole clip.
namespace tve {

enum class ClipAnimationKind { In, Out, Combo };

struct ClipAnimationPreset {
    const char* id;
    const char* title; // (marked for translation)
    ClipAnimationKind kind;
};

const std::vector<ClipAnimationPreset>& clipAnimationPresets();

// The picture properties the presets animate.
const std::vector<std::string_view>& clipAnimationProperties();

// Writes preset `id` on the clip: an in or out over `length` at its start or end (a combo's
// in and out each take `length`; a movement through it takes the whole clip). It starts from,
// or ends at, the clip as it is there; keyframes of the same properties inside the span go.
std::optional<std::string> applyClipAnimation(Clip& clip, std::string_view id, Ticks length);
// Takes off the keyframes the presets write (position, size, turn, opacity, everywhere).
void clearClipAnimation(Clip& clip);

// Pan and zoom: the picture framed `from` at the start and `to` at the end (scale, and
// position in canvas sizes), moving evenly between them over the whole clip.
struct PanZoom {
    double fromScale = 1.0, fromX = 0.0, fromY = 0.0;
    double toScale = 1.25, toX = 0.0, toY = 0.0;
};
struct PanZoomPreset {
    const char* id;
    const char* title;
    PanZoom framing;
};
const std::vector<PanZoomPreset>& panZoomPresets();
std::optional<std::string> applyPanZoom(Clip& clip, const PanZoom& framing);

} // namespace tve
