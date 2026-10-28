#pragma once

#include "gpu/EffectLibrary.h"

#include <QSize>

#include <array>
#include <map>
#include <memory>
#include <string>
#include <vector>

class QRhi;
class QRhiBuffer;
class QRhiCommandBuffer;
class QRhiGraphicsPipeline;
class QRhiRenderPassDescriptor;
class QRhiResourceUpdateBatch;
class QRhiSampler;
class QRhiShaderResourceBindings;
class QRhiTexture;
class QRhiTextureRenderTarget;

namespace tve {

// One effect applied to an image, with parameter values by param id. Missing values use
// the manifest default; values outside the param's range are clamped.
struct EffectInstance {
    std::string effectId;
    bool enabled = true;
    std::map<std::string, std::vector<float>> values;

    friend bool operator==(const EffectInstance&, const EffectInstance&) = default;
};

// Applies a stack of effects on the GPU. Each effect pass renders the whole image into one
// of three internal RGBA16F textures at the source's resolution: never the one it reads,
// nor its effect's input while a later pass of that effect still samples it (tve_input).
class EffectChain {
public:
    EffectChain(QRhi* rhi, const EffectLibrary& library);
    ~EffectChain();

    EffectChain(const EffectChain&) = delete;
    EffectChain& operator=(const EffectChain&) = delete;

    void setInstances(std::vector<EffectInstance> instances);
    const std::vector<EffectInstance>& instances() const { return m_instances; }

    // Records one render pass per pass of every enabled effect, reading `source`. Returns
    // the texture holding the result, which is `source` itself when nothing is enabled.
    // Must be called outside a render pass. The returned texture stays valid until the
    // next call. `bounds` is where the picture is in `source` (for canvas effects).
    QRhiTexture* process(QRhiCommandBuffer* cb, QRhiTexture* source, float timeSeconds,
                         const EffectBounds& bounds = kWholeImage);

    int activePassCount() const { return int(m_passes.size()); }

private:
    struct Pass {
        const LoadedEffect* effect = nullptr;
        std::size_t instanceIndex = 0;
        std::size_t passIndex = 0;
        std::unique_ptr<QRhiBuffer> uniforms;
        std::unique_ptr<QRhiShaderResourceBindings> bindings;
        std::unique_ptr<QRhiGraphicsPipeline> pipeline;
        QRhiTexture* boundInput = nullptr;
        QRhiTexture* boundEffectInput = nullptr;
    };

    void rebuildPasses();
    bool ensureTargets(QSize size);
    bool bindInput(Pass& pass, QRhiTexture* input, QRhiTexture* effectInput);
    void writeUniforms(const Pass& pass, QRhiResourceUpdateBatch* updates, QSize size, float timeSeconds,
                       const EffectBounds& bounds) const;

    QRhi* m_rhi;
    const EffectLibrary& m_library;
    std::vector<EffectInstance> m_instances;
    std::vector<Pass> m_passes;
    bool m_passesDirty = true;

    std::unique_ptr<QRhiSampler> m_sampler;
    std::array<std::unique_ptr<QRhiTexture>, 3> m_textures;
    std::array<std::unique_ptr<QRhiTextureRenderTarget>, 3> m_renderTargets;
    std::unique_ptr<QRhiRenderPassDescriptor> m_renderPass;
    QSize m_size;
};

} // namespace tve
