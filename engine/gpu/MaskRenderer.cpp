#include "gpu/MaskRenderer.h"

#include <QFile>
#include <rhi/qrhi.h>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace tve {

namespace {

// Matches the EffectParams block in mask.frag.
struct MaskUniforms {
    float texelSize[2];
    float time;
    float flipY;
    float key[4];
    float keyParams[4];
    float maskPlace[8][4];
    float maskShape[8][4];
    float maskKind[8][4];
    float points[16][4];
};

// Matches the EffectParams block in matte.frag.
struct MatteUniforms {
    float texelSize[2];
    float time;
    float flipY;
    std::int32_t mode;
    float padding[3];
};

constexpr int kUniformBytes = int(sizeof(MaskUniforms));

QShader readShader(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning("MaskRenderer: missing shader %s", qPrintable(path));
        return {};
    }
    return QShader::fromSerialized(file.readAll());
}

} // namespace

MaskRenderer::MaskRenderer(QRhi* rhi) : m_rhi(rhi)
{
    m_vertex = readShader(QStringLiteral(":/effects/common/fullscreen.vert.qsb"));
    m_maskFragment = readShader(QStringLiteral(":/gpu/shaders/mask.frag.qsb"));
    m_matteFragment = readShader(QStringLiteral(":/gpu/shaders/matte.frag.qsb"));
    m_sampler.reset(m_rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None,
                                      QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
    m_sampler->create();
}

MaskRenderer::~MaskRenderer() = default;

bool MaskRenderer::isValid() const
{
    return m_vertex.isValid() && m_maskFragment.isValid() && m_matteFragment.isValid();
}

void MaskRenderer::begin()
{
    m_used = 0;
}

MaskRenderer::Slot* MaskRenderer::nextSlot(QSize size, int uniformBytes)
{
    if (m_used == m_slots.size()) {
        Slot slot;
        slot.uniforms.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, kUniformBytes));
        if (!slot.uniforms->create())
            return nullptr;
        slot.bindings.reset(m_rhi->newShaderResourceBindings());
        m_slots.push_back(std::move(slot));
    }
    Q_UNUSED(uniformBytes);
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

bool MaskRenderer::draw(QRhiCommandBuffer* cb, Slot& slot, std::unique_ptr<QRhiGraphicsPipeline>& pipeline,
                        const QShader& fragment, const void* uniforms, int uniformBytes)
{
    if (!slot.bindings->create())
        return false;
    if (!pipeline) {
        pipeline.reset(m_rhi->newGraphicsPipeline());
        pipeline->setTopology(QRhiGraphicsPipeline::TriangleStrip);
        pipeline->setShaderStages({{QRhiShaderStage::Vertex, m_vertex}, {QRhiShaderStage::Fragment, fragment}});
        pipeline->setVertexInputLayout({});
        pipeline->setShaderResourceBindings(slot.bindings.get());
        pipeline->setRenderPassDescriptor(m_renderPass.get());
        if (!pipeline->create()) {
            pipeline.reset();
            return false;
        }
    }
    const QSize size = slot.output->pixelSize();
    QRhiResourceUpdateBatch* updates = m_rhi->nextResourceUpdateBatch();
    updates->updateDynamicBuffer(slot.uniforms.get(), 0, uniformBytes, uniforms);
    cb->beginPass(slot.target.get(), QColor(0, 0, 0, 0), {1.0f, 0}, updates);
    cb->setGraphicsPipeline(pipeline.get());
    cb->setViewport({0, 0, float(size.width()), float(size.height())});
    cb->setShaderResources(slot.bindings.get());
    cb->draw(4);
    cb->endPass();
    return true;
}

QRhiTexture* MaskRenderer::apply(QRhiCommandBuffer* cb, QRhiTexture* picture, const ChromaKey& key,
                                 const std::vector<ClipMask>& masks)
{
    if (!picture || (!key.enabled && masks.empty()) || !isValid())
        return picture;
    const QSize size = picture->pixelSize();
    Slot* slot = nextSlot(size, kUniformBytes);
    if (!slot)
        return picture;
    slot->bindings->setBindings({
        QRhiShaderResourceBinding::uniformBuffer(
            0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage, slot->uniforms.get()),
        QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage, picture, m_sampler.get()),
    });

    MaskUniforms u{};
    u.texelSize[0] = 1.0f / float(size.width());
    u.texelSize[1] = 1.0f / float(size.height());
    u.flipY = m_rhi->isYUpInNDC() ? 1.0f : 0.0f;
    for (int c = 0; c < 3; ++c)
        u.key[c] = float(key.color[std::size_t(c)]);
    u.key[3] = key.enabled ? 1.0f : 0.0f;
    u.keyParams[0] = float(key.tolerance);
    u.keyParams[1] = float(key.softness);
    u.keyParams[2] = float(key.spill);
    const std::size_t count = std::min(masks.size(), kMaxMasks);
    u.keyParams[3] = float(count);
    int point = 0;
    const float shorter = float(std::min(size.width(), size.height()));
    for (std::size_t i = 0; i < count; ++i) {
        const ClipMask& m = masks[i];
        u.maskPlace[i][0] = float(m.x);
        u.maskPlace[i][1] = float(m.y);
        u.maskPlace[i][2] = float(m.width);
        u.maskPlace[i][3] = float(m.height);
        u.maskShape[i][0] = float(m.rotation * std::numbers::pi / 180.0);
        u.maskShape[i][1] = float(m.roundness);
        u.maskShape[i][2] = float(m.feather) * shorter;
        u.maskShape[i][3] = m.invert ? 1.0f : 0.0f;
        u.maskKind[i][0] = float(int(m.shape));
        if (m.shape == MaskShape::Pen) {
            const int first = point;
            for (const auto& p : m.points) {
                if (point >= int(kMaxPenPoints))
                    break;
                u.points[point / 2][(point % 2) * 2] = float(p[0]);
                u.points[point / 2][(point % 2) * 2 + 1] = float(p[1]);
                ++point;
            }
            u.maskKind[i][1] = float(first);
            u.maskKind[i][2] = float(point - first);
        }
    }
    return draw(cb, *slot, m_maskPipeline, m_maskFragment, &u, kUniformBytes) ? slot->output.get() : picture;
}

QRhiTexture* MaskRenderer::combineMatte(QRhiCommandBuffer* cb, QRhiTexture* layer, QRhiTexture* matte, MatteMode mode)
{
    if (!layer || !matte || mode == MatteMode::None || !isValid())
        return layer;
    const QSize size = layer->pixelSize();
    Slot* slot = nextSlot(size, int(sizeof(MatteUniforms)));
    if (!slot)
        return layer;
    slot->bindings->setBindings({
        QRhiShaderResourceBinding::uniformBuffer(
            0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage, slot->uniforms.get()),
        QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage, layer, m_sampler.get()),
        QRhiShaderResourceBinding::sampledTexture(2, QRhiShaderResourceBinding::FragmentStage, matte, m_sampler.get()),
    });
    MatteUniforms u{};
    u.texelSize[0] = 1.0f / float(size.width());
    u.texelSize[1] = 1.0f / float(size.height());
    u.flipY = m_rhi->isYUpInNDC() ? 1.0f : 0.0f;
    u.mode = int(mode);
    return draw(cb, *slot, m_mattePipeline, m_matteFragment, &u, int(sizeof(MatteUniforms))) ? slot->output.get() : layer;
}

} // namespace tve
