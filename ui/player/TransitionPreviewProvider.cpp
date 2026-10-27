#include "TransitionPreviewProvider.h"

#include "gpu/TransitionRenderer.h"

#include <QCache>
#include <QLinearGradient>
#include <QMutex>
#include <QPainter>
#include <rhi/qrhi.h>
#include <rhi/qrhi_platform.h>

#include <memory>

namespace {

constexpr int kW = TransitionPreviewProvider::kWidth;
constexpr int kH = TransitionPreviewProvider::kHeight;

// The outgoing sample: warm, with a sun; the incoming one: cool, with hills.
QImage samplePicture(bool incoming)
{
    QImage image(kW, kH, QImage::Format_RGBA8888);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    QLinearGradient sky(0, 0, 0, kH);
    sky.setColorAt(0, incoming ? QColor(0x2B, 0x6C, 0xB0) : QColor(0xF2, 0x9E, 0x4C));
    sky.setColorAt(1, incoming ? QColor(0x7F, 0xD1, 0xC7) : QColor(0xC2, 0x3B, 0x4E));
    painter.fillRect(image.rect(), sky);
    painter.setPen(Qt::NoPen);
    if (incoming) {
        painter.setBrush(QColor(0x1D, 0x4E, 0x3A));
        painter.drawEllipse(QPointF(kW * 0.25, kH * 1.05), kW * 0.45, kH * 0.45);
        painter.setBrush(QColor(0x2E, 0x7D, 0x4F));
        painter.drawEllipse(QPointF(kW * 0.8, kH * 1.1), kW * 0.5, kH * 0.5);
    } else {
        painter.setBrush(QColor(0xFF, 0xE6, 0xA8));
        painter.drawEllipse(QPointF(kW * 0.62, kH * 0.42), kH * 0.22, kH * 0.22);
        painter.setBrush(QColor(0x3A, 0x1C, 0x32));
        painter.drawRect(QRectF(0, kH * 0.78, kW, kH * 0.22));
    }
    return image;
}

std::optional<QImage> render(QRhi* rhi, const std::string& id, float progress)
{
    static const QImage from = samplePicture(false);
    static const QImage to = samplePicture(true);
    std::unique_ptr<QRhiTexture> fromTexture(rhi->newTexture(QRhiTexture::RGBA8, from.size()));
    std::unique_ptr<QRhiTexture> toTexture(rhi->newTexture(QRhiTexture::RGBA8, to.size()));
    if (!fromTexture->create() || !toTexture->create())
        return std::nullopt;

    tve::TransitionRenderer renderer(rhi, tve::EffectLibrary::builtIn());
    QRhiCommandBuffer* cb = nullptr;
    if (rhi->beginOffscreenFrame(&cb) != QRhi::FrameOpSuccess)
        return std::nullopt;
    QRhiResourceUpdateBatch* upload = rhi->nextResourceUpdateBatch();
    upload->uploadTexture(fromTexture.get(), from);
    upload->uploadTexture(toTexture.get(), to);
    cb->resourceUpdate(upload);
    renderer.begin();
    QRhiTexture* output = renderer.render(cb, id, {}, progress, fromTexture.get(), toTexture.get(), 0.0f);
    QRhiReadbackResult result;
    if (output) {
        QRhiResourceUpdateBatch* readback = rhi->nextResourceUpdateBatch();
        readback->readBackTexture({output}, &result);
        cb->resourceUpdate(readback);
    }
    if (rhi->endOffscreenFrame() != QRhi::FrameOpSuccess || !output)
        return std::nullopt;

    QImage image(kW, kH, QImage::Format_RGBA8888);
    if (result.format == QRhiTexture::RGBA16F) {
        std::vector<float> rgba(std::size_t(kW * kH * 4));
        qFloatFromFloat16(rgba.data(), reinterpret_cast<const qfloat16*>(result.data.constData()), qsizetype(rgba.size()));
        for (int y = 0; y < kH; ++y) {
            uchar* line = image.scanLine(y);
            for (int x = 0; x < kW * 4; ++x)
                line[x] = uchar(std::clamp(rgba[std::size_t(y * kW * 4 + x)], 0.0f, 1.0f) * 255.0f + 0.5f);
        }
    } else {
        std::memcpy(image.bits(), result.data.constData(), std::min<qsizetype>(image.sizeInBytes(), result.data.size()));
    }
    return image;
}

} // namespace

TransitionPreviewProvider::TransitionPreviewProvider() : QQuickImageProvider(QQuickImageProvider::Image,
                                                                             ForceAsynchronousImageLoading)
{
}

QImage TransitionPreviewProvider::requestImage(const QString& id, QSize* size, const QSize&)
{
    static QMutex mutex;
    static QCache<QString, QImage> cache(400);
    QMutexLocker lock(&mutex);
    if (const QImage* cached = cache.object(id)) {
        *size = cached->size();
        return *cached;
    }

    // <transition id>@<progress>
    const qsizetype at = id.lastIndexOf(QLatin1Char('@'));
    const std::string transitionId = id.left(at).toStdString();
    const float progress = at < 0 ? 0.5f : id.mid(at + 1).toFloat();

    // One offscreen Metal device for all previews, made on first use in this thread.
    static std::unique_ptr<QRhi> rhi = [] {
        QRhiMetalInitParams params;
        return std::unique_ptr<QRhi>(QRhi::create(QRhi::Metal, &params));
    }();
    const auto image = rhi ? render(rhi.get(), transitionId, progress) : std::nullopt;
    if (!image)
        return {};
    cache.insert(id, new QImage(*image));
    *size = image->size();
    return *image;
}
