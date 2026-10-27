#include "gpu/EffectChain.h"

#include <rhi/qrhi.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace tve {

EffectChain::EffectChain(QRhi* rhi, const EffectLibrary& library) : m_rhi(rhi), m_library(library)
{
    m_sampler.reset(m_rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None,
                                      QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
    m_sampler->create();
}

EffectChain::~EffectChain() = default;

void EffectChain::setInstances(std::vector<EffectInstance> instances)
{
    // Pipelines only depend on which effects are enabled and in what order; value
    // changes just update uniforms.
    auto structure = [](const std::vector<EffectInstance>& list) {
        std::vector<std::string> ids;
        for (const EffectInstance& instance : list) {
            if (instance.enabled)
                ids.push_back(instance.effectId);
        }
        return ids;
    };
    if (structure(instances) != structure(m_instances))
        m_passesDirty = true;
    m_instances = std::move(instances);
}

void EffectChain::rebuildPasses()
{
    m_passes.clear();
    for (std::size_t i = 0; i < m_instances.size(); ++i) {
        if (!m_instances[i].enabled)
            continue;
        const LoadedEffect* effect = m_library.find(m_instances[i].effectId);
        if (!effect)
            continue; // unknown effect: skipped, like a missing plug-in
        for (std::size_t p = 0; p < effect->passes.size(); ++p) {
            Pass pass{effect, i, p, nullptr, nullptr, nullptr, nullptr};
            const int size = std::max(effect->passes[p].uniformBlockSize, 16);
            pass.uniforms.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, quint32(size)));
            pass.uniforms->create();
            m_passes.push_back(std::move(pass));
        }
    }
    m_passesDirty = false;
}

bool EffectChain::ensureTargets(QSize size)
{
    if (size == m_size && m_textures[0])
        return true;

    const QRhiTexture::Format format =
        m_rhi->isTextureFormatSupported(QRhiTexture::RGBA16F) ? QRhiTexture::RGBA16F : QRhiTexture::RGBA8;
    for (std::size_t i = 0; i < m_textures.size(); ++i) {
        m_renderTargets[i].reset();
        m_textures[i].reset(m_rhi->newTexture(format, size, 1,
                                              QRhiTexture::RenderTarget | QRhiTexture::UsedAsTransferSource));
        if (!m_textures[i]->create())
            return false;
        m_renderTargets[i].reset(m_rhi->newTextureRenderTarget({m_textures[i].get()}));
        if (!m_renderPass)
            m_renderPass.reset(m_renderTargets[i]->newCompatibleRenderPassDescriptor());
        m_renderTargets[i]->setRenderPassDescriptor(m_renderPass.get());
        if (!m_renderTargets[i]->create())
            return false;
    }
    for (Pass& pass : m_passes)
        pass.boundInput = nullptr; // inputs of later passes were just replaced
    m_size = size;
    return true;
}

bool EffectChain::bindInput(Pass& pass, QRhiTexture* input)
{
    if (pass.pipeline && pass.boundInput == input)
        return true;

    if (!pass.bindings)
        pass.bindings.reset(m_rhi->newShaderResourceBindings());
    pass.bindings->setBindings({
        QRhiShaderResourceBinding::uniformBuffer(
            0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage, pass.uniforms.get()),
        QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage, input, m_sampler.get()),
    });
    if (!pass.bindings->create())
        return false;
    pass.boundInput = input;

    if (!pass.pipeline) {
        pass.pipeline.reset(m_rhi->newGraphicsPipeline());
        pass.pipeline->setTopology(QRhiGraphicsPipeline::TriangleStrip);
        pass.pipeline->setShaderStages({
            {QRhiShaderStage::Vertex, m_library.vertexShader()},
            {QRhiShaderStage::Fragment, pass.effect->passes[pass.passIndex].fragment},
        });
        pass.pipeline->setVertexInputLayout({});
        pass.pipeline->setShaderResourceBindings(pass.bindings.get());
        pass.pipeline->setRenderPassDescriptor(m_renderPass.get());
        if (!pass.pipeline->create()) {
            pass.pipeline.reset();
            return false;
        }
    }
    return true;
}

void EffectChain::writeUniforms(const Pass& pass, QRhiResourceUpdateBatch* updates, QSize size, float timeSeconds) const
{
    std::vector<char> data(std::size_t(pass.uniforms->size()), 0);
    writeEffectUniforms(*pass.effect, pass.passIndex, m_instances[pass.instanceIndex].values, size, timeSeconds,
                        m_rhi->isYUpInNDC(), 0.0f, data);
    updates->updateDynamicBuffer(pass.uniforms.get(), 0, quint32(data.size()), data.data());
}

QRhiTexture* EffectChain::process(QRhiCommandBuffer* cb, QRhiTexture* source, float timeSeconds)
{
    if (m_passesDirty)
        rebuildPasses();
    if (m_passes.empty())
        return source;

    const QSize size = source->pixelSize();
    if (!ensureTargets(size))
        return source;

    QRhiTexture* input = source;
    for (std::size_t i = 0; i < m_passes.size(); ++i) {
        Pass& pass = m_passes[i];
        if (!bindInput(pass, input))
            return input; // pipeline failed to build: stop here, keep what we have
        QRhiResourceUpdateBatch* updates = m_rhi->nextResourceUpdateBatch();
        writeUniforms(pass, updates, size, timeSeconds);

        QRhiTextureRenderTarget* target = m_renderTargets[i % 2].get();
        cb->beginPass(target, Qt::black, {1.0f, 0}, updates);
        cb->setGraphicsPipeline(pass.pipeline.get());
        cb->setViewport({0, 0, float(size.width()), float(size.height())});
        cb->setShaderResources();
        cb->draw(4);
        cb->endPass();
        input = m_textures[i % 2].get();
    }
    return input;
}

} // namespace tve
