// Colour management on the real GPU (Metal, offscreen): colour.frag against the CPU maths
// in model/Colour.h, for the conversions the editor makes.
#include "gpu/ColourPass.h"

#include <rhi/qrhi.h>
#include <rhi/qrhi_platform.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <vector>

using namespace tve;

namespace {

QRhi* gpu()
{
    static std::unique_ptr<QRhi> rhi = [] {
        QRhiMetalInitParams params;
        return std::unique_ptr<QRhi>(QRhi::create(QRhi::Metal, &params));
    }();
    return rhi.get();
}

// Signals in the source's encoding, one per pixel: black, greys, primaries, and (for HDR
// sources) highlights near the top.
const std::vector<std::array<double, 3>> kSignals{{0.0, 0.0, 0.0},  {0.18, 0.18, 0.18}, {0.5, 0.5, 0.5},
                                                  {0.58, 0.58, 0.58}, {0.75, 0.75, 0.75}, {0.9, 0.9, 0.9},
                                                  {0.7, 0.2, 0.1},  {0.1, 0.6, 0.3},  {0.2, 0.3, 0.8},
                                                  {1.0, 1.0, 1.0}};

struct Run {
    bool passedThrough = false;
    std::vector<std::array<float, 3>> pixels;
};

Run run(const ColourConversion& conversion)
{
    QRhi* rhi = gpu();
    REQUIRE(rhi);
    const QSize size(int(kSignals.size()), 1);
    std::vector<qfloat16> data;
    for (const auto& s : kSignals)
        for (double v : {s[0], s[1], s[2], 1.0})
            data.push_back(qfloat16(float(v)));
    std::unique_ptr<QRhiTexture> source(rhi->newTexture(QRhiTexture::RGBA16F, size));
    REQUIRE(source->create());
    ColourPass pass(rhi);
    REQUIRE(pass.isValid());
    QRhiCommandBuffer* cb = nullptr;
    REQUIRE(rhi->beginOffscreenFrame(&cb) == QRhi::FrameOpSuccess);
    QRhiResourceUpdateBatch* upload = rhi->nextResourceUpdateBatch();
    QRhiTextureSubresourceUploadDescription pixels(data.data(), quint32(data.size() * sizeof(qfloat16)));
    upload->uploadTexture(source.get(), QRhiTextureUploadDescription({0, 0, pixels}));
    cb->resourceUpdate(upload);
    pass.begin();
    QRhiTexture* output = pass.apply(cb, source.get(), conversion);
    Run result;
    result.passedThrough = output == source.get();
    QRhiReadbackResult readback;
    if (!result.passedThrough) {
        QRhiResourceUpdateBatch* batch = rhi->nextResourceUpdateBatch();
        batch->readBackTexture({output}, &readback);
        cb->resourceUpdate(batch);
    }
    REQUIRE(rhi->endOffscreenFrame() == QRhi::FrameOpSuccess);
    if (!result.passedThrough) {
        REQUIRE(readback.format == QRhiTexture::RGBA16F);
        std::vector<float> rgba(kSignals.size() * 4);
        qFloatFromFloat16(rgba.data(), reinterpret_cast<const qfloat16*>(readback.data.constData()), qsizetype(rgba.size()));
        for (std::size_t i = 0; i < kSignals.size(); ++i)
            result.pixels.push_back({rgba[i * 4], rgba[i * 4 + 1], rgba[i * 4 + 2]});
    }
    return result;
}

void matchesCpu(ColourSpace from, ColourSpace to, double displayPeak = 1.0)
{
    const ColourConversion c = conversion(from, to, displayPeak);
    const Run result = run(c);
    REQUIRE_FALSE(result.passedThrough);
    for (std::size_t i = 0; i < kSignals.size(); ++i) {
        const auto expected = convert(c, kSignals[i]);
        INFO("signal " << i << " from " << toString(from.transfer) << " to " << toString(to.transfer));
        for (int ch = 0; ch < 3; ++ch) {
            const double want = expected[std::size_t(ch)];
            // Half floats in and out: about three decimal digits.
            CHECK(result.pixels[i][std::size_t(ch)] == Catch::Approx(want).margin(2e-3).epsilon(2e-3));
        }
    }
}

const ColourSpace kSdr{Primaries::Rec709, Transfer::Sdr};
const ColourSpace kPq{Primaries::Rec2020, Transfer::Pq};
const ColourSpace kHlg{Primaries::Rec2020, Transfer::Hlg};
const ColourSpace kEdr{Primaries::Rec709, Transfer::Linear};

} // namespace

TEST_CASE("the same colour space costs nothing", "[colour]")
{
    CHECK(run(conversion(kSdr, kSdr)).passedThrough);
    CHECK(run(conversion(kPq, kPq)).passedThrough);
}

TEST_CASE("the GPU colour transform matches the CPU", "[colour]")
{
    SECTION("SDR into a PQ project") { matchesCpu(kSdr, kPq); }
    SECTION("SDR into an HLG project") { matchesCpu(kSdr, kHlg); }
    SECTION("Display P3 into SDR") { matchesCpu({Primaries::DisplayP3, Transfer::Sdr}, kSdr); }
    SECTION("PQ tone-mapped to SDR") { matchesCpu(kPq, kSdr); }
    SECTION("HLG tone-mapped to SDR") { matchesCpu(kHlg, kSdr); }
    SECTION("HLG into a PQ project") { matchesCpu(kHlg, kPq); }
    SECTION("PQ into an HLG project") { matchesCpu(kPq, kHlg); }
    SECTION("PQ on an EDR display with 3× headroom") { matchesCpu(kPq, kEdr, 3.0); }
    SECTION("SDR on an EDR display") { matchesCpu(kSdr, kEdr, 3.0); }
}
