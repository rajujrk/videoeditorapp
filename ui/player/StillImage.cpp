#include "StillImage.h"

#include <QImageReader>

namespace still {

bool isStillImage(const QString& path)
{
    QImageReader reader(path);
    if (!reader.canRead())
        return false;
    // Several frames that play (an animated GIF or WebP) are video, not a still.
    return !(reader.supportsAnimation() && reader.imageCount() > 1);
}

std::optional<QSize> size(const QString& path)
{
    QImageReader reader(path);
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
    reader.setAutoTransform(true);
    QImage image = reader.read();
    if (image.isNull())
        return {};
    if (maxSide > 0 && std::max(image.width(), image.height()) > maxSide)
        image = image.scaled(maxSide, maxSide, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    return image.convertToFormat(QImage::Format_RGBA8888);
}

} // namespace still
