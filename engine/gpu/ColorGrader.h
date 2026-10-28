#pragma once

#include "model/ColorGrade.h"

#include <rhi/qshader.h>
#include <QSize>

#include <map>
#include <memory>
#include <string>
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

// Applies colour grades on the GPU: one pass per graded picture, into an RGBA16F texture
// of the picture's size. Each LUT file is read and uploaded once (as a 3D texture); one
// that cannot be read is left out of the grade, and lutError() says why.
class ColorGrader {
public:
    explicit ColorGrader(QRhi* rhi);
    ~ColorGrader();

    ColorGrader(const ColorGrader&) = delete;
    ColorGrader& operator=(const ColorGrader&) = delete;

    bool isValid() const;

    // Starts a frame: later apply() calls reuse the outputs from the start.
    void begin();

    // Records the grade pass for `source` (outside a render pass). Returns the graded
    // picture, valid until the next begin(), or `source` itself when the grade changes
    // nothing or the pass cannot be built.
    QRhiTexture* apply(QRhiCommandBuffer* cb, QRhiTexture* source, const ColorGrade& grade);

    // Why the LUT at `path` was left out (empty if it loaded or was never used).
    std::string lutError(const std::string& path) const;

private:
    struct Slot {
        std::unique_ptr<QRhiTexture> output;
        std::unique_ptr<QRhiTextureRenderTarget> target;
        std::unique_ptr<QRhiBuffer> uniforms;
        std::unique_ptr<QRhiTexture> curves;
        std::vector<float> curveData; // what `curves` holds, to skip identical uploads
        std::unique_ptr<QRhiShaderResourceBindings> bindings;
    };
    struct LoadedLut {
        std::unique_ptr<QRhiTexture> texture;
        int size = 0;
        float domainMin[3] = {0, 0, 0};
        float domainScale[3] = {1, 1, 1};
        std::string error;
    };

    Slot* nextSlot(QSize size);
    LoadedLut& lut(QRhiCommandBuffer* cb, const std::string& path);

    QRhi* m_rhi;
    QShader m_vertex;
    QShader m_fragment;
    std::unique_ptr<QRhiSampler> m_sampler;
    std::unique_ptr<QRhiRenderPassDescriptor> m_renderPass;
    std::unique_ptr<QRhiGraphicsPipeline> m_pipeline;
    std::unique_ptr<QRhiTexture> m_noLut; // bound when a grade has none
    std::vector<Slot> m_slots;
    std::size_t m_used = 0;
    std::map<std::string, LoadedLut> m_luts;
};

} // namespace tve
