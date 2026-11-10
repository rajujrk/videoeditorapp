#include "gpu/SpherePass.h"

#include "model/Sphere.h"

#include <QFile>

#include <cmath>
#include <numbers>

namespace tve {

namespace {

// Matches the EffectParams block in sphere.frag.
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
        qWarning("SpherePass: missing shader %s", qPrintable(path));
        return {};
    }
    return QShader::fromSerialized(file.readAll());
}

} // namespace

SpherePass::SpherePass(QRhi* rhi) : m_rhi(rhi)
{
    m_vertex = readShader(QStringLiteral(":/effects/common/fullscreen.vert.qsb"));
    m_fragment = readShader(QStringLiteral(":/gpu/shaders/sphere.frag.qsb"));
    // Round the sphere across, clamped at the poles.
    m_sampler.reset(m_rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None, QRhiSampler::Repeat,
                                      QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
    m_sampler->create();
}

SpherePass::~SpherePass() = default;

bool SpherePass::isValid() const
{
    return m_vertex.isValid() && m_fragment.isValid();
}

void SpherePass::begin()
{
    m_used = 0;
}

QRhiTexture* SpherePass::reframe(QRhiCommandBuffer* cb, QRhiTexture* source, const ClipSphereView& view, QSize size)
{
    return run(cb, source, view, size, false);
}

QRhiTexture* SpherePass::turn(QRhiCommandBuffer* cb, QRhiTexture* source, const ClipSphereView& view)
{
    if (!source || !view.turned())
        return source;
    return run(cb, source, view, source->pixelSize(), true);
}

QRhiTexture* SpherePass::run(QRhiCommandBuffer* cb, QRhiTexture* source, const ClipSphereView& view, QSize size,
                             bool turning)
{
    if (!source || !isValid() || size.isEmpty())
        return source;
    if (m_used == m_slots.size()) {
        Slot slot;
        slot.uniforms.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, sizeof(Uniforms)));
        if (!slot.uniforms->create())
            return source;
        slot.bindings.reset(m_rhi->newShaderResourceBindings());
        m_slots.push_back(std::move(slot));
    }
    Slot& slot = m_slots[m_used];
    if (!slot.output || slot.output->pixelSize() != size) {
        slot.target.reset();
        slot.output.reset(m_rhi->newTexture(QRhiTexture::RGBA16F, size, 1,
                                            QRhiTexture::RenderTarget | QRhiTexture::UsedAsTransferSource));
        if (!slot.output->create())
            return source;
        slot.target.reset(m_rhi->newTextureRenderTarget({slot.output.get()}));
        if (!m_renderPass)
            m_renderPass.reset(slot.target->newCompatibleRenderPassDescriptor());
        slot.target->setRenderPassDescriptor(m_renderPass.get());
        if (!slot.target->create())
            return source;
    }
    ++m_used;
    slot.bindings->setBindings({
        QRhiShaderResourceBinding::uniformBuffer(
            0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage, slot.uniforms.get()),
        QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage, source, m_sampler.get()),
    });
    if (!slot.bindings->create())
        return source;
    if (!m_pipeline) {
        m_pipeline.reset(m_rhi->newGraphicsPipeline());
        m_pipeline->setTopology(QRhiGraphicsPipeline::TriangleStrip);
        m_pipeline->setShaderStages({{QRhiShaderStage::Vertex, m_vertex}, {QRhiShaderStage::Fragment, m_fragment}});
        m_pipeline->setVertexInputLayout({});
        m_pipeline->setShaderResourceBindings(slot.bindings.get());
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
    const SphereMatrix m = sphereRotation(view);
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c)
            u.rows[r][c] = float(m[std::size_t(r * 3 + c)]);
    const double tanX = std::tan(view.fov * std::numbers::pi / 360.0);
    u.params[0] = turning ? 1.0f : 0.0f;
    u.params[1] = float(tanX);
    u.params[2] = float(tanX * size.height() / size.width());
    QRhiResourceUpdateBatch* updates = m_rhi->nextResourceUpdateBatch();
    updates->updateDynamicBuffer(slot.uniforms.get(), 0, sizeof(Uniforms), &u);
    cb->beginPass(slot.target.get(), Qt::black, {1.0f, 0}, updates);
    cb->setGraphicsPipeline(m_pipeline.get());
    cb->setViewport({0, 0, float(size.width()), float(size.height())});
    cb->setShaderResources(slot.bindings.get());
    cb->draw(4);
    cb->endPass();
    return slot.output.get();
}

} // namespace tve
