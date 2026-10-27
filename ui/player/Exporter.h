#pragma once

#include "encode/Mp4Writer.h"
#include "model/Project.h"

#include <QString>

#include <functional>
#include <memory>

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
    // export removes the partial file.
    static Result run(std::shared_ptr<const tve::Project> project, const QString& path, const tve::Mp4Settings& settings,
                      const std::function<bool(double)>& progress);
};
