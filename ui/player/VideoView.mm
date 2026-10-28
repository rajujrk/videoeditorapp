#include "VideoView.h"

#include "EffectInfo.h"
#include "FrameCompositor.h"

#include "gpu/Compositor.h"
#include "gpu/EffectChain.h"
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

// Rendering, per composite frame: the FrameCompositor draws the layers on the project
// canvas, then the output effects (the global stack) run on it, and the result is
// aspect-fitted into the item. The first two run when a new frame arrives or a
// preview/effect setting changes; the fit runs on every vsync.
class VideoRenderer final : public QQuickRhiItemRenderer {
public:
    ~VideoRenderer() override
    {
        for (FrameResources& resources : m_frames)
            release(resources);
        // A readback still in flight completes into its capture later: leave it alive.
        if (m_scopeCapture && !m_scopeCapture->pending)
            delete m_scopeCapture;
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

    static void release(FrameResources& resources);
    bool bindPresentInput(QRhiTexture* image);
    void renderFrame(QRhiCommandBuffer* cb, float seconds);
    void captureScope(QRhiCommandBuffer* cb);

    QRhi* m_rhi = nullptr;
    std::unique_ptr<QRhiSampler> m_sampler;
    QShader m_videoVert;
    QShader m_presentFrag;
    std::unique_ptr<FrameCompositor> m_frameCompositor;
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

    // Scopes: the processed frame drawn small and read back. A readback finishes in a later
    // frame; its result lives on its own so a renderer gone by then cannot be reached.
    struct ScopeCapture {
        QRhiReadbackResult result;
        std::atomic<bool> pending{false};
    };
    std::shared_ptr<ScopeFrame> m_scopeFrame;
    bool m_scopesEnabled = false;
    bool m_scopeWanted = false; // a frame not yet captured
    qint64 m_lastScopeMs = 0;
    std::unique_ptr<tve::Compositor> m_scopeCompositor;
    ScopeCapture* m_scopeCapture = nullptr;
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
    m_frameCompositor = std::make_unique<FrameCompositor>(m_rhi);
    if (!m_frameCompositor->isValid())
        return;
    m_videoVert = loadShader(QStringLiteral(":/shaders/video.vert.qsb"));
    m_presentFrag = loadShader(QStringLiteral(":/shaders/present.frag.qsb"));
    m_sampler.reset(m_rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None,
                                      QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
    m_sampler->create();
    m_presentUniforms.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 80));
    m_presentUniforms->create();
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
        if (m_frameCompositor)
            m_frameCompositor->reset();
    }
    if (m_outputChain && view->effectsRevision() != m_effectsRevision) {
        m_outputChain->setInstances(view->effects());
        m_effectsRevision = view->effectsRevision();
        m_needsProcessing = true;
    }
    m_scopeFrame = view->scopeFrame();
    if (view->scopesEnabled() && !m_scopesEnabled)
        m_scopeWanted = true; // show the frame already on screen
    m_scopesEnabled = view->scopesEnabled();
    m_overrides = view->previewOverrides();
    if (m_overrides && m_overrides->revision() != m_overridesRevision) {
        m_overridesRevision = m_overrides->revision();
        m_needsProcessing = true;
    }
}

void VideoRenderer::renderFrame(QRhiCommandBuffer* cb, float seconds)
{
    FrameResources& resources = m_frames[m_currentFrame];
    QRhiTexture* canvas =
        m_frameCompositor->render(cb, *resources.frame, m_canvasSize, seconds, m_overrides.get(), resources.textures);
    m_processed = canvas ? m_outputChain->process(cb, canvas, seconds) : nullptr;
}

void VideoRenderer::captureScope(QRhiCommandBuffer* cb)
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - m_lastScopeMs < 66 || (m_scopeCapture && m_scopeCapture->pending))
        return; // at most ~15 a second, one at a time
    if (!m_scopeCompositor)
        m_scopeCompositor = std::make_unique<tve::Compositor>(m_rhi);
    const int width = 256;
    const int height = std::max(16, int(std::lround(256.0 * m_canvasSize.height() / std::max(1, m_canvasSize.width()))));
    m_scopeCompositor->begin(cb, QSize(width, height));
    m_scopeCompositor->draw(cb, m_processed, tve::ClipVisual{});
    QRhiTexture* small = m_scopeCompositor->result();
    if (!small)
        return;
    if (!m_scopeCapture)
        m_scopeCapture = new ScopeCapture;
    ScopeCapture* capture = m_scopeCapture;
    capture->pending = true;
    capture->result = {};
    capture->result.completed = [capture, frame = m_scopeFrame, width, height] {
        const QByteArray& data = capture->result.data;
        tve::ScopeImage image{width, height, {}};
        const std::size_t pixels = std::size_t(width) * std::size_t(height);
        if (capture->result.format == QRhiTexture::RGBA16F && std::size_t(data.size()) >= pixels * 8) {
            std::vector<float> rgba(pixels * 4);
            qFloatFromFloat16(rgba.data(), reinterpret_cast<const qfloat16*>(data.constData()), qsizetype(rgba.size()));
            image.rgb.resize(pixels * 3);
            for (std::size_t i = 0; i < pixels; ++i)
                for (int c = 0; c < 3; ++c)
                    image.rgb[i * 3 + std::size_t(c)] = rgba[i * 4 + std::size_t(c)];
            frame->set(std::move(image));
        }
        capture->pending = false;
    };
    QRhiResourceUpdateBatch* batch = m_rhi->nextResourceUpdateBatch();
    batch->readBackTexture({small}, &capture->result);
    cb->resourceUpdate(batch);
    m_lastScopeMs = now;
    m_scopeWanted = false;
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
        m_frameCompositor->flushTextureCache();
    }

    if (m_hasFrame && m_needsProcessing) {
        renderFrame(cb, seconds);
        m_needsProcessing = false;
        m_scopeWanted = true;
    }
    if (m_scopesEnabled && m_scopeWanted && m_processed)
        captureScope(cb);

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
    // Effects first, then overlays.
    QVariantList effects;
    QVariantList overlays;
    for (const tve::LoadedEffect& effect : tve::EffectLibrary::builtIn().effects()) {
        const bool overlay = std::ranges::find(effect.manifest.categories, "overlay") != effect.manifest.categories.end();
        (overlay ? overlays : effects) << describeEffect(effect);
    }
    return effects + overlays;
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

void PreviewOverrides::setGrade(tve::Id clipId, const tve::ColorGrade& grade)
{
    std::lock_guard lock(m_mutex);
    m_grades[clipId] = grade;
    ++m_revision;
}

void PreviewOverrides::setLook(tve::Id clipId, const tve::ColorGrade& look)
{
    std::lock_guard lock(m_mutex);
    m_looks[clipId] = look;
    ++m_revision;
}

void PreviewOverrides::clear(tve::Id clipId)
{
    std::lock_guard lock(m_mutex);
    m_visual.erase(clipId);
    m_effects.erase(clipId);
    m_grades.erase(clipId);
    m_looks.erase(clipId);
    ++m_revision;
}

void PreviewOverrides::apply(tve::Id clipId, tve::ClipVisual& visual, std::vector<tve::ClipEffect>& effects,
                             tve::ColorGrade& grade, tve::ColorGrade& look)
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
    if (auto it = m_grades.find(clipId); it != m_grades.end()) {
        if (it->second == grade)
            m_grades.erase(it);
        else
            grade = it->second;
    }
    if (auto it = m_looks.find(clipId); it != m_looks.end()) {
        if (it->second == look)
            m_looks.erase(it);
        else
            look = it->second;
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

void ScopeFrame::set(tve::ScopeImage image)
{
    {
        std::lock_guard lock(m_mutex);
        m_image = std::move(image);
        ++m_serial;
    }
    emit changed(); // queued to the GUI thread's receivers
}

tve::ScopeImage ScopeFrame::image() const
{
    std::lock_guard lock(m_mutex);
    return m_image;
}

quint64 ScopeFrame::serial() const
{
    std::lock_guard lock(m_mutex);
    return m_serial;
}

void VideoView::setScopesEnabled(bool enabled)
{
    if (enabled == m_scopesEnabled)
        return;
    m_scopesEnabled = enabled;
    emit scopesEnabledChanged();
    update();
}

void VideoView::setPreviewGrade(tve::Id clipId, const tve::ColorGrade& grade)
{
    m_previewOverrides->setGrade(clipId, grade);
    update();
}

void VideoView::setPreviewLook(tve::Id clipId, const tve::ColorGrade& look)
{
    m_previewOverrides->setLook(clipId, look);
    update();
}

void VideoView::clearPreview(tve::Id clipId)
{
    m_previewOverrides->clear(clipId);
    update();
}
