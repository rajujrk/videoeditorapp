#pragma once

#include "analysis/MediaAnalysis.h"
#include "model/Motion.h"

#include <functional>
#include <string>
#include <vector>

namespace tve {

// Processed copies of part of a video (its pictures only, from `from` to `to` in media time),
// written as high-quality HEVC that a clip then plays instead of its media there:
// upscaled and smooth slow motion with Apple's VideoToolbox frame processors, and an object
// removed by our own fill. Each returns false with `error` on failure (empty when
// `progress` cancelled); nothing is left at `output` then.

// Apple's upscaling model, which macOS downloads when asked.
enum class UpscaleModel { Unsupported, NeedsDownload, Downloading, Ready };
UpscaleModel upscaleModelStatus();
// Starts the download; `done` is called (on another thread) when it ends.
void downloadUpscaleModel(std::function<void(bool ok, std::string error)> done);
// How much of it is here, 0..1.
double upscaleModelProgress();

// 2 or 4 times as wide and high (pictures up to 1920 × 1080; no side beyond 7680).
bool upscaleVideo(const std::string& path, Ticks from, Ticks to, int scale, const std::string& output,
                  const AnalysisProgress& progress = {}, std::string* error = nullptr);

// `factor` (2..8) frames for every one: the ones between made by frame-rate conversion, so
// the copy's frame rate is `factor` times the source's and slowed down it stays smooth.
bool smoothVideo(const std::string& path, Ticks from, Ticks to, int factor, const std::string& output,
                 const AnalysisProgress& progress = {}, std::string* error = nullptr);

// The object inside the track's box (grown by `expand`, a fraction of its size) taken out:
// each of its pixels filled with what other frames show there once the camera's motion is
// allowed for (the median of up to seven), and what no frame shows filled in from around it.
bool removeObject(const std::string& path, Ticks from, Ticks to, const std::vector<TrackSample>& object,
                  double expand, const std::string& output, const AnalysisProgress& progress = {},
                  std::string* error = nullptr);

} // namespace tve
