#pragma once

#include "model/Project.h"

#include <optional>
#include <vector>

namespace tve {

// An active transition between two touching clips on a track: it runs over
// [start, end) around `cut`, where `from` ends and `to` begins.
struct TransitionWindow {
    const Clip* from = nullptr;
    const Clip* to = nullptr;
    const ClipTransition* spec = nullptr;
    Ticks cut = 0;
    Ticks start = 0;
    Ticks end = 0;

    // 0 at the start of the window, 1 at its end.
    double progressAt(Ticks time) const;
};

// The transition into `clip`, if it has one and a clip on `track` ends where it starts.
// Its length is the requested duration placed by its alignment, with the part on each
// side of the cut limited to half that clip, so neighbouring transitions never overlap.
std::optional<TransitionWindow> transitionInto(const Track& track, const Clip& clip);
// The transition out of `clip` into the clip that starts where it ends, if any.
std::optional<TransitionWindow> transitionOutOf(const Track& track, const Clip& clip);

// Media time of `clip` at a timeline time, continuing past its edges into the handles and
// held at the first or last frame where the media runs out.
Ticks clampedSourceTime(const Project& project, const Clip& clip, Ticks time);

// The second picture of a layer that is in a transition.
struct TransitionLayer {
    const ClipTransition* spec = nullptr;
    const Clip* to = nullptr;
    Ticks toSourceTime = 0;
    double progress = 0.0;
};

// The video clip visible at a timeline time, and where in its media that time falls.
// During a transition `clip` is the outgoing clip and `transition` names the incoming one.
struct VisibleClip {
    const Track* track = nullptr;
    const Clip* clip = nullptr;
    Ticks sourceTime = 0;
    std::optional<TransitionLayer> transition;
};

// Video and title tracks are picture tracks; titles are clips with text instead of media.
// Topmost non-hidden picture track with a clip covering `time` wins. (Compositing several
// layers arrives with transforms; until then upper tracks fully cover lower ones.)
std::optional<VisibleClip> visibleVideoAt(const Project& project, Ticks time);

// Every visible video clip covering `time`, bottom track first: the layers to composite.
std::vector<VisibleClip> visibleVideoLayersAt(const Project& project, Ticks time);

// Where a clip is heard: its own span, widened into its handles by the transitions on
// either side of it.
struct AudibleSpan {
    Ticks start = 0;
    Ticks end = 0;
};
AudibleSpan audibleSpan(const Track& track, const Clip& clip);

// Crossfade gain for `clip` at `time` from its transitions (constant power): 1 outside
// them, falling to 0 across a transition out and rising from 0 across one in.
float transitionGainAt(const Track& track, const Clip& clip, Ticks time);

// Start of the next video clip on a visible track strictly after `time`, if any.
std::optional<Ticks> nextVideoClipStart(const Project& project, Ticks time);

// Every clip edge on every track plus zero, sorted and unique — the points edits snap to.
std::vector<Ticks> editPoints(const Project& project);

} // namespace tve
