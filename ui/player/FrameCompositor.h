#pragma once

#include "decode/LottieAnimation.h"
#include "playback/CompositeFrame.h"

#include <QSize>
#include <rhi/qshader.h>

#import <CoreVideo/CVMetalTextureCache.h>

#include <array>
#include <map>
#include <memory>
#include <vector>

class PreviewOverrides;
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
class Compositor;
class EffectChain;
class TransitionRenderer;
class ColorGrader;
class MaskRenderer;
}

// Draws composite frames onto the project canvas on the GPU (Metal), for the preview and
// for export alike:
//   1. each layer: YUV → RGB at the layer's own resolution (zero-copy from its
//      CVPixelBuffer), or a title's text; then that clip's effects
//   2. the compositor draws the layers onto the canvas (placement, crop, opacity, blend).
//      A clip with canvas effects (shadows, borders, fills) is placed on a transparent
//      canvas of its own first, those effects run there, and the result is drawn.
//      A layer in a transition places each of its two clips on a transparent canvas of
//      its own, blends them with the transition, and draws the result.
class FrameCompositor {
public:
    explicit FrameCompositor(QRhi* rhi);
    ~FrameCompositor();

    FrameCompositor(const FrameCompositor&) = delete;
    FrameCompositor& operator=(const FrameCompositor&) = delete;

    // False if the device is not Metal or the shaders are missing.
    bool isValid() const { return m_textureCache != nullptr && m_videoFrag.isValid(); }

    // Records the passes drawing `frame` on a canvas of `canvas` pixels (call outside a
    // render pass) and returns the canvas, valid until the next render(). `overrides`, if
    // given, replaces clips' committed visuals and effects (live preview). The Metal
    // textures made from the frame's pixel buffers are appended to `retained`: release
    // them (CFRelease) once the GPU has finished with this frame.
    QRhiTexture* render(QRhiCommandBuffer* cb, tve::CompositeFrame& frame, QSize canvas, float seconds,
                        PreviewOverrides* overrides, std::vector<CVMetalTextureRef>& retained);

    // Forgets per-clip state (effect chains, title textures), e.g. for a new project.
    void reset();
    // Lets Core Video recycle textures of pixel buffers no longer in use.
    void flushTextureCache();

private:
    // Per layer position within a frame: every pass of a frame is recorded before the GPU
    // runs it, so each layer needs its own textures, uniforms and bindings.
    struct LayerSlot {
        std::unique_ptr<QRhiTexture> luma;
        std::unique_ptr<QRhiTexture> chroma;
        std::unique_ptr<QRhiBuffer> uniforms;
        std::unique_ptr<QRhiShaderResourceBindings> bindings;
        std::unique_ptr<QRhiTexture> rgb;
        std::unique_ptr<QRhiTextureRenderTarget> target;
    };
    // Titles on screen: their text drawn at canvas size, redrawn only when it changes (an
    // animated title: when its moment changes).
    struct TitleTexture {
        tve::ClipText text;
        QSize canvas;
        double seconds = -1.0;
        std::unique_ptr<QRhiTexture> texture;
    };

    LayerSlot& slot(std::size_t index);
    // YUV → RGB of one layer into its slot's texture; nullptr if the buffer is unusable.
    QRhiTexture* convertLayer(QRhiCommandBuffer* cb, LayerSlot& slot, CVPixelBufferRef buffer,
                              std::vector<CVMetalTextureRef>& retained);
    QRhiTexture* titleTexture(QRhiCommandBuffer* cb, tve::Id clipId, const tve::ClipText& text, double seconds,
                              double duration);
    // A still image's picture, read once and kept while it is in use (and a few after).
    QRhiTexture* stillTexture(QRhiCommandBuffer* cb, const std::string& path);
    // A sticker's (Lottie) frame `seconds` into its clip, drawn when the frame changes.
    QRhiTexture* stickerTexture(QRhiCommandBuffer* cb, tve::Id clipId, const std::string& path, double seconds);

    QRhi* m_rhi;
    CVMetalTextureCacheRef m_textureCache = nullptr;
    std::unique_ptr<QRhiSampler> m_sampler;
    QShader m_videoVert;
    QShader m_videoFrag;
    QSize m_canvasSize;

    std::vector<LayerSlot> m_slots;
    std::unique_ptr<QRhiGraphicsPipeline> m_yuvPipeline;
    std::unique_ptr<QRhiRenderPassDescriptor> m_layerRenderPass;

    std::unique_ptr<tve::Compositor> m_compositor;
    // Per transition within a frame: the outgoing and incoming clips' own canvases.
    std::vector<std::array<std::unique_ptr<tve::Compositor>, 2>> m_transitionCanvases;
    std::unique_ptr<tve::TransitionRenderer> m_transitions;
    std::unique_ptr<tve::ColorGrader> m_grader;
    std::unique_ptr<tve::MaskRenderer> m_masks;
    // Per matted layer within a frame: the layer's and its matte's own canvases.
    std::vector<std::array<std::unique_ptr<tve::Compositor>, 2>> m_matteCanvases;
    std::map<tve::Id, TitleTexture> m_titles;
    struct StillTexture {
        std::unique_ptr<QRhiTexture> texture;
        std::uint64_t lastUsed = 0; // render count
    };
    std::map<std::string, StillTexture> m_stills;
    struct StickerTexture {
        std::string path;
        std::unique_ptr<tve::LottieAnimation> animation;
        long frame = -1;
        std::unique_ptr<QRhiTexture> texture;
    };
    std::map<tve::Id, StickerTexture> m_stickers;
    std::uint64_t m_renders = 0;
    std::map<tve::Id, std::unique_ptr<tve::EffectChain>> m_clipChains;
    std::map<tve::Id, std::unique_ptr<tve::EffectChain>> m_canvasChains; // canvas effects
    // Per clip with canvas effects within a frame: its own canvas.
    std::vector<std::unique_ptr<tve::Compositor>> m_effectCanvases;
};
