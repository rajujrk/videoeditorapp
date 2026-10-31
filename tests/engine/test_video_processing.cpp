#include "decode/Thumbnail.h"
#include "decode/VideoDecoder.h"
#include "encode/VideoProcessing.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <filesystem>

#include <unistd.h>

using namespace tve;
using Catch::Approx;
namespace fs = std::filesystem;

namespace {

std::string sample(const char* name)
{
    const std::string path = (fs::path(TVE_SAMPLES_DIR) / name).string();
    if (!fs::exists(path))
        SKIP(name << " missing (run tools/make_test_media.sh)");
    return path;
}

std::string scratch(const std::string& name)
{
    return (fs::temp_directory_path() / ("tve_processing_" + std::to_string(::getpid()) + "_" + name)).string();
}

std::vector<RgbaImage> frames(const std::string& path, int height = 180)
{
    std::vector<RgbaImage> out;
    auto decoder = VideoDecoder::open(path);
    REQUIRE(decoder);
    while (auto frame = decoder->nextFrame())
        if (auto picture = pictureOf(*frame, height))
            out.push_back(std::move(*picture));
    return out;
}

double difference(const RgbaImage& a, const RgbaImage& b)
{
    double sum = 0.0;
    for (std::size_t i = 0; i < a.pixels.size(); ++i)
        sum += std::abs(int(a.pixels[i]) - int(b.pixels[i]));
    return sum / double(a.pixels.size());
}

// Share of nearly white pixels inside a rectangle (fractions).
double whiteShare(const RgbaImage& p, double x0, double y0, double x1, double y1)
{
    int white = 0, all = 0;
    for (int y = int(y0 * p.height); y < int(y1 * p.height); ++y)
        for (int x = int(x0 * p.width); x < int(x1 * p.width); ++x) {
            const std::uint8_t* c = &p.pixels[(std::size_t(y) * std::size_t(p.width) + std::size_t(x)) * 4];
            white += c[0] > 230 && c[1] > 230 && c[2] > 230;
            ++all;
        }
    return double(white) / double(std::max(1, all));
}

} // namespace

TEST_CASE("smooth slow motion makes the frames between", "[apple]")
{
    const std::string out = scratch("smooth.mov");
    std::vector<double> reported;
    std::string error;
    REQUIRE(smoothVideo(sample("people.mp4"), 0, kTicksPerSecond, 4, out, [&](double p) { reported.push_back(p); return true; },
                        &error));
    INFO(error);
    CHECK_FALSE(reported.empty());
    auto decoder = VideoDecoder::open(out);
    REQUIRE(decoder);
    CHECK(decoder->info().frameRate.num == 120 * decoder->info().frameRate.den);
    const auto made = frames(out);
    CHECK(made.size() == 120);
    // A frame made between two of the source's lies between them: closer to each than they are
    // to each other (the portrait slides about 9.5 pixels a frame).
    const double apart = difference(made[0], made[4]);
    CHECK(difference(made[0], made[2]) < apart * 0.8);
    CHECK(difference(made[2], made[4]) < apart * 0.8);
    CHECK(difference(made[0], made[2]) > apart * 0.1); // not a copy
    fs::remove(out);
    CHECK_FALSE(smoothVideo(sample("people.mp4"), 0, kTicksPerSecond, 4, out, [](double) { return false; }, &error));
    CHECK(error.empty());
    CHECK_FALSE(fs::exists(out));
}

TEST_CASE("an object is removed by what the other frames show, or filled in", "[apple]")
{
    // card.mp4: the white card's centre moves from (480, 380) at 120 px a second; it is at most
    // 360 × 360 pixels across as it turns (of 1280 × 720).
    std::vector<TrackSample> card;
    for (int k = 0; k <= 60; ++k) {
        const double t = k / 30.0;
        card.push_back({Ticks(t * double(kTicksPerSecond)), (480.0 + 120.0 * t) / 1280.0, 380.0 / 720.0, 320.0 / 1280.0,
                        230.0 / 720.0, 0.0});
    }
    const std::string out = scratch("removed.mov");
    std::string error;
    REQUIRE(removeObject(sample("card.mp4"), 0, 2 * kTicksPerSecond, card, 0.15, out, {}, &error));
    INFO(error);
    const auto before = frames(sample("card.mp4"));
    const auto after = frames(out);
    REQUIRE(after.size() == 60);
    for (const int k : {5, 30, 55}) {
        const double cx = (480.0 + 4.0 * k) / 1280.0;
        const double x0 = cx - 0.08, x1 = cx + 0.08, y0 = 0.45, y1 = 0.6;
        CHECK(whiteShare(before[std::size_t(k)], x0, y0, x1, y1) > 0.9);
        CHECK(whiteShare(after[std::size_t(k)], x0, y0, x1, y1) < 0.1);
        // Away from the card, the picture is as it was.
        CHECK(difference(RgbaImage{1, 1, {after[std::size_t(k)].pixels.begin(), after[std::size_t(k)].pixels.begin() + 4}},
                         RgbaImage{1, 1, {before[std::size_t(k)].pixels.begin(), before[std::size_t(k)].pixels.begin() + 4}})
              < 12.0);
    }
    fs::remove(out);
    CHECK_FALSE(removeObject(sample("card.mp4"), 0, kTicksPerSecond, {}, 0.1, out, {}, &error));
}

TEST_CASE("upscaling uses Apple's model once it is here", "[apple]")
{
    const UpscaleModel status = upscaleModelStatus();
    CHECK(status != UpscaleModel::Unsupported);
    const std::string out = scratch("up.mov");
    std::string error;
    if (status != UpscaleModel::Ready) {
        CHECK_FALSE(upscaleVideo(sample("people.mp4"), 0, kTicksPerSecond / 2, 2, out, {}, &error));
        CHECK(error.find("Download") != std::string::npos);
        SKIP("Apple's upscaling model is not downloaded on this Mac");
    }
    REQUIRE(upscaleVideo(sample("card.mp4"), 0, kTicksPerSecond / 2, 2, out, {}, &error));
    auto decoder = VideoDecoder::open(out);
    REQUIRE(decoder);
    CHECK(decoder->info().width == 2560);
    CHECK(decoder->info().height == 1440);
    fs::remove(out);
}
