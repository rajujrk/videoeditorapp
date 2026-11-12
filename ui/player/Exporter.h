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
        // A Live Photo (encode/LivePhoto.h): the movie (a MOV of the range) and, beside it, the
        // key photo of the timeline at this time (HEIC, livePhotoStill), paired by a new
        // content identifier; added to the Photos library too if asked (TVE_LIVE_PHOTO_LIBRARY
        // names a folder to copy them into instead, for tests).
        std::optional<tve::Ticks> livePhotoKey;
        bool addToPhotos = false;
        // Made for a size (bytes): a file that comes out larger is made again with the
        // pictures' bitrate cut by as much as it was over (encoders overshoot short or busy
        // pictures), up to three times; refused if it still does not fit.
        std::int64_t targetBytes = 0;
    };
    struct Result {
        bool ok = false;
        QString error; // empty when cancelled
    };

    // `progress` gets 0–1 after each frame (it may block, to pause); returning false cancels.
    // A failed or cancelled export removes the partial file.
    static Result run(std::shared_ptr<const tve::Project> project, const QString& path, const Options& options,
                      const std::function<bool(double)>& progress);

    // A Live Photo's key photo, beside its movie: the same name, .heic.
    static QString livePhotoStill(const QString& moviePath);

private:
    // The file itself (a Live Photo's movie before it is paired).
    static Result render(std::shared_ptr<const tve::Project> project, const QString& path, const Options& options,
                         const std::function<bool(double)>& progress);
};
