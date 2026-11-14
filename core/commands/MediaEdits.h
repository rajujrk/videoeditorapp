#pragma once

#include "model/Project.h"

#include <map>
#include <optional>
#include <string>
#include <vector>

// The media library kept in order (slice 31b): folders, colour labels, media nobody uses, a
// clip's sound channels, and gathering a project's files into an archive. Edits work on a copy
// of the project (the app applies the result as one undo step) and return nullopt on success,
// or why not.
namespace tve {

// --- Folders ---

// A folder named `name` inside `parent` (kNoId: at the top). Names are trimmed and must not be
// empty; the same name in one place is made unique ("Shots 2").
std::optional<std::string> addBin(Project& project, const std::string& name, Id parent, Id* madeId);
std::optional<std::string> renameBin(Project& project, Id binId, const std::string& name);
// A folder taken away: what was in it (media and folders) moves to its parent.
std::optional<std::string> removeBin(Project& project, Id binId);
// Media, and folders, moved into a folder (kNoId: the top). A folder can't go inside itself.
std::optional<std::string> moveToBin(Project& project, const std::vector<Id>& mediaIds, const std::vector<Id>& binIds,
                                     Id binId);
// The folder's names from the top down to it ("Shoot", "Day 2"); empty for the top.
std::vector<std::string> binPath(const Project& project, Id binId);
// Whether `binId` is `ancestor` or inside it.
bool binWithin(const Project& project, Id binId, Id ancestor);

// --- Labels ---

std::optional<std::string> setMediaLabel(Project& project, const std::vector<Id>& mediaIds, MediaLabel label);

// --- Unused media ---

// Media no clip plays, on the timeline or inside a compound clip, and no template slot
// waits for; in the library's order.
std::vector<Id> unusedMedia(const Project& project);
// Those taken out of the project; how many.
int removeUnusedMedia(Project& project);

// --- Sound channels ---

// The clips' channels (clips with sound only: on a sound track, or a video's own sound).
std::optional<std::string> setClipChannels(Project& project, const std::vector<Id>& clipIds, AudioChannels channels);

// --- Archiving ---

// A file to copy: its path, and the folder of its own it goes in (an image sequence's frames
// stay together, in a folder named after it; empty for the rest).
struct ArchiveFile {
    std::string path;
    std::string folder;
    friend bool operator==(const ArchiveFile&, const ArchiveFile&) = default;
};
// Every file the project needs (media, every frame of an image sequence, cleaned sound and
// processed copies), each once; with `usedOnly`, media nobody uses is left out.
std::vector<ArchiveFile> archiveFiles(const Project& project, bool usedOnly);
// The project with its files' paths changed as `moved` says (old path → new path); with
// `usedOnly`, unused media taken out too. Paths `moved` doesn't name stay.
Project relocated(Project project, const std::map<std::string, std::string>& moved, bool usedOnly);

} // namespace tve
