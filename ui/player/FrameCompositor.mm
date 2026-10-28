#include "FrameCompositor.h"

#include "TitleRenderer.h"
#include "VideoView.h"

#include "gpu/ColorGrader.h"
#include "gpu/Compositor.h"
#include "gpu/EffectChain.h"
#include "gpu/TransitionRenderer.h"

#include <QFile>
#include <QMatrix4x4>
#include <rhi/qrhi.h>
#include <rhi/qrhi_platform.h>

#import <Metal/Metal.h>

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

} // namespace

FrameCompositor::FrameCompositor(QRhi* rhi) : m_rhi(rhi)
{
    if (m_rhi->backend() != QRhi::Metal) {
        qWarning("FrameCompositor: zero-copy video needs the Metal backend, got %s", m_rhi->backendName());
        return;
    }
    auto* handles = static_cast<const QRhiMetalNativeHandles*>(m_rhi->nativeHandles());
    if (CVMetalTextureCacheCreate(kCFAllocatorDefault, nullptr, (id<MTLDevice>)(void*)handles->dev, nullptr,
                                  &m_textureCache) != kCVReturnSuccess) {
        qWarning("FrameCompositor: cannot create CVMetalTextureCache");
        m_textureCache = nullptr;
        return;
    }
    m_videoVert = loadShader(QStringLiteral(":/shaders/video.vert.qsb"));
    m_videoFrag = loadShader(QStringLiteral(":/shaders/video.frag.qsb"));
    m_sampler.reset(m_rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None,
                                      QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
    m_sampler->create();
    // The layer slots' YUV pipeline is built on first use, against their render pass.
    m_compositor = std::make_unique<tve::Compositor>(m_rhi);
    m_transitions = std::make_unique<tve::TransitionRenderer>(m_rhi, tve::EffectLibrary::builtIn());
    m_grader = std::make_unique<tve::ColorGrader>(m_rhi);
}

FrameCompositor::~FrameCompositor()
{
    if (m_textureCache)
        CFRelease(m_textureCache);
}

void FrameCompositor::reset()
{
    m_clipChains.clear();
    m_titles.clear();
}

void FrameCompositor::flushTextureCache()
{
    if (m_textureCache)
        CVMetalTextureCacheFlush(m_textureCache, 0);
}

FrameCompositor::LayerSlot& FrameCompositor::slot(std::size_t index)
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

QRhiTexture* FrameCompositor::convertLayer(QRhiCommandBuffer* cb, LayerSlot& slot, CVPixelBufferRef buffer,
                                           std::vector<CVMetalTextureRef>& retained)
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
    retained.push_back(luma);
    retained.push_back(chroma);

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

QRhiTexture* FrameCompositor::titleTexture(QRhiCommandBuffer* cb, tve::Id clipId, const tve::ClipText& text)
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

QRhiTexture* FrameCompositor::render(QRhiCommandBuffer* cb, tve::CompositeFrame& frame, QSize canvas, float seconds,
                                     PreviewOverrides* overrides, std::vector<CVMetalTextureRef>& retained)
{
    if (!isValid())
        return nullptr;
    m_canvasSize = canvas;
    m_compositor->begin(cb, m_canvasSize);
    m_transitions->begin();
    m_grader->begin();
    std::set<tve::Id> liveClips;
    std::size_t slotIndex = 0;
    std::size_t transitionIndex = 0;

    std::set<tve::Id> liveTitles;
    // One clip's picture (decoded video, or a title's text), through its effects (with any
    // live preview).
    const auto clipPicture = [&](tve::Id clipId, const tve::DecodedFrame& decoded, tve::ClipVisual& visual,
                                 std::vector<tve::ClipEffect> effects, tve::ColorGrade grade, tve::ColorGrade look,
                                 const std::optional<tve::ClipText>& text = std::nullopt,
                                 QRhiTexture* canvasBelow = nullptr) -> QRhiTexture* {
        QRhiTexture* picture = nullptr;
        if (canvasBelow) {
            picture = canvasBelow; // an adjustment layer works on what is under it
        } else if (text) {
            picture = titleTexture(cb, clipId, *text);
            liveTitles.insert(clipId);
        } else if (decoded.pixelBuffer()) {
            picture = convertLayer(cb, slot(slotIndex++), decoded.pixelBuffer(), retained);
        }
        if (!picture)
            return nullptr;
        if (overrides)
            overrides->apply(clipId, visual, effects, grade, look);
        if (!text) { // colour first (the look, then the grade), then effects
            picture = m_grader->apply(cb, picture, look);
            picture = m_grader->apply(cb, picture, grade);
        }
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
        if (layer.adjustment) {
            // Grade and filter the canvas so far, then lay the result over it by the layer's
            // opacity (the passes read the canvas before the draw replaces it).
            QRhiTexture* below = m_compositor->result();
            QRhiTexture* adjusted =
                clipPicture(layer.clipId, layer.frame, visual, layer.effects, layer.grade, layer.look, std::nullopt, below);
            if (adjusted && adjusted != below) {
                tve::ClipVisual cover;
                cover.opacity = visual.opacity;
                m_compositor->draw(cb, adjusted, cover);
            }
            continue;
        }
        QRhiTexture* picture =
            clipPicture(layer.clipId, layer.frame, visual, layer.effects, layer.grade, layer.look, layer.text);
        if (!layer.transition) {
            if (picture)
                m_compositor->draw(cb, picture, visual);
            continue;
        }

        tve::LayerTransition& transition = *layer.transition;
        tve::ClipVisual incomingVisual = transition.visual;
        QRhiTexture* incoming =
            clipPicture(transition.clipId, transition.frame, incomingVisual, transition.effects, transition.grade,
                        transition.look);
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
    return m_compositor->result();
}

