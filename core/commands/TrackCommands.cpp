#include "commands/TrackCommands.h"

#include <algorithm>
#include <map>

namespace tve {

namespace {

// Puts back the project as it was, keeping ids handed out since (so none are reused).
void restoreKeepingIds(Project& project, const Project& before)
{
    const Id next = project.nextId();
    project = before;
    project.setNextId(std::max(next, before.nextId()));
}

const char* kindName(TrackKind kind)
{
    switch (kind) {
    case TrackKind::Video: return "Video";
    case TrackKind::Audio: return "Audio";
    case TrackKind::Title: return "Title";
    }
    return "Track";
}

// "Video 3": one more than the tracks of its kind.
std::string newTrackName(const Project& project, TrackKind kind)
{
    const auto count = std::ranges::count(project.tracks, kind, &Track::kind);
    return std::string(kindName(kind)) + " " + std::to_string(count + 1);
}

} // namespace

// --- InsertTrackCommand ---

std::optional<std::string> InsertTrackCommand::redo(Project& project)
{
    if (m_name.size() > kMaxTrackName)
        return "the track's name is too long";
    if (m_trackId == kNoId)
        m_trackId = project.allocateId();
    const std::size_t at = std::min(m_index, project.tracks.size());
    project.tracks.insert(project.tracks.begin() + std::ptrdiff_t(at), Track{.id = m_trackId, .kind = m_kind, .name = m_name});
    return std::nullopt;
}

void InsertTrackCommand::undo(Project& project)
{
    std::erase_if(project.tracks, [&](const Track& track) { return track.id == m_trackId; });
}

// --- RemoveTrackCommand ---

std::optional<std::string> RemoveTrackCommand::redo(Project& project)
{
    const auto found = std::ranges::find(project.tracks, m_trackId, &Track::id);
    if (found == project.tracks.end())
        return "track not found";
    if (found->locked)
        return "track is locked";
    if (found->kind == TrackKind::Video && std::ranges::count(project.tracks, TrackKind::Video, &Track::kind) == 1)
        return "the timeline keeps one video track";
    m_index = std::size_t(found - project.tracks.begin());
    m_removed = *found;
    project.tracks.erase(found);
    return std::nullopt;
}

void RemoveTrackCommand::undo(Project& project)
{
    project.tracks.insert(project.tracks.begin() + std::ptrdiff_t(std::min(m_index, project.tracks.size())), m_removed);
}

// --- RenameTrackCommand ---

std::optional<std::string> RenameTrackCommand::redo(Project& project)
{
    Track* track = project.findTrack(m_trackId);
    if (!track)
        return "track not found";
    const auto first = m_name.find_first_not_of(" \t");
    if (first == std::string::npos)
        return "a track needs a name";
    if (m_name.size() > kMaxTrackName)
        return "the track's name is too long";
    m_previous = track->name;
    track->name = m_name.substr(first, m_name.find_last_not_of(" \t") - first + 1);
    return std::nullopt;
}

void RenameTrackCommand::undo(Project& project)
{
    if (Track* track = project.findTrack(m_trackId))
        track->name = m_previous;
}

// --- Clipboard ---

std::vector<CopiedClip> copyClips(const Project& project, const std::vector<Id>& clipIds)
{
    struct Found {
        const Clip* clip;
        const Track* track;
        std::size_t trackIndex;
    };
    std::vector<Found> found;
    for (Id id : clipIds) {
        for (std::size_t t = 0; t < project.tracks.size(); ++t)
            if (const Clip* clip = project.tracks[t].findClip(id))
                found.push_back({clip, &project.tracks[t], t});
    }
    if (found.empty())
        return {};
    const Ticks earliest = std::ranges::min(found, {}, [](const Found& f) { return f.clip->start; }).clip->start;
    // Lanes: each kind's tracks counted from the lowest one a copied clip is on.
    std::map<TrackKind, std::size_t> lowest;
    for (const Found& f : found) {
        auto [it, added] = lowest.emplace(f.track->kind, f.trackIndex);
        if (!added)
            it->second = std::min(it->second, f.trackIndex);
    }
    std::vector<CopiedClip> copies;
    for (const Found& f : found) {
        int lane = 0;
        for (std::size_t t = lowest[f.track->kind]; t < f.trackIndex; ++t)
            lane += project.tracks[t].kind == f.track->kind ? 1 : 0;
        Clip clip = *f.clip;
        clip.start -= earliest;
        copies.push_back({std::move(clip), f.track->kind, lane});
    }
    std::ranges::stable_sort(copies, {}, [](const CopiedClip& c) { return c.clip.start; });
    return copies;
}

std::optional<std::string> PasteClipsCommand::redo(Project& project)
{
    if (m_clips.empty())
        return "there is nothing to paste";
    if (m_at < 0)
        return "paste at a time on the timeline";
    for (const CopiedClip& copied : m_clips) {
        const Clip& clip = copied.clip;
        if (clip.mediaId != kNoId && !project.findMedia(clip.mediaId))
            return "a copied clip's media is not in this project";
        if (clip.compound() && !project.findSequence(clip.sequenceId))
            return "a copied compound clip is not in this project";
    }
    m_before = project;
    m_pasted.clear();

    const Track* target = project.findTrack(m_targetTrack);
    // The tracks of a kind, bottom first, from the base track a lane counts from.
    const auto tracksFrom = [&](TrackKind kind) {
        std::vector<Id> ids;
        bool started = !(target && target->kind == kind);
        for (const Track& track : project.tracks) {
            if (target && track.id == target->id)
                started = true;
            if (started && track.kind == kind)
                ids.push_back(track.id);
        }
        return ids;
    };
    std::map<Id, Id> newGroups;
    for (const CopiedClip& copied : m_clips) {
        Clip clip = copied.clip;
        clip.id = project.allocateId();
        clip.start += m_at;
        if (clip.groupId != kNoId) {
            auto [it, added] = newGroups.emplace(clip.groupId, kNoId);
            if (added)
                it->second = project.allocateId();
            clip.groupId = it->second;
        }
        const std::vector<Id> lanes = tracksFrom(copied.kind);
        Track* placed = nullptr;
        for (std::size_t l = std::size_t(std::max(copied.lane, 0)); l < lanes.size() && !placed; ++l) {
            Track* track = project.findTrack(lanes[l]);
            if (!track->locked && !track->overlaps(clip.start, clip.duration))
                placed = track;
        }
        if (!placed) {
            project.tracks.push_back(Track{.id = project.allocateId(), .kind = copied.kind, .name = newTrackName(project, copied.kind)});
            placed = &project.tracks.back();
            target = project.findTrack(m_targetTrack); // (the vector may have moved)
        }
        placed->insertClip(clip);
        m_pasted.push_back(clip.id);
    }
    return std::nullopt;
}

void PasteClipsCommand::undo(Project& project)
{
    restoreKeepingIds(project, m_before);
}

// --- Media ---

std::size_t clipsUsingMedia(const Project& project, Id mediaId)
{
    std::size_t count = 0;
    const auto countIn = [&](const std::vector<Track>& tracks) {
        for (const Track& track : tracks)
            count += std::size_t(std::ranges::count(track.clips, mediaId, &Clip::mediaId));
    };
    countIn(project.tracks);
    for (const Sequence& sequence : project.sequences)
        countIn(sequence.tracks);
    return count;
}

std::optional<std::string> RemoveMediaCommand::redo(Project& project)
{
    if (!project.findMedia(m_mediaId))
        return "media not found";
    const auto locked = [&](const Track& track) {
        return track.locked && std::ranges::any_of(track.clips, [&](const Clip& c) { return c.mediaId == m_mediaId; });
    };
    if (std::ranges::any_of(project.tracks, locked))
        return "a locked track uses it";
    m_before = project;
    m_clipsRemoved = clipsUsingMedia(project, m_mediaId);
    const auto removeFrom = [&](std::vector<Track>& tracks) {
        for (Track& track : tracks)
            std::erase_if(track.clips, [&](const Clip& c) { return c.mediaId == m_mediaId; });
    };
    removeFrom(project.tracks);
    for (Sequence& sequence : project.sequences)
        removeFrom(sequence.tracks);
    std::erase_if(project.media, [&](const MediaItem& m) { return m.id == m_mediaId; });
    return std::nullopt;
}

void RemoveMediaCommand::undo(Project& project)
{
    restoreKeepingIds(project, m_before);
}

} // namespace tve
