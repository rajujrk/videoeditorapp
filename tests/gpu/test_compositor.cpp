// Composites solid-colour layers on the real GPU (Metal, offscreen) and checks the pixels.
#include "gpu/Compositor.h"
#include "gpu/LayerPlacement.h"

#include <QImage>
#include <rhi/qrhi.h>
#include <rhi/qrhi_platform.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <memory>

using namespace tve;

namespace {

constexpr int kCanvasW = 64;
constexpr int kCanvasH = 36; // 16:9
constexpr float kTolerance = 3.0f / 255.0f;

QRhi* gpu()
{
    static std::unique_ptr<QRhi> rhi = [] {
        QRhiMetalInitParams params;
        return std::unique_ptr<QRhi>(QRhi::create(QRhi::Metal, &params));
    }();
    return rhi.get();
}

struct Rgb {
    float r, g, b;
};

struct Canvas {
    std::vector<float> rgba;
    Rgb at(int x, int y) const
    {
        const std::size_t i = std::size_t((y * kCanvasW + x) * 4);
        return {rgba[i], rgba[i + 1], rgba[i + 2]};
    }
};

QImage solid(int w, int h, QColor color)
{
    QImage image(w, h, QImage::Format_RGBA8888);
    image.fill(color);
    return image;
}

// Left half `left`, right half `right`.
QImage halves(int w, int h, QColor left, QColor right)
{
    QImage image(w, h, QImage::Format_RGBA8888);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            image.setPixelColor(x, y, x < w / 2 ? left : right);
    return image;
}

struct Layer {
    QImage image;
    ClipVisual visual;
};

Canvas compose(const std::vector<Layer>& layers)
{
    QRhi* rhi = gpu();
    REQUIRE(rhi);
    std::vector<std::unique_ptr<QRhiTexture>> textures;
    for (const Layer& layer : layers) {
        textures.emplace_back(rhi->newTexture(QRhiTexture::RGBA8, layer.image.size()));
        REQUIRE(textures.back()->create());
    }

    Compositor compositor(rhi);
    QRhiCommandBuffer* cb = nullptr;
    REQUIRE(rhi->beginOffscreenFrame(&cb) == QRhi::FrameOpSuccess);
    QRhiResourceUpdateBatch* upload = rhi->nextResourceUpdateBatch();
    for (std::size_t i = 0; i < layers.size(); ++i)
        upload->uploadTexture(textures[i].get(), layers[i].image);
    cb->resourceUpdate(upload);

    compositor.begin(cb, QSize(kCanvasW, kCanvasH));
    for (std::size_t i = 0; i < layers.size(); ++i)
        compositor.draw(cb, textures[i].get(), layers[i].visual);

    QRhiReadbackResult result;
    QRhiResourceUpdateBatch* readback = rhi->nextResourceUpdateBatch();
    readback->readBackTexture({compositor.result()}, &result);
    cb->resourceUpdate(readback);
    REQUIRE(rhi->endOffscreenFrame() == QRhi::FrameOpSuccess);

    Canvas canvas;
    const std::size_t count = std::size_t(kCanvasW * kCanvasH * 4);
    canvas.rgba.resize(count);
    REQUIRE(result.format == QRhiTexture::RGBA16F);
    qFloatFromFloat16(canvas.rgba.data(), reinterpret_cast<const qfloat16*>(result.data.constData()), qsizetype(count));
    return canvas;
}

void checkColor(Rgb actual, Rgb expected)
{
    CHECK(actual.r == Catch::Approx(expected.r).margin(kTolerance));
    CHECK(actual.g == Catch::Approx(expected.g).margin(kTolerance));
    CHECK(actual.b == Catch::Approx(expected.b).margin(kTolerance));
}

const Rgb kBlack{0, 0, 0};
const Rgb kRed{1, 0, 0};
const Rgb kBlue{0, 0, 1};
const Rgb kGreen{0, 1, 0};

} // namespace

TEST_CASE("placement: a same-aspect layer maps one-to-one; others are fitted")
{
    const LayerPlacement same = placeLayer({}, 1920, 1080, 3840, 2160);
    CHECK(same.apply(0, 0)[0] == Catch::Approx(0.0).margin(1e-9));
    CHECK(same.apply(1, 1)[1] == Catch::Approx(1.0).margin(1e-9));
    CHECK(same.apply(0.3, 0.7)[0] == Catch::Approx(0.3).margin(1e-9));

    // 4:3 in 16:9: 1440 px wide, centered → canvas x 240..1680
    const LayerPlacement pillar = placeLayer({}, 1920, 1080, 1440, 1080);
    CHECK(pillar.apply(240.0 / 1920, 0.5)[0] == Catch::Approx(0.0).margin(1e-9));
    CHECK(pillar.apply(1680.0 / 1920, 0.5)[0] == Catch::Approx(1.0).margin(1e-9));

    ClipVisual rotated;
    rotated.transform.rotation = 37;
    const auto center = placeLayer(rotated, 1920, 1080, 1920, 1080).apply(0.5, 0.5);
    CHECK(center[0] == Catch::Approx(0.5).margin(1e-9)); // rotation is about the center
    CHECK(center[1] == Catch::Approx(0.5).margin(1e-9));
}

TEST_CASE("default placement fills the canvas; other aspects are letterboxed")
{
    checkColor(compose({{solid(32, 18, Qt::red), {}}}).at(3, 3), kRed);

    const Canvas pillar = compose({{solid(48, 36, Qt::blue), {}}}); // 4:3 → 48 px wide
    checkColor(pillar.at(2, 18), kBlack);
    checkColor(pillar.at(32, 18), kBlue);
    checkColor(pillar.at(61, 18), kBlack);
}

TEST_CASE("scale and position move the picture")
{
    ClipVisual visual;
    visual.transform.scale = 0.5;
    visual.transform.x = 0.25;
    const Canvas c = compose({{solid(32, 18, Qt::blue), visual}});
    checkColor(c.at(48, 18), kBlue);  // centered at 75% of the width
    checkColor(c.at(16, 18), kBlack);
    checkColor(c.at(48, 2), kBlack);  // half height → rows 9..27
}

TEST_CASE("crop removes part of the picture before fitting")
{
    ClipVisual visual;
    visual.crop.left = 0.5;
    const Canvas c = compose({{halves(64, 36, Qt::red, Qt::green), visual}});
    // The green half is 8:9, fitted to the canvas height: 32 px wide, centered.
    checkColor(c.at(32, 18), kGreen);
    checkColor(c.at(18, 18), kGreen);
    checkColor(c.at(10, 18), kBlack);
    for (int x = 0; x < kCanvasW; ++x)
        CHECK(c.at(x, 18).r < kTolerance); // no red anywhere
}

TEST_CASE("rotation is clockwise about the center")
{
    ClipVisual visual;
    visual.transform.rotation = 90;
    const Canvas c = compose({{halves(64, 36, Qt::red, Qt::blue), visual}});
    // Rotated, the 64×36 picture stands 36 wide and 64 tall: left (red) half on top.
    checkColor(c.at(32, 5), kRed);
    checkColor(c.at(32, 30), kBlue);
    checkColor(c.at(5, 18), kBlack);
}

TEST_CASE("opacity and blend modes")
{
    ClipVisual half;
    half.opacity = 0.5;
    checkColor(compose({{solid(32, 18, Qt::white), half}}).at(10, 10), {0.5f, 0.5f, 0.5f});

    const QColor gray = QColor::fromRgbF(0.5f, 0.5f, 0.5f);
    auto blended = [&](BlendMode mode) {
        ClipVisual top;
        top.blend = mode;
        return compose({{solid(32, 18, gray), {}}, {solid(32, 18, gray), top}}).at(10, 10).r;
    };
    const float g = float(gray.redF()); // 0.502 after 8-bit quantization
    CHECK(blended(BlendMode::Normal) == Catch::Approx(g).margin(kTolerance));
    CHECK(blended(BlendMode::Multiply) == Catch::Approx(g * g).margin(kTolerance));
    CHECK(blended(BlendMode::Screen) == Catch::Approx(1 - (1 - g) * (1 - g)).margin(kTolerance));
    CHECK(blended(BlendMode::Add) == Catch::Approx(2 * g).margin(kTolerance));
    CHECK(blended(BlendMode::Darken) == Catch::Approx(g).margin(kTolerance));
    CHECK(blended(BlendMode::Lighten) == Catch::Approx(g).margin(kTolerance));
    CHECK(blended(BlendMode::Overlay) == Catch::Approx(1 - 2 * (1 - g) * (1 - g)).margin(kTolerance));
}

TEST_CASE("later layers draw over earlier ones")
{
    ClipVisual pip;
    pip.transform.scale = 0.5;
    const Canvas c = compose({{solid(32, 18, Qt::red), {}}, {solid(32, 18, Qt::blue), pip}});
    checkColor(c.at(32, 18), kBlue);
    checkColor(c.at(2, 2), kRed);
}
