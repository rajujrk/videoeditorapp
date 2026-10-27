#pragma once

#include "model/Keyframes.h"
#include "model/SpeedRamp.h"
#include "model/Time.h"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace tve {

// Object IDs are unique within a project and never reused, so commands can refer to
// objects by ID across undo/redo.
using Id = std::uint64_t;
inline constexpr Id kNoId = 0;

enum class TrackKind { Video, Audio, Title };

struct MediaItem {
    Id id = kNoId;
    std::string path;
    Ticks duration = 0;
    // Stream facts from probing at import; 0 when unknown.
    bool hasVideo = true;
    bool hasAudio = false;
    std::int32_t width = 0;
    std::int32_t height = 0;
    FrameRate frameRate{30, 1};
    std::int32_t audioSampleRate = 0;
    std::int32_t audioChannels = 0;

    // A lighter copy of the pictures for the preview to decode instead (same frames and
    // times). Set only in the snapshots handed to playback; never saved.
    std::string proxyPath;

    friend bool operator==(const MediaItem&, const MediaItem&) = default;
};

inline constexpr float kMinGainDb = -60.0f; // treated as silence
inline constexpr float kMaxGainDb = 12.0f;
inline constexpr double kMinSpeed = 0.1;
inline constexpr double kMaxSpeed = 10.0;

enum class BlendMode { Normal, Multiply, Screen, Overlay, Add, Darken, Lighten };

// Where a clip's picture sits on the canvas. The picture is first fitted inside the canvas
// (keeping its aspect ratio), then scaled about its center, rotated, and moved.
struct ClipTransform {
    double x = 0.0;        // horizontal offset, in canvas widths (0 = centered)
    double y = 0.0;        // vertical offset, in canvas heights, positive is down
    double scale = 1.0;    // 1 = fitted to the canvas
    double rotation = 0.0; // degrees, clockwise

    friend bool operator==(const ClipTransform&, const ClipTransform&) = default;
};

// Fractions of the source picture trimmed from each edge before fitting.
struct ClipCrop {
    double left = 0.0;
    double top = 0.0;
    double right = 0.0;
    double bottom = 0.0;

    friend bool operator==(const ClipCrop&, const ClipCrop&) = default;
};

// How a clip looks: placement, crop, opacity and blend mode.
struct ClipVisual {
    ClipTransform transform;
    ClipCrop crop;
    double opacity = 1.0;
    BlendMode blend = BlendMode::Normal;

    friend bool operator==(const ClipVisual&, const ClipVisual&) = default;
};

// One effect in a clip's stack, by effect id (validated against the effect library by
// the app, not the core). Missing values use the effect's defaults.
struct ClipEffect {
    std::string effectId;
    bool enabled = true;
    std::map<std::string, std::vector<float>> values;
    // Animated scalar parameters, by parameter id; they override `values` (the app checks
    // their range against the effect's manifest).
    std::map<std::string, Keyframes> keyframes;

    friend bool operator==(const ClipEffect&, const ClipEffect&) = default;
};

struct ColorRgba {
    double r = 1.0;
    double g = 1.0;
    double b = 1.0;
    double a = 1.0;

    friend bool operator==(const ColorRgba&, const ColorRgba&) = default;
};

enum class TextAlignment { Left, Center, Right };

// The text of a title clip. Sizes are fractions so a title looks the same on any canvas:
// the font size of the canvas height, spacing and outline of the font size. The text is
// laid out across the canvas (wrapping at 90 % of its width), centred vertically, then
// placed by the clip's visual like any picture.
struct ClipText {
    std::string text = "Title";
    std::string fontFamily; // empty: the app's default
    double size = 0.08;
    bool bold = false;
    bool italic = false;
    ColorRgba color;
    TextAlignment alignment = TextAlignment::Center;
    double lineSpacing = 1.0;   // times the font's line height
    double letterSpacing = 0.0; // extra space between letters
    ColorRgba outlineColor{0.0, 0.0, 0.0, 1.0};
    double outlineWidth = 0.0;
    bool shadow = false;

    friend bool operator==(const ClipText&, const ClipText&) = default;
};

inline constexpr double kMinTextSize = 0.01;
inline constexpr double kMaxTextSize = 0.5;
inline constexpr std::size_t kMaxTextLength = 2000;

// Where a transition sits around the cut between two touching clips.
enum class TransitionAlignment {
    Center,      // half before the cut, half after
    EndsAtCut,   // entirely before the cut: the incoming clip starts early
    StartsAtCut, // entirely after the cut: the outgoing clip runs on
};

// A transition into a clip from the clip that ends where it starts (same track). Outside
// the clips the media's extra frames ("handles") are used; where the media runs out, its
// edge frame is held. Missing values use the transition's defaults.
struct ClipTransition {
    std::string transitionId;
    Ticks duration = kTicksPerSecond;
    TransitionAlignment alignment = TransitionAlignment::Center;
    std::map<std::string, std::vector<float>> values;

    friend bool operator==(const ClipTransition&, const ClipTransition&) = default;
};

inline constexpr Ticks kMinTransitionDuration = kTicksPerSecond / 10;
inline constexpr Ticks kMaxTransitionDuration = 10 * kTicksPerSecond;

struct Clip {
    Id id = kNoId;
    Id mediaId = kNoId;
    Ticks start = 0;    // position on the timeline
    Ticks duration = 0; // length on the timeline
    Ticks sourceIn = 0; // offset into the media where the clip begins

    // Audio. A clip on a video track plays its media's sound too, unless that sound has
    // been detached to an audio track (audioEnabled = false).
    bool audioEnabled = true;
    float gainDb = 0.0f;
    Ticks fadeIn = 0;
    Ticks fadeOut = 0;

    // Picture
    ClipVisual visual;
    std::vector<ClipEffect> effects; // applied in order, before placement on the canvas

    // Time. At speed 2 one second of timeline plays two seconds of source; reversed plays
    // the source span backwards; a freeze clip holds the single frame at sourceIn.
    double speed = 1.0;
    bool reversed = false;
    bool freeze = false;
    // A speed that changes through the clip (empty: constant `speed`). While set, `speed`
    // holds the ramp's average, so duration × speed is still the source span.
    SpeedRamp speedRamp;

    // Transition from the previous clip; plays only while a clip ends where this one starts.
    std::optional<ClipTransition> transitionIn;

    // A title: drawn text instead of media (mediaId is kNoId).
    std::optional<ClipText> text;

    // Animated properties (see animatableProperties()), by property id. A property with
    // keyframes takes its value from them; its static value above is then unused.
    std::map<std::string, Keyframes, std::less<>> keyframes;

    Ticks end() const { return start + duration; }

    // Source media the clip plays: duration × speed (0 for a freeze frame).
    Ticks sourceSpan() const;
    // Media time shown at a timeline time inside the clip.
    Ticks sourceTimeAt(Ticks time) const;
    // The same mapping continued past the clip's edges (into its handles, for transitions).
    // May fall outside the media; callers clamp.
    Ticks sourceTimeBeyond(Ticks time) const;

    // Linear amplitude at a timeline time inside the clip: gain (animated or not) × fade ramps.
    float gainAt(Ticks time) const;
    float gainDbAt(Ticks time) const;
    // The picture's placement at a timeline time, with animated properties evaluated.
    ClipVisual visualAt(Ticks time) const;
    // The effect stack at a timeline time: animated parameters evaluated into `values`,
    // keyframes left out.
    std::vector<ClipEffect> effectsAt(Ticks time) const;
    const Keyframes* keyframesOf(std::string_view propertyId) const;

    friend bool operator==(const Clip&, const Clip&) = default;
};

struct Track {
    Id id = kNoId;
    TrackKind kind = TrackKind::Video;
    std::string name;
    bool locked = false;
    bool hidden = false;
    bool muted = false;
    std::vector<Clip> clips; // kept sorted by start, never overlapping

    const Clip* findClip(Id clipId) const;
    Clip* findClip(Id clipId);

    // True if [start, start + duration) intersects any clip other than `ignoreId`.
    bool overlaps(Ticks start, Ticks duration, Id ignoreId = kNoId) const;

    void insertClip(Clip clip);
    std::optional<Clip> removeClip(Id clipId);

    friend bool operator==(const Track&, const Track&) = default;
};

struct ProjectSettings {
    std::int32_t width = 1920;
    std::int32_t height = 1080;
    FrameRate frameRate{30, 1};
    std::int32_t sampleRate = 48000;

    friend bool operator==(const ProjectSettings&, const ProjectSettings&) = default;
};

struct ClipLocation {
    Track* track = nullptr;
    Clip* clip = nullptr;
};

class Project {
public:
    ProjectSettings settings;
    std::vector<MediaItem> media;
    std::vector<Track> tracks;

    Id allocateId() { return m_nextId++; }
    Id nextId() const { return m_nextId; }
    void setNextId(Id id) { m_nextId = id; }

    Track* findTrack(Id trackId);
    const Track* findTrack(Id trackId) const;
    std::optional<std::size_t> trackIndex(Id trackId) const;
    const MediaItem* findMedia(Id mediaId) const;
    std::optional<ClipLocation> locateClip(Id clipId);

    // End time of the last clip on any track.
    Ticks duration() const;

    friend bool operator==(const Project&, const Project&) = default;

private:
    Id m_nextId = 1;
};

} // namespace tve
