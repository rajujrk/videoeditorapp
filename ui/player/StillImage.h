#pragma once

#include <QImage>
#include <QSize>
#include <QString>

#include <optional>

// Still images (photos, graphics) as media: PNG, JPEG, HEIC/HEIF, WebP, TIFF, BMP, a GIF of
// one frame and whatever else Qt's image readers take. Animated GIFs are left to the video
// decoder.
namespace still {

// The longest side a still is read at for the picture (larger photos are scaled down).
inline constexpr int kMaxSide = 4096;

// True when `path` is a still image this app can read.
bool isStillImage(const QString& path);

// Its size the right way up (EXIF orientation applied), if readable.
std::optional<QSize> size(const QString& path);

// The picture the right way up as RGBA8888, its longest side at most `maxSide` (0: as is).
QImage read(const QString& path, int maxSide = kMaxSide);

} // namespace still
