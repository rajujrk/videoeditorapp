#include "StillImage.h"

#include "decode/ImageFile.h"

#include <QImageReader>

namespace still {

namespace {

// Pictures Qt does not read (or reads wrongly) go to ImageIO: camera RAW first, since Qt's
// TIFF reader would take a DNG's small preview for the photo, then whatever Qt cannot read
// (OpenEXR…).
std::optional<tve::imagefile::Info> imageIO(const QString& path, const QImageReader& reader)
{
    auto info = tve::imagefile::probe(path.toStdString());
    if (!info || info->count != 1)
        return std::nullopt;
    if (info->raw || !reader.canRead())
        return info;
    return std::nullopt;
}

bool isVector(const QImageReader& reader)
{
    const QByteArray format = reader.format();
    return format == "svg" || format == "svgz";
}

// The size to draw at: `natural` fitted inside `maxSide` (0: as is). A drawing (SVG) has
// no pixels of its own, so it is drawn as large as allowed, not at its nominal size.
QSize fitted(QSize natural, int maxSide, bool vector)
{
    if (natural.isEmpty())
        return natural;
    const int side = maxSide > 0 ? maxSide : vector ? kMaxSide : 0;
    if (side > 0 && (vector || std::max(natural.width(), natural.height()) > side))
        natural.scale(side, side, Qt::KeepAspectRatio);
    return natural.expandedTo(QSize(1, 1));
}

} // namespace

bool isStillImage(const QString& path)
{
    QImageReader reader(path);
    if (imageIO(path, reader))
        return true;
    if (!reader.canRead())
        return false;
    // Several frames that play (an animated GIF or WebP) are video, not a still.
    return !(reader.supportsAnimation() && reader.imageCount() > 1);
}

std::optional<QSize> size(const QString& path)
{
    QImageReader reader(path);
    if (const auto info = imageIO(path, reader))
        return QSize(info->width, info->height);
    reader.setAutoTransform(true);
    if (!reader.canRead())
        return std::nullopt;
    QSize size = reader.size();
    if (!size.isValid())
        return std::nullopt;
    // Rotated a quarter turn either way, width and height trade places.
    if (reader.transformation() & QImageIOHandler::TransformationRotate90)
        size.transpose();
    return size;
}

QImage read(const QString& path, int maxSide)
{
    QImageReader reader(path);
    if (const auto info = imageIO(path, reader)) {
        const QSize target = fitted(QSize(info->width, info->height), maxSide, false);
        QImage image(target, QImage::Format_RGBA8888);
        if (image.isNull()
            || !tve::imagefile::decodeInto(path.toStdString(), target.width(), target.height(),
                                           tve::imagefile::Layout::Rgba, image.bits(), std::size_t(image.bytesPerLine())))
            return {};
        return image;
    }
    reader.setAutoTransform(true);
    const bool vector = isVector(reader);
    if (vector && reader.size().isValid())
        reader.setScaledSize(fitted(reader.size(), maxSide, true));
    QImage image = reader.read();
    if (image.isNull())
        return {};
    if (maxSide > 0 && std::max(image.width(), image.height()) > maxSide)
        image = image.scaled(maxSide, maxSide, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    return image.convertToFormat(QImage::Format_RGBA8888);
}

} // namespace still
