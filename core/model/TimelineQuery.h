#pragma once

#include "model/Project.h"

#include <optional>
#include <vector>

namespace tve {

// The video clip visible at a timeline time, and where in its media that time falls.
struct VisibleClip {
    const Track* track = nullptr;
    const Clip* clip = nullptr;
    Ticks sourceTime = 0;
};

// Topmost non-hidden video track with a clip covering `time` wins. (Compositing several
// layers arrives with transforms; until then upper tracks fully cover lower ones.)
std::optional<VisibleClip> visibleVideoAt(const Project& project, Ticks time);

// Every visible video clip covering `time`, bottom track first: the layers to composite.
std::vector<VisibleClip> visibleVideoLayersAt(const Project& project, Ticks time);

// Start of the next video clip on a visible track strictly after `time`, if any.
std::optional<Ticks> nextVideoClipStart(const Project& project, Ticks time);

// Every clip edge on every track plus zero, sorted and unique — the points edits snap to.
std::vector<Ticks> editPoints(const Project& project);

} // namespace tve
