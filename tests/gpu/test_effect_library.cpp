// The whole built-in effect library on the real GPU (Metal, offscreen): every package runs,
// and the framework's later-pass input (tve_input) and canvas bounds (tve_bounds) work.
#include "gpu/EffectChain.h"

#include <QImage>
#include <rhi/qrhi.h>
#include <rhi/qrhi_platform.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <memory>
#include <set>

using namespace tve;
using Catch::Approx;

namespace {

constexpr int kW = 96;
constexpr int kH = 54;

QRhi* gpu()
{
    static std::unique_ptr<QRhi> rhi = [] {
        QRhiMetalInitParams params;
        return std::unique_ptr<QRhi>(QRhi::create(QRhi::Metal, &params));
    }();
    return rhi.get();
}

struct Pixels {
    int width = kW;
    std::vector<float> rgba;
    const float* at(int x, int y) const { return &rgba[std::size_t((y * width + x) * 4)]; }
    float maxDifference(const Pixels& other) const
    {
        float worst = 0.0f;
        for (std::size_t i = 0; i < rgba.size(); ++i)
            worst = std::max(worst, std::abs(rgba[i] - other.rgba[i]));
        return worst;
    }
};

// Red ramps left to right, green top to bottom, blue in checks; optionally with some noise.
QImage pattern(int width = kW, int height = kH, bool noisy = false)
{
    QImage image(width, height, QImage::Format_RGBA8888);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const int grain = noisy ? int((x * 7919 + y * 104729) % 41) - 20 : 0;
            image.setPixelColor(x, y, QColor(std::clamp(x * 255 / (width - 1) + grain, 0, 255),
                                             std::clamp(y * 255 / (height - 1) - grain, 0, 255),
                                             ((x * 16 / width) + (y * 9 / height)) % 2 ? 230 : 20));
        }
    }
    return image;
}

// The pattern in the middle (a placed clip on its own canvas), transparent around it.
constexpr EffectBounds kInset{0.25f, 0.25f, 0.75f, 0.75f};
QImage inset()
{
    QImage image(kW, kH, QImage::Format_RGBA8888);
    image.fill(Qt::transparent);
    const QImage picture = pattern();
    for (int y = int(kH * kInset[1]); y < int(kH * kInset[3]); ++y)
        for (int x = int(kW * kInset[0]); x < int(kW * kInset[2]); ++x)
            image.setPixelColor(x, y, picture.pixelColor(x, y));
    return image;
}

Pixels fromImage(const QImage& image)
{
    Pixels pixels{image.width(), {}};
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const QColor c = image.pixelColor(x, y);
            pixels.rgba.insert(pixels.rgba.end(), {float(c.redF()), float(c.greenF()), float(c.blueF()), float(c.alphaF())});
        }
    }
    return pixels;
}

Pixels run(const std::vector<EffectInstance>& instances, const QImage& input = pattern(), float seconds = 0.0f,
           const EffectBounds& bounds = kWholeImage)
{
    QRhi* rhi = gpu();
    REQUIRE(rhi);
    std::unique_ptr<QRhiTexture> source(rhi->newTexture(QRhiTexture::RGBA8, input.size(), 1, QRhiTexture::UsedAsTransferSource));
    REQUIRE(source->create());
    EffectChain chain(rhi, EffectLibrary::builtIn());
    chain.setInstances(instances);
    QRhiCommandBuffer* cb = nullptr;
    REQUIRE(rhi->beginOffscreenFrame(&cb) == QRhi::FrameOpSuccess);
    QRhiResourceUpdateBatch* upload = rhi->nextResourceUpdateBatch();
    upload->uploadTexture(source.get(), input);
    cb->resourceUpdate(upload);
    QRhiTexture* output = chain.process(cb, source.get(), seconds, bounds);
    QRhiReadbackResult result;
    QRhiResourceUpdateBatch* readback = rhi->nextResourceUpdateBatch();
    readback->readBackTexture({output}, &result);
    cb->resourceUpdate(readback);
    REQUIRE(rhi->endOffscreenFrame() == QRhi::FrameOpSuccess);
    Pixels pixels{input.width(), {}};
    pixels.rgba.resize(std::size_t(input.width() * input.height() * 4));
    if (result.format == QRhiTexture::RGBA16F) {
        qFloatFromFloat16(pixels.rgba.data(), reinterpret_cast<const qfloat16*>(result.data.constData()),
                          qsizetype(pixels.rgba.size()));
    } else {
        for (std::size_t i = 0; i < pixels.rgba.size(); ++i)
            pixels.rgba[i] = std::uint8_t(result.data[qsizetype(i)]) / 255.0f;
    }
    return pixels;
}

Pixels run(const std::string& id, std::map<std::string, std::vector<float>> values = {}, const QImage& input = pattern(),
           float seconds = 0.0f, const EffectBounds& bounds = kWholeImage)
{
    return run({EffectInstance{id, true, std::move(values)}}, input, seconds, bounds);
}

bool isCanvas(const LoadedEffect& effect) { return effect.manifest.space == EffectSpace::Canvas; }

} // namespace

TEST_CASE("the library holds 61 original effects in eight categories", "[library]")
{
    const EffectLibrary& library = EffectLibrary::builtIn();
    INFO(library.errors().join(QLatin1Char('\n')).toStdString());
    CHECK(library.errors().isEmpty());
    CHECK(library.effects().size() == 61);
    const std::set<std::string> known{"blur", "color", "stylize", "distort", "glitch", "light", "frame", "overlay"};
    std::map<std::string, int> perCategory;
    std::set<std::string> titles;
    for (const LoadedEffect& effect : library.effects()) {
        INFO(effect.manifest.id);
        CHECK(effect.manifest.license == "TVE-Original");
        REQUIRE_FALSE(effect.manifest.categories.empty());
        CHECK(known.contains(effect.manifest.categories.front()));
        ++perCategory[effect.manifest.categories.front()];
        CHECK(titles.insert(effect.manifest.displayTitle()).second); // no two share a title
        // Canvas effects are the frame ones that draw outside the picture.
        if (isCanvas(effect))
            CHECK(effect.manifest.categories.front() == "frame");
    }
    CHECK(perCategory.size() == known.size());
    for (const auto& [category, count] : perCategory) {
        INFO(category);
        CHECK(count >= 2);
    }
    CHECK(std::ranges::count_if(library.effects(), isCanvas) == 4);
    CHECK(library.find("tve.light.glow")->passes[2].readsInput);
    CHECK_FALSE(library.find("tve.light.glow")->passes[0].readsInput);
    CHECK(library.find("tve.frame.background_fill")->passes[0].boundsOffset == 16);
    CHECK(library.find("tve.frame.background_fill")->passes[1].boundsOffset == -1);
}

TEST_CASE("every effect changes the picture at its defaults and keeps colours in range", "[library]")
{
    // A larger, noisy picture: effects are sized relative to the picture.
    const QImage picture = pattern(320, 180, true);
    const Pixels plain = fromImage(picture);
    const Pixels placed = fromImage(inset());
    for (const LoadedEffect& effect : EffectLibrary::builtIn().effects()) {
        INFO(effect.manifest.id);
        if (effect.manifest.id == "tve.color.adjust")
            continue; // neutral at its defaults by design
        // Canvas effects work around a placed clip; picture effects on a whole picture.
        const bool canvas = isCanvas(effect);
        // Smoothing is gentle by default; on this small picture, full strength shows it.
        std::map<std::string, std::vector<float>> values;
        if (effect.manifest.id == "tve.blur.smooth")
            values = {{"strength", {1}}, {"radius", {2}}};
        float changed = 0.0f;
        for (const float seconds : {0.0f, 0.7f}) {
            const Pixels out = canvas ? run(effect.manifest.id, values, inset(), seconds, kInset)
                                      : run(effect.manifest.id, values, picture, seconds);
            bool finite = true;
            float lowest = 0.0f;
            float highest = 1.0f;
            for (const float v : out.rgba) {
                finite = finite && std::isfinite(v);
                lowest = std::min(lowest, v);
                highest = std::max(highest, v);
            }
            CHECK(finite);
            CHECK(lowest >= -0.01f);
            CHECK(highest <= 1.01f);
            changed = std::max(changed, out.maxDifference(canvas ? placed : plain));
        }
        CHECK(changed > 0.05f);
    }
}

TEST_CASE("geometry effects at zero strength leave the picture alone", "[library]")
{
    const Pixels plain = fromImage(pattern());
    CHECK(run("tve.distort.swirl", {{"angle", {0}}}).maxDifference(plain) < 0.01f);
    CHECK(run("tve.distort.bulge", {{"amount", {0}}}).maxDifference(plain) < 0.01f);
    CHECK(run("tve.distort.lens", {{"amount", {0}}}).maxDifference(plain) < 0.01f);
    CHECK(run("tve.distort.wave", {{"amplitude", {0}}}).maxDifference(plain) < 0.01f);
    CHECK(run("tve.glitch.digital", {{"amount", {0}}, {"colourShift", {0}}}).maxDifference(plain) < 0.01f);
    CHECK(run("tve.blur.directional", {{"strength", {0}}}).maxDifference(plain) < 0.01f);
    CHECK(run("tve.light.glow", {{"intensity", {0}}}).maxDifference(plain) < 0.01f);
    CHECK(run("tve.frame.rounded_corners", {{"radius", {0}}}).maxDifference(plain) < 0.01f);
}

TEST_CASE("colour effects do what they say", "[library]")
{
    // Black & white: grey everywhere.
    const Pixels grey = run("tve.color.black_white");
    for (std::size_t i = 0; i < grey.rgba.size(); i += 4) {
        CHECK(grey.rgba[i] == Approx(grey.rgba[i + 1]).margin(0.01));
        CHECK(grey.rgba[i + 1] == Approx(grey.rgba[i + 2]).margin(0.01));
    }
    // Posterize to 3 levels: only 0, ½ and 1 in each channel.
    const Pixels bands = run("tve.color.posterize", {{"levels", {3}}});
    for (std::size_t i = 0; i < bands.rgba.size(); ++i) {
        if (i % 4 == 3)
            continue;
        const float v = bands.rgba[i];
        CHECK((std::abs(v) < 0.01f || std::abs(v - 0.5f) < 0.01f || std::abs(v - 1.0f) < 0.01f));
    }
    // Threshold: two colours only (but for a pixel or two right at the level).
    const Pixels two = run("tve.color.threshold", {{"softness", {0}}});
    std::size_t between = 0;
    for (std::size_t i = 0; i < two.rgba.size(); i += 4)
        between += two.rgba[i] > 0.01f && two.rgba[i] < 0.99f;
    CHECK(between * 100 <= two.rgba.size() / 4);
    // Hue rotate by 120°: red turns green.
    QImage red(kW, kH, QImage::Format_RGBA8888);
    red.fill(QColor(200, 30, 30));
    const float* turned = run("tve.color.hue_rotate", {{"angle", {120}}}, red).at(10, 10);
    CHECK(turned[1] > turned[0] + 0.2f);
    CHECK(turned[1] > turned[2]);
}

TEST_CASE("smoothing evens out grain but keeps edges", "[library]")
{
    // Mid grey with grain on the left half, black on the right.
    QImage grainy(kW, kH, QImage::Format_RGBA8888);
    for (int y = 0; y < kH; ++y)
        for (int x = 0; x < kW; ++x)
            grainy.setPixelColor(x, y, x < kW / 2 ? QColor::fromRgb(QRgb(0x808080 + ((x * 31 + y * 17) % 2 ? 0x101010 : 0)))
                                                  : QColor(Qt::black));
    const Pixels before = fromImage(grainy);
    const Pixels after = run("tve.blur.smooth", {{"strength", {1}}, {"radius", {4}}}, grainy);
    const auto spread = [](const Pixels& p) {
        float lo = 1.0f, hi = 0.0f;
        for (int y = 10; y < 40; ++y)
            for (int x = 10; x < 30; ++x) {
                lo = std::min(lo, p.at(x, y)[0]);
                hi = std::max(hi, p.at(x, y)[0]);
            }
        return hi - lo;
    };
    CHECK(spread(after) < spread(before) * 0.5f);
    CHECK(after.at(kW / 2 - 2, 20)[0] > 0.45f); // the edge stays sharp
    CHECK(after.at(kW / 2 + 1, 20)[0] < 0.05f);
}

TEST_CASE("flip and mirror rearrange the picture exactly", "[library]")
{
    const Pixels plain = fromImage(pattern());
    const Pixels flipped = run("tve.distort.flip");
    const Pixels mirrored = run("tve.distort.mirror");
    for (int y = 0; y < kH; y += 5) {
        for (int x = 0; x < kW; x += 7) {
            for (int c = 0; c < 3; ++c) {
                CHECK(flipped.at(x, y)[c] == Approx(plain.at(kW - 1 - x, y)[c]).margin(0.01));
                CHECK(mirrored.at(x, y)[c] == Approx(mirrored.at(kW - 1 - x, y)[c]).margin(0.01));
            }
        }
    }
    CHECK(mirrored.at(10, 20)[0] == Approx(plain.at(10, 20)[0]).margin(0.01)); // the left half stays
}

TEST_CASE("time-based effects follow the clock", "[library]")
{
    const Pixels plain = fromImage(pattern());
    // The strobe flashes at the start of each period and is off in between.
    const std::map<std::string, std::vector<float>> strobe{{"frequency", {2}}, {"intensity", {1}}};
    CHECK(run("tve.light.strobe", strobe, pattern(), 0.0f).at(5, 5)[0] == Approx(1.0f).margin(0.01));
    CHECK(run("tve.light.strobe", strobe, pattern(), 0.3f).maxDifference(plain) < 0.01f);
    // Shake moves between moments; rotating hues cycle.
    CHECK(run("tve.distort.shake", {}, pattern(), 0.0f).maxDifference(run("tve.distort.shake", {}, pattern(), 0.4f)) > 0.05f);
    const std::map<std::string, std::vector<float>> cycling{{"angle", {0}}, {"speed", {90}}};
    CHECK(run("tve.color.hue_rotate", cycling, pattern(), 0.0f).maxDifference(plain) < 0.01f);
    CHECK(run("tve.color.hue_rotate", cycling, pattern(), 1.0f).maxDifference(plain) > 0.1f);
}

TEST_CASE("a later pass reads its effect's input even mid-chain", "[library]")
{
    // A white dot on black: the glow spreads light around it and keeps the dot.
    QImage dot(kW, kH, QImage::Format_RGBA8888);
    dot.fill(Qt::black);
    for (int y = 24; y < 30; ++y)
        for (int x = 45; x < 51; ++x)
            dot.setPixelColor(x, y, Qt::white);
    const std::map<std::string, std::vector<float>> glow{{"threshold", {0.5}}, {"radius", {10}}, {"intensity", {2}}};
    const Pixels alone = run("tve.light.glow", glow, dot);
    CHECK(alone.at(47, 27)[0] == Approx(1.0f).margin(0.01));
    CHECK(alone.at(47, 33)[0] > 0.05f); // light below the dot
    CHECK(alone.at(5, 5)[0] < 0.01f);    // dark far away
    // After another effect its input is an internal texture, which the passes before the
    // last must not overwrite: the result is the same (colour adjust at defaults changes nothing).
    const Pixels after = run({{"tve.color.adjust", true, {}}, {"tve.light.glow", true, glow}}, dot);
    CHECK(after.maxDifference(alone) < 0.01f);
    // Twice in a row, each reading its own input.
    const Pixels twice = run({{"tve.light.glow", true, glow}, {"tve.light.glow", true, glow}}, dot);
    CHECK(twice.at(47, 33)[0] > alone.at(47, 33)[0] + 0.02f);
}

TEST_CASE("frame effects draw around a placed clip", "[library]")
{
    const QImage clip = inset();
    // The drop shadow falls down and to the right (45°), not up and to the left.
    const Pixels shadow = run("tve.frame.drop_shadow", {{"offset", {10}}, {"blur", {1}}, {"opacity", {1}}}, clip, 0.0f, kInset);
    CHECK(shadow.at(74, 42)[3] > 0.5f);
    CHECK(shadow.at(74, 42)[0] < 0.05f);
    CHECK(shadow.at(20, 11)[3] < 0.01f);
    CHECK(shadow.at(48, 27)[3] == Approx(1.0f)); // the clip itself is untouched
    CHECK(shadow.at(48, 27)[0] == Approx(fromImage(clip).at(48, 27)[0]).margin(0.01));

    // An outline just outside the clip, in its colour; nothing far away.
    const Pixels outline = run("tve.frame.outline", {{"width", {4}}, {"color", {1, 0, 1, 1}}}, clip, 0.0f, kInset);
    CHECK(outline.at(48, 12)[3] > 0.9f);
    CHECK(outline.at(48, 12)[0] > 0.9f);
    CHECK(outline.at(48, 12)[1] < 0.1f);
    CHECK(outline.at(48, 2)[3] < 0.01f);

    // Background fill covers the canvas: with a colour, or the clip enlarged and blurred
    // (its colours come from the clip: red grows to the right).
    const Pixels coloured = run("tve.frame.background_fill", {{"mode", {1}}, {"color", {0, 0, 1, 1}}}, clip, 0.0f, kInset);
    CHECK(coloured.at(3, 3)[3] == Approx(1.0f));
    CHECK(coloured.at(3, 3)[2] == Approx(1.0f).margin(0.01));
    const Pixels blurred = run("tve.frame.background_fill", {{"dim", {0}}}, clip, 0.0f, kInset);
    CHECK(blurred.at(3, 27)[3] == Approx(1.0f));
    CHECK(blurred.at(92, 27)[3] == Approx(1.0f));
    CHECK(blurred.at(92, 27)[0] > blurred.at(3, 27)[0] + 0.3f);

    // Outer glow lights the area around the clip.
    const Pixels glow = run("tve.frame.outer_glow", {{"size", {10}}}, clip, 0.0f, kInset);
    CHECK(glow.at(48, 11)[3] > 0.2f);
    CHECK(glow.at(48, 1)[3] < glow.at(48, 11)[3]);
}

TEST_CASE("rounded corners cut the picture's corners", "[library]")
{
    const Pixels round = run("tve.frame.rounded_corners", {{"radius", {100}}});
    CHECK(round.at(0, 0)[3] < 0.01f);
    CHECK(round.at(kW - 1, kH - 1)[3] < 0.01f);
    CHECK(round.at(kW / 2, kH / 2)[3] == Approx(1.0f));
    CHECK(round.at(kW / 2, 1)[3] > 0.9f); // a pill: the middle of each long edge stays
}
