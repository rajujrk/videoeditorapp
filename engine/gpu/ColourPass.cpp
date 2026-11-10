#include "gpu/ColourPass.h"

#include <QFile>

namespace tve {

namespace {

// Matches the EffectParams block in colour.frag.
struct Uniforms {
    float texelSize[2];
    float time;
    float flipY;
    float rows[3][4];
    float params[4];
};
static_assert(sizeof(Uniforms) == 80);

QShader readShader(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning("ColourPass: missing shader %s", qPrintable(path));
        return {};
    }
    return QShader::fromSerialized(file.readAll());
}

int transferIndex(Transfer t)
{
    switch (t) {
    case Transfer::Sdr: return 0;
    case Transfer::Linear: return 1;
    case Transfer::Pq: return 2;
    case Transfer::Hlg: return 3;
    }
    return 0;
}

} // namespace

ColourPass::ColourPass(QRhi* rhi) : m_rhi(rhi)
{
    m_vertex = readShader(QStringLiteral(":/effects/common/fullscreen.vert.qsb"));
    m_fragment = readShader(QStringLiteral(":/gpu/shaders/colour.frag.qsb"));
    m_sampler.reset(m_rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None,
                                      QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
    m_sampler->create();
}

ColourPass::~ColourPass() = default;

bool ColourPass::isValid() const
{
    return m_vertex.isValid() && m_fragment.isValid();
}

void ColourPass::begin()
{
    m_used = 0;
}

ColourPass::Slot* ColourPass::nextSlot(QSize size)
{
    if (m_used == m_slots.size()) {
        Slot slot;
        slot.uniforms.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, sizeof(Uniforms)));
        if (!slot.uniforms->create())
            return nullptr;
        slot.bindings.reset(m_rhi->newShaderResourceBindings());
        m_slots.push_back(std::move(slot));
    }
    Slot& slot = m_slots[m_used];
    if (!slot.output || slot.output->pixelSize() != size) {
        slot.target.reset();
        slot.output.reset(m_rhi->newTexture(QRhiTexture::RGBA16F, size, 1,
                                            QRhiTexture::RenderTarget | QRhiTexture::UsedAsTransferSource));
        if (!slot.output->create())
            return nullptr;
        slot.target.reset(m_rhi->newTextureRenderTarget({slot.output.get()}));
        if (!m_renderPass)
            m_renderPass.reset(slot.target->newCompatibleRenderPassDescriptor());
        slot.target->setRenderPassDescriptor(m_renderPass.get());
        if (!slot.target->create())
            return nullptr;
    }
    ++m_used;
    return &slot;
}

QRhiTexture* ColourPass::apply(QRhiCommandBuffer* cb, QRhiTexture* source, const ColourConversion& conversion)
{
    if (!source || conversion.identity || !isValid())
        return source;
    const QSize size = source->pixelSize();
    Slot* slot = nextSlot(size);
    if (!slot)
        return source;
    slot->bindings->setBindings({
        QRhiShaderResourceBinding::uniformBuffer(
            0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage, slot->uniforms.get()),
        QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage, source, m_sampler.get()),
    });
    if (!slot->bindings->create())
        return source;
    if (!m_pipeline) {
        m_pipeline.reset(m_rhi->newGraphicsPipeline());
        m_pipeline->setTopology(QRhiGraphicsPipeline::TriangleStrip);
        m_pipeline->setShaderStages({{QRhiShaderStage::Vertex, m_vertex}, {QRhiShaderStage::Fragment, m_fragment}});
        m_pipeline->setVertexInputLayout({});
        m_pipeline->setShaderResourceBindings(slot->bindings.get());
        m_pipeline->setRenderPassDescriptor(m_renderPass.get());
        if (!m_pipeline->create()) {
            m_pipeline.reset();
            return source;
        }
    }
    Uniforms u{};
    u.texelSize[0] = 1.0f / float(size.width());
    u.texelSize[1] = 1.0f / float(size.height());
    u.flipY = m_rhi->isYUpInNDC() ? 1.0f : 0.0f;
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c)
            u.rows[r][c] = conversion.matrix[std::size_t(r * 3 + c)];
    u.params[0] = float(transferIndex(conversion.from));
    u.params[1] = float(transferIndex(conversion.to));
    u.params[2] = conversion.targetPeak;
    QRhiResourceUpdateBatch* updates = m_rhi->nextResourceUpdateBatch();
    updates->updateDynamicBuffer(slot->uniforms.get(), 0, sizeof(Uniforms), &u);
    cb->beginPass(slot->target.get(), Qt::black, {1.0f, 0}, updates);
    cb->setGraphicsPipeline(m_pipeline.get());
    cb->setViewport({0, 0, float(size.width()), float(size.height())});
    cb->setShaderResources(slot->bindings.get());
    cb->draw(4);
    cb->endPass();
    return slot->output.get();
}

} // namespace tve
