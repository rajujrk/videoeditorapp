#pragma once

#include "model/Time.h"

#include <array>
#include <optional>
#include <utility>
#include <vector>

namespace tve {

// People in a clip's picture: cutting the person out, covering faces, and reframing to
// follow them. What finds them (person segmentation, face detection) is the app's
// (ui/player/PeopleVision); this is what a project keeps and how it is worked out.

// Portrait cutout: the person (or people) kept, the background made transparent — or,
// inverted, the person taken out. The matte is found for every frame as it is drawn.
struct PortraitCutout {
    bool enabled = false;
    bool invert = false;
    double softness = 0.3; // edge softness, 0 (hard) .. 1
    double shift = 0.0;    // the edge moved out (> 0, more kept) or in (< 0), -1 .. 1

    friend bool operator==(const PortraitCutout&, const PortraitCutout&) = default;
};

// A rectangle in a picture, as fractions of it (origin top left).
struct FaceBox {
    double x = 0.0; // centre
    double y = 0.0;
    double width = 0.0;
    double height = 0.0;

    friend bool operator==(const FaceBox&, const FaceBox&) = default;
};

// One face where it was seen, at a media time.
struct FaceSample {
    Ticks time = 0;
    FaceBox box;

    friend bool operator==(const FaceSample&, const FaceSample&) = default;
};

// One person's face through a file: the samples it was seen in, in time order. Between
// samples it is where they put it (linearly); it is there from its first sample to its last.
struct FaceTrack {
    int id = 0; // 1, 2… in order of first appearance
    std::vector<FaceSample> samples;

    std::optional<FaceBox> boxAt(Ticks mediaTime) const;
    Ticks start() const { return samples.empty() ? 0 : samples.front().time; }
    Ticks end() const { return samples.empty() ? 0 : samples.back().time; }

    friend bool operator==(const FaceTrack&, const FaceTrack&) = default;
};

enum class FaceCover { Mosaic, Blur, Fill };

// Faces covered on a clip: the tracks to cover (only those chosen), and how.
struct FaceMosaic {
    FaceCover style = FaceCover::Mosaic;
    double amount = 0.5; // mosaic block / blur size, 0..1
    double expand = 0.3; // how far past the face box (hair, chin), as a fraction of its size
    std::vector<FaceTrack> faces;

    bool active() const { return !faces.empty(); }
    // Every covered face's (expanded) box at a media time.
    std::vector<FaceBox> boxesAt(Ticks mediaTime) const;

    friend bool operator==(const FaceMosaic&, const FaceMosaic&) = default;
};
inline constexpr std::size_t kMaxCoveredFaces = 16; // at once, in a frame

// Faces as a detector saw them, frame by frame (media time, boxes).
struct FaceFrame {
    Ticks time = 0;
    std::vector<FaceBox> faces;
};

struct FaceTrackOptions {
    double minOverlap = 0.2;  // intersection over union to be the same face in the next frame
    Ticks maxGap = kTicksPerSecond / 2; // a face lost for longer starts a new track
    Ticks minLength = kTicksPerSecond / 5; // shorter tracks are false detections
    double tolerance = 0.01;  // samples dropped while a straight line stays this close (picture fractions)
};

// Links the detections into tracks (the best-overlapping face in the frames after; a face
// lost briefly continues), drops the fleeting ones, and keeps only the samples needed to
// follow each one to within `tolerance`.
std::vector<FaceTrack> linkFaces(const std::vector<FaceFrame>& frames, const FaceTrackOptions& options = {});

// Where the subject of a picture is (media time, box): faces, or what draws the eye.
struct SubjectSample {
    Ticks time = 0;
    FaceBox box;
};

// A reframing: the picture scaled to fill the canvas and moved (ClipVisual x, y) so the
// subject stays in view, smoothly.
struct ReframePath {
    double scale = 1.0;
    std::vector<std::pair<Ticks, std::array<double, 2>>> positions; // media time → (x, y)
};

struct ReframeOptions {
    double smoothing = 0.6; // 0: follow every move .. 1: barely move (seconds of easing ≈ 2·smoothing)
    double tolerance = 0.005; // keyframes dropped while a straight line stays this close (canvas fractions)
};

// The path that keeps the subject centred where the picture allows, for a picture of
// pictureWidth × pictureHeight on a canvas of canvasWidth × canvasHeight.
ReframePath reframe(const std::vector<SubjectSample>& subject, double pictureWidth, double pictureHeight,
                    double canvasWidth, double canvasHeight, const ReframeOptions& options = {});

} // namespace tve
