#include "gpu/Compositor.h"

#include <QFile>
#include <rhi/qrhi.h>

#include <cstring>

namespace tve {

namespace {

QShader readShader(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning("Compositor: missing shader %s", qPrintable(path));
        return {};
    }
    return QShader::fromSerialized(file.readAll());
}

// Matches the EffectParams block in composite.frag.
struct Uniforms {
    float texelSize[2];
    float time;
    float flipY;
    float mapU[4];
    float mapV[4];
    float cropRect[4];
    float opacity;
    std::int32_t blendMode;
    float padding[2];
};
static_assert(sizeof(Uniforms) == 80);

} // namespace

Compositor::Compositor(QRhi* rhi) : m_rhi(rhi)
{
    m_vertex = readShader(QStringLiteral(":/effects/common/fullscreen.vert.qsb"));
    m_fragment = readShader(QStringLiteral(":/gpu/shaders/composite.frag.qsb"));
    m_sampler.reset(m_rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None,
                                      QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
    m_sampler->create();
}

Compositor::~Compositor() = default;

bool Compositor::ensureTargets(QSize size)
{
    if (size == m_size && m_canvas[0])
        return true;
    const QRhiTexture::Format format =
        m_rhi->isTextureFormatSupported(QRhiTexture::RGBA16F) ? QRhiTexture::RGBA16F : QRhiTexture::RGBA8;
    for (std::size_t i = 0; i < m_canvas.size(); ++i) {
        m_targets[i].reset();
        m_canvas[i].reset(m_rhi->newTexture(format, size, 1,
                                            QRhiTexture::RenderTarget | QRhiTexture::UsedAsTransferSource));
        if (!m_canvas[i]->create())
            return false;
        m_targets[i].reset(m_rhi->newTextureRenderTarget({m_canvas[i].get()}));
        if (!m_renderPass)
            m_renderPass.reset(m_targets[i]->newCompatibleRenderPassDescriptor());
        m_targets[i]->setRenderPassDescriptor(m_renderPass.get());
        if (!m_targets[i]->create())
            return false;
    }
    m_size = size;
    return true;
}

bool Compositor::ensurePipeline(QRhiShaderResourceBindings* layout)
{
    if (m_pipeline)
        return true;
    if (!m_vertex.isValid() || !m_fragment.isValid())
        return false;
    m_pipeline.reset(m_rhi->newGraphicsPipeline());
    m_pipeline->setTopology(QRhiGraphicsPipeline::TriangleStrip);
    m_pipeline->setShaderStages({{QRhiShaderStage::Vertex, m_vertex}, {QRhiShaderStage::Fragment, m_fragment}});
    m_pipeline->setVertexInputLayout({});
    m_pipeline->setShaderResourceBindings(layout);
    m_pipeline->setRenderPassDescriptor(m_renderPass.get());
    if (!m_pipeline->create()) {
        m_pipeline.reset();
        return false;
    }
    return true;
}

void Compositor::begin(QRhiCommandBuffer* cb, QSize canvasSize, bool transparent)
{
    if (!ensureTargets(canvasSize))
        return;
    m_current = 0;
    m_drawIndex = 0;
    cb->beginPass(m_targets[0].get(), transparent ? QColor(0, 0, 0, 0) : QColor(Qt::black), {1.0f, 0});
    cb->endPass();
}

void Compositor::draw(QRhiCommandBuffer* cb, QRhiTexture* layer, const ClipVisual& visual)
{
    if (!m_canvas[0] || !layer)
        return;
    const int source = m_current;
    const int target = 1 - m_current;

    if (m_drawIndex == m_slots.size()) {
        DrawSlot slot;
        slot.uniforms.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, sizeof(Uniforms)));
        slot.uniforms->create();
        slot.bindings.reset(m_rhi->newShaderResourceBindings());
        m_slots.push_back(std::move(slot));
    }
    DrawSlot& slot = m_slots[m_drawIndex++];
    slot.bindings->setBindings({
        QRhiShaderResourceBinding::uniformBuffer(
            0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage, slot.uniforms.get()),
        QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage,
                                                  m_canvas[std::size_t(source)].get(), m_sampler.get()),
        QRhiShaderResourceBinding::sampledTexture(2, QRhiShaderResourceBinding::FragmentStage, layer, m_sampler.get()),
    });
    if (!slot.bindings->create() || !ensurePipeline(slot.bindings.get()))
        return;

    const QSize layerSize = layer->pixelSize();
    const LayerPlacement placement =
        placeLayer(visual, m_size.width(), m_size.height(), layerSize.width(), layerSize.height());
    const auto& m = placement.canvasToLayer;
    Uniforms u{};
    u.texelSize[0] = 1.0f / float(m_size.width());
    u.texelSize[1] = 1.0f / float(m_size.height());
    u.flipY = m_rhi->isYUpInNDC() ? 1.0f : 0.0f;
    u.mapU[0] = float(m[0]);
    u.mapU[1] = float(m[1]);
    u.mapU[2] = float(m[2]);
    u.mapV[0] = float(m[3]);
    u.mapV[1] = float(m[4]);
    u.mapV[2] = float(m[5]);
    for (int i = 0; i < 4; ++i)
        u.cropRect[i] = float(placement.cropRect[std::size_t(i)]);
    u.opacity = float(visual.opacity);
    u.blendMode = std::int32_t(visual.blend);

    QRhiResourceUpdateBatch* updates = m_rhi->nextResourceUpdateBatch();
    updates->updateDynamicBuffer(slot.uniforms.get(), 0, sizeof(Uniforms), &u);
    cb->beginPass(m_targets[std::size_t(target)].get(), Qt::black, {1.0f, 0}, updates);
    cb->setGraphicsPipeline(m_pipeline.get());
    cb->setViewport({0, 0, float(m_size.width()), float(m_size.height())});
    cb->setShaderResources(slot.bindings.get());
    cb->draw(4);
    cb->endPass();
    m_current = target;
}

QRhiTexture* Compositor::result() const
{
    return m_canvas[std::size_t(m_current)].get();
}

} // namespace tve
