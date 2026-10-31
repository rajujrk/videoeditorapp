#pragma once

#include "model/Masks.h"
#include "model/People.h"

#include <rhi/qshader.h>
#include <QSize>

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

// Cuts pictures out on the GPU: a clip's chroma key and masks (mask.frag), track mattes
// (matte.frag), its portrait cutout (cutout.frag) and covered faces (faces.frag). Each pass
// renders into an RGBA16F texture of its input's size.
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

    // The person kept (or, inverted, taken out) where `personMatte` (a one-channel matte of
    // any size, 1: person) says; `picture` itself when the cutout is off.
    QRhiTexture* cutout(QRhiCommandBuffer* cb, QRhiTexture* picture, QRhiTexture* personMatte,
                        const PortraitCutout& cutout);
    // The faces (picture fractions, at most kMaxCoveredFaces) covered; `picture` itself
    // when there are none.
    QRhiTexture* coverFaces(QRhiCommandBuffer* cb, QRhiTexture* picture, const std::vector<FaceBox>& faces,
                            FaceCover cover, float amount);

    // Stabilized and/or evened out (steady.frag): zoomed by `zoom`, the correction (a
    // SteadySample's dx, dy, angle, scale) applied, colour times `gain`.
    QRhiTexture* steady(QRhiCommandBuffer* cb, QRhiTexture* picture, float zoom, const std::array<float, 4>& correction,
                        float gain);
    // Denoised over time against the denoised frame before (denoise.frag); `previous` is
    // the same size.
    QRhiTexture* denoise(QRhiCommandBuffer* cb, QRhiTexture* picture, QRhiTexture* previous, float strength);

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
    QShader m_cutoutFragment;
    QShader m_facesFragment;
    QShader m_steadyFragment;
    QShader m_denoiseFragment;
    std::unique_ptr<QRhiSampler> m_sampler;
    std::unique_ptr<QRhiRenderPassDescriptor> m_renderPass;
    std::unique_ptr<QRhiGraphicsPipeline> m_maskPipeline;
    std::unique_ptr<QRhiGraphicsPipeline> m_mattePipeline;
    std::unique_ptr<QRhiGraphicsPipeline> m_cutoutPipeline;
    std::unique_ptr<QRhiGraphicsPipeline> m_facesPipeline;
    std::unique_ptr<QRhiGraphicsPipeline> m_steadyPipeline;
    std::unique_ptr<QRhiGraphicsPipeline> m_denoisePipeline;
    std::vector<Slot> m_slots;
    std::size_t m_used = 0;
};

} // namespace tve
