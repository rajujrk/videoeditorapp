#pragma once

#include "model/Project.h"

#include <QImage>
#include <QSize>

#include <memory>

// Renders single frames of a project offscreen on a GPU device of its own: the same
// timeline playback layers and FrameCompositor as the preview and export, scaled to the
// size asked for (letterboxed if its shape differs). For golden-frame tests and stills.
// Not thread-safe: keep one per thread.
class StillRenderer {
public:
    StillRenderer();
    ~StillRenderer();
    StillRenderer(const StillRenderer&) = delete;
    StillRenderer& operator=(const StillRenderer&) = delete;

    bool isValid() const;
    // The frame shown at `time`, as opaque RGB; a null image if it could not be rendered.
    QImage render(std::shared_ptr<const tve::Project> project, tve::Ticks time, QSize size);

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};
