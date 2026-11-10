#pragma once

#include "model/Colour.h"

#include <rhi/qrhi.h>

#include <memory>
#include <vector>

namespace tve {

// Colour management on the GPU (shaders/colour.frag): a picture from one colour space to
// another into an RGBA16F texture of its size. An identity conversion is skipped.
class ColourPass {
public:
    explicit ColourPass(QRhi* rhi);
    ~ColourPass();

    ColourPass(const ColourPass&) = delete;
    ColourPass& operator=(const ColourPass&) = delete;

    bool isValid() const;
    // Starts a frame: later apply() calls reuse the outputs from the start.
    void begin();
    // Records the pass for `source` (outside a render pass): the converted picture, valid
    // until the next begin(), or `source` itself when nothing changes or it can't be built.
    QRhiTexture* apply(QRhiCommandBuffer* cb, QRhiTexture* source, const ColourConversion& conversion);

private:
    struct Slot {
        std::unique_ptr<QRhiTexture> output;
        std::unique_ptr<QRhiTextureRenderTarget> target;
        std::unique_ptr<QRhiBuffer> uniforms;
        std::unique_ptr<QRhiShaderResourceBindings> bindings;
    };
    Slot* nextSlot(QSize size);

    QRhi* m_rhi;
    QShader m_vertex;
    QShader m_fragment;
    std::unique_ptr<QRhiSampler> m_sampler;
    std::unique_ptr<QRhiRenderPassDescriptor> m_renderPass;
    std::unique_ptr<QRhiGraphicsPipeline> m_pipeline;
    std::vector<Slot> m_slots;
    std::size_t m_used = 0;
};

} // namespace tve
