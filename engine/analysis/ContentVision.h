#pragma once

#include "analysis/MediaAnalysis.h"
#include "model/ContentSearch.h"

#include <CoreVideo/CoreVideo.h>

#include <optional>
#include <string>

namespace tve {

// What pictures show, for smart search, on this Mac with Apple's Vision (nothing to
// download): labels from its image classifier and lines of text from its text recognizer.

// One frame: the labels sure enough to keep (kLabelThreshold; most sure first, 12 at most)
// and the lines of text, top to bottom.
ContentFrame describeFrame(CVPixelBufferRef frame, Ticks time = 0);
// A still image file (one frame at time 0); nullopt with `error` if it cannot be read.
std::optional<ContentFrame> describeImage(const std::string& path, std::string* error = nullptr);

// A media's content: a frame each second (ContentIndex::kSpacing) of a video, or the one
// picture of a still. `progress` (0..1) may cancel (nullopt, `error` empty).
std::optional<ContentIndex> analyzeContent(const std::string& path, bool still, const AnalysisProgress& progress = {},
                                           std::string* error = nullptr);

// Kept beside the other analysis, one file per media.
std::optional<ContentIndex> loadContent(const AnalysisCache& cache, const std::string& fingerprint);
bool saveContent(const AnalysisCache& cache, const std::string& fingerprint, const ContentIndex& index);

} // namespace tve
