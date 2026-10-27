#include "VideoView.h"

#include "EffectInfo.h"
#include "TitleRenderer.h"

#include "gpu/Compositor.h"
#include "gpu/TransitionRenderer.h"
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
#include <map>
#include <set>

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

tve::EffectInstance toInstance(const tve::ClipEffect& effect)
{
    return tve::EffectInstance{effect.effectId, effect.enabled, effect.values};
}

// Rendering, per composite frame:
//   1. each layer: YUV → RGB at the layer's own resolution, then that clip's effects
//   2. the compositor draws the layers onto the project canvas (placement, crop, opacity,
//      blend). A layer in a transition places each of its two clips on a transparent
//      canvas of its own, blends them with the transition, and draws the result.
//   3. output effects (the global stack) on the canvas
//   4. aspect-fit of the canvas into the item
// Steps 1–3 run when a new frame arrives or a preview/effect setting changes; step 4 runs on
// every vsync.
class VideoRenderer final : public QQuickRhiItemRenderer {
public:
    ~VideoRenderer() override
    {
        for (FrameResources& resources : m_frames)
            release(resources);
        if (m_textureCache)
            CFRelease(m_textureCache);
    }

protected:
    void initialize(QRhiCommandBuffer*) override;
    void synchronize(QQuickRhiItem* item) override;
    void render(QRhiCommandBuffer* cb) override;

private:
    // A composite frame and the Metal textures made from its pixel buffers. Kept for a few
    // renders because the GPU may still be sampling them after the frame is replaced.
    struct FrameResources {
        std::optional<tve::CompositeFrame> frame;
        std::vector<CVMetalTextureRef> textures;
    };

    // Per layer position within a frame: every pass of a frame is recorded before the GPU
    // runs it, so each layer needs its own textures, uniforms and bindings.
    struct LayerSlot {
        std::unique_ptr<QRhiTexture> luma;
        std::unique_ptr<QRhiTexture> chroma;
        std::unique_ptr<QRhiBuffer> uniforms;
        std::unique_ptr<QRhiShaderResourceBindings> bindings;
        std::unique_ptr<QRhiTexture> rgb;
        std::unique_ptr<QRhiTextureRenderTarget> target;
    };

    static void release(FrameResources& resources);
    LayerSlot& slot(std::size_t index);
    // YUV → RGB of one layer into its slot's texture; nullptr if the buffer is unusable.
    QRhiTexture* convertLayer(QRhiCommandBuffer* cb, LayerSlot& slot, CVPixelBufferRef buffer,
                              FrameResources& resources);
    bool bindPresentInput(QRhiTexture* image);
    void renderFrame(QRhiCommandBuffer* cb, float seconds);

    QRhi* m_rhi = nullptr;
    std::unique_ptr<QRhiSampler> m_sampler;
    CVMetalTextureCacheRef m_textureCache = nullptr;
    QShader m_videoVert;
    QShader m_videoFrag;
    QShader m_presentFrag;

    std::vector<LayerSlot> m_slots;
    std::unique_ptr<QRhiGraphicsPipeline> m_yuvPipeline;
    std::unique_ptr<QRhiRenderPassDescriptor> m_layerRenderPass;

    std::unique_ptr<tve::Compositor> m_compositor;
    // Per transition within a frame: the outgoing and incoming clips' own canvases.
    std::vector<std::array<std::unique_ptr<tve::Compositor>, 2>> m_transitionCanvases;
    std::unique_ptr<tve::TransitionRenderer> m_transitions;
    // Titles on screen: their text drawn at canvas size, redrawn only when it changes.
    struct TitleTexture {
        tve::ClipText text;
        QSize canvas;
        std::unique_ptr<QRhiTexture> texture;
    };
    std::map<tve::Id, TitleTexture> m_titles;
    QRhiTexture* titleTexture(QRhiCommandBuffer* cb, tve::Id clipId, const tve::ClipText& text);
    std::map<tve::Id, std::unique_ptr<tve::EffectChain>> m_clipChains;
    std::unique_ptr<tve::EffectChain> m_outputChain;
    quint64 m_effectsRevision = 0;
    std::shared_ptr<PreviewOverrides> m_overrides;
    quint64 m_overridesRevision = 0;

    std::unique_ptr<QRhiBuffer> m_presentUniforms;
    std::unique_ptr<QRhiShaderResourceBindings> m_presentBindings;
    std::unique_ptr<QRhiGraphicsPipeline> m_presentPipeline;
    QRhiTexture* m_presentInput = nullptr;
    QRhiTexture* m_processed = nullptr;

    std::array<FrameResources, 3> m_frames;
    std::size_t m_currentFrame = 0;
    bool m_hasFrame = false;
    bool m_needsProcessing = false;
    QSize m_canvasSize;

    std::shared_ptr<tve::FrameSource> m_source;
    std::unique_ptr<QRhiTexture> m_placeholder;
};

void VideoRenderer::release(FrameResources& resources)
{
    for (CVMetalTextureRef texture : resources.textures)
        CFRelease(texture);
    resources.textures.clear();
    resources.frame.reset();
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

    m_videoVert = loadShader(QStringLiteral(":/shaders/video.vert.qsb"));
    m_videoFrag = loadShader(QStringLiteral(":/shaders/video.frag.qsb"));
    m_presentFrag = loadShader(QStringLiteral(":/shaders/present.frag.qsb"));
    m_sampler.reset(m_rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None,
                                      QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
    m_sampler->create();
    m_presentUniforms.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 80));
    m_presentUniforms->create();

    // The layer slots' YUV pipeline is built on first use, against their render pass.
    m_compositor = std::make_unique<tve::Compositor>(m_rhi);
    m_transitions = std::make_unique<tve::TransitionRenderer>(m_rhi, tve::EffectLibrary::builtIn());
    m_outputChain = std::make_unique<tve::EffectChain>(m_rhi, tve::EffectLibrary::builtIn());

    // A placeholder input gives the present pipeline its binding layout.
    m_placeholder.reset(m_rhi->newTexture(QRhiTexture::RGBA8, QSize(1, 1)));
    m_placeholder->create();
    if (!bindPresentInput(m_placeholder.get()))
        return;
    m_presentPipeline.reset(m_rhi->newGraphicsPipeline());
    m_presentPipeline->setTopology(QRhiGraphicsPipeline::TriangleStrip);
    m_presentPipeline->setShaderStages({{QRhiShaderStage::Vertex, m_videoVert}, {QRhiShaderStage::Fragment, m_presentFrag}});
    m_presentPipeline->setVertexInputLayout({});
    m_presentPipeline->setShaderResourceBindings(m_presentBindings.get());
    m_presentPipeline->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());
    m_presentPipeline->setSampleCount(renderTarget()->sampleCount());
    if (!m_presentPipeline->create()) {
        qWarning("VideoView: cannot create present pipeline");
        m_presentPipeline.reset();
    }
}

VideoRenderer::LayerSlot& VideoRenderer::slot(std::size_t index)
{
    while (m_slots.size() <= index) {
        LayerSlot slot;
        slot.luma.reset(m_rhi->newTexture(QRhiTexture::R8, QSize(1, 1)));
        slot.luma->create();
        slot.chroma.reset(m_rhi->newTexture(QRhiTexture::RG8, QSize(1, 1)));
        slot.chroma->create();
        slot.uniforms.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 80));
        slot.uniforms->create();
        slot.bindings.reset(m_rhi->newShaderResourceBindings());
        slot.bindings->setBindings({
            QRhiShaderResourceBinding::uniformBuffer(
                0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage,
                slot.uniforms.get()),
            QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage, slot.luma.get(),
                                                      m_sampler.get()),
            QRhiShaderResourceBinding::sampledTexture(2, QRhiShaderResourceBinding::FragmentStage, slot.chroma.get(),
                                                      m_sampler.get()),
        });
        slot.bindings->create();
        m_slots.push_back(std::move(slot));
    }
    return m_slots[index];
}

QRhiTexture* VideoRenderer::convertLayer(QRhiCommandBuffer* cb, LayerSlot& slot, CVPixelBufferRef buffer,
                                         FrameResources& resources)
{
    const PlaneFormats formats = planeFormats(CVPixelBufferGetPixelFormatType(buffer));
    if (!formats.supported)
        return nullptr;

    const size_t lumaWidth = CVPixelBufferGetWidthOfPlane(buffer, 0);
    const size_t lumaHeight = CVPixelBufferGetHeightOfPlane(buffer, 0);
    const size_t chromaWidth = CVPixelBufferGetWidthOfPlane(buffer, 1);
    const size_t chromaHeight = CVPixelBufferGetHeightOfPlane(buffer, 1);
    CVMetalTextureRef luma = nullptr;
    CVMetalTextureRef chroma = nullptr;
    if (CVMetalTextureCacheCreateTextureFromImage(kCFAllocatorDefault, m_textureCache, buffer, nullptr,
                                                  formats.metalLuma, lumaWidth, lumaHeight, 0, &luma)
            != kCVReturnSuccess
        || CVMetalTextureCacheCreateTextureFromImage(kCFAllocatorDefault, m_textureCache, buffer, nullptr,
                                                     formats.metalChroma, chromaWidth, chromaHeight, 1, &chroma)
            != kCVReturnSuccess) {
        if (luma)
            CFRelease(luma);
        return nullptr;
    }
    resources.textures.push_back(luma);
    resources.textures.push_back(chroma);

    slot.luma->setFormat(formats.luma);
    slot.luma->setPixelSize(QSize(int(lumaWidth), int(lumaHeight)));
    slot.luma->createFrom({quint64(quintptr((void*)CVMetalTextureGetTexture(luma))), 0});
    slot.chroma->setFormat(formats.chroma);
    slot.chroma->setPixelSize(QSize(int(chromaWidth), int(chromaHeight)));
    slot.chroma->createFrom({quint64(quintptr((void*)CVMetalTextureGetTexture(chroma))), 0});

    const QSize size(int(CVPixelBufferGetWidth(buffer)), int(CVPixelBufferGetHeight(buffer)));
    if (!slot.rgb || slot.rgb->pixelSize() != size) {
        const QRhiTexture::Format format =
            m_rhi->isTextureFormatSupported(QRhiTexture::RGBA16F) ? QRhiTexture::RGBA16F : QRhiTexture::RGBA8;
        slot.target.reset();
        slot.rgb.reset(m_rhi->newTexture(format, size, 1, QRhiTexture::RenderTarget));
        if (!slot.rgb->create())
            return nullptr;
        slot.target.reset(m_rhi->newTextureRenderTarget({slot.rgb.get()}));
        if (!m_layerRenderPass)
            m_layerRenderPass.reset(slot.target->newCompatibleRenderPassDescriptor());
        slot.target->setRenderPassDescriptor(m_layerRenderPass.get());
        if (!slot.target->create())
            return nullptr;
    }
    if (!m_yuvPipeline) {
        m_yuvPipeline.reset(m_rhi->newGraphicsPipeline());
        m_yuvPipeline->setTopology(QRhiGraphicsPipeline::TriangleStrip);
        m_yuvPipeline->setShaderStages({{QRhiShaderStage::Vertex, m_videoVert}, {QRhiShaderStage::Fragment, m_videoFrag}});
        m_yuvPipeline->setVertexInputLayout({});
        m_yuvPipeline->setShaderResourceBindings(slot.bindings.get());
        m_yuvPipeline->setRenderPassDescriptor(m_layerRenderPass.get());
        if (!m_yuvPipeline->create()) {
            m_yuvPipeline.reset();
            return nullptr;
        }
    }

    const QMatrix4x4 matrix = yuvToRgbMatrix(buffer, formats.fullRange);
    const float fullFrame[4] = {1.0f, 1.0f, m_rhi->isYUpInNDC() ? 1.0f : 0.0f, 0.0f};
    QRhiResourceUpdateBatch* updates = m_rhi->nextResourceUpdateBatch();
    updates->updateDynamicBuffer(slot.uniforms.get(), 0, 64, matrix.constData());
    updates->updateDynamicBuffer(slot.uniforms.get(), 64, 16, fullFrame);
    cb->beginPass(slot.target.get(), QColor(Qt::black), {1.0f, 0}, updates);
    cb->setGraphicsPipeline(m_yuvPipeline.get());
    cb->setViewport({0, 0, float(size.width()), float(size.height())});
    cb->setShaderResources(slot.bindings.get());
    cb->draw(4);
    cb->endPass();
    return slot.rgb.get();
}

QRhiTexture* VideoRenderer::titleTexture(QRhiCommandBuffer* cb, tve::Id clipId, const tve::ClipText& text)
{
    TitleTexture& title = m_titles[clipId];
    if (title.texture && title.text == text && title.canvas == m_canvasSize)
        return title.texture.get();
    const QImage image = renderTitle(text, m_canvasSize);
    if (!title.texture || title.texture->pixelSize() != image.size()) {
        title.texture.reset(m_rhi->newTexture(QRhiTexture::RGBA8, image.size()));
        if (!title.texture->create()) {
            title.texture.reset();
            return nullptr;
        }
    }
    QRhiResourceUpdateBatch* upload = m_rhi->nextResourceUpdateBatch();
    upload->uploadTexture(title.texture.get(), image);
    cb->resourceUpdate(upload);
    title.text = text;
    title.canvas = m_canvasSize;
    return title.texture.get();
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
    auto source = view->frameSource();
    if (source != m_source) {
        m_source = std::move(source);
        m_hasFrame = false;
        m_clipChains.clear();
    }
    if (m_outputChain && view->effectsRevision() != m_effectsRevision) {
        m_outputChain->setInstances(view->effects());
        m_effectsRevision = view->effectsRevision();
        m_needsProcessing = true;
    }
    m_overrides = view->previewOverrides();
    if (m_overrides && m_overrides->revision() != m_overridesRevision) {
        m_overridesRevision = m_overrides->revision();
        m_needsProcessing = true;
    }
}

void VideoRenderer::renderFrame(QRhiCommandBuffer* cb, float seconds)
{
    FrameResources& resources = m_frames[m_currentFrame];
    tve::CompositeFrame& frame = *resources.frame;
    m_compositor->begin(cb, m_canvasSize);
    m_transitions->begin();
    std::set<tve::Id> liveClips;
    std::size_t slotIndex = 0;
    std::size_t transitionIndex = 0;

    std::set<tve::Id> liveTitles;
    // One clip's picture (decoded video, or a title's text), through its effects (with any
    // live preview).
    const auto clipPicture = [&](tve::Id clipId, const tve::DecodedFrame& decoded, tve::ClipVisual& visual,
                                 std::vector<tve::ClipEffect> effects,
                                 const std::optional<tve::ClipText>& text = std::nullopt) -> QRhiTexture* {
        QRhiTexture* picture = nullptr;
        if (text) {
            picture = titleTexture(cb, clipId, *text);
            liveTitles.insert(clipId);
        } else if (decoded.pixelBuffer()) {
            picture = convertLayer(cb, slot(slotIndex++), decoded.pixelBuffer(), resources);
        }
        if (!picture)
            return nullptr;
        if (m_overrides)
            m_overrides->apply(clipId, visual, effects);
        if (!effects.empty()) {
            auto& chain = m_clipChains[clipId];
            if (!chain)
                chain = std::make_unique<tve::EffectChain>(m_rhi, tve::EffectLibrary::builtIn());
            std::vector<tve::EffectInstance> instances;
            for (const tve::ClipEffect& effect : effects)
                instances.push_back(toInstance(effect));
            chain->setInstances(std::move(instances));
            picture = chain->process(cb, picture, seconds);
            liveClips.insert(clipId);
        }
        return picture;
    };

    for (tve::VideoLayer& layer : frame.layers) {
        tve::ClipVisual visual = layer.visual;
        QRhiTexture* picture = clipPicture(layer.clipId, layer.frame, visual, layer.effects, layer.text);
        if (!layer.transition) {
            if (picture)
                m_compositor->draw(cb, picture, visual);
            continue;
        }

        tve::LayerTransition& transition = *layer.transition;
        tve::ClipVisual incomingVisual = transition.visual;
        QRhiTexture* incoming = clipPicture(transition.clipId, transition.frame, incomingVisual, transition.effects);
        if (!picture || !incoming) {
            if (picture || incoming)
                m_compositor->draw(cb, picture ? picture : incoming, picture ? visual : incomingVisual);
            continue;
        }
        if (m_transitionCanvases.size() <= transitionIndex)
            m_transitionCanvases.push_back({std::make_unique<tve::Compositor>(m_rhi), std::make_unique<tve::Compositor>(m_rhi)});
        auto& canvases = m_transitionCanvases[transitionIndex++];
        // Each side is placed by itself (blend modes need what is below, which comes after).
        const tve::BlendMode blend = visual.blend == incomingVisual.blend ? visual.blend : tve::BlendMode::Normal;
        visual.blend = tve::BlendMode::Normal;
        incomingVisual.blend = tve::BlendMode::Normal;
        canvases[0]->begin(cb, m_canvasSize, true);
        canvases[0]->draw(cb, picture, visual);
        canvases[1]->begin(cb, m_canvasSize, true);
        canvases[1]->draw(cb, incoming, incomingVisual);
        QRhiTexture* blended = m_transitions->render(cb, transition.transitionId, transition.values, transition.progress,
                                                     canvases[0]->result(), canvases[1]->result(), seconds);
        if (blended) {
            tve::ClipVisual whole;
            whole.blend = blend;
            m_compositor->draw(cb, blended, whole);
        } else {
            // Unknown transition (e.g. from a newer version): cut at the half-way point.
            m_compositor->draw(cb, transition.progress < 0.5f ? canvases[0]->result() : canvases[1]->result(),
                               tve::ClipVisual{});
        }
    }
    // Drop effect chains and title textures of clips no longer on screen.
    std::erase_if(m_clipChains, [&](const auto& entry) { return !liveClips.contains(entry.first); });
    std::erase_if(m_titles, [&](const auto& entry) { return !liveTitles.contains(entry.first); });

    m_processed = m_outputChain->process(cb, m_compositor->result(), seconds);
}

void VideoRenderer::render(QRhiCommandBuffer* cb)
{
    float seconds = 0.0f;
    if (m_source && m_presentPipeline) {
        auto& queue = m_source->queue();
        auto& clock = m_source->clock();
        if (m_source->startsOnFirstFrame() && !clock.started()) {
            if (auto firstPts = queue.frontPts()) {
                clock.seek(*firstPts);
                clock.play();
            }
        }
        if (clock.started()) {
            const tve::Ticks now = clock.now();
            seconds = float(tve::toSeconds(now));
            int skipped = 0;
            if (auto frame = queue.takeLatestDue(now, skipped)) {
                m_source->counters().skipped += skipped;
                m_currentFrame = (m_currentFrame + 1) % m_frames.size();
                release(m_frames[m_currentFrame]);
                if (!frame->layers.empty())
                    ++m_source->counters().presented;
                m_frames[m_currentFrame].frame = std::move(*frame);
                const auto canvas = m_source->canvasSize();
                m_canvasSize = QSize(std::max(canvas.width, 16), std::max(canvas.height, 16));
                m_hasFrame = true;
                m_needsProcessing = true;
            }
        }
        CVMetalTextureCacheFlush(m_textureCache, 0);
    }

    if (m_hasFrame && m_needsProcessing) {
        renderFrame(cb, seconds);
        m_needsProcessing = false;
    }

    // Aspect-fit the canvas into the item.
    const QSize outputSize = renderTarget()->pixelSize();
    const bool canPresent = m_hasFrame && m_processed && m_presentPipeline && bindPresentInput(m_processed);
    QRhiResourceUpdateBatch* updates = m_rhi->nextResourceUpdateBatch();
    if (canPresent) {
        float scale[4] = {1.0f, 1.0f, m_rhi->isYUpInNDC() ? 1.0f : 0.0f, 0.0f};
        const float canvasAspect = float(m_canvasSize.width()) / float(m_canvasSize.height());
        const float viewAspect = float(outputSize.width()) / float(outputSize.height());
        if (canvasAspect > viewAspect)
            scale[1] = viewAspect / canvasAspect;
        else
            scale[0] = canvasAspect / viewAspect;
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

    if (m_source)
        update(); // keep presenting on every vsync while there is something to play
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
    if (m_stopFileSession)
        m_stopFileSession();
}

void VideoView::setFrameSource(std::shared_ptr<tve::FrameSource> source, const QString& description)
{
    if (m_stopFileSession) {
        m_stopFileSession();
        m_stopFileSession = nullptr;
    }
    m_source.clear();
    m_frameSource = std::move(source);
    m_error.clear();
    m_description = description;
    m_decodeFps = m_presentFps = 0;
    m_skipped = m_lastDecoded = m_lastPresented = 0;
    m_statsTimer.stop();
    if (m_frameSource) {
        m_lastDecoded = m_frameSource->counters().decoded;
        m_lastPresented = m_frameSource->counters().presented;
        m_lastSampleMs = QDateTime::currentMSecsSinceEpoch();
        m_statsTimer.start();
    }
    emit sourceChanged();
    emit statsChanged();
    update();
}

void VideoView::setSource(const QString& source)
{
    if (source == m_source)
        return;

    std::shared_ptr<tve::PlaybackSession> session;
    QString description;
    QString error;
    if (!source.isEmpty()) {
        const QString path = source.startsWith(QLatin1String("file:")) ? QUrl(source).toLocalFile() : source;
        std::string message;
        session = tve::PlaybackSession::open(path.toStdString(), &message);
        if (!session) {
            error = QString::fromStdString(message);
        } else {
            const tve::VideoStreamInfo info = session->info();
            description = QStringLiteral("%1×%2 %3 @ %4 fps")
                              .arg(info.width)
                              .arg(info.height)
                              .arg(QString::fromStdString(info.codecName))
                              .arg(double(info.frameRate.num) / info.frameRate.den, 0, 'f', 2);
            session->start();
        }
    }
    setFrameSource(session, description);
    if (session)
        m_stopFileSession = [session] { session->stop(); };
    m_source = source;
    m_error = error;
    emit sourceChanged();
}

void VideoView::sampleStats()
{
    if (!m_frameSource)
        return;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const double seconds = double(now - m_lastSampleMs) / 1000.0;
    if (seconds <= 0)
        return;
    auto& counters = m_frameSource->counters();
    const qint64 decoded = counters.decoded;
    const qint64 presented = counters.presented;
    m_decodeFps = double(decoded - m_lastDecoded) / seconds;
    m_presentFps = double(presented - m_lastPresented) / seconds;
    m_skipped = counters.skipped;
    m_hardware = m_frameSource->hardwareAccelerated();
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
    for (const tve::LoadedEffect& effect : tve::EffectLibrary::builtIn().effects())
        list << describeEffect(effect);
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


// --- PreviewOverrides ---

void PreviewOverrides::setVisual(tve::Id clipId, const tve::ClipVisual& visual)
{
    std::lock_guard lock(m_mutex);
    m_visual[clipId] = visual;
    ++m_revision;
}

void PreviewOverrides::setEffects(tve::Id clipId, std::vector<tve::ClipEffect> effects)
{
    std::lock_guard lock(m_mutex);
    m_effects[clipId] = std::move(effects);
    ++m_revision;
}

void PreviewOverrides::clear(tve::Id clipId)
{
    std::lock_guard lock(m_mutex);
    m_visual.erase(clipId);
    m_effects.erase(clipId);
    ++m_revision;
}

void PreviewOverrides::apply(tve::Id clipId, tve::ClipVisual& visual, std::vector<tve::ClipEffect>& effects)
{
    std::lock_guard lock(m_mutex);
    if (auto it = m_visual.find(clipId); it != m_visual.end()) {
        if (it->second == visual)
            m_visual.erase(it); // the frames carry the committed value now
        else
            visual = it->second;
    }
    if (auto it = m_effects.find(clipId); it != m_effects.end()) {
        if (it->second == effects)
            m_effects.erase(it);
        else
            effects = it->second;
    }
}

void VideoView::setPreviewVisual(tve::Id clipId, const tve::ClipVisual& visual)
{
    m_previewOverrides->setVisual(clipId, visual);
    update();
}

void VideoView::setPreviewEffects(tve::Id clipId, std::vector<tve::ClipEffect> effects)
{
    m_previewOverrides->setEffects(clipId, std::move(effects));
    update();
}

void VideoView::clearPreview(tve::Id clipId)
{
    m_previewOverrides->clear(clipId);
    update();
}
