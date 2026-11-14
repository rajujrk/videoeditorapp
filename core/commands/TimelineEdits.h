#pragma once

#include "model/Project.h"

#include <optional>
#include <string>
#include <vector>

// Everyday timeline handling (slice 31a), as edits on a copy of the project (the app applies
// the result as one undo step) and questions about it. Each edit returns nullopt on success,
// or why not (the project is then unchanged or to be thrown away).
namespace tve {

// Other media in a clip's place: its start, length, speed, look, colour, effects, keyframes
// and transition kept, from the same point of the media when it is long enough there (else
// from as late as fits). A shorter piece of media shortens the clip (`shortened` says so).
// What was measured on the old media (steadying, cut-outs, cleaned sound…) is let go.
std::optional<std::string> replaceClipMedia(Project& project, Id clipId, Id mediaId, bool* shortened = nullptr);

// Clips turned off (neither seen nor heard; they keep their place) or on again.
std::optional<std::string> setClipsEnabled(Project& project, const std::vector<Id>& clipIds, bool enabled);

// The clip's edge nearer to `to` moved there: a roll when a clip touches that edge (both
// edges move, the cut slides), else a trim. Refused past either clip's media, into another
// clip, or emptying one.
std::optional<std::string> extendEdit(Project& project, Id clipId, Ticks to);
// The clip with an edge nearest `at` on an unlocked track (kNoId when there are none).
Id nearestEdit(const Project& project, Ticks at);

// Clips from `at` on (direction > 0: those still running at or after it) or up to it (< 0:
// those that began before it), on one track or on every unlocked one (trackId kNoId).
std::vector<Id> clipsFrom(const Project& project, Ticks at, int direction, Id trackId = kNoId);

// The next (direction > 0) or previous cut or clip edge on a shown track, and the end.
std::optional<Ticks> nextEdit(const Project& project, Ticks at, int direction);

// A track moved one place up (direction > 0, drawn higher) or down among the tracks of its
// kind group (pictures and titles together, sound apart). Refused at the top or bottom.
std::optional<std::string> moveTrack(Project& project, Id trackId, int direction);
// Every track locked or unlocked; how many changed.
int setAllTracksLocked(Project& project, bool locked);

// Every transition between clips (on the timeline and inside compound clips), and every
// marker, taken off; how many.
int removeAllTransitions(Project& project);
int removeAllMarkers(Project& project);

// Turned by `degrees` more (a right angle each time, say), its rotation keyframes with it;
// mirrored left to right (horizontal) or upside down.
std::optional<std::string> rotateClip(Project& project, Id clipId, double degrees);
std::optional<std::string> flipClip(Project& project, Id clipId, bool horizontal);

} // namespace tve
