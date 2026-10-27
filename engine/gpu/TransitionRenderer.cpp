#include "gpu/TransitionRenderer.h"

#include <rhi/qrhi.h>

namespace tve {

TransitionRenderer::TransitionRenderer(QRhi* rhi, const EffectLibrary& library) : m_rhi(rhi), m_library(library)
{
    m_sampler.reset(m_rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None,
                                      QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
    m_sampler->create();
}

TransitionRenderer::~TransitionRenderer() = default;

void TransitionRenderer::begin()
{
    m_used = 0;
}

TransitionRenderer::Slot* TransitionRenderer::nextSlot(QSize size)
{
    if (m_used == m_slots.size()) {
        Slot slot;
        slot.uniforms.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, kUniformBytes));
        if (!slot.uniforms->create())
            return nullptr;
        slot.bindings.reset(m_rhi->newShaderResourceBindings());
        m_slots.push_back(std::move(slot));
    }
    Slot& slot = m_slots[m_used];
    if (!slot.output || slot.output->pixelSize() != size) {
        const QRhiTexture::Format format =
            m_rhi->isTextureFormatSupported(QRhiTexture::RGBA16F) ? QRhiTexture::RGBA16F : QRhiTexture::RGBA8;
        slot.target.reset();
        slot.output.reset(m_rhi->newTexture(format, size, 1,
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

QRhiGraphicsPipeline* TransitionRenderer::pipelineFor(const LoadedEffect& transition, QRhiShaderResourceBindings* layout)
{
    auto found = m_pipelines.find(transition.manifest.id);
    if (found != m_pipelines.end())
        return found->second.get();
    std::unique_ptr<QRhiGraphicsPipeline> pipeline(m_rhi->newGraphicsPipeline());
    pipeline->setTopology(QRhiGraphicsPipeline::TriangleStrip);
    pipeline->setShaderStages({
        {QRhiShaderStage::Vertex, m_library.vertexShader()},
        {QRhiShaderStage::Fragment, transition.passes.front().fragment},
    });
    pipeline->setVertexInputLayout({});
    pipeline->setShaderResourceBindings(layout);
    pipeline->setRenderPassDescriptor(m_renderPass.get());
    if (!pipeline->create())
        return nullptr;
    return m_pipelines.emplace(transition.manifest.id, std::move(pipeline)).first->second.get();
}

QRhiTexture* TransitionRenderer::render(QRhiCommandBuffer* cb, std::string_view transitionId,
                                        const std::map<std::string, std::vector<float>>& values, float progress,
                                        QRhiTexture* from, QRhiTexture* to, float timeSeconds)
{
    const LoadedEffect* transition = m_library.findTransition(transitionId);
    if (!transition || !from || !to || transition->passes.front().uniformBlockSize > kUniformBytes)
        return nullptr;
    const QSize size = from->pixelSize();
    Slot* slot = nextSlot(size);
    if (!slot)
        return nullptr;

    slot->bindings->setBindings({
        QRhiShaderResourceBinding::uniformBuffer(
            0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage, slot->uniforms.get()),
        QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage, from, m_sampler.get()),
        QRhiShaderResourceBinding::sampledTexture(2, QRhiShaderResourceBinding::FragmentStage, to, m_sampler.get()),
    });
    if (!slot->bindings->create())
        return nullptr;
    QRhiGraphicsPipeline* pipeline = pipelineFor(*transition, slot->bindings.get());
    if (!pipeline)
        return nullptr;

    std::vector<char> data(kUniformBytes, 0);
    writeEffectUniforms(*transition, 0, values, size, timeSeconds, m_rhi->isYUpInNDC(), std::clamp(progress, 0.0f, 1.0f),
                        data);
    QRhiResourceUpdateBatch* updates = m_rhi->nextResourceUpdateBatch();
    updates->updateDynamicBuffer(slot->uniforms.get(), 0, kUniformBytes, data.data());

    cb->beginPass(slot->target.get(), QColor(0, 0, 0, 0), {1.0f, 0}, updates);
    cb->setGraphicsPipeline(pipeline);
    cb->setViewport({0, 0, float(size.width()), float(size.height())});
    cb->setShaderResources(slot->bindings.get());
    cb->draw(4);
    cb->endPass();
    return slot->output.get();
}

} // namespace tve
