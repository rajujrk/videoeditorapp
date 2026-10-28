#include "TransitionPreviewProvider.h"

#include "PreviewImages.h"

#include "gpu/TransitionRenderer.h"

#include <QCache>
#include <QMutex>
#include <rhi/qrhi.h>

#include <memory>

namespace {

constexpr int kW = TransitionPreviewProvider::kWidth;
constexpr int kH = TransitionPreviewProvider::kHeight;

std::optional<QImage> render(QRhi* rhi, const std::string& id, float progress)
{
    // The outgoing sample: warm, with a sun; the incoming one: cool, with hills.
    static const QImage from = preview::scene(kW, kH, 0);
    static const QImage to = preview::scene(kW, kH, 1);
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

    return preview::fromReadback(result, kW, kH);
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

    QRhi* rhi = preview::offscreenRhi();
    const auto image = rhi ? render(rhi, transitionId, progress) : std::nullopt;
    if (!image)
        return {};
    cache.insert(id, new QImage(*image));
    *size = image->size();
    return *image;
}
