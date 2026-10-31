#include "commands/StructureCommands.h"

#include "commands/EditCommands.h"

#include <algorithm>
#include <set>

namespace tve {

namespace {

void sortMarkers(Project& project)
{
    std::ranges::stable_sort(project.markers, {}, &Marker::time);
}

bool isPictureTrack(const Track& track)
{
    return track.kind == TrackKind::Video || track.kind == TrackKind::Title;
}

// Puts back the project as it was, keeping ids handed out since (so none are reused).
void restore(Project& project, const Project& before)
{
    const Id next = project.nextId();
    project = before;
    project.setNextId(std::max(next, before.nextId()));
}

// `clip` cut to the part between `from` and `to` (its own timeline), as trimming it would.
std::optional<Clip> trimmedTo(const Project& project, const Clip& clip, Ticks from, Ticks to)
{
    if (clip.start >= from && clip.end() <= to)
        return clip;
    Project scratch;
    scratch.media = project.media;
    scratch.sequences = project.sequences;
    Track track;
    track.id = 1;
    track.kind = TrackKind::Video;
    track.clips = {clip};
    scratch.tracks = {track};
    if (clip.start < from && TrimClipCommand(clip.id, TrimClipCommand::Edge::Start, from).redo(scratch))
        return std::nullopt;
    if (scratch.tracks[0].clips[0].end() > to && TrimClipCommand(clip.id, TrimClipCommand::Edge::End, to).redo(scratch))
        return std::nullopt;
    return scratch.tracks[0].clips[0];
}

} // namespace

// --- Markers ---

std::optional<std::string> validateMarker(const Marker& marker)
{
    if (marker.time < 0 || marker.duration < 0)
        return "a marker's time and length cannot be negative";
    if (marker.note.size() > kMaxMarkerNote)
        return "the marker's note is too long";
    return std::nullopt;
}

std::optional<std::string> AddMarkerCommand::redo(Project& project)
{
    if (auto error = validateMarker(m_marker))
        return error;
    if (m_marker.id == kNoId)
        m_marker.id = project.allocateId();
    project.markers.push_back(m_marker);
    sortMarkers(project);
    return std::nullopt;
}

void AddMarkerCommand::undo(Project& project)
{
    std::erase_if(project.markers, [&](const Marker& m) { return m.id == m_marker.id; });
}

std::optional<std::string> SetMarkerCommand::redo(Project& project)
{
    Marker* marker = project.findMarker(m_marker.id);
    if (!marker)
        return "marker not found";
    if (auto error = validateMarker(m_marker))
        return error;
    m_previous = *marker;
    *marker = m_marker;
    sortMarkers(project);
    return std::nullopt;
}

void SetMarkerCommand::undo(Project& project)
{
    *project.findMarker(m_marker.id) = m_previous;
    sortMarkers(project);
}

std::optional<std::string> RemoveMarkerCommand::redo(Project& project)
{
    Marker* marker = project.findMarker(m_markerId);
    if (!marker)
        return "marker not found";
    m_removed = *marker;
    std::erase_if(project.markers, [&](const Marker& m) { return m.id == m_markerId; });
    return std::nullopt;
}

void RemoveMarkerCommand::undo(Project& project)
{
    project.markers.push_back(m_removed);
    sortMarkers(project);
}

// --- Groups ---

std::vector<Id> groupMembers(const Project& project, Id groupId)
{
    std::vector<Id> members;
    if (groupId == kNoId)
        return members;
    for (const Track& track : project.tracks) {
        for (const Clip& clip : track.clips) {
            if (clip.groupId == groupId)
                members.push_back(clip.id);
        }
    }
    return members;
}

std::optional<std::string> GroupClipsCommand::redo(Project& project)
{
    std::set<Id> unique(m_clipIds.begin(), m_clipIds.end());
    if (unique.size() < 2)
        return "select two or more clips to group";
    for (Id id : unique) {
        auto location = project.locateClip(id);
        if (!location)
            return "clip not found";
        if (location->track->locked)
            return "track is locked";
    }
    if (m_groupId == kNoId)
        m_groupId = project.allocateId();
    m_previous.clear();
    for (Id id : m_clipIds) {
        Clip& clip = *project.locateClip(id)->clip;
        m_previous.push_back(clip.groupId);
        clip.groupId = m_groupId;
    }
    return std::nullopt;
}

void GroupClipsCommand::undo(Project& project)
{
    for (std::size_t i = 0; i < m_clipIds.size(); ++i)
        project.locateClip(m_clipIds[i])->clip->groupId = m_previous[i];
}

std::optional<std::string> UngroupCommand::redo(Project& project)
{
    m_members = groupMembers(project, m_groupId);
    if (m_members.empty())
        return "group not found";
    for (Id id : m_members)
        project.locateClip(id)->clip->groupId = kNoId;
    return std::nullopt;
}

void UngroupCommand::undo(Project& project)
{
    for (Id id : m_members)
        project.locateClip(id)->clip->groupId = m_groupId;
}

// --- Compound clips ---

std::optional<std::string> CreateCompoundCommand::redo(Project& project)
{
    std::set<Id> chosen(m_clipIds.begin(), m_clipIds.end());
    if (chosen.empty())
        return "select clips to make a compound clip";
    // Which tracks hold them (in project order), and the span they cover.
    std::vector<std::size_t> sourceTracks;
    Ticks first = 0;
    Ticks last = 0;
    bool any = false;
    for (std::size_t t = 0; t < project.tracks.size(); ++t) {
        const Track& track = project.tracks[t];
        bool holds = false;
        for (const Clip& clip : track.clips) {
            if (!chosen.contains(clip.id))
                continue;
            if (track.locked)
                return "track is locked";
            holds = true;
            first = any ? std::min(first, clip.start) : clip.start;
            last = any ? std::max(last, clip.end()) : clip.end();
            any = true;
        }
        if (holds)
            sourceTracks.push_back(t);
    }
    std::size_t found = 0;
    for (const Track& track : project.tracks)
        found += std::size_t(std::ranges::count_if(track.clips, [&](const Clip& c) { return chosen.contains(c.id); }));
    if (!any || found != chosen.size())
        return "clip not found";

    // Where the compound clip goes: the lowest video track among them, else the lowest sound
    // track (all sound); title tracks hold titles only.
    std::optional<std::size_t> target;
    for (std::size_t t : sourceTracks) {
        if (project.tracks[t].kind == TrackKind::Video) {
            target = t;
            break;
        }
    }
    if (!target) {
        if (std::ranges::any_of(sourceTracks, [&](std::size_t t) { return project.tracks[t].kind == TrackKind::Title; }))
            return "a compound clip needs a video clip among them";
        target = sourceTracks.front();
    }

    m_before = project;
    if (m_sequenceId == kNoId) {
        m_sequenceId = project.allocateId();
        m_compoundId = project.allocateId();
    }
    project.setNextId(std::max(project.nextId(), std::max(m_sequenceId, m_compoundId) + 1));
    Sequence sequence{m_sequenceId, m_name, {}};
    for (std::size_t t : sourceTracks) {
        Track& source = project.tracks[t];
        Track inner;
        inner.id = project.allocateId();
        inner.kind = source.kind;
        inner.name = source.name;
        inner.gainDb = source.gainDb;
        inner.pan = source.pan;
        for (const Clip& clip : source.clips) {
            if (!chosen.contains(clip.id))
                continue;
            Clip moved = clip;
            moved.start -= first;
            moved.groupId = kNoId;
            inner.clips.push_back(moved);
        }
        std::erase_if(source.clips, [&](const Clip& c) { return chosen.contains(c.id); });
        sequence.tracks.push_back(std::move(inner));
    }
    Track& destination = project.tracks[*target];
    if (destination.overlaps(first, last - first)) {
        restore(project, m_before);
        return "the compound clip needs the space its clips covered on " + destination.name;
    }
    project.sequences.push_back(std::move(sequence));
    Clip compound;
    compound.id = m_compoundId;
    compound.start = first;
    compound.duration = last - first;
    compound.sequenceId = m_sequenceId;
    compound.audioEnabled = true;
    destination.insertClip(compound);
    return std::nullopt;
}

void CreateCompoundCommand::undo(Project& project)
{
    restore(project, m_before);
}

std::optional<std::string> BreakCompoundCommand::redo(Project& project)
{
    auto location = project.locateClip(m_clipId);
    if (!location)
        return "clip not found";
    if (location->track->locked)
        return "track is locked";
    const Clip compound = *location->clip;
    const Sequence* found = compound.compound() ? project.findSequence(compound.sequenceId) : nullptr;
    if (!found)
        return "not a compound clip";
    const Sequence sequence = *found;
    m_before = project;

    const Id homeTrack = location->track->id;
    location->track->removeClip(m_clipId);
    // The part of the sequence the compound clip plays, and where that lands on the timeline.
    const Ticks from = compound.sourceIn;
    const Ticks to = compound.sourceIn + compound.duration;
    const Ticks offset = compound.start - compound.sourceIn;

    // Tracks to put each sequence track on: picture tracks from the compound's own track up,
    // sound tracks in order; new ones on top when there are not enough.
    const auto home = *project.trackIndex(homeTrack);
    const bool homeIsPicture = isPictureTrack(project.tracks[home]);
    std::vector<Id> pictureTargets;
    std::vector<Id> soundTargets;
    for (std::size_t t = 0; t < project.tracks.size(); ++t) {
        const Track& track = project.tracks[t];
        if (track.kind == TrackKind::Video && (!homeIsPicture || t >= home) && !track.locked)
            pictureTargets.push_back(track.id);
        if (track.kind == TrackKind::Audio && !track.locked)
            soundTargets.push_back(track.id);
    }
    if (homeIsPicture)
        std::ranges::stable_partition(pictureTargets, [&](Id id) { return id == homeTrack; });
    else
        std::ranges::stable_partition(soundTargets, [&](Id id) { return id == homeTrack; });
    std::size_t pictures = 0;
    std::size_t sounds = 0;
    for (const Track& inner : sequence.tracks) {
        const bool picture = isPictureTrack(inner);
        std::vector<Id>& targets = picture ? pictureTargets : soundTargets;
        std::size_t& used = picture ? pictures : sounds;
        if (used == targets.size()) {
            Track added;
            added.id = project.allocateId();
            added.kind = inner.kind == TrackKind::Audio ? TrackKind::Audio : TrackKind::Video;
            added.name = inner.name;
            project.tracks.push_back(added);
            targets.push_back(added.id);
        }
        Track& target = *project.findTrack(targets[used++]);
        for (const Clip& clip : inner.clips) {
            if (clip.end() <= from || clip.start >= to)
                continue; // outside the part it plays
            auto kept = trimmedTo(project, clip, from, to);
            if (!kept) {
                restore(project, m_before);
                return "a clip inside could not be cut to the compound clip's length";
            }
            kept->start += offset;
            if (target.overlaps(kept->start, kept->duration)) {
                restore(project, m_before);
                return "there is no room on " + target.name + " for the clips inside";
            }
            target.insertClip(*kept);
        }
    }
    // The sequence goes once no compound clip plays it any more (split halves share it).
    const bool stillUsed = std::ranges::any_of(project.tracks, [&](const Track& t) {
        return std::ranges::any_of(t.clips, [&](const Clip& c) { return c.sequenceId == compound.sequenceId; });
    });
    if (!stillUsed)
        std::erase_if(project.sequences, [&](const Sequence& s) { return s.id == compound.sequenceId; });
    return std::nullopt;
}

void BreakCompoundCommand::undo(Project& project)
{
    restore(project, m_before);
}

// --- Time ranges ---

std::optional<std::string> RemoveRangesCommand::redo(Project& project)
{
    if (m_after) {
        // Again after an undo: the same result, ids and all.
        const Id next = std::max(project.nextId(), m_after->nextId());
        m_before = project;
        project = *m_after;
        project.setNextId(next);
        return std::nullopt;
    }
    const Track* found = project.findTrack(m_trackId);
    if (!found)
        return "track not found";
    if (found->locked)
        return "track is locked";
    // In order, merged where they touch or overlap.
    std::vector<std::pair<Ticks, Ticks>> ranges;
    for (auto [from, to] : m_ranges)
        if (to > from && to > 0)
            ranges.emplace_back(std::max<Ticks>(from, 0), to);
    std::ranges::sort(ranges);
    std::vector<std::pair<Ticks, Ticks>> merged;
    for (const auto& range : ranges) {
        if (!merged.empty() && range.first <= merged.back().second)
            merged.back().second = std::max(merged.back().second, range.second);
        else
            merged.push_back(range);
    }
    if (merged.empty())
        return "no time to remove";

    m_before = project;
    m_removed = 0;
    // From the last range back, so earlier ranges keep their times.
    for (auto it = merged.rbegin(); it != merged.rend(); ++it) {
        const auto [from, to] = *it;
        for (const Ticks edge : {from, to}) {
            const Track& track = *project.findTrack(m_trackId);
            const auto crossing = std::ranges::find_if(track.clips, [&](const Clip& c) { return c.start < edge && edge < c.end(); });
            if (crossing != track.clips.end()) {
                if (auto error = SplitClipCommand(crossing->id, edge).redo(project)) {
                    restore(project, m_before);
                    return error;
                }
            }
        }
        Track& track = *project.findTrack(m_trackId);
        const auto before = track.clips.size();
        std::erase_if(track.clips, [&](const Clip& c) { return c.start >= from && c.end() <= to; });
        const bool removedClips = track.clips.size() != before;
        const Ticks end = track.clips.empty() ? 0 : track.clips.back().end();
        if (!removedClips && from >= end)
            continue; // past the track's end: nothing to close
        for (Clip& clip : track.clips)
            if (clip.start >= to)
                clip.start -= to - from;
        m_removed += to - from;
    }
    if (m_removed == 0) {
        restore(project, m_before);
        return "no time to remove";
    }
    m_after = project;
    return std::nullopt;
}

void RemoveRangesCommand::undo(Project& project)
{
    restore(project, m_before);
}

} // namespace tve
