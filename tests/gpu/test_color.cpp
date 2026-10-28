// Grades pictures on the real GPU (Metal, offscreen) and checks the pixels against the
// CPU reference of the same formula.
#include "gpu/ColorGrader.h"

#include <QImage>
#include <rhi/qrhi.h>
#include <rhi/qrhi_platform.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <memory>

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

// Test colours, one per pixel of an 8 × 1 picture.
const std::vector<std::array<int, 3>> kColors{{0, 0, 0},     {255, 255, 255}, {128, 128, 128}, {200, 60, 30},
                                              {30, 160, 220}, {90, 200, 90},  {250, 240, 10},  {12, 30, 60}};

QImage swatches()
{
    QImage image(int(kColors.size()), 1, QImage::Format_RGBA8888);
    for (int x = 0; x < image.width(); ++x)
        image.setPixelColor(x, 0, QColor(kColors[std::size_t(x)][0], kColors[std::size_t(x)][1], kColors[std::size_t(x)][2]));
    return image;
}

struct Result {
    bool passedThrough = false;
    std::vector<std::array<float, 3>> pixels;
    std::string lutError;
};

Result grade(const ColorGrade& g)
{
    QRhi* rhi = gpu();
    REQUIRE(rhi);
    const QImage image = swatches();
    std::unique_ptr<QRhiTexture> source(rhi->newTexture(QRhiTexture::RGBA8, image.size()));
    REQUIRE(source->create());
    ColorGrader grader(rhi);
    REQUIRE(grader.isValid());
    QRhiCommandBuffer* cb = nullptr;
    REQUIRE(rhi->beginOffscreenFrame(&cb) == QRhi::FrameOpSuccess);
    QRhiResourceUpdateBatch* upload = rhi->nextResourceUpdateBatch();
    upload->uploadTexture(source.get(), image);
    cb->resourceUpdate(upload);
    grader.begin();
    QRhiTexture* output = grader.apply(cb, source.get(), g);
    Result result;
    result.passedThrough = output == source.get();
    QRhiReadbackResult readback;
    if (!result.passedThrough) {
        QRhiResourceUpdateBatch* batch = rhi->nextResourceUpdateBatch();
        batch->readBackTexture({output}, &readback);
        cb->resourceUpdate(batch);
    }
    REQUIRE(rhi->endOffscreenFrame() == QRhi::FrameOpSuccess);
    result.lutError = grader.lutError(g.lutPath);
    if (!result.passedThrough) {
        REQUIRE(readback.format == QRhiTexture::RGBA16F);
        std::vector<float> rgba(kColors.size() * 4);
        qFloatFromFloat16(rgba.data(), reinterpret_cast<const qfloat16*>(readback.data.constData()), qsizetype(rgba.size()));
        for (std::size_t i = 0; i < kColors.size(); ++i)
            result.pixels.push_back({rgba[i * 4], rgba[i * 4 + 1], rgba[i * 4 + 2]});
    }
    return result;
}

// The GPU result matches the CPU reference (8-bit input, half-float output, filtered curve).
void matchesReference(const ColorGrade& g)
{
    const Result result = grade(g);
    REQUIRE_FALSE(result.passedThrough);
    const auto c = gradeCoefficients(g);
    const auto curves = bakeCurves(g);
    for (std::size_t i = 0; i < kColors.size(); ++i) {
        const auto expected = applyGrade(c, curves, {kColors[i][0] / 255.0f, kColors[i][1] / 255.0f, kColors[i][2] / 255.0f});
        INFO("colour " << i);
        for (int ch = 0; ch < 3; ++ch)
            CHECK(result.pixels[i][std::size_t(ch)] == Catch::Approx(expected[std::size_t(ch)]).margin(3.0 / 255.0));
    }
}

std::filesystem::path writeLut(const char* name, const std::string& text)
{
    const auto path = std::filesystem::temp_directory_path() / name;
    std::ofstream(path) << text;
    return path;
}

} // namespace

TEST_CASE("a neutral grade costs nothing", "[color]")
{
    CHECK(grade(ColorGrade{}).passedThrough);
}

TEST_CASE("the GPU grade matches the reference", "[color]")
{
    ColorGrade g;
    SECTION("basic")
    {
        g.exposure = 0.7;
        g.contrast = 0.3;
        g.saturation = -0.4;
        g.temperature = 0.5;
        g.tint = -0.3;
        matchesReference(g);
    }
    SECTION("wheels")
    {
        g.lift = {0.2, -0.4, 0.3};
        g.gamma = {-0.5, 0.1, -0.2};
        g.gain = {0.0, 0.6, 0.25};
        g.offset = {0.3, 0.3, -0.1};
        matchesReference(g);
    }
    SECTION("curves")
    {
        g.master = {{0, 0}, {0.25, 0.15}, {0.75, 0.85}, {1, 1}};
        g.red = {{0, 0.1}, {1, 0.9}};
        g.blue = {{0, 1}, {1, 0}};
        matchesReference(g);
    }
}

TEST_CASE("a LUT recolours the picture by its intensity", "[color]")
{
    // Swaps red and blue.
    std::string text = "LUT_3D_SIZE 2\n";
    for (int b = 0; b < 2; ++b)
        for (int g = 0; g < 2; ++g)
            for (int r = 0; r < 2; ++r)
                text += std::to_string(b) + " " + std::to_string(g) + " " + std::to_string(r) + "\n";
    const auto path = writeLut("tve_swap.cube", text);
    ColorGrade g;
    g.lutPath = path.string();
    Result full = grade(g);
    REQUIRE_FALSE(full.passedThrough);
    CHECK(full.lutError.empty());
    const float tolerance = 3.0f / 255.0f;
    // (200, 60, 30) → (30, 60, 200)
    CHECK(full.pixels[3][0] == Catch::Approx(30 / 255.0).margin(tolerance));
    CHECK(full.pixels[3][1] == Catch::Approx(60 / 255.0).margin(tolerance));
    CHECK(full.pixels[3][2] == Catch::Approx(200 / 255.0).margin(tolerance));

    g.lutIntensity = 0.5;
    const Result half = grade(g);
    CHECK(half.pixels[3][0] == Catch::Approx(115 / 255.0).margin(tolerance));
    CHECK(half.pixels[3][2] == Catch::Approx(115 / 255.0).margin(tolerance));

    g.lutIntensity = 0.0;
    CHECK(grade(g).passedThrough);
    std::filesystem::remove(path);
}

TEST_CASE("an unreadable LUT is left out and says why", "[color]")
{
    ColorGrade g;
    g.lutPath = writeLut("tve_broken.cube", "LUT_3D_SIZE 2\n0 0 0\n").string();
    const Result onlyLut = grade(g);
    CHECK(onlyLut.passedThrough);
    CHECK(onlyLut.lutError == "the table has 1 rows, expected 8");
    g.exposure = 1.0; // the rest still applies
    const Result rest = grade(g);
    CHECK_FALSE(rest.passedThrough);
    CHECK(rest.pixels[2][0] > 0.6f);
    std::filesystem::remove(g.lutPath);
}
