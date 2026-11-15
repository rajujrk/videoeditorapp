#pragma once

#include "model/Time.h"

#include <array>
#include <optional>
#include <utility>
#include <vector>

namespace tve {

// Camera motion and brightness through a shot: measured frame by frame (what the content
// did between two frames), then turned into what a clip keeps — a stabilization (a
// correction per frame, and the zoom that hides the edges it moves into view) and flicker
// removal (a brightness gain per frame).

// A grey picture: luma 0..1, rows top first.
struct GrayImage {
    int width = 0;
    int height = 0;
    std::vector<float> pixels;

    float at(int x, int y) const { return pixels[std::size_t(y) * std::size_t(width) + std::size_t(x)]; }
};

// How the content moved from one frame to the next, as a similarity about the picture's
// centre in pixels (x right, y down): next ≈ scale · R(angle) · previous + (dx, dy).
struct FrameMotion {
    double dx = 0.0;
    double dy = 0.0;
    double angle = 0.0; // radians, clockwise on screen
    double scale = 1.0;
    double confidence = 0.0; // share of the picture's blocks that agreed, 0..1

    friend bool operator==(const FrameMotion&, const FrameMotion&) = default;
};

// Block matching over a grid (coarse to fine, sub-pixel), then a similarity fitted to the
// blocks that agree (outliers — things moving on their own — dropped). Pictures of the same
// size; a few hundred pixels wide is plenty.
FrameMotion estimateMotion(const GrayImage& previous, const GrayImage& next);

// A file's measurements: per frame, its motion from the frame before (the first: none) and
// its mean brightness.
struct MotionAnalysis {
    static constexpr int kVersion = 1;
    int width = 0; // the size the motion is in (pixels)
    int height = 0;
    std::vector<Ticks> times;
    std::vector<FrameMotion> motion;
    std::vector<float> brightness; // mean luma, 0..1
};

// One frame's stabilizing correction: the picture moved by (dx, dy) (picture fractions),
// turned by `angle` and scaled about its centre, before the stabilization's zoom.
struct SteadySample {
    Ticks time = 0;
    double dx = 0.0;
    double dy = 0.0;
    double angle = 0.0;
    double scale = 1.0;

    friend bool operator==(const SteadySample&, const SteadySample&) = default;
};

struct Stabilization {
    bool enabled = false;
    double smoothness = 0.5; // 0: only the jitter .. 1: very slow camera moves
    bool lock = false;       // a camera on a tripod: no motion at all
    double maxZoom = 1.3;    // the zoom that hides the edges goes no further
    double zoom = 1.0;       // that zoom, as worked out
    std::vector<SteadySample> samples;

    // The correction at a media time (between frames: the nearer ones mixed).
    SteadySample at(Ticks mediaTime) const;

    friend bool operator==(const Stabilization&, const Stabilization&) = default;
};

struct GainSample {
    Ticks time = 0;
    float gain = 1.0f;

    friend bool operator==(const GainSample&, const GainSample&) = default;
};

struct Deflicker {
    bool enabled = false;
    double strength = 0.5; // 0: only fast flicker .. 1: slower changes in light too
    std::vector<GainSample> samples;

    float gainAt(Ticks mediaTime) const;

    friend bool operator==(const Deflicker&, const Deflicker&) = default;
};

// Four corners of a quadrilateral, picture fractions: top left, top right, bottom right,
// bottom left (x, y each).
using Quad = std::array<double, 8>;

// A projective map of the plane (a 3 × 3 matrix, rows first): what carries a flat surface
// from one view to another.
struct Homography {
    std::array<double, 9> m{1, 0, 0, 0, 1, 0, 0, 0, 1};

    std::pair<double, double> apply(double x, double y) const;
    Quad apply(const Quad& quad) const;
    std::optional<Homography> inverse() const;
    Homography then(const Homography& next) const; // this, then `next`
};
// The map from the unit square (0,0)–(1,1) onto a quad (corners in the order above), or
// nullopt when the quad is degenerate (three corners in a line).
std::optional<Homography> squareToQuad(const Quad& quad);
// The map carrying one quad's corners onto another's.
std::optional<Homography> quadToQuad(const Quad& from, const Quad& to);
// The best map carrying `from` points onto `to` points (least squares over four or more),
// or nullopt when they cannot fix one.
std::optional<Homography> fitHomography(const std::vector<std::pair<double, double>>& from,
                                        const std::vector<std::pair<double, double>>& to);

// Something followed through a shot (tracking): where it is in each frame, as picture
// fractions (origin top left), and — a flat surface — how it is turned; a plane tracked in
// perspective also has its four corners.
struct TrackSample {
    Ticks time = 0; // media time
    double x = 0.0; // centre
    double y = 0.0;
    double width = 0.0;
    double height = 0.0;
    double angle = 0.0; // radians, clockwise (surfaces; objects: 0)
    Quad corners{};     // a plane's corners (all zero for other tracks)

    bool hasCorners() const;
    friend bool operator==(const TrackSample&, const TrackSample&) = default;
};
// A plane's sample: its corners, and the centre, size and turn they make.
TrackSample sampleOfQuad(Ticks time, const Quad& corners);

// What a track follows: an object (a box that may change shape), a flat rectangle found in
// the box (which turns too), a plane in perspective (four corners, any flat textured
// surface: corner-pin something onto it), or a single point (a small spot followed for its
// position: to steady the shot on it, or to pin something there).
enum class MotionTrackKind { Object, Surface, Plane, Point };

struct MotionTrack {
    int id = 0;           // 1, 2… on its clip
    bool surface = false; // a flat rectangle (turns too) rather than an object
    std::vector<TrackSample> samples;
    MotionTrackKind kind = MotionTrackKind::Object; // (older projects: only `surface`)

    MotionTrackKind type() const { return surface ? MotionTrackKind::Surface : kind; }

    // Where it is at a media time (between samples: mixed); nullopt outside its span.
    std::optional<TrackSample> at(Ticks mediaTime) const;

    friend bool operator==(const MotionTrack&, const MotionTrack&) = default;
};

// Keeps only the samples needed to follow a track to within `tolerance` (picture fractions;
// angles in radians).
std::vector<TrackSample> thinTrack(const std::vector<TrackSample>& samples, double tolerance = 0.002);

// The corrections that keep the camera's path smooth (or still, locked), and the zoom that
// hides the edges (at most options.maxZoom).
Stabilization stabilize(const MotionAnalysis& analysis, double smoothness, bool lock, double maxZoom = 1.3);

// Gains that even out brightness changes faster than the strength allows (clamped to ½..2).
Deflicker deflicker(const MotionAnalysis& analysis, double strength);

} // namespace tve
