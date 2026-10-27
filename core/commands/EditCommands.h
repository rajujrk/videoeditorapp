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
    // `item` carries the probed facts; its id is assigned by the command.
    explicit ImportMediaCommand(MediaItem item) : m_item(std::move(item)) {}
    ImportMediaCommand(std::string path, Ticks duration) : m_item{.path = std::move(path), .duration = duration} {}

    std::string label() const override { return "Import Media"; }
    std::optional<std::string> redo(Project& project) override;
    void undo(Project& project) override;

    Id mediaId() const { return m_item.id; }

private:
    MediaItem m_item;
};

// Moves one edge of a clip. Trimming the start also moves the source in-point, so the
// frames under the remaining part of the clip stay where they were.
class TrimClipCommand final : public Command {
public:
    enum class Edge { Start, End };
    TrimClipCommand(Id clipId, Edge edge, Ticks newTime) : m_clipId(clipId), m_edge(edge), m_newTime(newTime) {}

    std::string label() const override { return "Trim Clip"; }
    std::optional<std::string> redo(Project& project) override;
    void undo(Project& project) override;

private:
    Id m_clipId;
    Edge m_edge;
    Ticks m_newTime;
    Id m_trackId = kNoId;
    Clip m_original;
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
    // Lets a composite command (freeze frame) keep the same id across redo.
    void setRightClipId(Id id) { m_rightClipId = id; }

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

struct ClipAudio {
    float gainDb = 0.0f;
    Ticks fadeIn = 0;
    Ticks fadeOut = 0;

    friend bool operator==(const ClipAudio&, const ClipAudio&) = default;
};

// Sets a clip's volume and fade lengths. Fades may not overlap (fadeIn + fadeOut must fit).
class SetClipAudioCommand final : public Command {
public:
    SetClipAudioCommand(Id clipId, ClipAudio audio) : m_clipId(clipId), m_audio(audio) {}

    std::string label() const override { return "Change Audio"; }
    std::optional<std::string> redo(Project& project) override;
    void undo(Project& project) override;

private:
    Id m_clipId;
    ClipAudio m_audio;
    ClipAudio m_previous;
};

// Toggles a track's lock / hide / mute. Allowed on locked tracks (that's how they unlock).
class SetTrackFlagCommand final : public Command {
public:
    enum class Flag { Locked, Hidden, Muted };
    SetTrackFlagCommand(Id trackId, Flag flag, bool value) : m_trackId(trackId), m_flag(flag), m_value(value) {}

    std::string label() const override;
    std::optional<std::string> redo(Project& project) override;
    void undo(Project& project) override;

private:
    Id m_trackId;
    Flag m_flag;
    bool m_value;
    bool m_previous = false;
};

// Moves a video clip's sound onto an audio track as its own clip (same timing, volume and
// fades), and silences the video clip.
class DetachAudioCommand final : public Command {
public:
    DetachAudioCommand(Id clipId, Id audioTrackId) : m_clipId(clipId), m_audioTrackId(audioTrackId) {}

    std::string label() const override { return "Detach Audio"; }
    std::optional<std::string> redo(Project& project) override;
    void undo(Project& project) override;

    Id audioClipId() const { return m_audioClipId; }

private:
    Id m_clipId;
    Id m_audioTrackId;
    Id m_audioClipId = kNoId;
};

// Sets a clip's placement, crop, opacity and blend mode (video tracks only).
class SetClipVisualCommand final : public Command {
public:
    SetClipVisualCommand(Id clipId, ClipVisual visual) : m_clipId(clipId), m_visual(visual) {}

    std::string label() const override { return "Change Picture"; }
    std::optional<std::string> redo(Project& project) override;
    void undo(Project& project) override;

private:
    Id m_clipId;
    ClipVisual m_visual;
    ClipVisual m_previous;
};

// Replaces a clip's effect stack. One command per user action (add, remove, reorder, or a
// parameter change), with a label saying which.
class SetClipEffectsCommand final : public Command {
public:
    SetClipEffectsCommand(Id clipId, std::vector<ClipEffect> effects, std::string label)
        : m_clipId(clipId), m_effects(std::move(effects)), m_label(std::move(label)) {}

    std::string label() const override { return m_label; }
    std::optional<std::string> redo(Project& project) override;
    void undo(Project& project) override;

private:
    Id m_clipId;
    std::vector<ClipEffect> m_effects;
    std::string m_label;
    std::vector<ClipEffect> m_previous;
};

// Changes the canvas size and frame rate.
class SetProjectSettingsCommand final : public Command {
public:
    explicit SetProjectSettingsCommand(ProjectSettings settings) : m_settings(settings) {}

    std::string label() const override { return "Change Canvas"; }
    std::optional<std::string> redo(Project& project) override;
    void undo(Project& project) override;

private:
    ProjectSettings m_settings;
    ProjectSettings m_previous;
};

// Changes a clip's speed and direction. The clip keeps its source span, so its timeline
// length changes and later clips on the same track move with its end.
class SetClipSpeedCommand final : public Command {
public:
    SetClipSpeedCommand(Id clipId, double speed, bool reversed) : m_clipId(clipId), m_speed(speed), m_reversed(reversed) {}

    std::string label() const override { return "Change Speed"; }
    std::optional<std::string> redo(Project& project) override;
    void undo(Project& project) override;

private:
    Id m_clipId;
    double m_speed;
    bool m_reversed;
    Id m_trackId = kNoId;
    Clip m_original;
    Ticks m_delta = 0;
};

// Inserts a still of the frame shown at `at` for `hold`, splitting the clip there and
// pushing the rest of the track right.
class FreezeFrameCommand final : public Command {
public:
    FreezeFrameCommand(Id clipId, Ticks at, Ticks hold) : m_clipId(clipId), m_at(at), m_hold(hold) {}

    std::string label() const override { return "Freeze Frame"; }
    std::optional<std::string> redo(Project& project) override;
    void undo(Project& project) override;

    Id freezeClipId() const { return m_freezeClipId; }

private:
    Id m_clipId;
    Ticks m_at;
    Ticks m_hold;
    Id m_trackId = kNoId;
    Clip m_original;
    Id m_rightClipId = kNoId;
    Id m_freezeClipId = kNoId;
};

} // namespace tve
