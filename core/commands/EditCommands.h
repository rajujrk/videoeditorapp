#pragma once

#include "commands/Command.h"
#include "model/Project.h"

#include <cstddef>

namespace tve {

class AddTrackCommand final : public Command {
public:
    AddTrackCommand(TrackKind kind, std::string name) : m_kind(kind), m_name(std::move(name)) {}

    std::string label() const override { return "Add Track"; }
    std::optional<std::string> redo(Project& project) override;
    void undo(Project& project) override;

    Id trackId() const { return m_trackId; }

private:
    TrackKind m_kind;
    std::string m_name;
    Id m_trackId = kNoId;
};

class ImportMediaCommand final : public Command {
public:
    ImportMediaCommand(std::string path, Ticks duration) : m_path(std::move(path)), m_duration(duration) {}

    std::string label() const override { return "Import Media"; }
    std::optional<std::string> redo(Project& project) override;
    void undo(Project& project) override;

    Id mediaId() const { return m_mediaId; }

private:
    std::string m_path;
    Ticks m_duration;
    Id m_mediaId = kNoId;
};

class AddClipCommand final : public Command {
public:
    AddClipCommand(Id trackId, Id mediaId, Ticks start, Ticks duration, Ticks sourceIn = 0)
        : m_trackId(trackId), m_mediaId(mediaId), m_start(start), m_duration(duration), m_sourceIn(sourceIn) {}

    std::string label() const override { return "Add Clip"; }
    std::optional<std::string> redo(Project& project) override;
    void undo(Project& project) override;

    Id clipId() const { return m_clipId; }

private:
    Id m_trackId;
    Id m_mediaId;
    Ticks m_start;
    Ticks m_duration;
    Ticks m_sourceIn;
    Id m_clipId = kNoId;
};

class MoveClipCommand final : public Command {
public:
    MoveClipCommand(Id clipId, Id toTrackId, Ticks newStart) : m_clipId(clipId), m_toTrackId(toTrackId), m_newStart(newStart) {}

    std::string label() const override { return "Move Clip"; }
    std::optional<std::string> redo(Project& project) override;
    void undo(Project& project) override;

private:
    Id m_clipId;
    Id m_toTrackId;
    Ticks m_newStart;
    Id m_fromTrackId = kNoId;
    Ticks m_oldStart = 0;
};

// Splits a clip in two at a timeline position strictly inside it. The left part keeps
// the original ID; the right part gets a new one.
class SplitClipCommand final : public Command {
public:
    SplitClipCommand(Id clipId, Ticks at) : m_clipId(clipId), m_at(at) {}

    std::string label() const override { return "Split Clip"; }
    std::optional<std::string> redo(Project& project) override;
    void undo(Project& project) override;

    Id rightClipId() const { return m_rightClipId; }

private:
    Id m_clipId;
    Ticks m_at;
    Id m_trackId = kNoId;
    Clip m_original;
    Id m_rightClipId = kNoId;
};

// Removes a clip. With ripple, later clips on the same track move left to close the gap.
class DeleteClipCommand final : public Command {
public:
    DeleteClipCommand(Id clipId, bool ripple) : m_clipId(clipId), m_ripple(ripple) {}

    std::string label() const override { return m_ripple ? "Ripple Delete" : "Delete Clip"; }
    std::optional<std::string> redo(Project& project) override;
    void undo(Project& project) override;

private:
    Id m_clipId;
    bool m_ripple;
    Id m_trackId = kNoId;
    Clip m_removed;
};

} // namespace tve
