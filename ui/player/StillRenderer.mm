#include "StillRenderer.h"

#include "FrameCompositor.h"

#include "gpu/Compositor.h"
#include "playback/TimelinePlayback.h"

#include <rhi/qrhi.h>
#include <rhi/qrhi_platform.h>

#include <chrono>
#include <thread>

struct StillRenderer::Impl {
    std::unique_ptr<QRhi> rhi;
    std::unique_ptr<FrameCompositor> compositor;
    std::unique_ptr<tve::Compositor> scaler;

    ~Impl()
    {
        // The GPU objects go before their device.
        scaler.reset();
        compositor.reset();
        rhi.reset();
    }
};

StillRenderer::StillRenderer() : d(std::make_unique<Impl>())
{
    QRhiMetalInitParams params;
    d->rhi.reset(QRhi::create(QRhi::Metal, &params));
    if (!d->rhi)
        return;
    d->compositor = std::make_unique<FrameCompositor>(d->rhi.get());
    d->scaler = std::make_unique<tve::Compositor>(d->rhi.get());
}

StillRenderer::~StillRenderer() = default;

bool StillRenderer::isValid() const
{
    return d->rhi && d->compositor && d->compositor->isValid();
}

QImage StillRenderer::render(std::shared_ptr<const tve::Project> project, tve::Ticks time, QSize size)
{
    using namespace std::chrono_literals;
    if (!isValid() || !project || size.isEmpty())
        return {};
    // The composite on screen at `time`: playback started there, its first frame.
    tve::TimelinePlayback playback;
    playback.setProject(project);
    playback.seek(time);
    tve::CompositeFrame frame{time, {}};
    const auto deadline = std::chrono::steady_clock::now() + 10s;
    for (;;) {
        const auto front = playback.queue().frontPts();
        if (front && *front <= time) {
            int skipped = 0;
            frame = std::move(*playback.queue().takeLatestDue(time, skipped));
            break;
        }
        if (front || playback.producerFinished() || std::chrono::steady_clock::now() > deadline)
            break; // nothing at that time: black
        std::this_thread::sleep_for(1ms);
    }

    QRhi* rhi = d->rhi.get();
    QRhiCommandBuffer* cb = nullptr;
    if (rhi->beginOffscreenFrame(&cb) != QRhi::FrameOpSuccess)
        return {};
    const QSize canvas(project->settings.width, project->settings.height);
    std::vector<CVMetalTextureRef> retained;
    QRhiTexture* picture = d->compositor->render(cb, frame, canvas, float(tve::toSeconds(time)), nullptr, retained);
    QRhiReadbackResult result;
    if (picture) {
        d->scaler->begin(cb, size);
        d->scaler->draw(cb, picture, tve::ClipVisual{});
        QRhiResourceUpdateBatch* readback = rhi->nextResourceUpdateBatch();
        readback->readBackTexture({d->scaler->result()}, &result);
        cb->resourceUpdate(readback);
    }
    const bool ended = rhi->endOffscreenFrame() == QRhi::FrameOpSuccess; // waits for the GPU
    for (CVMetalTextureRef texture : retained)
        CFRelease(texture);
    d->compositor->flushTextureCache();
    if (!ended || result.data.isEmpty())
        return {};

    QImage image(result.pixelSize, QImage::Format_RGB32);
    const int w = result.pixelSize.width();
    const int h = result.pixelSize.height();
    const bool half = result.format == QRhiTexture::RGBA16F;
    const qsizetype stride = result.data.size() / h;
    for (int y = 0; y < h; ++y) {
        auto* out = reinterpret_cast<QRgb*>(image.scanLine(y));
        const char* row = result.data.constData() + y * stride;
        for (int x = 0; x < w; ++x) {
            float c[4];
            if (half) {
                qFloatFromFloat16(c, reinterpret_cast<const qfloat16*>(row) + x * 4, 4);
            } else {
                const auto* p = reinterpret_cast<const std::uint8_t*>(row) + x * 4;
                for (int i = 0; i < 4; ++i)
                    c[i] = p[i] / 255.0f;
            }
            const auto byte = [](float v) { return int(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)); };
            out[x] = qRgb(byte(c[0]), byte(c[1]), byte(c[2]));
        }
    }
    return image;
}
