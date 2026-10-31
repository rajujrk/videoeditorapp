#pragma once

#include "analysis/MediaAnalysis.h"
#include "model/Motion.h"

#include <optional>
#include <string>

namespace tve {

// A file's camera motion and brightness, frame by frame (core/model/Motion.h), measured on
// its pictures scaled to 180 pixels high (box-filtered from 360, so sub-pixel shifts show).
std::optional<MotionAnalysis> analyzeMotion(const std::string& path, const AnalysisProgress& progress = {},
                                            std::string* error = nullptr);

// Kept beside the other analysis (AnalysisCache's folder), per media.
std::optional<MotionAnalysis> loadMotion(const AnalysisCache& cache, const std::string& fingerprint);
bool saveMotion(const AnalysisCache& cache, const std::string& fingerprint, const MotionAnalysis& analysis);

} // namespace tve
