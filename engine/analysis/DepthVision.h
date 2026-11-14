#pragma once

#include "model/People.h"

#include <CoreVideo/CoreVideo.h>

#include <cstdint>
#include <string>
#include <vector>

namespace tve {

// How far things are in a picture, estimated by a depth model on this Mac: Apple's Core ML
// conversion of Depth Anything V2 Small (Apache-2.0), downloaded on request.

// Nearness per pixel, 0 the farthest in the frame .. 255 the nearest (relative to that frame:
// the model's output from its 2nd to its 98th percentile).
struct DepthMap {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> pixels;

    explicit operator bool() const { return width > 0 && height > 0; }
    // At a point (fractions of the picture, origin top left), averaged over a few pixels.
    std::uint8_t at(double u, double v) const;
    // The value this fraction (0..1) of the pixels are at or under.
    std::uint8_t percentile(double fraction) const;
};

// Uses the model package (.mlpackage) at this path from now on: compiled once (kept beside
// it as .mlmodelc) and loaded. False with `error` if it cannot be; an empty path unloads.
bool setDepthModel(const std::string& packagePath, std::string* error = nullptr);
bool hasDepthModel();

// A frame's depth (empty without a model, or if the model failed).
DepthMap estimateDepth(CVPixelBufferRef frame);

// The nearness to keep sharp: with autofocus, the largest face's (of `faces`, picture
// fractions) or, with none, the nearest subject's (what is nearer than nine pixels in ten);
// else what is at the focus point.
std::uint8_t focusNearness(const DepthMap& depth, const DepthOfField& settings, const std::vector<FaceBox>& faces = {});

} // namespace tve
