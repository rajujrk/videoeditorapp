// Runs the built-in effects on the real GPU (Metal, offscreen) and checks the pixels.
#include "gpu/EffectChain.h"

#include <QImage>
#include <rhi/qrhi.h>
#include <rhi/qrhi_platform.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <memory>

using namespace tve;

namespace {

constexpr int kWidth = 128;
constexpr int kHeight = 96; // not square, to catch aspect and orientation mistakes
constexpr float kTolerance = 2.0f / 255.0f;

QRhi* gpu()
{
    static std::unique_ptr<QRhi> rhi = [] {
        QRhiMetalInitParams params;
        return std::unique_ptr<QRhi>(QRhi::create(QRhi::Metal, &params));
    }();
    return rhi.get();
}

struct Image {
    int width = 0;
    int height = 0;
    std::vector<float> rgba;

    float at(int x, int y, int channel) const { return rgba[std::size_t((y * width + x) * 4 + channel)]; }
    float luma(int x, int y) const { return 0.2126f * at(x, y, 0) + 0.7152f * at(x, y, 1) + 0.0722f * at(x, y, 2); }
};

// Red ramps left→right, green ramps top→bottom, blue is an 8-pixel checkerboard.
QImage testPattern()
{
    QImage image(kWidth, kHeight, QImage::Format_RGBA8888);
    for (int y = 0; y < kHeight; ++y) {
        for (int x = 0; x < kWidth; ++x) {
            const int blue = ((x / 8) + (y / 8)) % 2 ? 255 : 0;
            image.setPixelColor(x, y, QColor(x * 2, y * 255 / (kHeight - 1), blue));
        }
    }
    return image;
}

QImage flatGray()
{
    QImage image(kWidth, kHeight, QImage::Format_RGBA8888);
    image.fill(QColor(128, 128, 128));
    return image;
}

Image toImage(const QImage& source)
{
    Image image{source.width(), source.height(), {}};
    for (int y = 0; y < source.height(); ++y) {
        for (int x = 0; x < source.width(); ++x) {
            const QColor c = source.pixelColor(x, y);
            image.rgba.insert(image.rgba.end(), {float(c.redF()), float(c.greenF()), float(c.blueF()), float(c.alphaF())});
        }
    }
    return image;
}

Image readBack(const QRhiReadbackResult& result)
{
    Image image{result.pixelSize.width(), result.pixelSize.height(), {}};
    const std::size_t count = std::size_t(image.width * image.height * 4);
    image.rgba.resize(count);
    if (result.format == QRhiTexture::RGBA16F) {
        REQUIRE(std::size_t(result.data.size()) == count * sizeof(qfloat16));
        qFloatFromFloat16(image.rgba.data(), reinterpret_cast<const qfloat16*>(result.data.constData()), qsizetype(count));
    } else {
        REQUIRE(result.format == QRhiTexture::RGBA8);
        REQUIRE(std::size_t(result.data.size()) == count);
        for (std::size_t i = 0; i < count; ++i)
            image.rgba[i] = float(std::uint8_t(result.data[qsizetype(i)])) / 255.0f;
    }
    return image;
}

// Uploads `input`, runs the chain once and reads the result back.
Image run(std::vector<EffectInstance> instances, const QImage& input = testPattern())
{
    QRhi* rhi = gpu();
    REQUIRE(rhi);
    std::unique_ptr<QRhiTexture> source(rhi->newTexture(QRhiTexture::RGBA8, input.size(), 1,
                                                        QRhiTexture::UsedAsTransferSource));
    REQUIRE(source->create());

    EffectChain chain(rhi, EffectLibrary::builtIn());
    chain.setInstances(std::move(instances));

    QRhiCommandBuffer* cb = nullptr;
    REQUIRE(rhi->beginOffscreenFrame(&cb) == QRhi::FrameOpSuccess);
    QRhiResourceUpdateBatch* upload = rhi->nextResourceUpdateBatch();
    upload->uploadTexture(source.get(), input);
    cb->resourceUpdate(upload);

    QRhiTexture* output = chain.process(cb, source.get(), 0.0f);

    QRhiReadbackResult result;
    QRhiResourceUpdateBatch* readback = rhi->nextResourceUpdateBatch();
    readback->readBackTexture({output}, &result);
    cb->resourceUpdate(readback);
    REQUIRE(rhi->endOffscreenFrame() == QRhi::FrameOpSuccess);
    return readBack(result);
}

void requireSameImage(const Image& actual, const Image& expected)
{
    REQUIRE(actual.width == expected.width);
    REQUIRE(actual.height == expected.height);
    float worst = 0.0f;
    for (std::size_t i = 0; i < actual.rgba.size(); ++i)
        worst = std::max(worst, std::abs(actual.rgba[i] - expected.rgba[i]));
    CHECK(worst <= kTolerance);
}

EffectInstance effect(std::string id, std::map<std::string, std::vector<float>> values = {})
{
    return EffectInstance{std::move(id), true, std::move(values)};
}

} // namespace

TEST_CASE("built-in library loads all five effects")
{
    const EffectLibrary& library = EffectLibrary::builtIn();
    INFO(library.errors().join(QLatin1Char('\n')).toStdString());
    CHECK(library.errors().isEmpty());
    CHECK(library.vertexShader().isValid());
    for (const char* id : {"tve.color.adjust", "tve.blur.gaussian", "tve.stylize.vignette", "tve.glitch.rgb_split",
                           "tve.stylize.pixelate"}) {
        INFO(id);
        CHECK(library.find(id));
    }
    CHECK(library.find("tve.blur.gaussian")->passes.size() == 2);
}

TEST_CASE("broken effect packages are rejected with a reason")
{
    const EffectLibrary library = EffectLibrary::load(QStringLiteral(":/test_effects/video"),
                                                      QStringLiteral(":/effects/common/fullscreen.vert.qsb"));
    CHECK(library.effects().empty());
    const QString errors = library.errors().join(QLatin1Char('\n'));
    INFO(errors.toStdString());
    CHECK(library.errors().size() == 3);
    CHECK(errors.contains(QStringLiteral("missing_param: param \"strength\" is not declared")));
    CHECK(errors.contains(QStringLiteral("extra_uniform: gain.frag: uniform \"gain\" has no matching manifest param")));
    CHECK(errors.contains(QStringLiteral("wrong_type: tint.frag: uniform \"tint\" has the wrong type")));
}

TEST_CASE("a chain with nothing enabled returns the source untouched")
{
    QRhi* rhi = gpu();
    REQUIRE(rhi);
    std::unique_ptr<QRhiTexture> source(rhi->newTexture(QRhiTexture::RGBA8, QSize(4, 4)));
    REQUIRE(source->create());
    EffectChain chain(rhi, EffectLibrary::builtIn());
    chain.setInstances({EffectInstance{"tve.color.adjust", false, {}}});

    QRhiCommandBuffer* cb = nullptr;
    REQUIRE(rhi->beginOffscreenFrame(&cb) == QRhi::FrameOpSuccess);
    CHECK(chain.process(cb, source.get(), 0.0f) == source.get());
    rhi->endOffscreenFrame();
}

TEST_CASE("all five effects at neutral settings reproduce the input exactly")
{
    // Six passes in a row also exercises ping-pong and keeps orientation honest.
    const Image output = run({
        effect("tve.color.adjust"),
        effect("tve.blur.gaussian", {{"radius", {0}}}),
        effect("tve.stylize.vignette", {{"amount", {0}}}),
        effect("tve.glitch.rgb_split", {{"amount", {0}}}),
        effect("tve.stylize.pixelate", {{"blockSize", {1}}}),
    });
    requireSameImage(output, toImage(testPattern()));
}

TEST_CASE("color adjust: brightness, invert and saturation")
{
    const Image input = toImage(testPattern());

    const Image brighter = run({effect("tve.color.adjust", {{"brightness", {0.1f}}})});
    CHECK(brighter.at(10, 10, 0) == Catch::Approx(input.at(10, 10, 0) + 0.1f).margin(kTolerance));

    const Image inverted = run({effect("tve.color.adjust", {{"invert", {1}}})});
    CHECK(inverted.at(40, 20, 1) == Catch::Approx(1.0f - input.at(40, 20, 1)).margin(kTolerance));

    const Image gray = run({effect("tve.color.adjust", {{"saturation", {0}}})});
    CHECK(gray.at(70, 50, 0) == Catch::Approx(gray.at(70, 50, 1)).margin(kTolerance));
    CHECK(gray.at(70, 50, 1) == Catch::Approx(gray.at(70, 50, 2)).margin(kTolerance));
}

TEST_CASE("values outside a param's range are clamped")
{
    const Image clamped = run({effect("tve.color.adjust", {{"brightness", {5.0f}}})});
    const Image maximum = run({effect("tve.color.adjust", {{"brightness", {1.0f}}})});
    requireSameImage(clamped, maximum);
}

TEST_CASE("gaussian blur smooths the checkerboard and keeps the average")
{
    const Image input = toImage(testPattern());
    const Image blurred = run({effect("tve.blur.gaussian", {{"radius", {6}}})});

    auto variance = [](const Image& image) {
        double sum = 0, sumSq = 0;
        int n = 0;
        for (int y = 16; y < image.height - 16; ++y) {
            for (int x = 16; x < image.width - 16; ++x) {
                sum += image.at(x, y, 2);
                sumSq += double(image.at(x, y, 2)) * image.at(x, y, 2);
                ++n;
            }
        }
        const double mean = sum / n;
        return std::pair{mean, sumSq / n - mean * mean};
    };
    const auto [inputMean, inputVariance] = variance(input);
    const auto [blurMean, blurVariance] = variance(blurred);
    CHECK(blurVariance < inputVariance * 0.25);
    CHECK(blurMean == Catch::Approx(inputMean).margin(0.02));
}

TEST_CASE("pixelate makes every block one color")
{
    const Image output = run({effect("tve.stylize.pixelate", {{"blockSize", {8}}})});
    const Image input = toImage(testPattern());
    for (int y = 0; y < kHeight; ++y) {
        for (int x = 0; x < kWidth; ++x) {
            const int cx = (x / 8) * 8 + 4;
            const int cy = (y / 8) * 8 + 4;
            for (int c = 0; c < 3; ++c) {
                INFO("pixel " << x << "," << y << " channel " << c);
                REQUIRE(output.at(x, y, c) == Catch::Approx(input.at(cx, cy, c)).margin(kTolerance));
            }
        }
    }
}

TEST_CASE("vignette darkens corners and leaves the center")
{
    const Image output = run({effect("tve.stylize.vignette", {{"amount", {1}}, {"size", {0.9f}}, {"softness", {0.6f}}})},
                             flatGray());
    const float center = output.luma(kWidth / 2, kHeight / 2);
    CHECK(center == Catch::Approx(128.0f / 255.0f).margin(kTolerance));
    for (auto [x, y] : {std::pair{0, 0}, {kWidth - 1, 0}, {0, kHeight - 1}, {kWidth - 1, kHeight - 1}}) {
        INFO("corner " << x << "," << y);
        CHECK(output.luma(x, y) < center * 0.5f);
    }
    // Symmetric: left and right edges match.
    CHECK(output.luma(0, kHeight / 2) == Catch::Approx(output.luma(kWidth - 1, kHeight / 2)).margin(kTolerance));
}

TEST_CASE("vignette color tints instead of darkening")
{
    const Image output = run({effect("tve.stylize.vignette", {{"amount", {1}}, {"color", {1, 0, 0, 1}}})}, flatGray());
    CHECK(output.at(0, 0, 0) > 0.9f);
    CHECK(output.at(0, 0, 1) < 0.1f);
}

TEST_CASE("rgb split shifts red and blue in opposite directions")
{
    const Image input = toImage(testPattern());
    const Image output = run({effect("tve.glitch.rgb_split", {{"amount", {4}}, {"angle", {0}}, {"channels", {0}}})});
    for (int x = 8; x < kWidth - 8; x += 7) {
        const int y = 30;
        INFO("x " << x);
        CHECK(output.at(x, y, 0) == Catch::Approx(input.at(x + 4, y, 0)).margin(kTolerance));
        CHECK(output.at(x, y, 1) == Catch::Approx(input.at(x, y, 1)).margin(kTolerance));
        CHECK(output.at(x, y, 2) == Catch::Approx(input.at(x - 4, y, 2)).margin(kTolerance));
    }
}
