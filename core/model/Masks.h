#pragma once

#include <array>
#include <optional>
#include <string>
#include <vector>

namespace tve {

enum class MaskShape { Rectangle, Ellipse, Line, Star, Heart, Pen };

// A mask on a clip's picture: what is inside its shape stays visible (or, inverted, what is
// outside). Positions are fractions of the whole picture (before cropping, origin top left),
// so the mask moves, scales and turns with the clip.
struct ClipMask {
    MaskShape shape = MaskShape::Rectangle;
    double x = 0.5;      // centre
    double y = 0.5;
    double width = 0.5;  // size (not used by Line and Pen)
    double height = 0.5;
    double rotation = 0.0;  // degrees clockwise; a Line's edge runs at this angle
    double roundness = 0.0; // rectangle corners, 0..1
    double feather = 0.02;  // soft edge, a fraction of the picture's shorter side, 0..0.5
    bool invert = false;
    std::vector<std::array<double, 2>> points; // Pen: the outline's corners, 3..32

    friend bool operator==(const ClipMask&, const ClipMask&) = default;
};
inline constexpr std::size_t kMaxMasks = 8;
inline constexpr std::size_t kMaxPenPoints = 32; // across all of a clip's pen masks

// Chroma key: colours near `color` turn transparent (a green or blue screen).
struct ChromaKey {
    bool enabled = false;
    std::array<double, 3> color{0.0, 0.69, 0.25}; // screen green
    double tolerance = 0.12; // colour distance that is fully keyed, 0..1
    double softness = 0.08;  // width of the edge between keyed and kept, 0..1
    double spill = 0.5;      // how much of the screen's colour cast to take off what stays, 0..1

    friend bool operator==(const ChromaKey&, const ChromaKey&) = default;
};

// Track matte: the clip uses the picture directly above it (on the track above, at the same
// time) as its matte, and that picture is not shown itself.
enum class MatteMode { None, Alpha, InvertedAlpha, Luma, InvertedLuma };

std::optional<std::string> validateMasks(const std::vector<ClipMask>& masks);
std::optional<std::string> validateChromaKey(const ChromaKey& key);

} // namespace tve
