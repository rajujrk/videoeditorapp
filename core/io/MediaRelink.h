#pragma once

#include "model/Project.h"

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace tve {

// A media file's content fingerprint: "v1:<size>:<hash>", the hash (64-bit FNV-1a) taken
// over the first and last 64 KB. Cheap to compute on large files, and the same for a copy,
// a moved file or a renamed one; nullopt if the file cannot be read.
std::optional<std::string> fingerprintFile(const std::string& path);

// Whether `path` could be the file a media item was imported from: its fingerprint matches
// (or the item has none).
bool sameMedia(const MediaItem& item, const std::string& path);

// The media whose files are not where the project says (resources built into the app, ":/…",
// never are).
std::vector<const MediaItem*> missingMedia(const Project& project);

struct RelinkSearch {
    std::size_t maxFiles = 50'000; // files looked at before giving up
};

// Looks through `folder` (and the folders in it) for the missing media's files: first by file
// name (ignoring case), then — for media with a fingerprint — by content, which finds renamed
// copies. A name match whose content differs is not taken. Returns media id → found path.
std::map<Id, std::string> findMissingMedia(const std::vector<const MediaItem*>& missing, const std::string& folder,
                                           const RelinkSearch& limits = {});

} // namespace tve
