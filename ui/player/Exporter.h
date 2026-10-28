#pragma once

#include "encode/Mp4Writer.h"
#include "model/Project.h"

#include <QString>

#include <functional>
#include <memory>
#include <optional>

// Renders a project to an MP4 file, frame by frame at the project's frame rate: the same
// timeline playback and FrameCompositor as the preview, unpaced, on an offscreen GPU
// device; scaled to the output size (letterboxed if its shape differs); converted to NV12
// on the GPU; encoded by Mp4Writer with the mixed sound. Blocks; call it off the UI thread.
class Exporter {
public:
    struct Result {
        bool ok = false;
        QString error; // empty when cancelled
    };

    // `progress` gets 0–1 after each frame; returning false cancels. A failed or cancelled
    // export removes the partial file. With `loudnessTarget` (LUFS) the mix is measured
    // first and its volume set to reach it, with a limiter at -1 dBFS for the peaks.
    static Result run(std::shared_ptr<const tve::Project> project, const QString& path, const tve::Mp4Settings& settings,
                      const std::function<bool(double)>& progress, std::optional<double> loudnessTarget = std::nullopt);
};
