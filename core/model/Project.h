#pragma once

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

    friend bool operator==(const MediaItem&, const MediaItem&) = default;
};

inline constexpr float kMinGainDb = -60.0f; // treated as silence
inline constexpr float kMaxGainDb = 12.0f;

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

    friend bool operator==(const ClipEffect&, const ClipEffect&) = default;
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

    // Picture
    ClipVisual visual;
    std::vector<ClipEffect> effects; // applied in order, before placement on the canvas

    Ticks end() const { return start + duration; }

    // Linear amplitude at a timeline time inside the clip: gain × fade ramps.
    float gainAt(Ticks time) const;

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
