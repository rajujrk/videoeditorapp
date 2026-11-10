// 360° reframing and turning on the real GPU (Metal, offscreen): sphere.frag against the
// CPU maths in model/Sphere.h, with a source whose colour is its own (u, v).
#include "gpu/SpherePass.h"
#include "model/Sphere.h"

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

constexpr int kW = 512, kH = 256;

// Runs the pass; the output as RGBA floats, row by row from the top.
std::vector<float> run(const ClipSphereView& view, bool turning, QSize size, bool* passedThrough = nullptr)
{
    QRhi* rhi = gpu();
    REQUIRE(rhi);
    std::vector<float> pixels;
    for (int y = 0; y < kH; ++y)
        for (int x = 0; x < kW; ++x)
            for (float v : {(float(x) + 0.5f) / kW, (float(y) + 0.5f) / kH, 0.0f, 1.0f})
                pixels.push_back(v);
    std::unique_ptr<QRhiTexture> source(rhi->newTexture(QRhiTexture::RGBA32F, QSize(kW, kH)));
    REQUIRE(source->create());
    SpherePass pass(rhi);
    REQUIRE(pass.isValid());
    QRhiCommandBuffer* cb = nullptr;
    REQUIRE(rhi->beginOffscreenFrame(&cb) == QRhi::FrameOpSuccess);
    QRhiResourceUpdateBatch* upload = rhi->nextResourceUpdateBatch();
    QRhiTextureSubresourceUploadDescription data(pixels.data(), quint32(pixels.size() * sizeof(float)));
    upload->uploadTexture(source.get(), QRhiTextureUploadDescription({0, 0, data}));
    cb->resourceUpdate(upload);
    pass.begin();
    QRhiTexture* output = turning ? pass.turn(cb, source.get(), view) : pass.reframe(cb, source.get(), view, size);
    if (passedThrough)
        *passedThrough = output == source.get();
    QRhiReadbackResult readback;
    if (output != source.get()) {
        QRhiResourceUpdateBatch* batch = rhi->nextResourceUpdateBatch();
        batch->readBackTexture({output}, &readback);
        cb->resourceUpdate(batch);
    }
    REQUIRE(rhi->endOffscreenFrame() == QRhi::FrameOpSuccess);
    if (output == source.get())
        return {};
    REQUIRE(readback.format == QRhiTexture::RGBA16F);
    const QSize out = readback.pixelSize;
    std::vector<float> rgba(std::size_t(out.width() * out.height() * 4));
    const qsizetype stride = readback.data.size() / out.height();
    for (int y = 0; y < out.height(); ++y)
        qFloatFromFloat16(rgba.data() + std::size_t(y * out.width() * 4),
                          reinterpret_cast<const qfloat16*>(readback.data.constData() + y * stride), out.width() * 4);
    return rgba;
}

// Each sampled output pixel's colour is the source point the CPU says it shows.
template <typename Expected>
void matches(const std::vector<float>& rgba, QSize size, Expected expected)
{
    for (int y = 8; y < size.height(); y += 24) {
        for (int x = 8; x < size.width(); x += 24) {
            const auto want = expected((x + 0.5) / size.width(), (y + 0.5) / size.height());
            // Near the seam the wrapped sample averages both sides; away from it, exact.
            if (want[0] < 0.02 || want[0] > 0.98)
                continue;
            const float* px = rgba.data() + std::size_t((y * size.width() + x) * 4);
            INFO("pixel " << x << ", " << y);
            CHECK(px[0] == Catch::Approx(want[0]).margin(3e-3));
            CHECK(px[1] == Catch::Approx(want[1]).margin(3e-3));
        }
    }
}

} // namespace

TEST_CASE("the GPU reframes 360° video as the CPU does", "[sphere]")
{
    const QSize size(320, 180);
    for (const ClipSphereView view : {ClipSphereView{}, ClipSphereView{90, 0, 0, 90}, ClipSphereView{-40, 25, 0, 70},
                                      ClipSphereView{160, -30, 15, 120}, ClipSphereView{0, 80, 0, 100}}) {
        INFO("yaw " << view.yaw << " pitch " << view.pitch << " roll " << view.roll << " fov " << view.fov);
        const auto rgba = run(view, false, size);
        REQUIRE(rgba.size() == std::size_t(size.width() * size.height() * 4));
        matches(rgba, size, [&](double u, double v) { return reframedSource(view, 16.0 / 9.0, u, v); });
    }
}

TEST_CASE("the GPU turns a 360° sphere as the CPU does, and leaves an unturned one alone", "[sphere]")
{
    bool passed = false;
    run(ClipSphereView{}, true, {}, &passed);
    CHECK(passed);
    const ClipSphereView view{75, -20, 10, 90};
    const auto rgba = run(view, true, {});
    matches(rgba, QSize(kW, kH), [&](double u, double v) { return turnedSource(view, u, v); });
}
