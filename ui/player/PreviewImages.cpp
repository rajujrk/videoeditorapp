#include "PreviewImages.h"

#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <rhi/qrhi.h>
#include <rhi/qrhi_platform.h>

#include <algorithm>
#include <cstring>
#include <vector>

namespace preview {

QImage scene(int w, int h, int variant)
{
    QImage image(w, h, QImage::Format_RGBA8888);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    const bool cool = variant == 1;
    QLinearGradient sky(0, 0, 0, h);
    sky.setColorAt(0, cool ? QColor(0x2B, 0x6C, 0xB0) : QColor(0xF2, 0x9E, 0x4C));
    sky.setColorAt(1, cool ? QColor(0x7F, 0xD1, 0xC7) : QColor(0xC2, 0x3B, 0x4E));
    painter.fillRect(image.rect(), sky);
    painter.setPen(Qt::NoPen);
    if (variant != 1) {
        painter.setBrush(QColor(0xFF, 0xE6, 0xA8));
        painter.drawEllipse(QPointF(w * 0.62, h * 0.42), h * 0.22, h * 0.22);
    }
    if (variant != 0) {
        painter.setBrush(QColor(0x1D, 0x4E, 0x3A));
        painter.drawEllipse(QPointF(w * 0.25, h * 1.05), w * 0.45, h * 0.45);
        painter.setBrush(QColor(0x2E, 0x7D, 0x4F));
        painter.drawEllipse(QPointF(w * 0.8, h * 1.1), w * 0.5, h * 0.5);
    } else {
        painter.setBrush(QColor(0x3A, 0x1C, 0x32));
        painter.drawRect(QRectF(0, h * 0.78, w, h * 0.22));
    }
    if (variant == 2) {
        // A small house on the left hill: straight edges and a lit window.
        const QRectF wall(w * 0.16, h * 0.56, w * 0.14, h * 0.2);
        painter.setBrush(QColor(0xF4, 0xEF, 0xE6));
        painter.drawRect(wall);
        QPainterPath roof;
        roof.moveTo(wall.left() - w * 0.02, wall.top());
        roof.lineTo(wall.center().x(), wall.top() - h * 0.12);
        roof.lineTo(wall.right() + w * 0.02, wall.top());
        roof.closeSubpath();
        painter.setBrush(QColor(0xB2, 0x3A, 0x2E));
        painter.drawPath(roof);
        painter.setBrush(QColor(0xFF, 0xC8, 0x4A));
        painter.drawRect(QRectF(wall.left() + wall.width() * 0.2, wall.top() + wall.height() * 0.3, wall.width() * 0.25,
                                wall.height() * 0.3));
    }
    return image;
}

QImage fromReadback(const QRhiReadbackResult& result, int w, int h)
{
    QImage image(w, h, QImage::Format_RGBA8888);
    if (result.format == QRhiTexture::RGBA16F) {
        std::vector<float> rgba(std::size_t(w * h * 4));
        qFloatFromFloat16(rgba.data(), reinterpret_cast<const qfloat16*>(result.data.constData()), qsizetype(rgba.size()));
        for (int y = 0; y < h; ++y) {
            uchar* line = image.scanLine(y);
            for (int x = 0; x < w * 4; ++x)
                line[x] = uchar(std::clamp(rgba[std::size_t(y * w * 4 + x)], 0.0f, 1.0f) * 255.0f + 0.5f);
        }
    } else {
        std::memcpy(image.bits(), result.data.constData(), std::min<qsizetype>(image.sizeInBytes(), result.data.size()));
    }
    return image;
}

QRhi* offscreenRhi()
{
    static thread_local std::unique_ptr<QRhi> rhi = [] {
        QRhiMetalInitParams params;
        return std::unique_ptr<QRhi>(QRhi::create(QRhi::Metal, &params));
    }();
    return rhi.get();
}

} // namespace preview
