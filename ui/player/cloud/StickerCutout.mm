#include "cloud/StickerCutout.h"

#include "analysis/PeopleVision.h"

#include <QCoreApplication>
#include <QImage>

#include <CoreVideo/CoreVideo.h>

namespace {

// Enough clear pixels to be a cut-out already (2 %).
bool hasTransparency(const QImage& image)
{
    if (!image.hasAlphaChannel())
        return false;
    const QImage rgba = image.convertToFormat(QImage::Format_RGBA8888);
    qint64 clear = 0;
    for (int y = 0; y < rgba.height(); ++y) {
        const uchar* row = rgba.constScanLine(y);
        for (int x = 0; x < rgba.width(); ++x)
            if (row[x * 4 + 3] < 128)
                ++clear;
    }
    return clear * 50 >= qint64(rgba.width()) * rgba.height();
}

} // namespace

bool cutOutSticker(const QString& path, QString* error)
{
    QImage image(path);
    if (image.isNull()) {
        if (error)
            *error = QCoreApplication::translate("StickerCutout", "The picture could not be read.");
        return false;
    }
    if (hasTransparency(image))
        return true;
    // Into a pixel buffer for Vision.
    const QImage bgra = image.convertToFormat(QImage::Format_ARGB32); // BGRA in memory
    CVPixelBufferRef buffer = nullptr;
    if (CVPixelBufferCreate(kCFAllocatorDefault, std::size_t(bgra.width()), std::size_t(bgra.height()),
                            kCVPixelFormatType_32BGRA, nullptr, &buffer) != kCVReturnSuccess) {
        if (error)
            *error = QCoreApplication::translate("StickerCutout", "The picture could not be prepared.");
        return false;
    }
    CVPixelBufferLockBaseAddress(buffer, 0);
    auto* base = static_cast<uchar*>(CVPixelBufferGetBaseAddress(buffer));
    const std::size_t stride = CVPixelBufferGetBytesPerRow(buffer);
    for (int y = 0; y < bgra.height(); ++y)
        std::memcpy(base + std::size_t(y) * stride, bgra.constScanLine(y), std::size_t(bgra.width()) * 4);
    CVPixelBufferUnlockBaseAddress(buffer, 0);
    const tve::PersonMatte matte = tve::segmentSubject(buffer, tve::FaceBox{0.5, 0.5, 1.0, 1.0});
    CVPixelBufferRelease(buffer);
    if (!matte) {
        if (error)
            *error = QCoreApplication::translate("StickerCutout", "No subject stood out to cut out; the picture is kept as it is.");
        return false;
    }
    // The matte, scaled to the picture, becomes its alpha.
    QImage mask(matte.pixels.data(), matte.width, matte.height, matte.width, QImage::Format_Grayscale8);
    const QImage alpha = mask.scaled(image.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    QImage cut = image.convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < cut.height(); ++y) {
        auto* row = reinterpret_cast<QRgb*>(cut.scanLine(y));
        const uchar* a = alpha.constScanLine(y);
        for (int x = 0; x < cut.width(); ++x)
            row[x] = qRgba(qRed(row[x]), qGreen(row[x]), qBlue(row[x]), a[x]);
    }
    if (!cut.save(path, "PNG")) {
        if (error)
            *error = QCoreApplication::translate("StickerCutout", "The sticker could not be saved.");
        return false;
    }
    return true;
}
