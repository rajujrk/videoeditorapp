#pragma once

#include "commands/Command.h"
#include "model/Project.h"

#include <optional>
#include <string>
#include <vector>

namespace tve {

// --- Tracks ---

inline constexpr std::size_t kMaxTrackName = 64;

// A new, empty track at `index` in Project::tracks (bottom first; clamped to the ends).
class InsertTrackCommand final : public Command {
public:
    InsertTrackCommand(TrackKind kind, std::string name, std::size_t index)
        : m_kind(kind), m_name(std::move(name)), m_index(index)
    {
    }

    std::string label() const override { return TVE_TR("core", "Add Track"); }
    std::optional<std::string> redo(Project& project) override;
    void undo(Project& project) override;

    Id trackId() const { return m_trackId; }

private:
    TrackKind m_kind;
    std::string m_name;
    std::size_t m_index;
    Id m_trackId = kNoId;
};

// Takes a track and its clips off the timeline. The last video track stays (it is the main
// track every edit falls back on).
class RemoveTrackCommand final : public Command {
public:
    explicit RemoveTrackCommand(Id trackId) : m_trackId(trackId) {}

    std::string label() const override { return TVE_TR("core", "Remove Track"); }
    std::optional<std::string> redo(Project& project) override;
    void undo(Project& project) override;

private:
    Id m_trackId;
    std::size_t m_index = 0;
    Track m_removed;
};

class RenameTrackCommand final : public Command {
public:
    RenameTrackCommand(Id trackId, std::string name) : m_trackId(trackId), m_name(std::move(name)) {}

    std::string label() const override { return TVE_TR("core", "Rename Track"); }
    std::optional<std::string> redo(Project& project) override;
    void undo(Project& project) override;

private:
    Id m_trackId;
    std::string m_name;
    std::string m_previous;
};

// --- Clipboard ---

// A copied clip: its start counted from the earliest copied clip's, and its track as the
// kind and the lane (how many tracks of that kind above the lowest copied one).
struct CopiedClip {
    Clip clip;
    TrackKind kind = TrackKind::Video;
    int lane = 0;

    friend bool operator==(const CopiedClip&, const CopiedClip&) = default;
};

// The selected clips (or the one), ready for the clipboard; empty if none are found.
std::vector<CopiedClip> copyClips(const Project& project, const std::vector<Id>& clipIds);

// Pastes copies of clips from `at`: each on the track its lane names counted up from
// `targetTrack` (when of its kind; otherwise from the lowest track of its kind), moving up
// past tracks where it would overlap a clip, onto a new track of its kind if none has room.
// The copies get new ids, and copied groups new groups.
class PasteClipsCommand final : public Command {
public:
    PasteClipsCommand(std::vector<CopiedClip> clips, Ticks at, Id targetTrack, std::string label)
        : m_clips(std::move(clips)), m_at(at), m_targetTrack(targetTrack), m_label(std::move(label))
    {
    }

    std::string label() const override { return m_label; }
    std::optional<std::string> redo(Project& project) override;
    void undo(Project& project) override;

    // The pasted clips' ids, in the order given.
    const std::vector<Id>& pastedIds() const { return m_pasted; }

private:
    std::vector<CopiedClip> m_clips;
    Ticks m_at;
    Id m_targetTrack;
    std::string m_label;
    Project m_before;
    std::vector<Id> m_pasted;
};

// --- Media ---

// Takes a media item out of the project, with every clip that plays it (in compound
// clips' sequences too).
class RemoveMediaCommand final : public Command {
public:
    explicit RemoveMediaCommand(Id mediaId) : m_mediaId(mediaId) {}

    std::string label() const override { return TVE_TR("core", "Remove Media"); }
    std::optional<std::string> redo(Project& project) override;
    void undo(Project& project) override;

    // Clips the removal took off the timeline (known after redo).
    std::size_t clipsRemoved() const { return m_clipsRemoved; }

private:
    Id m_mediaId;
    Project m_before;
    std::size_t m_clipsRemoved = 0;
};

// How many clips play a media item (in compound clips' sequences too).
std::size_t clipsUsingMedia(const Project& project, Id mediaId);

} // namespace tve
