#pragma once

#include "encode/MediaWriter.h"
#include "model/Project.h"

#include <QString>

#include <functional>
#include <memory>
#include <optional>

// Renders a project (or part of it) to a video or sound file, frame by frame at the file's
// frame rate: the same timeline playback and FrameCompositor as the preview, unpaced, on an
// offscreen GPU device; scaled to the output size (letterboxed if its shape differs, or
// filled and cropped); converted on the GPU to NV12 (H.264, HEVC) or read back as RGBA
// (ProRes, GIF); written by MediaWriter with the mixed sound. Sound-only files skip the
// pictures. Blocks; call it off the UI thread.
class Exporter {
public:
    struct Options {
        tve::MediaSettings settings;
        tve::Ticks from = 0; // the range of the timeline; `to` 0 (or past the end): to the end
        tve::Ticks to = 0;
        bool fill = false; // another shape than the canvas: fill it and crop, rather than letterbox
        // Measure the range's mix first and set its volume to reach this (LUFS), with a
        // limiter at -1 dBFS for the peaks.
        std::optional<double> loudnessTarget;
    };
    struct Result {
        bool ok = false;
        QString error; // empty when cancelled
    };

    // `progress` gets 0–1 after each frame (it may block, to pause); returning false cancels.
    // A failed or cancelled export removes the partial file.
    static Result run(std::shared_ptr<const tve::Project> project, const QString& path, const Options& options,
                      const std::function<bool(double)>& progress);
};
