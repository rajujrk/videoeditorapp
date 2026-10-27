#pragma once

#include "gpu/EffectLibrary.h"

#include <QSize>

#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

class QRhi;
class QRhiBuffer;
class QRhiCommandBuffer;
class QRhiGraphicsPipeline;
class QRhiRenderPassDescriptor;
class QRhiSampler;
class QRhiShaderResourceBindings;
class QRhiTexture;
class QRhiTextureRenderTarget;

namespace tve {

// Blends two pictures of the same size with a transition from the library. Every render()
// within a frame gets its own output texture and uniforms, because all passes are recorded
// before the GPU runs them.
class TransitionRenderer {
public:
    TransitionRenderer(QRhi* rhi, const EffectLibrary& library);
    ~TransitionRenderer();

    TransitionRenderer(const TransitionRenderer&) = delete;
    TransitionRenderer& operator=(const TransitionRenderer&) = delete;

    // Starts a frame: later render() calls reuse the outputs from the start.
    void begin();

    // Records one pass blending `from` into `to` at `progress` (0–1), at the size of
    // `from`. Returns the output (valid until the next begin()), or nullptr if the
    // transition is unknown or its pipeline cannot be built. Must be called outside a
    // render pass.
    QRhiTexture* render(QRhiCommandBuffer* cb, std::string_view transitionId,
                        const std::map<std::string, std::vector<float>>& values, float progress, QRhiTexture* from,
                        QRhiTexture* to, float timeSeconds);

private:
    struct Slot {
        std::unique_ptr<QRhiTexture> output;
        std::unique_ptr<QRhiTextureRenderTarget> target;
        std::unique_ptr<QRhiBuffer> uniforms;
        std::unique_ptr<QRhiShaderResourceBindings> bindings;
    };
    static constexpr int kUniformBytes = 256;

    Slot* nextSlot(QSize size);
    QRhiGraphicsPipeline* pipelineFor(const LoadedEffect& transition, QRhiShaderResourceBindings* layout);

    QRhi* m_rhi;
    const EffectLibrary& m_library;
    std::unique_ptr<QRhiSampler> m_sampler;
    std::unique_ptr<QRhiRenderPassDescriptor> m_renderPass;
    std::map<std::string, std::unique_ptr<QRhiGraphicsPipeline>, std::less<>> m_pipelines;
    std::vector<Slot> m_slots;
    std::size_t m_used = 0;
};

} // namespace tve
