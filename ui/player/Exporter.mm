#include "Exporter.h"

#include "FrameCompositor.h"

#include "decode/AudioDecoder.h"
#include "gpu/Compositor.h"
#include "playback/TimelineAudio.h"
#include "playback/TimelinePlayback.h"

#include <QFile>
#include <rhi/qrhi.h>
#include <rhi/qrhi_platform.h>

#include <chrono>
#include <cmath>
#include <cstring>
#include <thread>

namespace {

using namespace std::chrono_literals;

QShader loadShader(const QString& name)
{
    QFile file(name);
    return file.open(QIODevice::ReadOnly) ? QShader::fromSerialized(file.readAll()) : QShader();
}

// One NV12 plane: a render target of the plane's size and format, drawn from the finished
// picture by export_yuv.frag.
struct PlanePass {
    std::unique_ptr<QRhiTexture> texture;
    std::unique_ptr<QRhiTextureRenderTarget> target;
    std::unique_ptr<QRhiRenderPassDescriptor> renderPass;
    std::unique_ptr<QRhiBuffer> uniforms;
    std::unique_ptr<QRhiShaderResourceBindings> bindings;
    std::unique_ptr<QRhiGraphicsPipeline> pipeline;
    QRhiTexture* boundInput = nullptr;

    bool create(QRhi* rhi, QRhiTexture::Format format, QSize size)
    {
        texture.reset(rhi->newTexture(format, size, 1, QRhiTexture::RenderTarget | QRhiTexture::UsedAsTransferSource));
        if (!texture->create())
            return false;
        target.reset(rhi->newTextureRenderTarget({texture.get()}));
        renderPass.reset(target->newCompatibleRenderPassDescriptor());
        target->setRenderPassDescriptor(renderPass.get());
        uniforms.reset(rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 32));
        bindings.reset(rhi->newShaderResourceBindings());
        return target->create() && uniforms->create();
    }

    bool bind(QRhi* rhi, QRhiTexture* input, QRhiSampler* sampler, const QShader& vertex, const QShader& fragment)
    {
        if (pipeline && boundInput == input)
            return true;
        bindings->setBindings({
            QRhiShaderResourceBinding::uniformBuffer(
                0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage, uniforms.get()),
            QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage, input, sampler),
        });
        if (!bindings->create())
            return false;
        boundInput = input;
        if (!pipeline) {
            pipeline.reset(rhi->newGraphicsPipeline());
            pipeline->setTopology(QRhiGraphicsPipeline::TriangleStrip);
            pipeline->setShaderStages({{QRhiShaderStage::Vertex, vertex}, {QRhiShaderStage::Fragment, fragment}});
            pipeline->setVertexInputLayout({});
            pipeline->setShaderResourceBindings(bindings.get());
            pipeline->setRenderPassDescriptor(renderPass.get());
            if (!pipeline->create()) {
                pipeline.reset();
                return false;
            }
        }
        return true;
    }

    void draw(QRhi* rhi, QRhiCommandBuffer* cb, int plane)
    {
        const QSize size = texture->pixelSize();
        struct {
            float texelSize[2];
            float time;
            float flipY;
            std::int32_t plane;
            float padding[3];
        } u{{1.0f / float(size.width()), 1.0f / float(size.height())}, 0.0f, rhi->isYUpInNDC() ? 1.0f : 0.0f, plane, {}};
        QRhiResourceUpdateBatch* updates = rhi->nextResourceUpdateBatch();
        updates->updateDynamicBuffer(uniforms.get(), 0, sizeof u, &u);
        cb->beginPass(target.get(), Qt::black, {1.0f, 0}, updates);
        cb->setGraphicsPipeline(pipeline.get());
        cb->setViewport({0, 0, float(size.width()), float(size.height())});
        cb->setShaderResources(bindings.get());
        cb->draw(4);
        cb->endPass();
    }
};

// Copies a readback into rows of `width` bytes (the readback may pad its rows).
std::vector<std::uint8_t> packedRows(const QRhiReadbackResult& result, int width, int height)
{
    std::vector<std::uint8_t> rows(std::size_t(width) * std::size_t(height));
    const qsizetype stride = height > 0 ? result.data.size() / height : 0;
    for (int y = 0; y < height && stride >= width; ++y)
        std::memcpy(rows.data() + std::size_t(y) * std::size_t(width), result.data.constData() + y * stride, std::size_t(width));
    return rows;
}

} // namespace

Exporter::Result Exporter::run(std::shared_ptr<const tve::Project> project, const QString& path,
                               const tve::Mp4Settings& settings, const std::function<bool(double)>& progress)
{
    Result result;
    std::string error;
    auto writer = tve::Mp4Writer::open(path.toStdString(), settings, &error);
    if (!writer) {
        result.error = QString::fromStdString(error);
        return result;
    }
    const auto abandon = [&](const QString& message) {
        writer.reset();
        QFile::remove(path);
        result.error = message;
        return result;
    };

    QRhiMetalInitParams params;
    std::unique_ptr<QRhi> rhi(QRhi::create(QRhi::Metal, &params));
    if (!rhi)
        return abandon(QStringLiteral("cannot use the GPU"));
    // Members of the GPU objects must go before the device, so keep them in this scope.
    {
        FrameCompositor compositor(rhi.get());
        if (!compositor.isValid())
            return abandon(QStringLiteral("cannot set up the renderer"));
        const QSize canvas(project->settings.width, project->settings.height);
        const QSize output(settings.width, settings.height);
        tve::Compositor scaler(rhi.get());
        const QShader vertex = loadShader(QStringLiteral(":/effects/common/fullscreen.vert.qsb"));
        const QShader fragment = loadShader(QStringLiteral(":/shaders/export_yuv.frag.qsb"));
        std::unique_ptr<QRhiSampler> sampler(rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None,
                                                             QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
        PlanePass luma;
        PlanePass chroma;
        if (!sampler->create() || !vertex.isValid() || !fragment.isValid()
            || !luma.create(rhi.get(), QRhiTexture::R8, output)
            || !chroma.create(rhi.get(), QRhiTexture::RG8, output / 2))
            return abandon(QStringLiteral("cannot set up the renderer"));

        // Pictures: the timeline playback engine, never started, produces every frame
        // as fast as it can. Sound: the mixer, likewise.
        tve::TimelinePlayback playback;
        playback.setProject(project);
        tve::TimelineAudio mixer;
        mixer.restart(project, 0);

        const tve::Ticks frameTicks = tve::frameDuration(settings.frameRate);
        const tve::Ticks duration = project->duration();
        const std::int64_t totalFrames = std::max<std::int64_t>(1, (duration + frameTicks - 1) / frameTicks);
        tve::CompositeFrame current{0, {}};
        std::int64_t samplesWritten = 0;
        std::vector<float> silence;

        for (std::int64_t n = 0; n < totalFrames; ++n) {
            const tve::Ticks time = n * frameTicks;
            // The newest composite at or before this frame's time (a gap keeps its black).
            for (;;) {
                const auto front = playback.queue().frontPts();
                if (front && *front <= time) {
                    int skipped = 0;
                    current = std::move(*playback.queue().takeLatestDue(time, skipped));
                    continue;
                }
                if (front || playback.producerFinished())
                    break;
                std::this_thread::sleep_for(1ms);
            }

            QRhiCommandBuffer* cb = nullptr;
            if (rhi->beginOffscreenFrame(&cb) != QRhi::FrameOpSuccess)
                return abandon(QStringLiteral("the GPU stopped responding"));
            std::vector<CVMetalTextureRef> retained;
            QRhiTexture* picture = compositor.render(cb, current, canvas, float(tve::toSeconds(time)), nullptr, retained);
            if (picture && output != canvas) {
                scaler.begin(cb, output);
                scaler.draw(cb, picture, tve::ClipVisual{});
                picture = scaler.result();
            }
            QRhiReadbackResult lumaData;
            QRhiReadbackResult chromaData;
            if (picture && luma.bind(rhi.get(), picture, sampler.get(), vertex, fragment)
                && chroma.bind(rhi.get(), picture, sampler.get(), vertex, fragment)) {
                luma.draw(rhi.get(), cb, 0);
                chroma.draw(rhi.get(), cb, 1);
                QRhiResourceUpdateBatch* readback = rhi->nextResourceUpdateBatch();
                readback->readBackTexture({luma.texture.get()}, &lumaData);
                readback->readBackTexture({chroma.texture.get()}, &chromaData);
                cb->resourceUpdate(readback);
            }
            const bool ended = rhi->endOffscreenFrame() == QRhi::FrameOpSuccess; // waits for the GPU
            for (CVMetalTextureRef texture : retained)
                CFRelease(texture);
            compositor.flushTextureCache();
            if (!ended || lumaData.data.isEmpty() || chromaData.data.isEmpty())
                return abandon(QStringLiteral("rendering frame %1 failed").arg(n));

            const auto y = packedRows(lumaData, output.width(), output.height());
            const auto uv = packedRows(chromaData, output.width(), output.height() / 2); // width/2 × 2 bytes
            if (!writer->writeVideo(y.data(), output.width(), uv.data(), output.width()))
                return abandon(QString::fromStdString(writer->lastError()));

            // Sound up to the end of this frame.
            const std::int64_t wanted = tve::audio::samplesFromTicks((n + 1) * frameTicks);
            while (samplesWritten < wanted) {
                if (auto chunk = mixer.queue().tryPop()) {
                    if (!writer->writeAudio(chunk->samples.data(), tve::AudioChunk::kFrames))
                        return abandon(QString::fromStdString(writer->lastError()));
                    samplesWritten += tve::AudioChunk::kFrames;
                } else if (mixer.producerFinished() && mixer.queue().size() == 0) {
                    const int frames = int(wanted - samplesWritten);
                    silence.assign(std::size_t(frames) * tve::audio::kChannels, 0.0f);
                    if (!writer->writeAudio(silence.data(), frames))
                        return abandon(QString::fromStdString(writer->lastError()));
                    samplesWritten = wanted;
                } else {
                    std::this_thread::sleep_for(1ms);
                }
            }

            if (progress && !progress(double(n + 1) / double(totalFrames)))
                return abandon(QString()); // cancelled
        }
    }
    if (!writer->finish())
        return abandon(QString::fromStdString(writer->lastError()));
    result.ok = true;
    return result;
}
