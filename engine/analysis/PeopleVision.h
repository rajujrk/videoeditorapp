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

// The faces in a frame (picture fractions, origin top left).
std::vector<FaceBox> detectFaces(CVPixelBufferRef frame);

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
