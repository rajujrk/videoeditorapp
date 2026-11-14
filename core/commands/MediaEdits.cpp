#include "commands/MediaEdits.h"

#include "model/ImageSequence.h"
#include "model/Translate.h"

#include <algorithm>
#include <filesystem>
#include <set>

namespace tve {

namespace {

std::string trimmed(const std::string& text)
{
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos)
        return {};
    const auto last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

MediaBin* findBin(Project& project, Id binId)
{
    const auto it = std::ranges::find(project.bins, binId, &MediaBin::id);
    return it == project.bins.end() ? nullptr : &*it;
}

const MediaBin* findBin(const Project& project, Id binId)
{
    const auto it = std::ranges::find(project.bins, binId, &MediaBin::id);
    return it == project.bins.end() ? nullptr : &*it;
}

// `name`, or "name 2", "name 3"… so no other folder in `parent` has it.
std::string uniqueName(const Project& project, const std::string& name, Id parent, Id except)
{
    const auto taken = [&](const std::string& candidate) {
        return std::ranges::any_of(project.bins, [&](const MediaBin& b) {
            return b.parent == parent && b.id != except && b.name == candidate;
        });
    };
    if (!taken(name))
        return name;
    for (int n = 2;; ++n)
        if (const std::string candidate = name + " " + std::to_string(n); !taken(candidate))
            return candidate;
}

template <typename F>
void eachClip(const Project& project, F&& f)
{
    for (const Track& track : project.tracks)
        for (const Clip& clip : track.clips)
            f(track, clip);
    for (const Sequence& sequence : project.sequences)
        for (const Track& track : sequence.tracks)
            for (const Clip& clip : track.clips)
                f(track, clip);
}

template <typename F>
void eachClip(Project& project, F&& f)
{
    for (Track& track : project.tracks)
        for (Clip& clip : track.clips)
            f(track, clip);
    for (Sequence& sequence : project.sequences)
        for (Track& track : sequence.tracks)
            for (Clip& clip : track.clips)
                f(track, clip);
}

std::set<Id> usedMedia(const Project& project)
{
    std::set<Id> used;
    eachClip(project, [&](const Track&, const Clip& clip) {
        if (clip.mediaId != kNoId)
            used.insert(clip.mediaId);
    });
    return used;
}

} // namespace

// --- Folders ---

std::optional<std::string> addBin(Project& project, const std::string& name, Id parent, Id* madeId)
{
    const std::string clean = trimmed(name);
    if (clean.empty())
        return TVE_TR("core", "Give the folder a name");
    if (parent != kNoId && !findBin(project, parent))
        return TVE_TR("core", "That folder is not in the project");
    MediaBin bin{.id = project.allocateId(), .name = uniqueName(project, clean, parent, kNoId), .parent = parent};
    project.bins.push_back(bin);
    if (madeId)
        *madeId = bin.id;
    return std::nullopt;
}

std::optional<std::string> renameBin(Project& project, Id binId, const std::string& name)
{
    MediaBin* bin = findBin(project, binId);
    if (!bin)
        return TVE_TR("core", "That folder is not in the project");
    const std::string clean = trimmed(name);
    if (clean.empty())
        return TVE_TR("core", "Give the folder a name");
    bin->name = uniqueName(project, clean, bin->parent, binId);
    return std::nullopt;
}

std::optional<std::string> removeBin(Project& project, Id binId)
{
    const MediaBin* bin = findBin(project, binId);
    if (!bin)
        return TVE_TR("core", "That folder is not in the project");
    const Id parent = bin->parent;
    for (MediaItem& media : project.media)
        if (media.binId == binId)
            media.binId = parent;
    for (MediaBin& other : project.bins)
        if (other.parent == binId)
            other.parent = parent;
    // Names now side by side in the parent stay apart.
    std::erase_if(project.bins, [&](const MediaBin& b) { return b.id == binId; });
    for (MediaBin& other : project.bins)
        if (other.parent == parent)
            other.name = uniqueName(project, other.name, parent, other.id);
    return std::nullopt;
}

bool binWithin(const Project& project, Id binId, Id ancestor)
{
    for (int guard = 0; binId != kNoId && guard < 10000; ++guard) {
        if (binId == ancestor)
            return true;
        const MediaBin* bin = findBin(project, binId);
        if (!bin)
            return false;
        binId = bin->parent;
    }
    return false;
}

std::optional<std::string> moveToBin(Project& project, const std::vector<Id>& mediaIds, const std::vector<Id>& binIds,
                                     Id binId)
{
    if (binId != kNoId && !findBin(project, binId))
        return TVE_TR("core", "That folder is not in the project");
    if (mediaIds.empty() && binIds.empty())
        return TVE_TR("core", "Choose something to move");
    for (Id id : mediaIds)
        if (!project.findMedia(id))
            return TVE_TR("core", "That media is not in the project");
    for (Id id : binIds) {
        if (!findBin(project, id))
            return TVE_TR("core", "That folder is not in the project");
        if (binId != kNoId && binWithin(project, binId, id))
            return TVE_TR("core", "A folder can't go inside itself");
    }
    for (MediaItem& media : project.media)
        if (std::ranges::find(mediaIds, media.id) != mediaIds.end())
            media.binId = binId;
    for (Id id : binIds) {
        MediaBin* bin = findBin(project, id);
        bin->parent = binId;
        bin->name = uniqueName(project, bin->name, binId, id);
    }
    return std::nullopt;
}

std::vector<std::string> binPath(const Project& project, Id binId)
{
    std::vector<std::string> names;
    for (int guard = 0; binId != kNoId && guard < 10000; ++guard) {
        const MediaBin* bin = findBin(project, binId);
        if (!bin)
            break;
        names.push_back(bin->name);
        binId = bin->parent;
    }
    std::ranges::reverse(names);
    return names;
}

// --- Labels ---

std::optional<std::string> setMediaLabel(Project& project, const std::vector<Id>& mediaIds, MediaLabel label)
{
    if (mediaIds.empty())
        return TVE_TR("core", "Choose media to label");
    for (Id id : mediaIds)
        if (!project.findMedia(id))
            return TVE_TR("core", "That media is not in the project");
    for (MediaItem& media : project.media)
        if (std::ranges::find(mediaIds, media.id) != mediaIds.end())
            media.label = label;
    return std::nullopt;
}

// --- Unused media ---

std::vector<Id> unusedMedia(const Project& project)
{
    const std::set<Id> used = usedMedia(project);
    std::vector<Id> unused;
    for (const MediaItem& media : project.media)
        if (!used.contains(media.id))
            unused.push_back(media.id);
    return unused;
}

int removeUnusedMedia(Project& project)
{
    const std::vector<Id> unused = unusedMedia(project);
    std::erase_if(project.media, [&](const MediaItem& m) { return std::ranges::find(unused, m.id) != unused.end(); });
    return int(unused.size());
}

// --- Sound channels ---

std::optional<std::string> setClipChannels(Project& project, const std::vector<Id>& clipIds, AudioChannels channels)
{
    if (clipIds.empty())
        return TVE_TR("core", "Select a clip");
    for (Id id : clipIds) {
        const auto location = project.locateClip(id);
        if (!location)
            return TVE_TR("core", "Select a clip");
        if (location->track->locked)
            return TVE_TR("core", "The clip's track is locked");
        const Clip& clip = *location->clip;
        const MediaItem* media = project.findMedia(clip.mediaId);
        const bool sound = clip.compound() || (media && media->hasAudio);
        if (!sound || location->track->kind == TrackKind::Title)
            return TVE_TR("core", "Select a clip with sound");
    }
    for (Id id : clipIds)
        project.locateClip(id)->clip->channels = channels;
    return std::nullopt;
}

// --- Archiving ---

std::vector<ArchiveFile> archiveFiles(const Project& project, bool usedOnly)
{
    std::vector<ArchiveFile> files;
    std::set<std::string> seen;
    const auto add = [&](const std::string& path, const std::string& folder = {}) {
        if (!path.empty() && seen.insert(path).second)
            files.push_back({path, folder});
    };
    const std::set<Id> used = usedMedia(project);
    for (const MediaItem& media : project.media) {
        if (usedOnly && !used.contains(media.id))
            continue;
        if (media.sequence) {
            // Every frame, together in a folder named after the sequence.
            std::string folder = sequenceDisplayName(*media.sequence);
            std::ranges::replace(folder, '#', 'N');
            std::ranges::replace(folder, '/', '-');
            for (const std::string& frame : sequenceFrameFiles(media.path, *media.sequence))
                add(frame, folder);
        } else {
            add(media.path);
        }
    }
    eachClip(project, [&](const Track&, const Clip& clip) {
        if (clip.enhancedAudio)
            add(clip.enhancedAudio->path);
        if (clip.processedVideo)
            add(clip.processedVideo->path);
        add(clip.color.lutPath);
        if (clip.look)
            add(clip.look->grade.lutPath);
        if (clip.text && clip.text->fill.kind == TextFillKind::Image)
            add(clip.text->fill.imagePath);
    });
    return files;
}

Project relocated(Project project, const std::map<std::string, std::string>& moved, bool usedOnly)
{
    if (usedOnly)
        removeUnusedMedia(project);
    const auto move = [&](std::string& path) {
        if (const auto it = moved.find(path); it != moved.end())
            path = it->second;
    };
    for (MediaItem& media : project.media)
        move(media.path);
    eachClip(project, [&](Track&, Clip& clip) {
        if (clip.enhancedAudio)
            move(clip.enhancedAudio->path);
        if (clip.processedVideo)
            move(clip.processedVideo->path);
        move(clip.color.lutPath);
        if (clip.look)
            move(clip.look->grade.lutPath);
        if (clip.text)
            move(clip.text->fill.imagePath);
    });
    return project;
}

} // namespace tve
