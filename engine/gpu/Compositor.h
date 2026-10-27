#pragma once

#include "gpu/LayerPlacement.h"

#include <QSize>
#include <rhi/qshader.h>

#include <array>
#include <memory>
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

// Builds the project canvas from layers: each draw() blends one layer texture over the
// canvas so far (ping-ponging between two RGBA16F canvas textures).
class Compositor {
public:
    explicit Compositor(QRhi* rhi);
    ~Compositor();

    Compositor(const Compositor&) = delete;
    Compositor& operator=(const Compositor&) = delete;

    // Starts a black canvas. Must be called outside a render pass.
    void begin(QRhiCommandBuffer* cb, QSize canvasSize);
    // Draws `layer` (any RGBA texture of the clip's picture) with `visual`.
    void draw(QRhiCommandBuffer* cb, QRhiTexture* layer, const ClipVisual& visual);
    // The canvas as drawn so far; valid until the next begin().
    QRhiTexture* result() const;

private:
    // Per draw within a frame: all passes are recorded before the GPU runs them, so each
    // needs its own uniform buffer and bindings.
    struct DrawSlot {
        std::unique_ptr<QRhiBuffer> uniforms;
        std::unique_ptr<QRhiShaderResourceBindings> bindings;
    };

    bool ensureTargets(QSize size);
    bool ensurePipeline(QRhiShaderResourceBindings* layout);

    QRhi* m_rhi;
    QShader m_vertex;
    QShader m_fragment;
    std::unique_ptr<QRhiSampler> m_sampler;
    std::vector<DrawSlot> m_slots;
    std::size_t m_drawIndex = 0;
    std::unique_ptr<QRhiGraphicsPipeline> m_pipeline;
    std::array<std::unique_ptr<QRhiTexture>, 2> m_canvas;
    std::array<std::unique_ptr<QRhiTextureRenderTarget>, 2> m_targets;
    std::unique_ptr<QRhiRenderPassDescriptor> m_renderPass;
    QSize m_size;
    int m_current = 0; // index of the canvas texture holding the latest result
};

} // namespace tve
