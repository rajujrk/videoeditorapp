// Built-in looks and overlays on the real GPU (Metal, offscreen).
#include "gpu/ColorGrader.h"
#include "gpu/EffectChain.h"
#include "io/LutFile.h"

#include <QFile>
#include <QImage>
#include <rhi/qrhi.h>
#include <rhi/qrhi_platform.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <memory>

using namespace tve;

namespace {

constexpr int kW = 64;
constexpr int kH = 36;

QRhi* gpu()
{
    static std::unique_ptr<QRhi> rhi = [] {
        QRhiMetalInitParams params;
        return std::unique_ptr<QRhi>(QRhi::create(QRhi::Metal, &params));
    }();
    return rhi.get();
}

// A mid-grey picture through one effect at `seconds`; RGBA floats.
std::vector<float> throughEffect(const std::string& effectId, float seconds)
{
    QRhi* rhi = gpu();
    REQUIRE(rhi);
    QImage image(kW, kH, QImage::Format_RGBA8888);
    image.fill(QColor(128, 128, 128));
    std::unique_ptr<QRhiTexture> source(rhi->newTexture(QRhiTexture::RGBA8, image.size()));
    REQUIRE(source->create());
    EffectChain chain(rhi, EffectLibrary::builtIn());
    chain.setInstances({EffectInstance{effectId, true, {}}});
    QRhiCommandBuffer* cb = nullptr;
    REQUIRE(rhi->beginOffscreenFrame(&cb) == QRhi::FrameOpSuccess);
    QRhiResourceUpdateBatch* upload = rhi->nextResourceUpdateBatch();
    upload->uploadTexture(source.get(), image);
    cb->resourceUpdate(upload);
    QRhiTexture* output = chain.process(cb, source.get(), seconds);
    REQUIRE(output != source.get());
    QRhiReadbackResult result;
    QRhiResourceUpdateBatch* batch = rhi->nextResourceUpdateBatch();
    batch->readBackTexture({output}, &result);
    cb->resourceUpdate(batch);
    REQUIRE(rhi->endOffscreenFrame() == QRhi::FrameOpSuccess);
    std::vector<float> rgba(std::size_t(kW * kH * 4));
    REQUIRE(result.format == QRhiTexture::RGBA16F);
    qFloatFromFloat16(rgba.data(), reinterpret_cast<const qfloat16*>(result.data.constData()), qsizetype(rgba.size()));
    return rgba;
}

} // namespace

TEST_CASE("every overlay changes the picture and keeps it opaque", "[looks]")
{
    const EffectLibrary& library = EffectLibrary::builtIn();
    int overlays = 0;
    for (const LoadedEffect& effect : library.effects()) {
        if (std::ranges::find(effect.manifest.categories, "overlay") == effect.manifest.categories.end())
            continue;
        ++overlays;
        INFO(effect.manifest.id);
        CHECK(effect.manifest.license == "TVE-Original");
        // Some overlays blink or move: look at a few moments.
        float changed = 0.0f;
        for (float t : {0.25f, 0.75f, 1.6f}) {
            const auto rgba = throughEffect(effect.manifest.id, t);
            for (std::size_t i = 0; i < rgba.size(); i += 4) {
                changed = std::max(changed, std::abs(rgba[i] - 128.0f / 255.0f) + std::abs(rgba[i + 2] - 128.0f / 255.0f));
                CHECK(rgba[i + 3] == Catch::Approx(1.0f));
            }
        }
        CHECK(changed > 0.02f);
    }
    CHECK(overlays == 6);
}

TEST_CASE("built-in looks and their LUTs load", "[looks]")
{
    QFile file(QStringLiteral(":/effects/looks/looks.json"));
    REQUIRE(file.open(QIODevice::ReadOnly));
    const auto json = nlohmann::json::parse(file.readAll().toStdString());
    REQUIRE(json.at("looks").size() == 12);
    CHECK(json.at("license") == "TVE-Original");
    int luts = 0;
    for (const auto& look : json.at("looks")) {
        const auto grade = look.at("grade");
        if (!grade.contains("lutPath"))
            continue;
        ++luts;
        const Lut3d lut = readLut(grade.at("lutPath").get<std::string>());
        CHECK(lut.size == 17);
    }
    CHECK(luts == 10);
    // Sepia turns grey brown: more red than blue.
    const auto sepia = readLut(":/effects/looks/luts/sepia.cube").lookup({0.5f, 0.5f, 0.5f});
    CHECK(sepia[0] > sepia[1]);
    CHECK(sepia[1] > sepia[2]);
    // Black & white has no LUT: it is a grade.
    CHECK_THROWS_AS(readLut(":/effects/looks/luts/missing.cube"), LutError);
}
