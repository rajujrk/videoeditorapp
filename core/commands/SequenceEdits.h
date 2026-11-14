#pragma once

#include "model/MusicFit.h"
#include "model/Project.h"

#include <optional>
#include <string>
#include <vector>

// Edits on a copy of the project (the app applies the result as one undo step): opening a
// compound clip to edit its insides, multicam clips, a project brought in as a compound
// clip, and media placed from the source monitor. Each returns nullopt on success, or why not
// (the project is then unchanged).
namespace tve {

// --- A compound clip opened on the timeline ---

// While a compound clip is open its sequence's tracks are the timeline's (so every edit works
// on them), and the tracks it came from wait in a holder sequence of their own (so media
// removed, or a sequence still in use, is seen there too). Opening inside an open one nests.
struct OpenedSequence {
    Id sequenceId = kNoId; // the sequence being edited
    Id clipId = kNoId;     // the compound clip it was opened from
    Id holderId = kNoId;   // the sequence holding the tracks it came from
    std::vector<Marker> markers; // the timeline's markers (they belong to it)
    friend bool operator==(const OpenedSequence&, const OpenedSequence&) = default;
};

// The holder's name (never shown; skipped where sequences are counted or listed).
inline constexpr const char* kHolderSequenceName = "\x01opened";

std::optional<std::string> openSequence(Project& project, Id compoundClipId, OpenedSequence* opened);
// The edited tracks back in their sequence, the timeline as it was; compound clips playing the
// sequence fitted to its new length (shortened where it got shorter; one that played it to
// the end grows with it, up to the next clip on its track).
void closeSequence(Project& project, const OpenedSequence& opened);
// The project with every open sequence closed (the innermost first): what is saved.
Project withSequencesClosed(Project project, const std::vector<OpenedSequence>& open);

// --- Multicam ---

// Makes a multicam clip of clips as they are lined up on the timeline (by sound, say): each
// picture clip becomes an angle (a sequence of its own, in the order given), each sound-only
// clip plays under every angle. The multicam clip takes their place on the lowest picture
// track among them, from the earliest start to the latest end, showing the first angle.
std::optional<std::string> makeMulticam(Project& project, const std::vector<Id>& clipIds, Id* madeClipId);
// Shows angle `angle` (0-based) in the whole clip.
std::optional<std::string> setAngle(Project& project, Id clipId, int angle);
// Cuts the multicam clip at `at` and shows `angle` from there to the clip's end (at its
// start: the whole clip). `rightClipId`: the part that now shows it.
std::optional<std::string> cutToAngle(Project& project, Id clipId, Ticks at, int angle, Id* rightClipId);

// --- A project as a compound clip ---

// Brings `other`'s media (reusing what this project has of the same files), sequences and
// tracks in, every id renumbered; its tracks become a new sequence named `name`, played by a
// compound clip as long as it put at `at` on a picture track with room there (a new one on
// top if none has). Its markers and template stay behind.
std::optional<std::string> importProjectAsCompound(Project& project, const Project& other, const std::string& name,
                                                   Ticks at, Id* madeClipId);

// --- From the source monitor ---

enum class PlaceMode { Insert, Overwrite, Append };

// Media `mediaId` from `sourceIn` for `duration` onto a track: inserted at `at` (a clip there
// is split; everything from there on the track moves later), over what is there from `at`
// (cut out), or after the track's last clip.
std::optional<std::string> placeMedia(Project& project, Id trackId, Id mediaId, Ticks sourceIn, Ticks duration,
                                      Ticks at, PlaceMode mode, Id* madeClipId);

// --- Made to fit (AI transitions, music fitted to a length) ---

// Room for `length` at `at`: every clip starting at or after it, on every track, and the
// markers from there, moved that much later (clips running across it stay). A locked track
// with clips to move refuses.
std::optional<std::string> insertTime(Project& project, Ticks at, Ticks length);

// A clip of music replaced by a clip per piece of the fit, one after another from where it
// starts, grouped, played at the fit's speed with the pitch kept, a short fade at each join
// and the end faded when the fit trimmed it; its keyframes are dropped (they were timed for
// the whole). Refused for a clip not at normal speed, or when the music would run into the
// next clip on its track. `made`: the new clips, in order.
std::optional<std::string> fitClipToPieces(Project& project, Id clipId, const MusicFit& fit, std::vector<Id>* made);

} // namespace tve
