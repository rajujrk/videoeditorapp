#pragma once

#include "model/Project.h"

#include <rhi/qrhi.h>

#include <memory>
#include <vector>

namespace tve {

// 360° video on the GPU (shaders/sphere.frag): an equirectangular picture reframed into a
// flat view of `size` (in the view's direction, with its field of view), or turned so the
// view's direction is its front. Output RGBA16F, valid until the next begin().
class SpherePass {
public:
    explicit SpherePass(QRhi* rhi);
    ~SpherePass();

    SpherePass(const SpherePass&) = delete;
    SpherePass& operator=(const SpherePass&) = delete;

    bool isValid() const;
    void begin();
    // Outside a render pass; `source` itself if it can't be built.
    QRhiTexture* reframe(QRhiCommandBuffer* cb, QRhiTexture* source, const ClipSphereView& view, QSize size);
    // `source` itself when the view isn't turned.
    QRhiTexture* turn(QRhiCommandBuffer* cb, QRhiTexture* source, const ClipSphereView& view);

private:
    struct Slot {
        std::unique_ptr<QRhiTexture> output;
        std::unique_ptr<QRhiTextureRenderTarget> target;
        std::unique_ptr<QRhiBuffer> uniforms;
        std::unique_ptr<QRhiShaderResourceBindings> bindings;
    };
    QRhiTexture* run(QRhiCommandBuffer* cb, QRhiTexture* source, const ClipSphereView& view, QSize size, bool turning);

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
