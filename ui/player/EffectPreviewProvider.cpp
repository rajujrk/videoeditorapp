#include "EffectPreviewProvider.h"

#include "PreviewImages.h"

#include "gpu/EffectChain.h"

#include <QCache>
#include <QMutex>
#include <QPainter>
#include <rhi/qrhi.h>

#include <memory>
#include <optional>

namespace {

constexpr int kW = EffectPreviewProvider::kWidth;
constexpr int kH = EffectPreviewProvider::kHeight;
// Where a canvas effect's clip sits in its preview.
constexpr tve::EffectBounds kInset{0.2f, 0.2f, 0.8f, 0.8f};

std::optional<QImage> render(QRhi* rhi, const tve::LoadedEffect& effect, float seconds)
{
    const bool canvas = effect.manifest.space == tve::EffectSpace::Canvas;
    QImage input = preview::scene(kW, kH, 2);
    if (canvas) {
        QImage placed(kW, kH, QImage::Format_RGBA8888);
        placed.fill(Qt::transparent);
        QPainter painter(&placed);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.drawImage(QRectF(kW * kInset[0], kH * kInset[1], kW * (kInset[2] - kInset[0]), kH * (kInset[3] - kInset[1])),
                          input);
        painter.end();
        input = placed;
    }
    std::unique_ptr<QRhiTexture> source(rhi->newTexture(QRhiTexture::RGBA8, input.size()));
    if (!source->create())
        return std::nullopt;
    tve::EffectChain chain(rhi, tve::EffectLibrary::builtIn());
    chain.setInstances({tve::EffectInstance{effect.manifest.id, true, {}}});
    QRhiCommandBuffer* cb = nullptr;
    if (rhi->beginOffscreenFrame(&cb) != QRhi::FrameOpSuccess)
        return std::nullopt;
    QRhiResourceUpdateBatch* upload = rhi->nextResourceUpdateBatch();
    upload->uploadTexture(source.get(), input);
    cb->resourceUpdate(upload);
    QRhiTexture* output = chain.process(cb, source.get(), seconds, canvas ? kInset : tve::kWholeImage);
    QRhiReadbackResult result;
    QRhiResourceUpdateBatch* readback = rhi->nextResourceUpdateBatch();
    readback->readBackTexture({output}, &result);
    cb->resourceUpdate(readback);
    if (rhi->endOffscreenFrame() != QRhi::FrameOpSuccess)
        return std::nullopt;
    QImage image = preview::fromReadback(result, kW, kH);
    if (canvas) {
        // On a mid-grey canvas, so dark shadows and light outlines both show.
        QImage shown(kW, kH, QImage::Format_RGBA8888);
        shown.fill(QColor(0x6B, 0x70, 0x7A));
        QPainter painter(&shown);
        painter.drawImage(0, 0, image);
        painter.end();
        image = shown;
    }
    return image;
}

} // namespace

EffectPreviewProvider::EffectPreviewProvider() : QQuickImageProvider(QQuickImageProvider::Image, ForceAsynchronousImageLoading)
{
}

QImage EffectPreviewProvider::requestImage(const QString& id, QSize* size, const QSize&)
{
    static QMutex mutex;
    static QCache<QString, QImage> cache(600);
    QMutexLocker lock(&mutex);
    if (const QImage* cached = cache.object(id)) {
        *size = cached->size();
        return *cached;
    }
    // <effect id>@<seconds>
    const qsizetype at = id.lastIndexOf(QLatin1Char('@'));
    const std::string effectId = id.left(at).toStdString();
    const float seconds = at < 0 ? 0.0f : id.mid(at + 1).toFloat();
    const tve::LoadedEffect* effect = tve::EffectLibrary::builtIn().find(effectId);
    QRhi* rhi = preview::offscreenRhi();
    const auto image = effect && rhi ? render(rhi, *effect, seconds) : std::nullopt;
    if (!image)
        return {};
    cache.insert(id, new QImage(*image));
    *size = image->size();
    return *image;
}
