#pragma once

#include "model/Project.h"

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace tve {

// Timelines to and from other editors: OpenTimelineIO (.otio) and Final Cut Pro XML
// (.fcpxml) both ways, a CMX 3600 EDL (.edl) out. What carries over: media clips (their
// source range and constant speed, reversed or frozen), tracks, gaps, cross dissolves,
// markers, and titles as placeholders with their text. Everything else — colour, effects,
// keyframes, transforms, the app's own kinds of clip — is left out and listed in the notes,
// so the person knows what to redo.
enum class InterchangeFormat { Otio, Fcpxml, Edl };

// "otio", "fcpxml" (or "fcpxmld", a bundle's Info.fcpxml) or "edl" by a path's extension.
std::optional<InterchangeFormat> interchangeFormatOf(const std::string& path);

struct ExportedTimeline {
    std::string text;
    std::vector<std::string> notes; // what didn't carry over, one line each
};
// The project's main timeline as another editor's file, named `name`.
ExportedTimeline exportTimeline(const Project& project, InterchangeFormat format, const std::string& name);

struct ImportedTimeline {
    // A new project: its media by path, with no stream facts but the length the clips need
    // (the app probes them); its tracks, clips, transitions and markers.
    Project project;
    std::vector<std::string> notes;
    // The file gave the canvas's size (else it is 1920 × 1080 until the app sees the media).
    bool canvasStated = true;
};
// Reads another editor's timeline; relative media paths are taken from `baseDirectory`.
// Throws std::runtime_error saying why when the text isn't a timeline it can read.
ImportedTimeline importTimeline(const std::string& text, InterchangeFormat format, const std::string& baseDirectory);

namespace interchange {

// The timeline as the formats see it, in between: what exporters write and importers read.
struct Transition {
    Ticks before = 0; // how far it reaches into the outgoing clip
    Ticks after = 0;  // and into the incoming one
    std::string kind = "dissolve";
};

struct Item {
    enum class Kind { Media, Title, Gap } kind = Kind::Media;
    std::string name;
    std::string path; // media: an absolute file path
    bool hasVideo = true;
    bool hasAudio = true;
    bool still = false;       // a photo
    Ticks mediaDuration = 0;  // the whole file's, when known
    Ticks start = 0;    // on the timeline
    Ticks duration = 0; // on the timeline
    Ticks sourceIn = 0; // into the media
    double speed = 1.0; // source seconds a timeline second (constant)
    bool reversed = false;
    bool freeze = false;
    std::string text; // a title's
    std::optional<Transition> transitionIn;

    // Source the item plays (0 for a freeze frame).
    Ticks sourceSpan() const { return freeze ? 0 : Ticks(std::llround(double(duration) * speed)); }
};

struct Track {
    bool audio = false;
    std::string name;
    bool enabled = true;
    std::vector<Item> items; // sorted by start, not overlapping; no gap items (spaces are gaps)
};

struct Timeline {
    std::string name;
    FrameRate rate{30, 1};
    int width = 1920; // 0: not stated
    int height = 1080;
    std::vector<Track> tracks; // video tracks bottom first, then audio tracks
    std::vector<Marker> markers;

    Ticks duration() const;
};

// Counts what didn't carry over, by what it was, for the notes.
class Notes {
public:
    // `what` once more (e.g. "Colour grades and looks").
    void add(const std::string& what);
    void addLine(const std::string& line); // said as it is, once
    std::vector<std::string> lines() const;

private:
    std::map<std::string, int> m_counts;
    std::vector<std::string> m_order;
    std::vector<std::string> m_lines;
};

// A project's main timeline flattened, with notes on what is left out.
Timeline fromProject(const Project& project, const std::string& name, Notes& notes);
// A project of a timeline.
Project toProject(const Timeline& timeline, Notes& notes);

// The formats: each writes a Timeline, or reads one (throwing std::runtime_error).
std::string writeOtio(const Timeline& timeline, Notes& notes);
Timeline readOtio(const std::string& text, const std::string& baseDirectory, Notes& notes);
std::string writeFcpxml(const Timeline& timeline, Notes& notes);
Timeline readFcpxml(const std::string& text, const std::string& baseDirectory, Notes& notes);
std::string writeEdl(const Timeline& timeline, Notes& notes);

// Helpers shared by the formats: `ticks` as whole frames of `rate` (rounded), and back.
std::int64_t framesOf(Ticks ticks, FrameRate rate);
Ticks ticksOfFrames(std::int64_t frames, FrameRate rate);
// file:// URLs and paths, both ways (percent-encoded); a relative path joined to `base`.
std::string fileUrl(const std::string& path);
std::string pathFromUrl(const std::string& url, const std::string& base);
std::string fileName(const std::string& path);

} // namespace interchange

} // namespace tve
