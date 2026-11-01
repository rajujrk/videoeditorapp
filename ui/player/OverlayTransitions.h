#pragma once

#include "model/Project.h"

#include <QImage>
#include <QQuickImageProvider>
#include <QString>

#include <string>
#include <vector>

// Overlay transitions: short clips with transparency laid over a cut, on a track above it —
// light leaks, film burns and paint strokes. Each is drawn by our own code, frame by frame,
// and made once into a ProRes 4444 movie with an alpha channel, named for its title, in the
// overlay cache (TVE_OVERLAY_CACHE_DIR, or "Overlay cache" in the app's data folder).
namespace overlays {

struct Kind {
    std::string id;    // e.g. "leak.warm"
    std::string title; // e.g. "Warm light leak"
    tve::BlendMode blend = tve::BlendMode::Normal; // how it goes over the picture
};
const std::vector<Kind>& kinds();
const Kind* find(const std::string& id);

inline constexpr double kSeconds = 1.2;
inline constexpr int kFramesPerSecond = 30;
inline constexpr int kWidth = 1280;
inline constexpr int kHeight = 720;

// One frame, `t` (0..1) of the way through, `size` big, straight-alpha RGBA8888.
QImage frame(const std::string& id, double t, QSize size);

QString cacheDirectory();
// The overlay's movie, made into the cache the first time; empty with `error` set if it
// could not be.
QString movie(const std::string& id, QString* error = nullptr);

} // namespace overlays

// "image://tveoverlay/<id>@<t>": a frame of an overlay over a sample picture, for the library.
class OverlayPreviewProvider : public QQuickImageProvider {
public:
    static constexpr const char* kId = "tveoverlay";

    OverlayPreviewProvider();
    QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override;
};
