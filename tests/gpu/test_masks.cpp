// Chroma key, masks and track mattes on the real GPU (Metal, offscreen).
#include "gpu/MaskRenderer.h"

#include <QImage>
#include <rhi/qrhi.h>
#include <rhi/qrhi_platform.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <functional>
#include <numbers>
#include <memory>

using namespace tve;
using Catch::Approx;

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

// RGBA floats of a kW × kH result, row 0 at the top.
struct Pixels {
    std::vector<float> rgba;
    const float* at(double u, double v) const
    {
        const int x = std::clamp(int(u * kW), 0, kW - 1);
        const int y = std::clamp(int(v * kH), 0, kH - 1);
        return &rgba[std::size_t((y * kW + x) * 4)];
    }
    float alpha(double u, double v) const { return at(u, v)[3]; }
};

QImage filled(const std::function<QColor(int x, int y)>& color)
{
    QImage image(kW, kH, QImage::Format_RGBA8888);
    for (int y = 0; y < kH; ++y)
        for (int x = 0; x < kW; ++x)
            image.setPixelColor(x, y, color(x, y));
    return image;
}

// Runs `pass` on uploaded copies of `images` and reads its result back.
Pixels run(const std::vector<QImage>& images,
           const std::function<QRhiTexture*(MaskRenderer&, QRhiCommandBuffer*, std::vector<QRhiTexture*>)>& pass)
{
    QRhi* rhi = gpu();
    REQUIRE(rhi);
    MaskRenderer masks(rhi);
    REQUIRE(masks.isValid());
    masks.begin();
    std::vector<std::unique_ptr<QRhiTexture>> textures;
    std::vector<QRhiTexture*> raw;
    QRhiCommandBuffer* cb = nullptr;
    REQUIRE(rhi->beginOffscreenFrame(&cb) == QRhi::FrameOpSuccess);
    QRhiResourceUpdateBatch* upload = rhi->nextResourceUpdateBatch();
    for (const QImage& image : images) {
        textures.emplace_back(rhi->newTexture(QRhiTexture::RGBA8, image.size()));
        REQUIRE(textures.back()->create());
        upload->uploadTexture(textures.back().get(), image);
        raw.push_back(textures.back().get());
    }
    cb->resourceUpdate(upload);
    QRhiTexture* output = pass(masks, cb, raw);
    QRhiReadbackResult result;
    QRhiResourceUpdateBatch* batch = rhi->nextResourceUpdateBatch();
    batch->readBackTexture({output}, &result);
    cb->resourceUpdate(batch);
    REQUIRE(rhi->endOffscreenFrame() == QRhi::FrameOpSuccess);
    Pixels pixels;
    pixels.rgba.resize(std::size_t(kW * kH * 4));
    if (result.format == QRhiTexture::RGBA16F) {
        qFloatFromFloat16(pixels.rgba.data(), reinterpret_cast<const qfloat16*>(result.data.constData()),
                          qsizetype(pixels.rgba.size()));
    } else {
        REQUIRE(result.format == QRhiTexture::RGBA8);
        for (std::size_t i = 0; i < pixels.rgba.size(); ++i)
            pixels.rgba[i] = std::uint8_t(result.data[qsizetype(i)]) / 255.0f;
    }
    return pixels;
}

Pixels masked(const std::vector<ClipMask>& list, const ChromaKey& key = {},
              const QImage& picture = filled([](int, int) { return QColor(128, 128, 128); }))
{
    return run({picture}, [&](MaskRenderer& m, QRhiCommandBuffer* cb, std::vector<QRhiTexture*> in) {
        return m.apply(cb, in[0], key, list);
    });
}

ClipMask shape(MaskShape s)
{
    ClipMask m;
    m.shape = s;
    m.feather = 0.0;
    return m;
}

} // namespace

TEST_CASE("nothing to apply leaves the picture as it is", "[masks]")
{
    QRhi* rhi = gpu();
    REQUIRE(rhi);
    MaskRenderer masks(rhi);
    std::unique_ptr<QRhiTexture> picture(rhi->newTexture(QRhiTexture::RGBA8, QSize(kW, kH)));
    REQUIRE(picture->create());
    masks.begin();
    QRhiCommandBuffer* cb = nullptr;
    REQUIRE(rhi->beginOffscreenFrame(&cb) == QRhi::FrameOpSuccess);
    CHECK(masks.apply(cb, picture.get(), ChromaKey{}, {}) == picture.get());
    REQUIRE(rhi->endOffscreenFrame() == QRhi::FrameOpSuccess);
}

TEST_CASE("shape masks keep the inside, or the outside when inverted", "[masks]")
{
    // A rectangle over the middle half.
    const Pixels box = masked({shape(MaskShape::Rectangle)});
    CHECK(box.alpha(0.5, 0.5) == Approx(1.0f));
    CHECK(box.alpha(0.3, 0.3) == Approx(1.0f));
    CHECK(box.alpha(0.1, 0.5) == Approx(0.0f));
    CHECK(box.alpha(0.5, 0.1) == Approx(0.0f));
    CHECK(box.at(0.5, 0.5)[0] == Approx(128.0f / 255.0f).margin(0.01)); // colour untouched
    ClipMask inverted = shape(MaskShape::Rectangle);
    inverted.invert = true;
    const Pixels hole = masked({inverted});
    CHECK(hole.alpha(0.5, 0.5) == Approx(0.0f));
    CHECK(hole.alpha(0.1, 0.5) == Approx(1.0f));

    // An ellipse misses the corners of its box.
    const Pixels ellipse = masked({shape(MaskShape::Ellipse)});
    CHECK(ellipse.alpha(0.5, 0.5) == Approx(1.0f));
    CHECK(ellipse.alpha(0.27, 0.27) == Approx(0.0f));
    CHECK(ellipse.alpha(0.3, 0.5) == Approx(1.0f));

    // Rotated a quarter turn, a wide box stands up.
    ClipMask bar = shape(MaskShape::Rectangle);
    bar.width = 0.9;
    bar.height = 0.1;
    const Pixels flat = masked({bar});
    CHECK(flat.alpha(0.1, 0.5) == Approx(1.0f));
    bar.rotation = 90;
    bar.width = 0.9 * kH / kW; // the same length in pixels, upright
    bar.height = 0.1 * kW / kH;
    const Pixels upright = masked({bar});
    CHECK(upright.alpha(0.1, 0.5) == Approx(0.0f));
    CHECK(upright.alpha(0.5, 0.1) == Approx(1.0f));

    // A line keeps everything above it.
    const Pixels line = masked({shape(MaskShape::Line)});
    CHECK(line.alpha(0.5, 0.2) == Approx(1.0f));
    CHECK(line.alpha(0.5, 0.8) == Approx(0.0f));

    // A star and a heart keep their middles but not their box's corners.
    for (MaskShape s : {MaskShape::Star, MaskShape::Heart}) {
        ClipMask m = shape(s);
        m.width = m.height = 0.9;
        const Pixels p = masked({m});
        CHECK(p.alpha(0.5, 0.5) == Approx(1.0f));
        CHECK(p.alpha(0.5 - 0.4 * kH / kW, 0.92) == Approx(0.0f));
    }
}

TEST_CASE("pen masks keep the inside of their outline; masks add up", "[masks]")
{
    ClipMask triangle = shape(MaskShape::Pen);
    triangle.points = {{0, 0}, {1, 0}, {0, 1}};
    const Pixels pen = masked({triangle});
    CHECK(pen.alpha(0.2, 0.2) == Approx(1.0f));
    CHECK(pen.alpha(0.8, 0.8) == Approx(0.0f));

    ClipMask left = shape(MaskShape::Rectangle);
    left.x = 0.2;
    left.width = 0.2;
    ClipMask right = left;
    right.x = 0.8;
    const Pixels both = masked({left, right});
    CHECK(both.alpha(0.2, 0.5) == Approx(1.0f));
    CHECK(both.alpha(0.8, 0.5) == Approx(1.0f));
    CHECK(both.alpha(0.5, 0.5) == Approx(0.0f));
}

TEST_CASE("feather softens the edge", "[masks]")
{
    ClipMask soft = shape(MaskShape::Line);
    soft.feather = 0.2; // of the picture's shorter side
    const Pixels p = masked({soft});
    const float edge = p.alpha(0.5, 0.5);
    CHECK(edge > 0.2f);
    CHECK(edge < 0.8f);
    CHECK(p.alpha(0.5, 0.42) > edge);
    CHECK(p.alpha(0.5, 0.58) < edge);
    CHECK(p.alpha(0.5, 0.05) == Approx(1.0f));
}

TEST_CASE("the chroma key removes its colour and the spill around it", "[masks]")
{
    // Green screen on the left, grey on the right, a greenish grey in between.
    const QImage picture = filled([](int x, int) {
        return x < kW / 3 ? QColor(0, 176, 64) : x < 2 * kW / 3 ? QColor(110, 150, 110) : QColor(128, 128, 128);
    });
    ChromaKey key;
    key.enabled = true;
    key.color = {0.0, 176.0 / 255.0, 64.0 / 255.0};
    key.spill = 1.0;
    const Pixels p = masked({}, key, picture);
    CHECK(p.alpha(0.1, 0.5) == Approx(0.0f).margin(0.01));
    CHECK(p.alpha(0.9, 0.5) == Approx(1.0f));
    CHECK(p.at(0.9, 0.5)[1] == Approx(128.0f / 255.0f).margin(0.01));
    // The greenish grey stays but loses its green cast.
    CHECK(p.alpha(0.5, 0.5) > 0.5f);
    CHECK(p.at(0.5, 0.5)[1] <= p.at(0.5, 0.5)[0] + 0.01f);

    // A blue key leaves a green screen alone.
    key.color = {0.05, 0.28, 0.85};
    CHECK(masked({}, key, picture).alpha(0.1, 0.5) == Approx(1.0f));
    // Off, nothing happens.
    key.enabled = false;
    key.color = {0.0, 176.0 / 255.0, 64.0 / 255.0};
    CHECK(masked({}, key, picture).alpha(0.1, 0.5) == Approx(1.0f));
}

TEST_CASE("track mattes keep the layer where the matte lets it through", "[masks]")
{
    const QImage layer = filled([](int, int) { return QColor(200, 50, 50); });
    // Matte: opaque white on the left, transparent on the right.
    const QImage alphaMatte = filled([](int x, int) { return x < kW / 2 ? QColor(255, 255, 255) : QColor(0, 0, 0, 0); });
    // Matte: opaque white on the left, opaque black on the right.
    const QImage lumaMatte = filled([](int x, int) { return x < kW / 2 ? QColor(255, 255, 255) : QColor(0, 0, 0); });
    const auto through = [&](const QImage& matte, MatteMode mode) {
        return run({layer, matte}, [&](MaskRenderer& m, QRhiCommandBuffer* cb, std::vector<QRhiTexture*> in) {
            return m.combineMatte(cb, in[0], in[1], mode);
        });
    };
    const Pixels alpha = through(alphaMatte, MatteMode::Alpha);
    CHECK(alpha.alpha(0.25, 0.5) == Approx(1.0f));
    CHECK(alpha.alpha(0.75, 0.5) == Approx(0.0f));
    CHECK(alpha.at(0.25, 0.5)[0] == Approx(200.0f / 255.0f).margin(0.01));
    const Pixels invertedAlpha = through(alphaMatte, MatteMode::InvertedAlpha);
    CHECK(invertedAlpha.alpha(0.25, 0.5) == Approx(0.0f));
    CHECK(invertedAlpha.alpha(0.75, 0.5) == Approx(1.0f));
    // An opaque matte lets everything through by alpha, but not by brightness.
    CHECK(through(lumaMatte, MatteMode::Alpha).alpha(0.75, 0.5) == Approx(1.0f));
    const Pixels luma = through(lumaMatte, MatteMode::Luma);
    CHECK(luma.alpha(0.25, 0.5) == Approx(1.0f));
    CHECK(luma.alpha(0.75, 0.5) == Approx(0.0f));
    const Pixels invertedLuma = through(lumaMatte, MatteMode::InvertedLuma);
    CHECK(invertedLuma.alpha(0.25, 0.5) == Approx(0.0f));
    CHECK(invertedLuma.alpha(0.75, 0.5) == Approx(1.0f));
}

TEST_CASE("portrait cutout keeps what the person matte marks")
{
    const QImage picture = filled([](int, int) { return QColor(200, 120, 40); });
    // A small matte (as Vision's are): the person in the top-left quarter.
    QImage matte(16, 9, QImage::Format_RGBA8888);
    for (int y = 0; y < 9; ++y)
        for (int x = 0; x < 16; ++x)
            matte.setPixelColor(x, y, x < 8 && y < 4 ? QColor(255, 255, 255) : QColor(0, 0, 0));
    PortraitCutout cutout;
    cutout.enabled = true;
    cutout.softness = 0.0;
    const auto run2 = [&](const PortraitCutout& c) {
        return run({picture, matte}, [&](MaskRenderer& m, QRhiCommandBuffer* cb, std::vector<QRhiTexture*> t) {
            return m.cutout(cb, t[0], t[1], c);
        });
    };
    const Pixels kept = run2(cutout);
    CHECK(kept.alpha(0.1, 0.1) == Approx(1.0f).margin(0.01));
    CHECK(kept.at(0.1, 0.1)[0] == Approx(200.0f / 255.0f).margin(0.01)); // colour untouched
    CHECK(kept.alpha(0.9, 0.9) == Approx(0.0f).margin(0.01));
    CHECK(kept.alpha(0.1, 0.9) == Approx(0.0f).margin(0.01)); // the top stays the top
    CHECK(kept.alpha(0.9, 0.1) == Approx(0.0f).margin(0.01));

    cutout.invert = true;
    const Pixels removed = run2(cutout);
    CHECK(removed.alpha(0.1, 0.1) == Approx(0.0f).margin(0.01));
    CHECK(removed.alpha(0.9, 0.9) == Approx(1.0f).margin(0.01));

    // Shifted out, the edge moves past the matte's boundary (more is kept).
    cutout.invert = false;
    cutout.shift = 0.0;
    const double edgeU = 0.5 + 0.25 / 16.0; // just right of the boundary, on the matte's ramp (0.25)
    const float atEdge = run2(cutout).alpha(edgeU, 0.1);
    cutout.shift = 1.0;
    CHECK(run2(cutout).alpha(edgeU, 0.1) > atEdge + 0.2f);

    // Off: the picture itself.
    cutout.enabled = false;
    run({picture, matte}, [&](MaskRenderer& m, QRhiCommandBuffer* cb, std::vector<QRhiTexture*> t) {
        CHECK(m.cutout(cb, t[0], t[1], cutout) == t[0]);
        return t[0];
    });
}

TEST_CASE("faces are covered by mosaic, blur or fill inside their ellipses only")
{
    // A one-pixel checkerboard: covering it shows plainly.
    const QImage picture = filled([](int x, int y) { return (x + y) % 2 ? QColor(255, 255, 255) : QColor(0, 0, 0); });
    const std::vector<FaceBox> face{{0.5, 0.5, 0.5, 0.8}};
    const auto cover = [&](FaceCover style, float amount) {
        return run({picture}, [&](MaskRenderer& m, QRhiCommandBuffer* cb, std::vector<QRhiTexture*> t) {
            return m.coverFaces(cb, t[0], face, style, amount);
        });
    };
    const auto value = [](const Pixels& p, int x, int y) { return p.at((x + 0.5) / kW, (y + 0.5) / kH)[0]; };

    const Pixels mosaic = cover(FaceCover::Mosaic, 1.0f);
    // Inside: neighbouring pixels share their block's colour. Outside: the checkerboard.
    int same = 0;
    for (int x = 28; x < 36; ++x)
        same += std::abs(value(mosaic, x, 18) - value(mosaic, x + 1, 18)) < 0.01f;
    CHECK(same >= 6);
    CHECK(std::abs(value(mosaic, 2, 2) - value(mosaic, 3, 2)) > 0.9f);

    const Pixels blur = cover(FaceCover::Blur, 0.5f);
    CHECK(value(blur, 32, 18) == Approx(0.5f).margin(0.1));
    CHECK(value(blur, 33, 18) == Approx(0.5f).margin(0.1));
    CHECK(std::abs(value(blur, 2, 2) - value(blur, 3, 2)) > 0.9f);

    const Pixels fill = cover(FaceCover::Fill, 0.5f);
    CHECK(value(fill, 32, 18) == Approx(0.08f).margin(0.02));
    CHECK(value(fill, 33, 18) == Approx(0.08f).margin(0.02));
    CHECK(fill.alpha(0.5, 0.5) == Approx(1.0f));
    // A corner of the face's box is outside its ellipse: untouched.
    CHECK(std::abs(value(fill, 17, 3) - value(fill, 18, 3)) > 0.9f);

    // No faces: the picture itself.
    run({picture}, [&](MaskRenderer& m, QRhiCommandBuffer* cb, std::vector<QRhiTexture*> t) {
        CHECK(m.coverFaces(cb, t[0], {}, FaceCover::Mosaic, 0.5f) == t[0]);
        return t[0];
    });
}

TEST_CASE("the steady pass moves, turns and zooms the picture and evens its brightness")
{
    // A white column at x = 20..23 on grey, a red dot at the top left.
    const QImage picture = filled([](int x, int y) {
        if (x < 4 && y < 4)
            return QColor(255, 0, 0);
        return x >= 20 && x < 24 ? QColor(255, 255, 255) : QColor(100, 100, 100);
    });
    const auto steady = [&](float zoom, std::array<float, 4> c, float gain) {
        return run({picture}, [&](MaskRenderer& m, QRhiCommandBuffer* cb, std::vector<QRhiTexture*> t) {
            return m.steady(cb, t[0], zoom, c, gain);
        });
    };
    const auto value = [](const Pixels& p, int x, int y) { return p.at((x + 0.5) / kW, (y + 0.5) / kH)[0]; };

    // Moved right by 8 pixels: the column is at 28..31, and the left edge is empty.
    const Pixels moved = steady(1.0f, {8.0f / kW, 0.0f, 0.0f, 1.0f}, 1.0f);
    CHECK(value(moved, 29, 18) == Approx(1.0f).margin(0.02));
    CHECK(value(moved, 21, 18) == Approx(100.0f / 255.0f).margin(0.02));
    CHECK(moved.alpha(0.02, 0.5) == Approx(0.0f).margin(0.01));
    // Turned half a turn: the red dot goes to the bottom right.
    const Pixels turned = steady(1.0f, {0.0f, 0.0f, float(std::numbers::pi), 1.0f}, 1.0f);
    CHECK(turned.at(0.97, 0.95)[0] == Approx(1.0f).margin(0.02));
    CHECK(turned.at(0.97, 0.95)[1] == Approx(0.0f).margin(0.02));
    // Zoomed in twice about the centre: the column (at 0.31..0.37) moves out to 0.13..0.25.
    const Pixels zoomed = steady(2.0f, {0.0f, 0.0f, 0.0f, 1.0f}, 1.0f);
    CHECK(value(zoomed, 12, 18) == Approx(1.0f).margin(0.02));
    CHECK(zoomed.alpha(0.02, 0.5) == Approx(1.0f).margin(0.01)); // no edges show
    // A gain of 1.5 brightens (and stops at white).
    const Pixels brighter = steady(1.0f, {0.0f, 0.0f, 0.0f, 1.0f}, 1.5f);
    CHECK(value(brighter, 40, 18) == Approx(150.0f / 255.0f).margin(0.02));
    CHECK(value(brighter, 21, 18) == Approx(1.0f).margin(0.01));
}

TEST_CASE("denoising over time averages still grain and leaves motion alone")
{
    // The frame before: flat grey. Now: grey with ±0.04 grain on the left, and a bright square
    // (something that moved in) on the right.
    const QImage before = filled([](int, int) { return QColor(128, 128, 128); });
    const QImage now = filled([](int x, int y) {
        if (x >= 44 && x < 60 && y >= 10 && y < 26)
            return QColor(255, 255, 255);
        const int g = 128 + ((x * 7 + y * 13) % 3 - 1) * 10;
        return QColor(g, g, g);
    });
    const Pixels out = run({now, before}, [&](MaskRenderer& m, QRhiCommandBuffer* cb, std::vector<QRhiTexture*> t) {
        return m.denoise(cb, t[0], t[1], 1.0f);
    });
    double grainBefore = 0.0, grainAfter = 0.0;
    for (int y = 4; y < 30; ++y)
        for (int x = 4; x < 30; ++x) {
            const float mean = 128.0f / 255.0f;
            grainBefore += std::abs(now.pixelColor(x, y).redF() - mean);
            grainAfter += std::abs(out.at((x + 0.5) / kW, (y + 0.5) / kH)[0] - mean);
        }
    CHECK(grainAfter < grainBefore * 0.4);
    CHECK(out.at(52.5 / kW, 18.5 / kH)[0] == Approx(1.0f).margin(0.02)); // the square: as it is now
    // Strength 0 (or no frame before): the picture itself.
    run({now, before}, [&](MaskRenderer& m, QRhiCommandBuffer* cb, std::vector<QRhiTexture*> t) {
        CHECK(m.denoise(cb, t[0], t[1], 0.0f) == t[0]);
        CHECK(m.denoise(cb, t[0], nullptr, 1.0f) == t[0]);
        return t[0];
    });
}

TEST_CASE("cursor effects ring, highlight and spotlight around the cursor")
{
    const QImage picture = filled([](int, int) { return QColor(100, 100, 100); });
    const auto draw = [&](float ring, bool highlight, bool spotlight) {
        return run({picture}, [&](MaskRenderer& m, QRhiCommandBuffer* cb, std::vector<QRhiTexture*> t) {
            return m.drawCursor(cb, t[0], 0.5f, 0.5f, 0.2f, ring, highlight, spotlight);
        });
    };
    const float grey = 100.0f / 255.0f;
    // A spotlight: far away dimmed, at the cursor as it was.
    const Pixels spot = draw(-1.0f, false, true);
    CHECK(spot.at(0.05, 0.05)[0] < grey * 0.5f);
    CHECK(spot.at(0.5, 0.5)[0] == Approx(grey).margin(0.01));
    // A highlight: yellower at the cursor, untouched far away.
    const Pixels lit = draw(-1.0f, true, false);
    CHECK(lit.at(0.5, 0.5)[0] > grey + 0.1f);
    CHECK(lit.at(0.5, 0.5)[2] < lit.at(0.5, 0.5)[0]);
    CHECK(lit.at(0.05, 0.05)[0] == Approx(grey).margin(0.01));
    // A ring half-way: on its circle (radius 0.85 × 0.2 of the height) not at the centre.
    const Pixels ring = draw(0.5f, false, false);
    CHECK(ring.at(0.5, 0.5)[0] == Approx(grey).margin(0.01));
    CHECK(ring.at(0.5, 0.5 + 0.17)[0] > grey + 0.05f);
    // Nothing on: the picture itself.
    run({picture}, [&](MaskRenderer& m, QRhiCommandBuffer* cb, std::vector<QRhiTexture*> t) {
        CHECK(m.drawCursor(cb, t[0], 0.5f, 0.5f, 0.2f, -1.0f, false, false) == t[0]);
        return t[0];
    });
}
