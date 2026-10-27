#include "VideoView.h"

#include "playback/PlaybackSession.h"

#include <QDateTime>
#include <QFile>
#include <QMatrix4x4>
#include <QUrl>
#include <rhi/qrhi.h>
#include <rhi/qrhi_platform.h>

#import <CoreVideo/CVMetalTextureCache.h>
#import <Metal/Metal.h>

#include <algorithm>
#include <array>
#include <chrono>

namespace {

QShader loadShader(const QString& name)
{
    QFile file(name);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning("VideoView: missing shader %s", qPrintable(name));
        return {};
    }
    return QShader::fromSerialized(file.readAll());
}

struct PlaneFormats {
    bool supported = false;
    bool fullRange = false;
    QRhiTexture::Format luma = QRhiTexture::R8;
    QRhiTexture::Format chroma = QRhiTexture::RG8;
    MTLPixelFormat metalLuma = MTLPixelFormatR8Unorm;
    MTLPixelFormat metalChroma = MTLPixelFormatRG8Unorm;
};

PlaneFormats planeFormats(OSType pixelFormat)
{
    switch (pixelFormat) {
    case kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange:
        return {.supported = true};
    case kCVPixelFormatType_420YpCbCr8BiPlanarFullRange:
        return {.supported = true, .fullRange = true};
    case kCVPixelFormatType_420YpCbCr10BiPlanarVideoRange:
    case kCVPixelFormatType_420YpCbCr10BiPlanarFullRange:
        // 10-bit samples sit in the high bits of 16-bit words, so normalized values
        // match the 8-bit ones closely enough for the same matrix.
        return {.supported = true,
                .fullRange = pixelFormat == kCVPixelFormatType_420YpCbCr10BiPlanarFullRange,
                .luma = QRhiTexture::R16,
                .chroma = QRhiTexture::RG16,
                .metalLuma = MTLPixelFormatR16Unorm,
                .metalChroma = MTLPixelFormatRG16Unorm};
    default:
        return {};
    }
}

// Maps normalized (Y, Cb, Cr, 1) samples to RGB, folding the range expansion in.
QMatrix4x4 yuvToRgbMatrix(CVPixelBufferRef buffer, bool fullRange)
{
    double kr = 0.2126;
    double kb = 0.0722; // BT.709 unless the buffer says otherwise
    if (CFTypeRef matrix = CVBufferCopyAttachment(buffer, kCVImageBufferYCbCrMatrixKey, nullptr)) {
        if (CFEqual(matrix, kCVImageBufferYCbCrMatrix_ITU_R_601_4)) {
            kr = 0.299;
            kb = 0.114;
        } else if (CFEqual(matrix, kCVImageBufferYCbCrMatrix_ITU_R_2020)) {
            kr = 0.2627;
            kb = 0.0593;
        }
        CFRelease(matrix);
    }
    const double kg = 1.0 - kr - kb;

    const double ys = fullRange ? 1.0 : 255.0 / 219.0;
    const double yo = fullRange ? 0.0 : -16.0 / 219.0;
    const double cs = fullRange ? 1.0 : 255.0 / 224.0;
    const double co = fullRange ? -128.0 / 255.0 : -128.0 / 224.0;

    const double rCr = 2.0 * (1.0 - kr);
    const double bCb = 2.0 * (1.0 - kb);
    const double gCb = 2.0 * kb * (1.0 - kb) / kg;
    const double gCr = 2.0 * kr * (1.0 - kr) / kg;

    auto f = [](double v) { return static_cast<float>(v); };
    return QMatrix4x4(f(ys), 0.0f, f(rCr * cs), f(yo + rCr * co),
                      f(ys), f(-gCb * cs), f(-gCr * cs), f(yo - (gCb + gCr) * co),
                      f(ys), f(bCb * cs), 0.0f, f(yo + bCb * co),
                      0.0f, 0.0f, 0.0f, 1.0f);
}

tve::Ticks ticksSince(std::chrono::steady_clock::time_point origin)
{
    const auto micros = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - origin).count();
    return micros * (tve::kTicksPerSecond / 1'000'000);
}


// Rendering happens in three stages:
//   1. YUV → RGB into m_frame, an RGBA16F texture at the video's own resolution
//   2. the effect chain, which reads m_frame and returns the processed texture
//   3. aspect-fit of that texture into the item
// Stages 1–2 only run when a new frame arrives or the effects change; stage 3 runs on
// every vsync.
class VideoRenderer final : public QQuickRhiItemRenderer {
public:
    ~VideoRenderer() override
    {
        for (InFlight& slot : m_inFlight)
            release(slot);
        if (m_textureCache)
            CFRelease(m_textureCache);
    }

protected:
    void initialize(QRhiCommandBuffer*) override;
    void synchronize(QQuickRhiItem* item) override;
    void render(QRhiCommandBuffer* cb) override;

private:
    // A frame's Metal textures stay alive for a few renders because the GPU may still be
    // sampling them after the frame has been replaced.
    struct InFlight {
        tve::DecodedFrame frame;
        CVMetalTextureRef luma = nullptr;
        CVMetalTextureRef chroma = nullptr;
    };

    static void release(InFlight& slot);
    bool showFrame(tve::DecodedFrame frame);
    bool ensureFrameTarget(QSize size);
    bool bindPresentInput(QRhiTexture* image);

    QRhi* m_rhi = nullptr;
    std::unique_ptr<QRhiSampler> m_sampler;
    CVMetalTextureCacheRef m_textureCache = nullptr;

    // Stage 1: YUV → RGB
    std::unique_ptr<QRhiBuffer> m_yuvUniforms;
    std::unique_ptr<QRhiTexture> m_luma;
    std::unique_ptr<QRhiTexture> m_chroma;
    std::unique_ptr<QRhiShaderResourceBindings> m_yuvBindings;
    std::unique_ptr<QRhiGraphicsPipeline> m_yuvPipeline;
    std::unique_ptr<QRhiTexture> m_frame;
    std::unique_ptr<QRhiTextureRenderTarget> m_frameTarget;
    std::unique_ptr<QRhiRenderPassDescriptor> m_frameRenderPass;

    // Stage 2: effects
    std::unique_ptr<tve::EffectChain> m_chain;
    quint64 m_effectsRevision = 0;

    // Stage 3: present
    std::unique_ptr<QRhiBuffer> m_presentUniforms;
    std::unique_ptr<QRhiShaderResourceBindings> m_presentBindings;
    std::unique_ptr<QRhiGraphicsPipeline> m_presentPipeline;
    QRhiTexture* m_presentInput = nullptr;
    QRhiTexture* m_processed = nullptr;

    std::array<InFlight, 3> m_inFlight;
    std::size_t m_nextSlot = 0;

    std::shared_ptr<tve::PlaybackSession> m_session;
    bool m_hasFrame = false;
    bool m_needsProcessing = false;
    QSize m_videoSize;
    QMatrix4x4 m_colorMatrix;
    bool m_clockStarted = false;
    std::chrono::steady_clock::time_point m_clockOrigin;
};

void VideoRenderer::release(InFlight& slot)
{
    if (slot.luma)
        CFRelease(slot.luma);
    if (slot.chroma)
        CFRelease(slot.chroma);
    slot.luma = slot.chroma = nullptr;
    slot.frame.reset();
}

void VideoRenderer::initialize(QRhiCommandBuffer*)
{
    if (m_rhi != rhi()) {
        m_presentPipeline.reset();
        m_rhi = rhi();
    }
    if (m_presentPipeline)
        return;
    if (m_rhi->backend() != QRhi::Metal) {
        qWarning("VideoView: zero-copy playback needs the Metal backend, got %s", m_rhi->backendName());
        return;
    }

    auto* handles = static_cast<const QRhiMetalNativeHandles*>(m_rhi->nativeHandles());
    if (!m_textureCache
        && CVMetalTextureCacheCreate(kCFAllocatorDefault, nullptr, (id<MTLDevice>)(void*)handles->dev, nullptr,
                                     &m_textureCache) != kCVReturnSuccess) {
        qWarning("VideoView: cannot create CVMetalTextureCache");
        return;
    }

    m_sampler.reset(m_rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None,
                                      QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
    m_sampler->create();
    m_luma.reset(m_rhi->newTexture(QRhiTexture::R8, QSize(1, 1)));
    m_luma->create();
    m_chroma.reset(m_rhi->newTexture(QRhiTexture::RG8, QSize(1, 1)));
    m_chroma->create();
    m_yuvUniforms.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 80));
    m_yuvUniforms->create();
    m_presentUniforms.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 80));
    m_presentUniforms->create();

    m_yuvBindings.reset(m_rhi->newShaderResourceBindings());
    m_yuvBindings->setBindings({
        QRhiShaderResourceBinding::uniformBuffer(
            0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage, m_yuvUniforms.get()),
        QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage, m_luma.get(),
                                                  m_sampler.get()),
        QRhiShaderResourceBinding::sampledTexture(2, QRhiShaderResourceBinding::FragmentStage, m_chroma.get(),
                                                  m_sampler.get()),
    });
    m_yuvBindings->create();

    // The frame target only gets its real size with the first frame; a placeholder
    // gives the render pass descriptor the YUV pipeline is built against.
    if (!ensureFrameTarget(QSize(16, 16)))
        return;
    m_yuvPipeline.reset(m_rhi->newGraphicsPipeline());
    m_yuvPipeline->setTopology(QRhiGraphicsPipeline::TriangleStrip);
    m_yuvPipeline->setShaderStages({
        {QRhiShaderStage::Vertex, loadShader(QStringLiteral(":/shaders/video.vert.qsb"))},
        {QRhiShaderStage::Fragment, loadShader(QStringLiteral(":/shaders/video.frag.qsb"))},
    });
    m_yuvPipeline->setVertexInputLayout({});
    m_yuvPipeline->setShaderResourceBindings(m_yuvBindings.get());
    m_yuvPipeline->setRenderPassDescriptor(m_frameRenderPass.get());
    if (!m_yuvPipeline->create()) {
        qWarning("VideoView: cannot create YUV pipeline");
        return;
    }

    m_chain = std::make_unique<tve::EffectChain>(m_rhi, tve::EffectLibrary::builtIn());

    if (!bindPresentInput(m_frame.get()))
        return;
    m_presentPipeline.reset(m_rhi->newGraphicsPipeline());
    m_presentPipeline->setTopology(QRhiGraphicsPipeline::TriangleStrip);
    m_presentPipeline->setShaderStages({
        {QRhiShaderStage::Vertex, loadShader(QStringLiteral(":/shaders/video.vert.qsb"))},
        {QRhiShaderStage::Fragment, loadShader(QStringLiteral(":/shaders/present.frag.qsb"))},
    });
    m_presentPipeline->setVertexInputLayout({});
    m_presentPipeline->setShaderResourceBindings(m_presentBindings.get());
    m_presentPipeline->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());
    m_presentPipeline->setSampleCount(renderTarget()->sampleCount());
    if (!m_presentPipeline->create()) {
        qWarning("VideoView: cannot create present pipeline");
        m_presentPipeline.reset();
    }
}

bool VideoRenderer::ensureFrameTarget(QSize size)
{
    if (m_frame && m_frame->pixelSize() == size)
        return true;
    const QRhiTexture::Format format =
        m_rhi->isTextureFormatSupported(QRhiTexture::RGBA16F) ? QRhiTexture::RGBA16F : QRhiTexture::RGBA8;
    m_frameTarget.reset();
    m_frame.reset(m_rhi->newTexture(format, size, 1, QRhiTexture::RenderTarget));
    if (!m_frame->create())
        return false;
    m_frameTarget.reset(m_rhi->newTextureRenderTarget({m_frame.get()}));
    if (!m_frameRenderPass)
        m_frameRenderPass.reset(m_frameTarget->newCompatibleRenderPassDescriptor());
    m_frameTarget->setRenderPassDescriptor(m_frameRenderPass.get());
    return m_frameTarget->create();
}

bool VideoRenderer::bindPresentInput(QRhiTexture* image)
{
    if (m_presentBindings && m_presentInput == image)
        return true;
    if (!m_presentBindings)
        m_presentBindings.reset(m_rhi->newShaderResourceBindings());
    m_presentBindings->setBindings({
        QRhiShaderResourceBinding::uniformBuffer(
            0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage,
            m_presentUniforms.get()),
        QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage, image, m_sampler.get()),
    });
    m_presentInput = image;
    return m_presentBindings->create();
}

void VideoRenderer::synchronize(QQuickRhiItem* item)
{
    auto* view = static_cast<VideoView*>(item);
    auto session = view->session();
    if (session != m_session) {
        m_session = std::move(session);
        m_clockStarted = false;
        m_hasFrame = false;
    }
    if (m_chain && view->effectsRevision() != m_effectsRevision) {
        m_chain->setInstances(view->effects());
        m_effectsRevision = view->effectsRevision();
        m_needsProcessing = true;
    }
}

bool VideoRenderer::showFrame(tve::DecodedFrame frame)
{
    CVPixelBufferRef buffer = frame.pixelBuffer();
    const PlaneFormats formats = planeFormats(CVPixelBufferGetPixelFormatType(buffer));
    if (!formats.supported)
        return false;

    InFlight& slot = m_inFlight[m_nextSlot];
    m_nextSlot = (m_nextSlot + 1) % m_inFlight.size();
    release(slot);

    const size_t lumaWidth = CVPixelBufferGetWidthOfPlane(buffer, 0);
    const size_t lumaHeight = CVPixelBufferGetHeightOfPlane(buffer, 0);
    const size_t chromaWidth = CVPixelBufferGetWidthOfPlane(buffer, 1);
    const size_t chromaHeight = CVPixelBufferGetHeightOfPlane(buffer, 1);
    if (CVMetalTextureCacheCreateTextureFromImage(kCFAllocatorDefault, m_textureCache, buffer, nullptr,
                                                  formats.metalLuma, lumaWidth, lumaHeight, 0, &slot.luma)
            != kCVReturnSuccess
        || CVMetalTextureCacheCreateTextureFromImage(kCFAllocatorDefault, m_textureCache, buffer, nullptr,
                                                     formats.metalChroma, chromaWidth, chromaHeight, 1, &slot.chroma)
            != kCVReturnSuccess) {
        release(slot);
        return false;
    }

    id<MTLTexture> luma = CVMetalTextureGetTexture(slot.luma);
    id<MTLTexture> chroma = CVMetalTextureGetTexture(slot.chroma);
    m_luma->setFormat(formats.luma);
    m_luma->setPixelSize(QSize(int(lumaWidth), int(lumaHeight)));
    m_luma->createFrom({quint64(quintptr((void*)luma)), 0});
    m_chroma->setFormat(formats.chroma);
    m_chroma->setPixelSize(QSize(int(chromaWidth), int(chromaHeight)));
    m_chroma->createFrom({quint64(quintptr((void*)chroma)), 0});

    m_colorMatrix = yuvToRgbMatrix(buffer, formats.fullRange);
    m_videoSize = QSize(int(CVPixelBufferGetWidth(buffer)), int(CVPixelBufferGetHeight(buffer)));
    slot.frame = std::move(frame);
    m_hasFrame = true;
    m_needsProcessing = true;
    return true;
}

void VideoRenderer::render(QRhiCommandBuffer* cb)
{
    float playbackSeconds = 0.0f;
    if (m_session && m_presentPipeline) {
        auto& queue = m_session->queue();
        if (!m_clockStarted) {
            if (auto firstPts = queue.frontPts()) {
                m_clockOrigin = std::chrono::steady_clock::now()
                    - std::chrono::microseconds(*firstPts / (tve::kTicksPerSecond / 1'000'000));
                m_clockStarted = true;
            }
        }
        if (m_clockStarted) {
            const tve::Ticks now = ticksSince(m_clockOrigin);
            playbackSeconds = float(tve::toSeconds(now));
            int skipped = 0;
            if (auto frame = queue.takeLatestDue(now, skipped)) {
                m_session->counters().skipped += skipped;
                if (showFrame(std::move(*frame)))
                    ++m_session->counters().presented;
            }
        }
        CVMetalTextureCacheFlush(m_textureCache, 0);
    }

    if (m_hasFrame && m_needsProcessing && ensureFrameTarget(m_videoSize)) {
        // Stage 1: YUV → RGB at full resolution.
        QRhiResourceUpdateBatch* updates = m_rhi->nextResourceUpdateBatch();
        const float fullFrame[4] = {1.0f, 1.0f, m_rhi->isYUpInNDC() ? 1.0f : 0.0f, 0.0f};
        updates->updateDynamicBuffer(m_yuvUniforms.get(), 0, 64, m_colorMatrix.constData());
        updates->updateDynamicBuffer(m_yuvUniforms.get(), 64, 16, fullFrame);
        cb->beginPass(m_frameTarget.get(), QColor(Qt::black), {1.0f, 0}, updates);
        cb->setGraphicsPipeline(m_yuvPipeline.get());
        cb->setViewport({0, 0, float(m_videoSize.width()), float(m_videoSize.height())});
        cb->setShaderResources();
        cb->draw(4);
        cb->endPass();

        // Stage 2: effects.
        m_processed = m_chain->process(cb, m_frame.get(), playbackSeconds);
        m_needsProcessing = false;
    }

    // Stage 3: aspect-fit into the item.
    const QSize outputSize = renderTarget()->pixelSize();
    const bool canPresent = m_hasFrame && m_processed && m_presentPipeline && bindPresentInput(m_processed);
    QRhiResourceUpdateBatch* updates = m_rhi->nextResourceUpdateBatch();
    if (canPresent) {
        float scale[4] = {1.0f, 1.0f, m_rhi->isYUpInNDC() ? 1.0f : 0.0f, 0.0f};
        const float videoAspect = float(m_videoSize.width()) / float(m_videoSize.height());
        const float viewAspect = float(outputSize.width()) / float(outputSize.height());
        if (videoAspect > viewAspect)
            scale[1] = viewAspect / videoAspect;
        else
            scale[0] = videoAspect / viewAspect;
        updates->updateDynamicBuffer(m_presentUniforms.get(), 64, 16, scale);
    }
    cb->beginPass(renderTarget(), QColor(Qt::black), {1.0f, 0}, updates);
    if (canPresent) {
        cb->setGraphicsPipeline(m_presentPipeline.get());
        cb->setViewport({0, 0, float(outputSize.width()), float(outputSize.height())});
        cb->setShaderResources();
        cb->draw(4);
    }
    cb->endPass();

    if (m_session)
        update(); // keep presenting on every vsync while a video is loaded
}

} // namespace

VideoView::VideoView(QQuickItem* parent) : QQuickRhiItem(parent)
{
    // Every built-in effect starts in the stack, disabled, at its default values.
    for (const tve::LoadedEffect& effect : tve::EffectLibrary::builtIn().effects())
        m_effects.push_back(tve::EffectInstance{effect.manifest.id, false, {}});

    m_statsTimer.setInterval(1000);
    connect(&m_statsTimer, &QTimer::timeout, this, &VideoView::sampleStats);
}

VideoView::~VideoView()
{
    if (m_session)
        m_session->stop();
}

void VideoView::setSource(const QString& source)
{
    if (source == m_source)
        return;
    m_source = source;

    if (m_session)
        m_session->stop();
    m_session.reset();
    m_statsTimer.stop();
    m_error.clear();
    m_description.clear();
    m_decodeFps = m_presentFps = 0;
    m_skipped = m_lastDecoded = m_lastPresented = 0;

    if (!source.isEmpty()) {
        const QString path = source.startsWith(QLatin1String("file:")) ? QUrl(source).toLocalFile() : source;
        std::string error;
        m_session = tve::PlaybackSession::open(path.toStdString(), &error);
        if (!m_session) {
            m_error = QString::fromStdString(error);
        } else {
            const tve::VideoStreamInfo info = m_session->info();
            m_description = QStringLiteral("%1×%2 %3 @ %4 fps")
                                .arg(info.width)
                                .arg(info.height)
                                .arg(QString::fromStdString(info.codecName))
                                .arg(double(info.frameRate.num) / info.frameRate.den, 0, 'f', 2);
            m_session->start();
            m_lastSampleMs = QDateTime::currentMSecsSinceEpoch();
            m_statsTimer.start();
        }
    }
    emit sourceChanged();
    emit statsChanged();
    update();
}

void VideoView::sampleStats()
{
    if (!m_session)
        return;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const double seconds = double(now - m_lastSampleMs) / 1000.0;
    if (seconds <= 0)
        return;
    auto& counters = m_session->counters();
    const qint64 decoded = counters.decoded;
    const qint64 presented = counters.presented;
    m_decodeFps = double(decoded - m_lastDecoded) / seconds;
    m_presentFps = double(presented - m_lastPresented) / seconds;
    m_skipped = counters.skipped;
    m_hardware = m_session->hardwareAccelerated();
    m_lastDecoded = decoded;
    m_lastPresented = presented;
    m_lastSampleMs = now;
    emit statsChanged();
}

QQuickRhiItemRenderer* VideoView::createRenderer()
{
    return new VideoRenderer;
}

QVariantList VideoView::availableEffects() const
{
    QVariantList list;
    for (const tve::LoadedEffect& effect : tve::EffectLibrary::builtIn().effects()) {
        const tve::EffectManifest& m = effect.manifest;
        QVariantList params;
        for (const tve::EffectParamSpec& p : m.params) {
            QVariantList defaults;
            for (float v : p.defaultValue)
                defaults << double(v);
            QStringList values;
            for (const std::string& v : p.enumValues)
                values << QString::fromStdString(v);
            params << QVariantMap{
                {QStringLiteral("id"), QString::fromStdString(p.id)},
                {QStringLiteral("title"), QString::fromStdString(p.title.at("en"))},
                {QStringLiteral("type"), QString::fromLatin1(tve::toString(p.type).data())},
                {QStringLiteral("min"), p.min},
                {QStringLiteral("max"), p.max},
                {QStringLiteral("default"), p.type == tve::ParamType::Color ? QVariant(defaults) : defaults.value(0)},
                {QStringLiteral("values"), values},
            };
        }
        list << QVariantMap{
            {QStringLiteral("id"), QString::fromStdString(m.id)},
            {QStringLiteral("title"), QString::fromStdString(m.displayTitle())},
            {QStringLiteral("params"), params},
        };
    }
    return list;
}

int VideoView::enabledEffectCount() const
{
    return int(std::ranges::count_if(m_effects, &tve::EffectInstance::enabled));
}

tve::EffectInstance* VideoView::findEffect(const QString& effectId)
{
    const std::string id = effectId.toStdString();
    auto it = std::ranges::find(m_effects, id, &tve::EffectInstance::effectId);
    return it == m_effects.end() ? nullptr : &*it;
}

void VideoView::markEffectsChanged()
{
    ++m_effectsRevision;
    emit effectsChanged();
    update();
}

bool VideoView::isEffectEnabled(const QString& effectId) const
{
    const std::string id = effectId.toStdString();
    auto it = std::ranges::find(m_effects, id, &tve::EffectInstance::effectId);
    return it != m_effects.end() && it->enabled;
}

void VideoView::setEffectEnabled(const QString& effectId, bool enabled)
{
    tve::EffectInstance* effect = findEffect(effectId);
    if (!effect) {
        qWarning("VideoView: unknown effect %s", qPrintable(effectId));
        return;
    }
    if (effect->enabled == enabled)
        return;
    effect->enabled = enabled;
    markEffectsChanged();
}

void VideoView::setEffectParam(const QString& effectId, const QString& paramId, const QVariant& value)
{
    tve::EffectInstance* effect = findEffect(effectId);
    if (!effect)
        return;
    std::vector<float> components;
    if (value.canConvert<QVariantList>() && value.typeId() != QMetaType::QString) {
        for (const QVariant& v : value.toList())
            components.push_back(v.toFloat());
    } else {
        components.push_back(value.toFloat());
    }
    effect->values[paramId.toStdString()] = std::move(components);
    markEffectsChanged();
}

