#pragma once

#include "model/ColorGrade.h"
#include "model/Keyframes.h"
#include "model/Masks.h"
#include "model/Motion.h"
#include "model/People.h"
#include "model/SpeedRamp.h"
#include "model/TextAnimation.h"
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
    // A still image (photo, graphic): one picture for as long as its clips last. `duration`
    // is then the length a new clip of it gets (kStillDuration).
    bool still = false;
    // The pictures have transparency (ProRes 4444, GIF…): they play over what is below.
    bool alpha = false;
    // A Lottie animation (a sticker): drawn, not decoded, looping; like a still it lasts as
    // long as its clips (`still` is set too), `duration` being one cycle.
    bool lottie = false;
    // Identifies the file's content (see io/MediaRelink.h) so a moved or renamed copy can be
    // found again; empty when not known (media imported before it was recorded).
    std::string fingerprint;

    // A lighter copy of the pictures for the preview to decode instead (same frames and
    // times). Set only in the snapshots handed to playback; never saved.
    std::string proxyPath;

    friend bool operator==(const MediaItem&, const MediaItem&) = default;
};

inline constexpr Ticks kStillDuration = 5 * kTicksPerSecond;

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

// A clip's sound cleaned by speech enhancement: a copy of its media's whole sound with the
// same timing (engine/speech/SpeechEnhancer), played instead of the media's own.
struct EnhancedAudio {
    std::string path;      // a WAV in the app's analysis folder
    double limitDb = 100;  // noise lowered by at most this much (≥ 100: all of it)

    friend bool operator==(const EnhancedAudio&, const EnhancedAudio&) = default;
};

struct ColorRgba {
    double r = 1.0;
    double g = 1.0;
    double b = 1.0;
    double a = 1.0;

    friend bool operator==(const ColorRgba&, const ColorRgba&) = default;
};

enum class TextAlignment { Left, Center, Right };

// A shape behind a title's text, sized to it: boxes, a pill, speech and thought bubbles, a
// banner, an underline, a circle, an arrow and a tag.
enum class TitleShapeKind { None, Box, Rounded, Pill, Speech, Thought, Banner, Underline, Circle, Arrow, Tag };

struct TitleShape {
    TitleShapeKind kind = TitleShapeKind::None;
    ColorRgba color{0.0, 0.0, 0.0, 0.6};
    double padding = 0.4; // around the text, in ems
    double tail = 0.25;   // where a bubble's tail is, along its bottom edge (0..1)

    friend bool operator==(const TitleShape&, const TitleShape&) = default;
};

// One spoken word of a caption: when it is said (from the caption clip's start) and how it
// is written, for captions that follow the speech word by word.
struct CaptionWord {
    Ticks start = 0;
    Ticks end = 0;
    std::string text;

    friend bool operator==(const CaptionWord&, const CaptionWord&) = default;
};

// How a caption follows its speech, word by word (with its words' timing): the word being
// said in the highlight colour, popping out, words filling with the colour as they are said
// (karaoke), or appearing as they are said.
enum class CaptionMotion { None, Highlight, Pop, Karaoke, Reveal };

// A title that moves through the frame over the whole clip: credits rolling up, or a news
// crawl from right to left.
enum class TextScroll { None, RollUp, CrawlLeft };

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
    TitleShape shape;
    // How the text arrives, leaves and keeps moving (see TextAnimation), and its scrolling.
    TextAnimation animationIn;
    TextAnimation animationOut;
    TextAnimation animationLoop{.preset = {}, .duration = 1.5, .unit = TextUnit::Character, .stagger = 0.0};
    TextScroll scroll = TextScroll::None;
    // A caption's words with their timing (empty for other titles), and how it follows them.
    std::vector<CaptionWord> words;
    CaptionMotion captionMotion = CaptionMotion::None;
    ColorRgba highlightColor{1.0, 0.82, 0.2, 1.0};

    // Whether it looks different from one moment to the next.
    bool animated() const
    {
        return animationIn.active() || animationOut.active() || animationLoop.active() || scroll != TextScroll::None
            || (captionMotion != CaptionMotion::None && !words.empty());
    }

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

// A look (filter) from the library on a clip: the look's own grade, applied before the
// clip's grade at `intensity` (0..1).
struct ClipLook {
    std::string id;
    std::string title;
    double intensity = 1.0;
    ColorGrade grade;

    // The grade to render: the look's at its intensity.
    ColorGrade effective() const { return scaledGrade(grade, intensity); }

    friend bool operator==(const ClipLook&, const ClipLook&) = default;
};

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
    // Audio effects (engine/audio), in order, after the clip's volume and fades; their tails
    // (echoes, reverb) ring on past the clip's end.
    std::vector<ClipEffect> audioEffects;
    // Plays this cleaned copy of the sound (if the file is there; else the media's own).
    std::optional<EnhancedAudio> enhancedAudio;

    // Picture
    ClipVisual visual;
    ColorGrade color; // applied before the effects
    std::optional<ClipLook> look; // applied before `color`
    // After colour, before effects: the chroma key, then the masks (alpha of their union).
    ChromaKey chromaKey;
    std::vector<ClipMask> masks;
    MatteMode matte = MatteMode::None;
    // People (core/model/People.h): the person cut out, and faces covered — first of all, on
    // the decoded picture, where Vision saw them.
    PortraitCutout cutout;
    FaceMosaic faceMosaic;
    // Motion (core/model/Motion.h), after the people, before colour, masks and effects:
    // steadied and its flicker evened out, then denoised over time (strength 0..1, 0: off).
    Stabilization stabilization;
    Deflicker deflicker;
    double denoise = 0.0;
    // Things followed through the clip's picture (in its media's time), for other clips
    // (titles, stickers…) to follow.
    std::vector<MotionTrack> motionTracks;
    std::vector<ClipEffect> effects; // applied in order, before placement on the canvas

    // Time. At speed 2 one second of timeline plays two seconds of source; reversed plays
    // the source span backwards; a freeze clip holds the single frame at sourceIn.
    double speed = 1.0;
    bool reversed = false;
    bool freeze = false;
    // Played faster or slower, the sound keeps its pitch (time-stretched) unless this is
    // off (then the pitch follows the speed, like tape). Reversed sound always follows it.
    bool keepPitch = true;
    // A speed that changes through the clip (empty: constant `speed`). While set, `speed`
    // holds the ramp's average, so duration × speed is still the source span.
    SpeedRamp speedRamp;

    // Transition from the previous clip; plays only while a clip ends where this one starts.
    std::optional<ClipTransition> transitionIn;

    // A title: drawn text instead of media (mediaId is kNoId).
    std::optional<ClipText> text;
    // An adjustment layer (no media either): its look, grade and effects apply to
    // everything below it on screen while it lasts.
    bool adjustment = false;
    // A compound clip: plays a nested sequence (Project::sequences) instead of media; its
    // sourceIn and duration select part of that sequence.
    Id sequenceId = kNoId;
    // Clips sharing a group move and are deleted together (kNoId: not grouped).
    Id groupId = kNoId;

    // A clip with no media to play: a title or an adjustment layer.
    bool generated() const { return text.has_value() || adjustment; }
    bool compound() const { return sequenceId != kNoId; }

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
    // Mixer: the track's volume, its left/right balance (-1..1), and solo (while any track is
    // soloed only soloed tracks are heard).
    float gainDb = 0.0f;
    float pan = 0.0f;
    bool solo = false;
    // A title track holding captions (subtitles): what SRT/VTT export writes and import fills.
    bool captions = false;
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

// The colours a marker can have.
enum class MarkerColor { Red, Orange, Yellow, Green, Blue, Purple };

// A point (or, with a duration, a range) on the timeline with a note. Chapter markers
// become chapters of the exported video, titled by their note.
struct Marker {
    Id id = kNoId;
    Ticks time = 0;
    Ticks duration = 0;
    MarkerColor color = MarkerColor::Blue;
    std::string note;
    bool chapter = false;

    friend bool operator==(const Marker&, const Marker&) = default;
};

inline constexpr std::size_t kMaxMarkerNote = 500;

// A chapter of an exported file: from one chapter marker to the next (or the end).
struct Chapter {
    Ticks start = 0;
    Ticks end = 0;
    std::string title;

    friend bool operator==(const Chapter&, const Chapter&) = default;
};

// Tracks of their own, played by compound clips (Clip::sequenceId).
struct Sequence {
    Id id = kNoId;
    std::string name;
    std::vector<Track> tracks;

    // End of the last clip on any of its tracks.
    Ticks duration() const;

    friend bool operator==(const Sequence&, const Sequence&) = default;
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
    std::vector<Marker> markers;     // sorted by time
    std::vector<Sequence> sequences; // the insides of compound clips

    Id allocateId() { return m_nextId++; }
    Id nextId() const { return m_nextId; }
    void setNextId(Id id) { m_nextId = id; }

    Track* findTrack(Id trackId);
    const Track* findTrack(Id trackId) const;
    std::optional<std::size_t> trackIndex(Id trackId) const;
    const MediaItem* findMedia(Id mediaId) const;
    std::optional<ClipLocation> locateClip(Id clipId);
    const Sequence* findSequence(Id sequenceId) const;
    Marker* findMarker(Id markerId);
    // How much source a clip can play: its media's length, its sequence's, or none (titles,
    // adjustment layers, stills: any length).
    std::optional<Ticks> sourceLimit(const Clip& clip) const;

    // End time of the last clip on any track.
    Ticks duration() const;
    // The chapters an export gets from the chapter markers before the end, titled by their
    // notes ("Chapter N" when empty). When the first starts after 0, an "Opening" chapter
    // covers the time before it (players expect chapters to start at the beginning).
    std::vector<Chapter> chapters() const;
    // The chapters of an export of `from`–`to` only: those above, cut to it and timed from its start.
    std::vector<Chapter> chapters(Ticks from, Ticks to) const;

    friend bool operator==(const Project&, const Project&) = default;

private:
    Id m_nextId = 1;
};

} // namespace tve
