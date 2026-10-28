#pragma once

#include <QImage>

#include <memory>

class QRhi;
struct QRhiReadbackResult;

// Shared by the library's GPU-rendered previews (transitions, effects).
namespace preview {

// A small original sample scene: a warm sunset with a sun (`variant` 0), or cool hills (1);
// variant 2 has both, with a house, for effects that need detail.
QImage scene(int width, int height, int variant);

// A read-back texture (RGBA8 or RGBA16F) as an 8-bit RGBA image.
QImage fromReadback(const QRhiReadbackResult& result, int width, int height);

// One offscreen Metal device for the calling thread's previews, made on first use.
QRhi* offscreenRhi();

} // namespace preview
