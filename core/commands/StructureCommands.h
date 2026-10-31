#pragma once

#include "commands/Command.h"
#include "model/Captions.h"
#include "model/Project.h"

#include <optional>
#include <string>
#include <vector>

namespace tve {

// --- Markers ---

// Nullopt if fine: a time on the timeline, a non-negative length, a note of reasonable size.
std::optional<std::string> validateMarker(const Marker& marker);

class AddMarkerCommand final : public Command {
public:
    explicit AddMarkerCommand(Marker marker) : m_marker(std::move(marker)) {}

    std::string label() const override { return "Add Marker"; }
    std::optional<std::string> redo(Project& project) override;
    void undo(Project& project) override;

    Id markerId() const { return m_marker.id; }

private:
    Marker m_marker;
};

// Changes a marker (its id stays).
class SetMarkerCommand final : public Command {
public:
    explicit SetMarkerCommand(Marker marker) : m_marker(std::move(marker)) {}

    std::string label() const override { return "Change Marker"; }
    std::optional<std::string> redo(Project& project) override;
    void undo(Project& project) override;

private:
    Marker m_marker;
    Marker m_previous;
};

class RemoveMarkerCommand final : public Command {
public:
    explicit RemoveMarkerCommand(Id markerId) : m_markerId(markerId) {}

    std::string label() const override { return "Remove Marker"; }
    std::optional<std::string> redo(Project& project) override;
    void undo(Project& project) override;

private:
    Id m_markerId;
    Marker m_removed;
};

// --- Groups ---

// Puts clips (two or more, on unlocked tracks) in a new group, leaving any group they were in.
class GroupClipsCommand final : public Command {
public:
    explicit GroupClipsCommand(std::vector<Id> clipIds) : m_clipIds(std::move(clipIds)) {}

    std::string label() const override { return "Group"; }
    std::optional<std::string> redo(Project& project) override;
    void undo(Project& project) override;

    Id groupId() const { return m_groupId; }

private:
    std::vector<Id> m_clipIds;
    Id m_groupId = kNoId;
    std::vector<Id> m_previous; // each clip's group before
};

// Takes every clip out of group `groupId`.
class UngroupCommand final : public Command {
public:
    explicit UngroupCommand(Id groupId) : m_groupId(groupId) {}

    std::string label() const override { return "Ungroup"; }
    std::optional<std::string> redo(Project& project) override;
    void undo(Project& project) override;

private:
    Id m_groupId;
    std::vector<Id> m_members;
};

// The clips of `groupId` on the timeline, in track order.
std::vector<Id> groupMembers(const Project& project, Id groupId);

// --- Compound clips ---

// Moves clips into a new sequence and puts one compound clip playing it where they were:
// on the lowest picture track among them (or the lowest sound track if they are all sound),
// from the earliest start to the latest end, which must otherwise be free. The sequence
// keeps their tracks' kinds and order, and their times relative to the earliest start.
class CreateCompoundCommand final : public Command {
public:
    CreateCompoundCommand(std::vector<Id> clipIds, std::string name)
        : m_clipIds(std::move(clipIds)), m_name(std::move(name)) {}

    std::string label() const override { return "Create Compound Clip"; }
    std::optional<std::string> redo(Project& project) override;
    void undo(Project& project) override;

    Id clipId() const { return m_compoundId; }
    Id sequenceId() const { return m_sequenceId; }

private:
    std::vector<Id> m_clipIds;
    std::string m_name;
    Id m_compoundId = kNoId;
    Id m_sequenceId = kNoId;
    Project m_before; // the timeline as it was (restored on undo)
};

// Puts a compound clip's insides back on the timeline where it plays them (the sequence's
// first track on the compound's own track, the next ones on the tracks above it of the same
// kind, sound on the sound tracks), clips outside the part it plays left out and those
// crossing its edges trimmed to it. The tracks must be free there.
class BreakCompoundCommand final : public Command {
public:
    explicit BreakCompoundCommand(Id clipId) : m_clipId(clipId) {}

    std::string label() const override { return "Break Apart"; }
    std::optional<std::string> redo(Project& project) override;
    void undo(Project& project) override;

private:
    Id m_clipId;
    Project m_before;
};

// --- Captions ---

// Puts captions on the project's caption track (made on top if there is none), replacing what
// it held: one title clip per caption, in the caption style, keeping its words' timing.
// A redo after an undo gives the clips the same ids.
class SetCaptionsCommand final : public Command {
public:
    SetCaptionsCommand(std::vector<CaptionBlock> captions, std::string label = "Set Captions")
        : m_captions(std::move(captions)), m_label(std::move(label)) {}

    std::string label() const override { return m_label; }
    std::optional<std::string> redo(Project& project) override;
    void undo(Project& project) override;

private:
    std::vector<CaptionBlock> m_captions;
    std::string m_label;
    Project m_before;
    std::optional<Project> m_after;
};

// --- Time ranges ---

// Cuts time ranges out of a track (silences, say): clips inside a range go, clips crossing
// its edges are split there, and everything after it moves back to close the gap. Ranges
// may come in any order and overlap. A redo after an undo gives the pieces the same ids.
class RemoveRangesCommand final : public Command {
public:
    RemoveRangesCommand(Id trackId, std::vector<std::pair<Ticks, Ticks>> ranges, std::string label = "Remove Ranges")
        : m_trackId(trackId), m_ranges(std::move(ranges)), m_label(std::move(label)) {}

    std::string label() const override { return m_label; }
    std::optional<std::string> redo(Project& project) override;
    void undo(Project& project) override;

    // How much time was taken out.
    Ticks removed() const { return m_removed; }

private:
    Id m_trackId;
    std::vector<std::pair<Ticks, Ticks>> m_ranges;
    std::string m_label;
    Ticks m_removed = 0;
    Project m_before;
    std::optional<Project> m_after; // the result of the first redo
};

} // namespace tve
