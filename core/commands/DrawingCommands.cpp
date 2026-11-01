#include "commands/DrawingCommands.h"

#include <algorithm>
#include <cmath>

namespace tve {

std::optional<std::string> validateDrawing(const ClipDrawing& drawing)
{
    if (drawing.paths.size() > kMaxDrawnPaths)
        return "a drawing holds at most " + std::to_string(kMaxDrawnPaths) + " paths";
    if (drawing.drawIn < 0 || drawing.drawOut < 0 || drawing.drawIn > 60 || drawing.drawOut > 60)
        return "drawing on and off take 0 to 60 seconds";
    for (const DrawnPath& path : drawing.paths) {
        if (path.points.size() > kMaxPathPoints)
            return "a path holds at most " + std::to_string(kMaxPathPoints) + " points";
        if (path.points.size() < 2)
            return "a path needs at least two points";
        if (path.width <= 0 || path.width > 0.2)
            return "a path's width is out of range";
        for (const auto& [x, y] : path.points)
            if (!std::isfinite(x) || !std::isfinite(y) || x < -1 || x > 2 || y < -1 || y > 2)
                return "a path's points are out of range";
    }
    return std::nullopt;
}

std::optional<std::string> AddDrawingCommand::redo(Project& project)
{
    if (auto error = validateDrawing(m_drawing))
        return error;
    if (m_start < 0 || m_duration <= 0)
        return "clip times are out of range";
    Track* track = nullptr;
    if (m_trackId != kNoId) {
        track = project.findTrack(m_trackId);
        if (!track)
            return "track not found";
        if (track->locked)
            return "track is locked";
        if (track->kind == TrackKind::Audio)
            return "drawings go on video tracks";
        if (track->overlaps(m_start, m_duration))
            return "clip overlaps an existing clip";
    } else {
        if (m_newTrackId == kNoId)
            m_newTrackId = project.allocateId();
        const auto videoTracks = std::ranges::count(project.tracks, TrackKind::Video, &Track::kind);
        project.tracks.push_back(
            Track{.id = m_newTrackId, .kind = TrackKind::Video, .name = "Video " + std::to_string(videoTracks + 1)});
        track = &project.tracks.back();
    }
    if (m_clipId == kNoId)
        m_clipId = project.allocateId();
    Clip clip{.id = m_clipId, .start = m_start, .duration = m_duration};
    clip.audioEnabled = false;
    clip.drawing = m_drawing;
    track->insertClip(std::move(clip));
    return std::nullopt;
}

void AddDrawingCommand::undo(Project& project)
{
    if (m_trackId == kNoId) {
        std::erase_if(project.tracks, [&](const Track& t) { return t.id == m_newTrackId; });
        return;
    }
    if (Track* track = project.findTrack(m_trackId))
        track->removeClip(m_clipId);
}

std::optional<std::string> SetClipDrawingCommand::redo(Project& project)
{
    auto location = project.locateClip(m_clipId);
    if (!location)
        return "clip not found";
    if (location->track->locked)
        return "track is locked";
    if (!location->clip->drawing)
        return "not a drawing";
    if (auto error = validateDrawing(m_drawing))
        return error;
    m_before = location->clip->drawing;
    location->clip->drawing = m_drawing;
    return std::nullopt;
}

void SetClipDrawingCommand::undo(Project& project)
{
    if (auto location = project.locateClip(m_clipId); location && m_before)
        location->clip->drawing = m_before;
}

std::optional<std::string> AddOverlayClipCommand::redo(Project& project)
{
    if (m_item.path.empty() || !m_item.hasVideo)
        return "the overlay has no picture";
    if (m_start < 0 || m_duration <= 0 || m_duration > m_item.duration)
        return "clip times are out of range";
    Track* track = nullptr;
    if (m_trackId != kNoId) {
        track = project.findTrack(m_trackId);
        if (!track)
            return "track not found";
        if (track->locked)
            return "track is locked";
        if (track->kind == TrackKind::Audio)
            return "overlays go on video tracks";
        if (track->overlaps(m_start, m_duration))
            return "clip overlaps an existing clip";
    }
    const auto known = std::ranges::find(project.media, m_item.path, &MediaItem::path);
    m_importedMedia = known == project.media.end();
    if (!m_importedMedia)
        m_item.id = known->id;
    else if (m_item.id == kNoId)
        m_item.id = project.allocateId();
    if (m_clipId == kNoId)
        m_clipId = project.allocateId();
    if (!track) {
        if (m_newTrackId == kNoId)
            m_newTrackId = project.allocateId();
        const auto existing = std::ranges::count_if(project.tracks, [&](const Track& t) { return t.name.starts_with(m_trackName); });
        std::string name = existing == 0 ? m_trackName : m_trackName + " " + std::to_string(existing + 1);
        project.tracks.push_back(Track{.id = m_newTrackId, .kind = TrackKind::Video, .name = std::move(name)});
        track = &project.tracks.back();
    }
    if (m_importedMedia)
        project.media.push_back(m_item);
    Clip clip{.id = m_clipId, .mediaId = m_item.id, .start = m_start, .duration = m_duration};
    clip.audioEnabled = false;
    clip.visual = m_visual;
    track->insertClip(std::move(clip));
    return std::nullopt;
}

void AddOverlayClipCommand::undo(Project& project)
{
    if (m_trackId == kNoId)
        std::erase_if(project.tracks, [&](const Track& t) { return t.id == m_newTrackId; });
    else if (Track* track = project.findTrack(m_trackId))
        track->removeClip(m_clipId);
    if (m_importedMedia)
        std::erase_if(project.media, [&](const MediaItem& item) { return item.id == m_item.id; });
}

} // namespace tve
