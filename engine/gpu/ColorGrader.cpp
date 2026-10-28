#include "gpu/ColorGrader.h"

#include "io/LutFile.h"

#include <QFile>
#include <QFloat16>
#include <rhi/qrhi.h>

#include <algorithm>
#include <cstring>

namespace tve {

namespace {

constexpr int kCurveSize = 256;

// Matches the EffectParams block in grade.frag.
struct Uniforms {
    float texelSize[2];
    float time;
    float flipY;
    float linearGain[4];
    float lift[4];
    float gammaExp[4];
    float gain[4];
    float offset[4];
    float lutMin[4];
    float lutScale[4];
    float hslBands[8][4];
    float flags[4];
};
static_assert(sizeof(Uniforms) == 272);

QShader readShader(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning("ColorGrader: missing shader %s", qPrintable(path));
        return {};
    }
    return QShader::fromSerialized(file.readAll());
}

QByteArray halves(const float* values, std::size_t count)
{
    QByteArray bytes(qsizetype(count * sizeof(qfloat16)), Qt::Uninitialized);
    qFloatToFloat16(reinterpret_cast<qfloat16*>(bytes.data()), values, qsizetype(count));
    return bytes;
}

// A 3D texture of n³ RGBA16F texels from RGB floats, one depth slice per upload entry.
std::unique_ptr<QRhiTexture> make3d(QRhi* rhi, QRhiCommandBuffer* cb, int n, const float* rgb)
{
    std::unique_ptr<QRhiTexture> texture(rhi->newTexture(QRhiTexture::RGBA16F, n, n, n, 1, QRhiTexture::ThreeDimensional));
    if (!texture->create())
        return nullptr;
    QList<QRhiTextureUploadEntry> entries;
    std::vector<float> slice(std::size_t(n) * std::size_t(n) * 4);
    for (int z = 0; z < n; ++z) {
        for (int i = 0; i < n * n; ++i) {
            const std::size_t src = (std::size_t(z) * std::size_t(n) * std::size_t(n) + std::size_t(i)) * 3;
            for (int c = 0; c < 3; ++c)
                slice[std::size_t(i) * 4 + std::size_t(c)] = rgb ? rgb[src + std::size_t(c)] : 0.0f;
            slice[std::size_t(i) * 4 + 3] = 1.0f;
        }
        entries.append(QRhiTextureUploadEntry(z, 0, QRhiTextureSubresourceUploadDescription(halves(slice.data(), slice.size()))));
    }
    QRhiResourceUpdateBatch* updates = rhi->nextResourceUpdateBatch();
    QRhiTextureUploadDescription description;
    description.setEntries(entries.cbegin(), entries.cend());
    updates->uploadTexture(texture.get(), description);
    cb->resourceUpdate(updates);
    return texture;
}

} // namespace

Lut3d readLut(const std::string& path)
{
    if (!path.starts_with(":/"))
        return loadLut(path);
    QFile file(QString::fromStdString(path));
    if (!file.open(QIODevice::ReadOnly))
        throw LutError("no built-in LUT " + path);
    const std::string text = file.readAll().toStdString();
    if (path.ends_with(".cube"))
        return parseCubeLut(text);
    if (path.ends_with(".3dl"))
        return parse3dlLut(text);
    throw LutError("unsupported LUT format " + path);
}

ColorGrader::ColorGrader(QRhi* rhi) : m_rhi(rhi)
{
    m_vertex = readShader(QStringLiteral(":/effects/common/fullscreen.vert.qsb"));
    m_fragment = readShader(QStringLiteral(":/gpu/shaders/grade.frag.qsb"));
    m_sampler.reset(m_rhi->newSampler(QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None,
                                      QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
    m_sampler->create();
}

ColorGrader::~ColorGrader() = default;

bool ColorGrader::isValid() const
{
    return m_vertex.isValid() && m_fragment.isValid() && m_rhi->isFeatureSupported(QRhi::ThreeDimensionalTextures);
}

void ColorGrader::begin()
{
    m_used = 0;
}

ColorGrader::Slot* ColorGrader::nextSlot(QSize size)
{
    if (m_used == m_slots.size()) {
        Slot slot;
        slot.uniforms.reset(m_rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, sizeof(Uniforms)));
        slot.curves.reset(m_rhi->newTexture(QRhiTexture::RGBA16F, QSize(kCurveSize, 2)));
        if (!slot.uniforms->create() || !slot.curves->create())
            return nullptr;
        slot.bindings.reset(m_rhi->newShaderResourceBindings());
        m_slots.push_back(std::move(slot));
    }
    Slot& slot = m_slots[m_used];
    if (!slot.output || slot.output->pixelSize() != size) {
        slot.target.reset();
        slot.output.reset(m_rhi->newTexture(QRhiTexture::RGBA16F, size, 1,
                                            QRhiTexture::RenderTarget | QRhiTexture::UsedAsTransferSource));
        if (!slot.output->create())
            return nullptr;
        slot.target.reset(m_rhi->newTextureRenderTarget({slot.output.get()}));
        if (!m_renderPass)
            m_renderPass.reset(slot.target->newCompatibleRenderPassDescriptor());
        slot.target->setRenderPassDescriptor(m_renderPass.get());
        if (!slot.target->create())
            return nullptr;
    }
    ++m_used;
    return &slot;
}

ColorGrader::LoadedLut& ColorGrader::lut(QRhiCommandBuffer* cb, const std::string& path)
{
    if (auto found = m_luts.find(path); found != m_luts.end())
        return found->second;
    LoadedLut& loaded = m_luts[path];
    try {
        const Lut3d table = readLut(path);
        loaded.texture = make3d(m_rhi, cb, table.size, table.rgb.data());
        if (!loaded.texture) {
            loaded.error = "cannot create the LUT texture";
            return loaded;
        }
        loaded.size = table.size;
        for (int c = 0; c < 3; ++c) {
            loaded.domainMin[c] = table.domainMin[std::size_t(c)];
            loaded.domainScale[c] = 1.0f / (table.domainMax[std::size_t(c)] - table.domainMin[std::size_t(c)]);
        }
    } catch (const std::exception& e) {
        loaded.error = e.what();
        qWarning("ColorGrader: LUT %s left out: %s", path.c_str(), e.what());
    }
    return loaded;
}

std::string ColorGrader::lutError(const std::string& path) const
{
    const auto found = m_luts.find(path);
    return found != m_luts.end() ? found->second.error : std::string();
}

QRhiTexture* ColorGrader::apply(QRhiCommandBuffer* cb, QRhiTexture* source, const ColorGrade& grade)
{
    if (!source || grade.isNeutral() || !isValid())
        return source;
    const LoadedLut* table = nullptr;
    if (!grade.lutPath.empty() && grade.lutIntensity > 0.0) {
        const LoadedLut& loaded = lut(cb, grade.lutPath);
        if (loaded.texture)
            table = &loaded;
    }
    ColorGrade withoutLut = grade;
    withoutLut.lutPath.clear();
    withoutLut.lutIntensity = 1.0;
    if (!table && withoutLut.isNeutral())
        return source; // only a LUT, and it is unusable

    if (!m_noLut) {
        m_noLut = make3d(m_rhi, cb, 2, nullptr);
        if (!m_noLut)
            return source;
    }
    const QSize size = source->pixelSize();
    Slot* slot = nextSlot(size);
    if (!slot)
        return source;
    slot->bindings->setBindings({
        QRhiShaderResourceBinding::uniformBuffer(
            0, QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage, slot->uniforms.get()),
        QRhiShaderResourceBinding::sampledTexture(1, QRhiShaderResourceBinding::FragmentStage, source, m_sampler.get()),
        QRhiShaderResourceBinding::sampledTexture(2, QRhiShaderResourceBinding::FragmentStage, slot->curves.get(),
                                                  m_sampler.get()),
        QRhiShaderResourceBinding::sampledTexture(3, QRhiShaderResourceBinding::FragmentStage,
                                                  table ? table->texture.get() : m_noLut.get(), m_sampler.get()),
    });
    if (!slot->bindings->create())
        return source;
    if (!m_pipeline) {
        m_pipeline.reset(m_rhi->newGraphicsPipeline());
        m_pipeline->setTopology(QRhiGraphicsPipeline::TriangleStrip);
        m_pipeline->setShaderStages({{QRhiShaderStage::Vertex, m_vertex}, {QRhiShaderStage::Fragment, m_fragment}});
        m_pipeline->setVertexInputLayout({});
        m_pipeline->setShaderResourceBindings(slot->bindings.get());
        m_pipeline->setRenderPassDescriptor(m_renderPass.get());
        if (!m_pipeline->create()) {
            m_pipeline.reset();
            return source;
        }
    }

    const GradeCoefficients c = gradeCoefficients(grade);
    Uniforms u{};
    u.texelSize[0] = 1.0f / float(size.width());
    u.texelSize[1] = 1.0f / float(size.height());
    u.flipY = m_rhi->isYUpInNDC() ? 1.0f : 0.0f;
    for (int i = 0; i < 3; ++i) {
        u.linearGain[i] = c.linearGain[std::size_t(i)];
        u.lift[i] = c.lift[std::size_t(i)];
        u.gammaExp[i] = c.gammaExponent[std::size_t(i)];
        u.gain[i] = c.gain[std::size_t(i)];
        u.offset[i] = c.offset[std::size_t(i)];
    }
    u.lift[3] = c.contrast;
    u.gammaExp[3] = c.saturation;
    u.gain[3] = c.curves ? 1.0f : 0.0f;
    u.offset[3] = table ? float(grade.lutIntensity) : 0.0f;
    for (int b = 0; b < kHslBands; ++b) {
        for (int k = 0; k < 3; ++k)
            u.hslBands[b][k] = c.hsl[std::size_t(b)][std::size_t(k)];
        u.hslBands[b][3] = float(kHslBandHues[std::size_t(b)]);
    }
    u.flags[0] = c.hslActive ? 1.0f : 0.0f;
    u.flags[1] = c.hueCurves ? 1.0f : 0.0f;
    if (table) {
        for (int i = 0; i < 3; ++i) {
            u.lutMin[i] = table->domainMin[i];
            u.lutScale[i] = table->domainScale[i];
        }
        u.lutMin[3] = float(table->size);
    }

    QRhiResourceUpdateBatch* updates = m_rhi->nextResourceUpdateBatch();
    updates->updateDynamicBuffer(slot->uniforms.get(), 0, sizeof(Uniforms), &u);
    if (c.curves || c.hueCurves) {
        // Row 0: the tone curves; row 1: the hue curves.
        const auto tone = bakeCurves(grade, kCurveSize);
        const auto hue = bakeHueCurves(grade, kCurveSize);
        std::vector<float> rgba(kCurveSize * 2 * 4, 1.0f);
        for (int i = 0; i < kCurveSize; ++i) {
            for (int ch = 0; ch < 3; ++ch) {
                rgba[std::size_t(i) * 4 + std::size_t(ch)] = tone[std::size_t(ch)][std::size_t(i)];
                rgba[std::size_t(kCurveSize + i) * 4 + std::size_t(ch)] = hue[std::size_t(ch)][std::size_t(i)];
            }
        }
        if (rgba != slot->curveData) {
            updates->uploadTexture(slot->curves.get(), QRhiTextureUploadDescription(QRhiTextureUploadEntry(
                                                           0, 0, QRhiTextureSubresourceUploadDescription(halves(rgba.data(), rgba.size())))));
            slot->curveData = std::move(rgba);
        }
    }
    cb->beginPass(slot->target.get(), QColor(0, 0, 0, 0), {1.0f, 0}, updates);
    cb->setGraphicsPipeline(m_pipeline.get());
    cb->setViewport({0, 0, float(size.width()), float(size.height())});
    cb->setShaderResources(slot->bindings.get());
    cb->draw(4);
    cb->endPass();
    return slot->output.get();
}

} // namespace tve
