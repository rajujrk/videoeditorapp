#include "commands/EditCommands.h"

#include <algorithm>
#include <cassert>
#include <iterator>

namespace tve {

namespace {

std::optional<std::string> checkEditable(const Track* track)
{
    if (!track)
        return "track not found";
    if (track->locked)
        return "track is locked";
    return std::nullopt;
}

// Shifts every clip on the track that starts at or after `from` by `delta`.
void shiftClipsFrom(Track& track, Ticks from, Ticks delta)
{
    for (Clip& clip : track.clips) {
        if (clip.start >= from)
            clip.start += delta;
    }
}

} // namespace

// --- AddTrackCommand ---

std::optional<std::string> AddTrackCommand::redo(Project& project)
{
    if (m_trackId == kNoId)
        m_trackId = project.allocateId();
    project.tracks.push_back(Track{.id = m_trackId, .kind = m_kind, .name = m_name});
    return std::nullopt;
}

void AddTrackCommand::undo(Project& project)
{
    std::erase_if(project.tracks, [&](const Track& track) { return track.id == m_trackId; });
}

// --- ImportMediaCommand ---

std::optional<std::string> ImportMediaCommand::redo(Project& project)
{
    if (m_path.empty())
        return "media path is empty";
    if (m_duration <= 0)
        return "media duration must be positive";
    if (m_mediaId == kNoId)
        m_mediaId = project.allocateId();
    project.media.push_back(MediaItem{.id = m_mediaId, .path = m_path, .duration = m_duration});
    return std::nullopt;
}

void ImportMediaCommand::undo(Project& project)
{
    std::erase_if(project.media, [&](const MediaItem& item) { return item.id == m_mediaId; });
}

// --- AddClipCommand ---

std::optional<std::string> AddClipCommand::redo(Project& project)
{
    Track* track = project.findTrack(m_trackId);
    if (auto error = checkEditable(track))
        return error;
    const MediaItem* media = project.findMedia(m_mediaId);
    if (!media)
        return "media not found";
    if (m_start < 0 || m_duration <= 0 || m_sourceIn < 0)
        return "clip times are out of range";
    if (m_sourceIn + m_duration > media->duration)
        return "clip extends past the end of its media";
    if (track->overlaps(m_start, m_duration))
        return "clip overlaps an existing clip";

    if (m_clipId == kNoId)
        m_clipId = project.allocateId();
    track->insertClip(Clip{.id = m_clipId, .mediaId = m_mediaId, .start = m_start, .duration = m_duration, .sourceIn = m_sourceIn});
    return std::nullopt;
}

void AddClipCommand::undo(Project& project)
{
    Track* track = project.findTrack(m_trackId);
    assert(track);
    track->removeClip(m_clipId);
}

// --- MoveClipCommand ---

std::optional<std::string> MoveClipCommand::redo(Project& project)
{
    auto location = project.locateClip(m_clipId);
    if (!location)
        return "clip not found";
    Track* from = location->track;
    Track* to = project.findTrack(m_toTrackId);
    if (auto error = checkEditable(from))
        return error;
    if (auto error = checkEditable(to))
        return error;
    if (from->kind != to->kind)
        return "clips can only move between tracks of the same kind";
    if (m_newStart < 0)
        return "clip cannot start before zero";
    if (to->overlaps(m_newStart, location->clip->duration, m_clipId))
        return "clip overlaps an existing clip";

    m_fromTrackId = from->id;
    m_oldStart = location->clip->start;
    Clip clip = *from->removeClip(m_clipId);
    clip.start = m_newStart;
    to->insertClip(std::move(clip));
    return std::nullopt;
}

void MoveClipCommand::undo(Project& project)
{
    Track* to = project.findTrack(m_toTrackId);
    Track* from = project.findTrack(m_fromTrackId);
    assert(to && from);
    Clip clip = *to->removeClip(m_clipId);
    clip.start = m_oldStart;
    from->insertClip(std::move(clip));
}

// --- SplitClipCommand ---

std::optional<std::string> SplitClipCommand::redo(Project& project)
{
    auto location = project.locateClip(m_clipId);
    if (!location)
        return "clip not found";
    if (auto error = checkEditable(location->track))
        return error;
    Clip& clip = *location->clip;
    if (m_at <= clip.start || m_at >= clip.end())
        return "split point must be inside the clip";

    m_trackId = location->track->id;
    m_original = clip;
    if (m_rightClipId == kNoId)
        m_rightClipId = project.allocateId();

    const Ticks leftDuration = m_at - clip.start;
    Clip right{
        .id = m_rightClipId,
        .mediaId = clip.mediaId,
        .start = m_at,
        .duration = clip.duration - leftDuration,
        .sourceIn = clip.sourceIn + leftDuration,
    };
    clip.duration = leftDuration;
    location->track->insertClip(std::move(right));
    return std::nullopt;
}

void SplitClipCommand::undo(Project& project)
{
    Track* track = project.findTrack(m_trackId);
    assert(track);
    track->removeClip(m_rightClipId);
    *track->findClip(m_clipId) = m_original;
}

// --- DeleteClipCommand ---

std::optional<std::string> DeleteClipCommand::redo(Project& project)
{
    auto location = project.locateClip(m_clipId);
    if (!location)
        return "clip not found";
    if (auto error = checkEditable(location->track))
        return error;

    m_trackId = location->track->id;
    m_removed = *location->track->removeClip(m_clipId);
    if (m_ripple)
        shiftClipsFrom(*location->track, m_removed.end(), -m_removed.duration);
    return std::nullopt;
}

void DeleteClipCommand::undo(Project& project)
{
    Track* track = project.findTrack(m_trackId);
    assert(track);
    if (m_ripple)
        shiftClipsFrom(*track, m_removed.start, m_removed.duration);
    track->insertClip(m_removed);
}

} // namespace tve
