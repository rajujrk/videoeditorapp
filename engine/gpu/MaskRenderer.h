#pragma once

#include "model/Masks.h"

#include <rhi/qshader.h>
#include <QSize>

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

// Cuts pictures out on the GPU: a clip's chroma key and masks (mask.frag), and track mattes
// (matte.frag). Each pass renders into an RGBA16F texture of its input's size.
class MaskRenderer {
public:
    explicit MaskRenderer(QRhi* rhi);
    ~MaskRenderer();

    MaskRenderer(const MaskRenderer&) = delete;
    MaskRenderer& operator=(const MaskRenderer&) = delete;

    bool isValid() const;

    // Starts a frame: later passes reuse the outputs from the start.
    void begin();

    // The picture with its key and masks applied; `picture` itself when there are none (or
    // the pass cannot be built). Valid until the next begin(). Outside a render pass.
    QRhiTexture* apply(QRhiCommandBuffer* cb, QRhiTexture* picture, const ChromaKey& key,
                       const std::vector<ClipMask>& masks);

    // `layer` kept where `matte` lets it through (both the same size: placed canvases).
    QRhiTexture* combineMatte(QRhiCommandBuffer* cb, QRhiTexture* layer, QRhiTexture* matte, MatteMode mode);

private:
    struct Slot {
        std::unique_ptr<QRhiTexture> output;
        std::unique_ptr<QRhiTextureRenderTarget> target;
        std::unique_ptr<QRhiBuffer> uniforms;
        std::unique_ptr<QRhiShaderResourceBindings> bindings;
    };

    Slot* nextSlot(QSize size, int uniformBytes);
    bool draw(QRhiCommandBuffer* cb, Slot& slot, std::unique_ptr<QRhiGraphicsPipeline>& pipeline,
              const QShader& fragment, const void* uniforms, int uniformBytes);

    QRhi* m_rhi;
    QShader m_vertex;
    QShader m_maskFragment;
    QShader m_matteFragment;
    std::unique_ptr<QRhiSampler> m_sampler;
    std::unique_ptr<QRhiRenderPassDescriptor> m_renderPass;
    std::unique_ptr<QRhiGraphicsPipeline> m_maskPipeline;
    std::unique_ptr<QRhiGraphicsPipeline> m_mattePipeline;
    std::vector<Slot> m_slots;
    std::size_t m_used = 0;
};

} // namespace tve
