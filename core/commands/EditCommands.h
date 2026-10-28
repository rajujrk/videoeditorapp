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
    // `visual` places the new clip (a sticker starts small, say); `label` names the step.
    AddClipCommand(Id trackId, Id mediaId, Ticks start, Ticks duration, Ticks sourceIn = 0, ClipVisual visual = {},
                   std::string label = "Add Clip")
        : m_trackId(trackId), m_mediaId(mediaId), m_start(start), m_duration(duration), m_sourceIn(sourceIn),
          m_visual(visual), m_label(std::move(label)) {}

    std::string label() const override { return m_label; }
    std::optional<std::string> redo(Project& project) override;
    void undo(Project& project) override;

    Id clipId() const { return m_clipId; }

private:
    Id m_trackId;
    Id m_mediaId;
    Ticks m_start;
    Ticks m_duration;
    Ticks m_sourceIn;
    ClipVisual m_visual;
    std::string m_label;
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
    // A moved clip leaves its transitions behind: its own, and the one into the clip that
    // followed it.
    std::optional<ClipTransition> m_ownTransition;
    Id m_followerId = kNoId;
    std::optional<ClipTransition> m_followerTransition;
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
    // The transition into the clip that followed the removed one goes with it.
    Id m_followerId = kNoId;
    std::optional<ClipTransition> m_followerTransition;
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

// Sets a clip's audio effect stack (ids checked against the audio effect library by the app).
class SetClipAudioEffectsCommand final : public Command {
public:
    SetClipAudioEffectsCommand(Id clipId, std::vector<ClipEffect> effects, std::string label)
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

// Whether a clip's sound keeps its pitch when the clip plays faster or slower.
class SetClipKeepPitchCommand final : public Command {
public:
    SetClipKeepPitchCommand(Id clipId, bool keep) : m_clipId(clipId), m_keep(keep) {}

    std::string label() const override { return m_keep ? "Keep Pitch" : "Pitch Follows Speed"; }
    std::optional<std::string> redo(Project& project) override;
    void undo(Project& project) override;

private:
    Id m_clipId;
    bool m_keep;
    bool m_previous = true;
};

struct TrackMix {
    float gainDb = 0.0f;
    float pan = 0.0f;
    bool solo = false;

    friend bool operator==(const TrackMix&, const TrackMix&) = default;
};

// Sets a track's mixer volume, balance and solo. Allowed on locked tracks, like mute.
class SetTrackMixCommand final : public Command {
public:
    SetTrackMixCommand(Id trackId, TrackMix mix) : m_trackId(trackId), m_mix(mix) {}

    std::string label() const override { return "Change Track Mix"; }
    std::optional<std::string> redo(Project& project) override;
    void undo(Project& project) override;

private:
    Id m_trackId;
    TrackMix m_mix;
    TrackMix m_previous;
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

// Gives a clip a speed ramp (or, with an empty ramp, back to its average constant speed),
// optionally also setting its direction. It keeps playing the same source span, so its
// length changes and later clips on the track move with its end.
class SetClipSpeedRampCommand final : public Command {
public:
    SetClipSpeedRampCommand(Id clipId, SpeedRamp ramp, std::optional<bool> reversed = std::nullopt)
        : m_clipId(clipId), m_ramp(std::move(ramp)), m_reversed(reversed) {}

    std::string label() const override
    {
        return m_reversed ? "Change Speed" : m_ramp.empty() ? "Remove Speed Ramp" : "Speed Ramp";
    }
    std::optional<std::string> redo(Project& project) override;
    void undo(Project& project) override;

private:
    Id m_clipId;
    SpeedRamp m_ramp;
    std::optional<bool> m_reversed;
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
    Id rightClipId() const { return m_rightClipId; }

private:
    Id m_clipId;
    Ticks m_at;
    Ticks m_hold;
    Id m_trackId = kNoId;
    Clip m_original;
    Id m_rightClipId = kNoId;
    Id m_freezeClipId = kNoId;
};

// Adds a recorded voice-over in one step: imports its file and places it at `start` on an
// audio track — `trackId`, or with kNoId a new "Voice-over" track below the others.
class AddVoiceOverCommand final : public Command {
public:
    AddVoiceOverCommand(MediaItem item, Id trackId, Ticks start)
        : m_item(std::move(item)), m_trackId(trackId), m_start(start) {}

    std::string label() const override { return "Record Voice-over"; }
    std::optional<std::string> redo(Project& project) override;
    void undo(Project& project) override;

    Id clipId() const { return m_clipId; }
    Id mediaId() const { return m_item.id; }
    Id trackId() const { return m_trackId != kNoId ? m_trackId : m_newTrackId; }

private:
    MediaItem m_item;
    Id m_trackId;
    Ticks m_start;
    Id m_clipId = kNoId;
    Id m_newTrackId = kNoId;
};

// Replaces the keyframes of some of a clip's properties (an empty list stops animating
// that property), and optionally its static picture and volume, as one step. `label`
// names the step for undo ("Add Keyframe", "Change Picture", …).
class SetClipKeyframesCommand final : public Command {
public:
    SetClipKeyframesCommand(Id clipId, std::map<std::string, Keyframes> changes, std::string label,
                            std::optional<ClipVisual> visual = std::nullopt, std::optional<float> gainDb = std::nullopt)
        : m_clipId(clipId), m_changes(std::move(changes)), m_label(std::move(label)), m_visual(visual), m_gainDb(gainDb)
    {
    }

    // Replaces the effect stack in the same step (e.g. its parameters' keyframes).
    void setEffects(std::vector<ClipEffect> effects) { m_effects = std::move(effects); }

    std::string label() const override { return m_label; }
    std::optional<std::string> redo(Project& project) override;
    void undo(Project& project) override;

private:
    Id m_clipId;
    std::map<std::string, Keyframes> m_changes;
    std::optional<std::vector<ClipEffect>> m_effects;
    std::string m_label;
    std::optional<ClipVisual> m_visual;
    std::optional<float> m_gainDb;
    Clip m_previous;
};

// Sets the colour grade of one or more video clips as one step ("Change Color", or
// "Paste Grade" for several).
class SetClipColorCommand final : public Command {
public:
    SetClipColorCommand(std::vector<Id> clipIds, ColorGrade grade, std::string label = "Change Color")
        : m_clipIds(std::move(clipIds)), m_grade(std::move(grade)), m_label(std::move(label)) {}

    std::string label() const override { return m_label; }
    std::optional<std::string> redo(Project& project) override;
    void undo(Project& project) override;

private:
    std::vector<Id> m_clipIds;
    ColorGrade m_grade;
    std::string m_label;
    std::vector<ColorGrade> m_previous;
};

// A clip's masks, chroma key and track matte, as one step ("Change Mask", "Change Key", …).
struct ClipKeying {
    std::vector<ClipMask> masks;
    ChromaKey chromaKey;
    MatteMode matte = MatteMode::None;
};
class SetClipKeyingCommand final : public Command {
public:
    SetClipKeyingCommand(Id clipId, ClipKeying keying, std::string label)
        : m_clipId(clipId), m_keying(std::move(keying)), m_label(std::move(label)) {}

    std::string label() const override { return m_label; }
    std::optional<std::string> redo(Project& project) override;
    void undo(Project& project) override;

private:
    Id m_clipId;
    ClipKeying m_keying;
    std::string m_label;
    ClipKeying m_previous;
};

// Several commands as one step, done in order and undone in reverse. If one is refused,
// those before it are undone and the whole is refused.
class CompositeCommand final : public Command {
public:
    CompositeCommand(std::vector<std::unique_ptr<Command>> commands, std::string label)
        : m_commands(std::move(commands)), m_label(std::move(label)) {}

    std::string label() const override { return m_label; }
    std::optional<std::string> redo(Project& project) override;
    void undo(Project& project) override;

private:
    std::vector<std::unique_ptr<Command>> m_commands;
    std::string m_label;
};

// Puts a look on a video clip, changes its intensity, or (nullopt) takes it off.
class SetClipLookCommand final : public Command {
public:
    SetClipLookCommand(Id clipId, std::optional<ClipLook> look, std::string label)
        : m_clipId(clipId), m_look(std::move(look)), m_label(std::move(label)) {}

    std::string label() const override { return m_label; }
    std::optional<std::string> redo(Project& project) override;
    void undo(Project& project) override;

private:
    Id m_clipId;
    std::optional<ClipLook> m_look;
    std::string m_label;
    std::optional<ClipLook> m_previous;
};

// Checks effect ids and their keyframes' order; returns why they are invalid.
std::optional<std::string> validateEffects(const std::vector<ClipEffect>& effects);

// Checks a picture's scale, opacity and crop; returns why it is invalid.
std::optional<std::string> validateVisual(const ClipVisual& visual);

// Checks a title's text and sizes; returns why it is invalid.
std::optional<std::string> validateText(const ClipText& text);

// Adds a title clip to a video or title track. With trackId kNoId it first adds a new
// video track on top (undone together).
class AddTitleCommand final : public Command {
public:
    AddTitleCommand(Id trackId, Ticks start, Ticks duration, ClipText text, ClipVisual visual = {})
        : m_trackId(trackId), m_start(start), m_duration(duration), m_text(std::move(text)), m_visual(visual) {}

    std::string label() const override { return "Add Title"; }
    std::optional<std::string> redo(Project& project) override;
    void undo(Project& project) override;

    Id clipId() const { return m_clipId; }
    Id trackId() const { return m_trackId != kNoId ? m_trackId : m_newTrackId; }

private:
    Id m_trackId;
    Ticks m_start;
    Ticks m_duration;
    ClipText m_text;
    ClipVisual m_visual;
    Id m_clipId = kNoId;
    Id m_newTrackId = kNoId;
};

// Adds an adjustment layer to a video track. With trackId kNoId it first adds a new video
// track on top (undone together).
class AddAdjustmentCommand final : public Command {
public:
    AddAdjustmentCommand(Id trackId, Ticks start, Ticks duration)
        : m_trackId(trackId), m_start(start), m_duration(duration) {}

    std::string label() const override { return "Add Adjustment Layer"; }
    std::optional<std::string> redo(Project& project) override;
    void undo(Project& project) override;

    Id clipId() const { return m_clipId; }
    Id trackId() const { return m_trackId != kNoId ? m_trackId : m_newTrackId; }

private:
    Id m_trackId;
    Ticks m_start;
    Ticks m_duration;
    Id m_clipId = kNoId;
    Id m_newTrackId = kNoId;
};

// Replaces a title clip's text and style.
class SetClipTextCommand final : public Command {
public:
    SetClipTextCommand(Id clipId, ClipText text) : m_clipId(clipId), m_text(std::move(text)) {}

    std::string label() const override { return "Edit Title"; }
    std::optional<std::string> redo(Project& project) override;
    void undo(Project& project) override;

private:
    Id m_clipId;
    ClipText m_text;
    ClipText m_previous;
};

// Sets, changes or (with nullopt) removes the transition into a clip. Setting one needs a
// clip ending where this one starts on the same track.
class SetClipTransitionCommand final : public Command {
public:
    SetClipTransitionCommand(Id clipId, std::optional<ClipTransition> transition)
        : m_clipId(clipId), m_transition(std::move(transition)) {}

    std::string label() const override;
    std::optional<std::string> redo(Project& project) override;
    void undo(Project& project) override;

private:
    Id m_clipId;
    std::optional<ClipTransition> m_transition;
    std::optional<ClipTransition> m_previous;
};

} // namespace tve
