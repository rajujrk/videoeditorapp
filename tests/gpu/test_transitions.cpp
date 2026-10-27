// Renders transitions between solid-colour pictures on the real GPU (Metal, offscreen) and
// checks the pixels.
#include "gpu/TransitionRenderer.h"

#include <QImage>
#include <rhi/qrhi.h>
#include <rhi/qrhi_platform.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <memory>

using namespace tve;

namespace {

constexpr int kW = 64;
constexpr int kH = 36;
constexpr float kTolerance = 3.0f / 255.0f;

QRhi* gpu()
{
    static std::unique_ptr<QRhi> rhi = [] {
        QRhiMetalInitParams params;
        return std::unique_ptr<QRhi>(QRhi::create(QRhi::Metal, &params));
    }();
    return rhi.get();
}

struct Rgba {
    float r, g, b, a;
};

struct Picture {
    std::vector<float> rgba;
    Rgba at(int x, int y) const
    {
        const std::size_t i = std::size_t((y * kW + x) * 4);
        return {rgba[i], rgba[i + 1], rgba[i + 2], rgba[i + 3]};
    }
};

QImage solid(QColor color)
{
    QImage image(kW, kH, QImage::Format_RGBA8888);
    image.fill(color);
    return image;
}

QImage halves(QColor left, QColor right)
{
    QImage image(kW, kH, QImage::Format_RGBA8888);
    for (int y = 0; y < kH; ++y)
        for (int x = 0; x < kW; ++x)
            image.setPixelColor(x, y, x < kW / 2 ? left : right);
    return image;
}

std::optional<Picture> render(const char* id, float progress, const QImage& from, const QImage& to,
                              const std::map<std::string, std::vector<float>>& values = {})
{
    QRhi* rhi = gpu();
    REQUIRE(rhi);
    std::unique_ptr<QRhiTexture> fromTexture(rhi->newTexture(QRhiTexture::RGBA8, from.size()));
    std::unique_ptr<QRhiTexture> toTexture(rhi->newTexture(QRhiTexture::RGBA8, to.size()));
    REQUIRE(fromTexture->create());
    REQUIRE(toTexture->create());

    TransitionRenderer renderer(rhi, EffectLibrary::builtIn());
    QRhiCommandBuffer* cb = nullptr;
    REQUIRE(rhi->beginOffscreenFrame(&cb) == QRhi::FrameOpSuccess);
    QRhiResourceUpdateBatch* upload = rhi->nextResourceUpdateBatch();
    upload->uploadTexture(fromTexture.get(), from);
    upload->uploadTexture(toTexture.get(), to);
    cb->resourceUpdate(upload);

    renderer.begin();
    QRhiTexture* output = renderer.render(cb, id, values, progress, fromTexture.get(), toTexture.get(), 0.0f);
    QRhiReadbackResult result;
    if (output) {
        QRhiResourceUpdateBatch* readback = rhi->nextResourceUpdateBatch();
        readback->readBackTexture({output}, &result);
        cb->resourceUpdate(readback);
    }
    REQUIRE(rhi->endOffscreenFrame() == QRhi::FrameOpSuccess);
    if (!output)
        return std::nullopt;

    Picture picture;
    const std::size_t count = std::size_t(kW * kH * 4);
    picture.rgba.resize(count);
    REQUIRE(result.format == QRhiTexture::RGBA16F);
    qFloatFromFloat16(picture.rgba.data(), reinterpret_cast<const qfloat16*>(result.data.constData()), qsizetype(count));
    return picture;
}

void checkColor(Rgba actual, Rgba expected)
{
    CHECK(actual.r == Catch::Approx(expected.r).margin(kTolerance));
    CHECK(actual.g == Catch::Approx(expected.g).margin(kTolerance));
    CHECK(actual.b == Catch::Approx(expected.b).margin(kTolerance));
    CHECK(actual.a == Catch::Approx(expected.a).margin(kTolerance));
}

const Rgba kRed{1, 0, 0, 1};
const Rgba kGreen{0, 1, 0, 1};
const Rgba kBlue{0, 0, 1, 1};

// Direction enum indices: left, right, up, down.
const std::vector<float> kLeft{0.0f};
const std::vector<float> kUp{2.0f};

} // namespace

TEST_CASE("built-in library loads all 21 transitions")
{
    const EffectLibrary& library = EffectLibrary::builtIn();
    INFO(library.errors().join(QLatin1Char('\n')).toStdString());
    CHECK(library.errors().isEmpty());
    CHECK(library.transitions().size() == 21);
    CHECK(library.findTransition("tve.transition.dissolve"));
    CHECK_FALSE(library.find("tve.transition.dissolve")); // transitions are not clip effects
    for (const LoadedEffect& transition : library.transitions()) {
        INFO(transition.manifest.id);
        CHECK(transition.manifest.kind == "transition");
        CHECK(transition.passes.size() == 1);
    }
}

TEST_CASE("a transition without tve_progress is rejected")
{
    const EffectLibrary library =
        EffectLibrary::load(QStringLiteral(":/test_effects/video"), QStringLiteral(":/effects/common/fullscreen.vert.qsb"),
                            QStringLiteral(":/test_effects/transitions"));
    CHECK(library.transitions().empty());
    CHECK(library.errors().join(QLatin1Char('\n')).contains(QStringLiteral("no_progress: no_progress.frag: a transition's "
                                                                            "EffectParams must have tve_progress")));
}

TEST_CASE("every transition starts on the outgoing picture and ends on the incoming one")
{
    const QImage red = solid(Qt::red);
    const QImage blue = solid(Qt::blue);
    for (const LoadedEffect& transition : EffectLibrary::builtIn().transitions()) {
        const std::string& id = transition.manifest.id;
        INFO(id);
        auto start = render(id.c_str(), 0.0f, red, blue);
        auto end = render(id.c_str(), 1.0f, red, blue);
        REQUIRE(start);
        REQUIRE(end);
        for (auto [x, y] : {std::pair{2, 2}, std::pair{kW / 2, kH / 2}, std::pair{kW - 3, kH - 3}}) {
            INFO("pixel " << x << "," << y);
            checkColor(start->at(x, y), kRed);
            checkColor(end->at(x, y), kBlue);
        }
    }
}

TEST_CASE("cross dissolve mixes the pictures by progress")
{
    auto half = render("tve.transition.dissolve", 0.5f, solid(Qt::red), solid(Qt::blue));
    REQUIRE(half);
    checkColor(half->at(10, 10), {0.5f, 0.0f, 0.5f, 1.0f});
}

TEST_CASE("a wipe's edge moves the way it is named")
{
    // Wiping left, the incoming picture appears on the right first.
    auto left = render("tve.transition.wipe", 0.5f, solid(Qt::red), solid(Qt::blue),
                       {{"direction", kLeft}, {"softness", {0.0f}}});
    REQUIRE(left);
    checkColor(left->at(8, kH / 2), kRed);
    checkColor(left->at(kW - 8, kH / 2), kBlue);

    // Wiping up, it appears at the bottom first (picture rows run top to bottom).
    auto up = render("tve.transition.wipe", 0.5f, solid(Qt::red), solid(Qt::blue), {{"direction", kUp}, {"softness", {0.0f}}});
    REQUIRE(up);
    checkColor(up->at(kW / 2, 4), kRed);
    checkColor(up->at(kW / 2, kH - 4), kBlue);
}

TEST_CASE("push moves the outgoing picture out and the incoming one in")
{
    // From: red | green. Half way through a push to the left, the outgoing picture's right
    // half (green) has moved to the left half and the incoming picture fills the right.
    auto half = render("tve.transition.push", 0.5f, halves(Qt::red, Qt::green), solid(Qt::blue), {{"direction", kLeft}});
    REQUIRE(half);
    checkColor(half->at(8, kH / 2), kGreen);
    checkColor(half->at(kW - 8, kH / 2), kBlue);
}

TEST_CASE("an unknown transition renders nothing")
{
    CHECK_FALSE(render("tve.transition.nope", 0.5f, solid(Qt::red), solid(Qt::blue)));
}
