#pragma once

#include "model/Time.h"

#include <cstdint>
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

    friend bool operator==(const MediaItem&, const MediaItem&) = default;
};

struct Clip {
    Id id = kNoId;
    Id mediaId = kNoId;
    Ticks start = 0;    // position on the timeline
    Ticks duration = 0; // length on the timeline
    Ticks sourceIn = 0; // offset into the media where the clip begins

    Ticks end() const { return start + duration; }

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
