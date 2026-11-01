#pragma once

#include "commands/Command.h"
#include "model/Project.h"

#include <optional>
#include <string>

namespace tve {

// Why a drawing is not one (too many paths or points, a path too short for its kind, a width
// or timing out of range), or nullopt.
std::optional<std::string> validateDrawing(const ClipDrawing& drawing);

// Adds a drawing clip to a video track; with trackId kNoId, on a new video track on top
// (undone together).
class AddDrawingCommand final : public Command {
public:
    AddDrawingCommand(Id trackId, Ticks start, Ticks duration, ClipDrawing drawing)
        : m_trackId(trackId), m_start(start), m_duration(duration), m_drawing(std::move(drawing)) {}

    std::string label() const override { return "Add Drawing"; }
    std::optional<std::string> redo(Project& project) override;
    void undo(Project& project) override;

    Id clipId() const { return m_clipId; }

private:
    Id m_trackId;
    Ticks m_start;
    Ticks m_duration;
    ClipDrawing m_drawing;
    Id m_clipId = kNoId;
    Id m_newTrackId = kNoId;
};

// Replaces a drawing clip's drawing (its paths and how it comes and goes).
class SetClipDrawingCommand final : public Command {
public:
    SetClipDrawingCommand(Id clipId, ClipDrawing drawing, std::string label)
        : m_clipId(clipId), m_drawing(std::move(drawing)), m_label(std::move(label)) {}

    std::string label() const override { return m_label; }
    std::optional<std::string> redo(Project& project) override;
    void undo(Project& project) override;

private:
    Id m_clipId;
    ClipDrawing m_drawing;
    std::string m_label;
    std::optional<ClipDrawing> m_before;
};

// Lays a clip of `item` (imported, unless the project has its path already) over others on a
// video track, with its own look and no sound; with trackId kNoId, on a new video track
// named `trackName` on top (undone together). For overlay transitions.
class AddOverlayClipCommand final : public Command {
public:
    AddOverlayClipCommand(MediaItem item, Id trackId, Ticks start, Ticks duration, ClipVisual visual, std::string trackName,
                          std::string label)
        : m_item(std::move(item)), m_trackId(trackId), m_start(start), m_duration(duration), m_visual(visual),
          m_trackName(std::move(trackName)), m_label(std::move(label)) {}

    std::string label() const override { return m_label; }
    std::optional<std::string> redo(Project& project) override;
    void undo(Project& project) override;

    Id clipId() const { return m_clipId; }
    Id trackId() const { return m_trackId != kNoId ? m_trackId : m_newTrackId; }

private:
    MediaItem m_item;
    Id m_trackId;
    Ticks m_start;
    Ticks m_duration;
    ClipVisual m_visual;
    std::string m_trackName;
    std::string m_label;
    Id m_clipId = kNoId;
    Id m_newTrackId = kNoId;
    bool m_importedMedia = false;
};

} // namespace tve
