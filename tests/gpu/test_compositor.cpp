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

Canvas compose(const std::vector<Layer>& layers, bool transparent = false)
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

    compositor.begin(cb, QSize(kCanvasW, kCanvasH), transparent);
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

TEST_CASE("placement: the anchor point is what scaling and rotation go about")
{
    // Anchored at the top-left corner, at half size: the picture keeps that corner.
    ClipVisual visual;
    visual.transform.anchorX = -0.5;
    visual.transform.anchorY = -0.5;
    visual.transform.scale = 0.5;
    const LayerPlacement p = placeLayer(visual, 1920, 1080, 1920, 1080);
    CHECK(p.toCanvas(0, 0)[0] == Catch::Approx(0.0).margin(1e-9));
    CHECK(p.toCanvas(0, 0)[1] == Catch::Approx(0.0).margin(1e-9));
    CHECK(p.toCanvas(1, 1)[0] == Catch::Approx(0.5).margin(1e-9));
    // An anchor alone moves nothing.
    ClipVisual moved;
    moved.transform.anchorX = 0.3;
    CHECK(placeLayer(moved, 1920, 1080, 1920, 1080).toCanvas(0.2, 0.7)[0] == Catch::Approx(0.2).margin(1e-9));
    // Turned a quarter about the top-left corner: the top edge now runs down the left side.
    visual.transform.scale = 1;
    visual.transform.rotation = 90;
    const LayerPlacement turned = placeLayer(visual, 1920, 1080, 1920, 1080);
    CHECK(turned.toCanvas(1, 0)[0] * 1920 == Catch::Approx(0.0).margin(1e-6));
    CHECK(turned.toCanvas(1, 0)[1] * 1080 == Catch::Approx(1920.0).margin(1e-6));
    // The inverse undoes it.
    const auto back = turned.apply(turned.toCanvas(0.3, 0.6)[0], turned.toCanvas(0.3, 0.6)[1]);
    CHECK(back[0] == Catch::Approx(0.3).margin(1e-9));
    CHECK(back[1] == Catch::Approx(0.6).margin(1e-9));
}

TEST_CASE("placement: a corner pin moves each corner, in perspective between them")
{
    ClipVisual visual;
    visual.pin.topRightX = -0.2;
    visual.pin.topRightY = 0.25;
    visual.pin.bottomRightY = -0.25;
    const LayerPlacement p = placeLayer(visual, 1920, 1080, 1920, 1080);
    CHECK(p.toCanvas(1, 0)[0] == Catch::Approx(0.8).margin(1e-9));
    CHECK(p.toCanvas(1, 0)[1] == Catch::Approx(0.25).margin(1e-9));
    CHECK(p.toCanvas(1, 1)[1] == Catch::Approx(0.75).margin(1e-9));
    CHECK(p.toCanvas(0, 0)[0] == Catch::Approx(0.0).margin(1e-9));
    for (const auto& [u, v] : {std::pair{0.25, 0.4}, std::pair{0.9, 0.1}}) {
        const auto c = p.toCanvas(u, v);
        CHECK(p.inFront(c[0], c[1]));
        CHECK(p.apply(c[0], c[1])[0] == Catch::Approx(u).margin(1e-9));
        CHECK(p.apply(c[0], c[1])[1] == Catch::Approx(v).margin(1e-9));
    }
}

TEST_CASE("a corner-pinned picture is drawn in perspective")
{
    ClipVisual visual;
    visual.pin.topRightY = 0.4;
    visual.pin.bottomRightY = -0.4;
    const Canvas c = compose({{solid(64, 36, Qt::blue), visual}});
    checkColor(c.at(2, 2), kBlue);    // the left edge as it was
    checkColor(c.at(61, 3), kBlack);  // the right edge squeezed to the middle
    checkColor(c.at(61, 18), kBlue);
}

TEST_CASE("accumulated draws average (motion blur's samples)")
{
    // Red and blue at half weight each, on a transparent canvas: purple.
    ClipVisual red, blue;
    red.blend = blue.blend = BlendMode::Accumulate;
    red.opacity = blue.opacity = 0.5;
    const Canvas c = compose({{solid(32, 18, Qt::red), red}, {solid(32, 18, Qt::blue), blue}}, true);
    checkColor(c.at(20, 18), Rgb{0.5f, 0.0f, 0.5f});
}

TEST_CASE("A flipped picture is mirrored inside its frame", "[compositor]")
{
    ClipVisual v;
    // 16:9 on a 16:9 canvas: the picture's left edge is at the canvas's left…
    auto plain = placeLayer(v, 1920, 1080, 1920, 1080);
    auto at = plain.toCanvas(0.0, 0.25);
    CHECK(at[0] == Catch::Approx(0.0).margin(1e-9));
    CHECK(at[1] == Catch::Approx(0.25));
    // …and flipped left to right, at its right; upside down, its top at the bottom.
    v.transform.flipH = true;
    at = placeLayer(v, 1920, 1080, 1920, 1080).toCanvas(0.0, 0.25);
    CHECK(at[0] == Catch::Approx(1.0));
    CHECK(at[1] == Catch::Approx(0.25));
    v.transform.flipH = false;
    v.transform.flipV = true;
    at = placeLayer(v, 1920, 1080, 1920, 1080).toCanvas(0.0, 0.25);
    CHECK(at[0] == Catch::Approx(0.0).margin(1e-9));
    CHECK(at[1] == Catch::Approx(0.75));
    // Moved and turned, the frame stays where it was: the centre lands in the same place.
    v.transform.x = 0.1;
    v.transform.rotation = 30;
    ClipVisual unflipped = v;
    unflipped.transform.flipV = false;
    const auto c1 = placeLayer(v, 1920, 1080, 1920, 1080).toCanvas(0.5, 0.5);
    const auto c2 = placeLayer(unflipped, 1920, 1080, 1920, 1080).toCanvas(0.5, 0.5);
    CHECK(c1[0] == Catch::Approx(c2[0]));
    CHECK(c1[1] == Catch::Approx(c2[1]));
    // A corner pin follows the frame's corner it is on, not the picture's.
    v = {};
    v.transform.flipH = true;
    v.pin.topRightX = 0.1; // the frame's top right…
    at = placeLayer(v, 1920, 1080, 1920, 1080).toCanvas(0.0, 0.0); // …is the picture's top left when flipped
    CHECK(at[0] == Catch::Approx(1.1));
    CHECK(at[1] == Catch::Approx(0.0).margin(1e-9));
}
