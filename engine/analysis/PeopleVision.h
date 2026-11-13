#pragma once

#include "analysis/MediaAnalysis.h"
#include "model/People.h"

#include <CoreVideo/CoreVideo.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace tve {

// Finding people with Apple's Vision framework, on this Mac (no models to download): person
// segmentation, face rectangles and attention saliency, on decoded frames.

struct PersonMatte {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> pixels; // 255: person, 0: background; rows top first

    explicit operator bool() const { return width > 0 && height > 0; }
};

enum class SegmentationQuality { Fast, Balanced, Accurate };

// The people in a frame as a matte (smaller than the frame; scale it up). Empty when Vision
// cannot read the frame.
PersonMatte segmentPeople(CVPixelBufferRef frame, SegmentationQuality quality = SegmentationQuality::Balanced);

// Smart cutout: what stands out in the frame under `box` (picture fractions) as a matte —
// the foreground instances (Vision's subject lifting) that the box mostly covers, or the one
// it covers most. Empty when there is none.
PersonMatte segmentSubject(CVPixelBufferRef frame, const FaceBox& box);

// The faces in a frame (picture fractions, origin top left).
std::vector<FaceBox> detectFaces(CVPixelBufferRef frame);

// A face's parts, for the beauty filter: points in picture fractions, origin top left.
struct FacePoint {
    float x = 0.0f;
    float y = 0.0f;
};
using FaceOutline = std::vector<FacePoint>;
struct FaceParts {
    FaceBox box;
    FaceOutline contour;               // the jaw line, from ear to ear
    std::vector<FaceOutline> features; // eyes, brows and lips: left as they are
    FaceOutline innerLips;             // where teeth show
};

// The faces in a frame with their landmarks (Vision's face landmarks). Empty when there are
// none or Vision cannot read the frame.
std::vector<FaceParts> detectFaceParts(CVPixelBufferRef frame);

// Where the beauty filter works, `width` × `height` (smaller than the picture; scale it up):
// two bytes a pixel, rows top first — the skin of each face (its outline and forehead, the
// eyes, brows and lips cut out) and its teeth area, both with soft edges.
struct BeautyMask {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> pixels; // skin, teeth; skin, teeth…

    explicit operator bool() const { return width > 0 && height > 0; }
    std::uint8_t skinAt(int x, int y) const { return pixels[std::size_t(y * width + x) * 2]; }
    std::uint8_t teethAt(int x, int y) const { return pixels[std::size_t(y * width + x) * 2 + 1]; }
};
BeautyMask beautyMask(const std::vector<FaceParts>& faces, int width, int height);

// Where the subject of a frame is: the faces (all of them, or the largest when they are far
// apart), else what draws the eye, else nothing.
std::optional<FaceBox> findSubjectIn(CVPixelBufferRef frame);

// A whole file's faces, frame by frame (every frame, at most 30 a second).
std::optional<std::vector<FaceFrame>> findFaces(const std::string& path, const AnalysisProgress& progress = {},
                                                std::string* error = nullptr);

// A whole file's subject, 10 times a second.
std::optional<std::vector<SubjectSample>> findSubject(const std::string& path, const AnalysisProgress& progress = {},
                                                      std::string* error = nullptr);

// Kept beside the other analysis (AnalysisCache's folder), per media.
std::optional<std::vector<FaceFrame>> loadFaces(const AnalysisCache& cache, const std::string& fingerprint);
bool saveFaces(const AnalysisCache& cache, const std::string& fingerprint, const std::vector<FaceFrame>& frames);
std::optional<std::vector<SubjectSample>> loadSubject(const AnalysisCache& cache, const std::string& fingerprint);
bool saveSubject(const AnalysisCache& cache, const std::string& fingerprint, const std::vector<SubjectSample>& samples);

} // namespace tve
