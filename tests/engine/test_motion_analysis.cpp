#include "analysis/MotionAnalysis.h"
#include "analysis/Tracking.h"
#include "encode/MediaWriter.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <filesystem>
#include <random>

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

constexpr Ticks kFrame = kTicksPerSecond / 30;

} // namespace

TEST_CASE("a shaky shot's camera motion is measured and steadied", "[apple]")
{
    // shaky.mp4: a window over a picture at x = 96 + 40 sin(1.3 n), y = 780 + 30 sin(2.1 n)
    // (1920 × 1080 pixels): the content moves the other way, at 320 × 180 a sixth as far.
    const auto analysis = analyzeMotion(sample("shaky.mp4"));
    REQUIRE(analysis);
    CHECK(analysis->width == 320);
    CHECK(analysis->height == 180);
    REQUIRE(analysis->times.size() == 90);
    int close = 0;
    for (int n = 1; n < 90; ++n) {
        const double dx = -(40.0 * (std::sin(1.3 * n) - std::sin(1.3 * (n - 1)))) / 6.0;
        const double dy = -(30.0 * (std::sin(2.1 * n) - std::sin(2.1 * (n - 1)))) / 6.0;
        const FrameMotion& m = analysis->motion[std::size_t(n)];
        close += std::abs(m.dx - dx) < 0.4 && std::abs(m.dy - dy) < 0.4 && m.confidence > 0.5;
    }
    CHECK(close >= 80);

    // Steadied, every frame lands where the picture is still: the correction undoes the shake
    // (about its mean), and the zoom hides ±40 / ±30 pixels of it.
    const Stabilization s = stabilize(*analysis, 0.5, true);
    for (int n = 10; n < 80; ++n) {
        const SteadySample& c = s.samples[std::size_t(n)];
        CHECK(c.dx * 1920 == Approx(40.0 * std::sin(1.3 * n)).margin(6.0));
        CHECK(c.dy * 1080 == Approx(30.0 * std::sin(2.1 * n)).margin(6.0));
    }
    CHECK(s.zoom > 1.0 + 2.0 * 30.0 / 1080.0);
    CHECK(s.zoom < 1.12);

    // Kept on disk.
    const AnalysisCache cache((fs::temp_directory_path() / ("tve_motion_" + std::to_string(::getpid()))).string());
    REQUIRE(saveMotion(cache, "v1:9:abc", *analysis));
    const auto loaded = loadMotion(cache, "v1:9:abc");
    REQUIRE(loaded);
    CHECK(loaded->times == analysis->times);
    CHECK(loaded->motion[40].dx == Approx(analysis->motion[40].dx));
    CHECK(loaded->brightness[40] == Approx(analysis->brightness[40]));
    fs::remove_all(cache.directory());
    CHECK_FALSE(analyzeMotion(sample("shaky.mp4"), [](double) { return false; }));
}

TEST_CASE("flicker is measured and evened out", "[apple]")
{
    const auto analysis = analyzeMotion(sample("flicker.mp4"));
    REQUIRE(analysis);
    // Brighter and darker by turns.
    CHECK(analysis->brightness[11] > analysis->brightness[10] + 0.05f);
    CHECK(analysis->brightness[12] < analysis->brightness[11] - 0.05f);
    const Deflicker d = deflicker(*analysis, 0.3);
    for (int n = 10; n < 80; ++n) {
        const float even = analysis->brightness[std::size_t(n)] * d.samples[std::size_t(n)].gain;
        const float next = analysis->brightness[std::size_t(n + 1)] * d.samples[std::size_t(n + 1)].gain;
        CHECK(std::abs(even - next) < 0.015f);
    }
}

TEST_CASE("an object and a flat surface are followed", "[apple]")
{
    // people.mp4: the face's centre moves from x ≈ 0.30 to ≈ 0.75 over 3 s, at y ≈ 0.24.
    const std::string people = sample("people.mp4");
    const auto face = trackRegion(people, 0, 3 * kTicksPerSecond, {0, 0.304, 0.24, 0.1, 0.14, 0.0}, false);
    REQUIRE(face);
    CHECK(face->size() >= 85);
    const auto faceX = [](double t) { return (100 + (1920 - 864 - 200) * t / 3 + 0.56 * 864) / 1920; };
    CHECK(face->back().x == Approx(faceX(toSeconds(face->back().time))).margin(0.03));
    CHECK(face->back().y == Approx(0.24).margin(0.03));
    // Thinned, a steady slide needs few samples.
    CHECK(thinTrack(*face).size() < face->size() / 3);

    // card.mp4: a 300 × 200 card whose centre moves right from (480, 380) at 120 px a second
    // (of 1280 × 720), turning 0.1 rad a second.
    const std::string card = sample("card.mp4");
    const auto surface = trackRegion(card, 0, 2 * kTicksPerSecond, {0, 0.375, 0.528, 0.3, 0.36, 0.0}, true);
    REQUIRE(surface);
    CHECK(surface->size() >= 55);
    // Nearly every sample is right (a frame now and then can be off by more).
    int right = 0;
    for (const TrackSample& at : *surface) {
        const double t = toSeconds(at.time);
        right += std::abs(at.x - (480.0 + 120.0 * t) / 1280.0) < 0.02 && std::abs(at.y - 380.0 / 720.0) < 0.02
              && std::abs(at.angle - 0.1 * t) < 0.03;
    }
    CHECK(right >= int(surface->size()) * 9 / 10);
    const TrackSample& last = surface->back();
    CHECK(last.width * 1280 == Approx(300).margin(25));

    // Nothing rectangular in the box: said so.
    std::string error;
    CHECK_FALSE(trackRegion(people, 0, kTicksPerSecond, {0, 0.03, 0.5, 0.04, 0.1, 0.0}, true, {}, &error));
    CHECK(error.find("rectangle") != std::string::npos);
    CHECK_FALSE(trackRegion(people, 0, kTicksPerSecond, {0, 0.5, 0.5, 0.0, 0.0, 0.0}, false, {}, &error));
}

namespace {

// A plane in perspective over a still background, moving and turning: its corners at frame n
// (pixels of 640 × 360), and the video of it written to `path`.
Quad planeCorners(int n)
{
    const double t = n / 30.0;
    return {180 + 60 * t, 90 + 10 * t, 440 + 40 * t, 100 + 25 * t, 460 + 20 * t, 280 - 15 * t, 170 + 45 * t, 270 + 5 * t};
}

std::string writePlaneVideo(int frames)
{
    const std::string path = (fs::temp_directory_path() / ("tve_plane_" + std::to_string(::getpid()) + ".mov")).string();
    MediaSettings settings;
    settings.container = Container::Mov;
    settings.codec = VideoCodec::ProRes; // (takes RGBA)
    settings.width = 640;
    settings.height = 360;
    settings.frameRate = {30, 1};
    settings.audio = false;
    auto writer = MediaWriter::open(path, settings);
    REQUIRE(writer);
    // A blotchy texture on the plane, a softer one behind it.
    std::mt19937 random(7);
    std::uniform_int_distribution<int> shade(20, 235);
    std::vector<std::array<std::uint8_t, 3>> texture(32 * 32), background(40 * 23);
    for (auto& c : texture)
        c = {std::uint8_t(shade(random)), std::uint8_t(shade(random)), std::uint8_t(shade(random))};
    for (auto& c : background) {
        const auto g = std::uint8_t(60 + shade(random) / 4);
        c = {g, g, std::uint8_t(g + 10)};
    }
    std::vector<std::uint8_t> rgba(640 * 360 * 4);
    for (int n = 0; n < frames; ++n) {
        const Quad px = planeCorners(n);
        Quad q{};
        for (std::size_t i = 0; i < 8; ++i)
            q[i] = px[i];
        const auto toPlane = squareToQuad(q)->inverse();
        for (int y = 0; y < 360; ++y)
            for (int x = 0; x < 640; ++x) {
                const auto [u, v] = toPlane->apply(x + 0.5, y + 0.5);
                const auto& c = (u >= 0 && u < 1 && v >= 0 && v < 1)
                                    ? texture[std::size_t(int(v * 32) * 32 + int(u * 32))]
                                    : background[std::size_t((y / 16) * 40 + x / 16)];
                std::uint8_t* p = &rgba[std::size_t(y * 640 + x) * 4];
                p[0] = c[0], p[1] = c[1], p[2] = c[2], p[3] = 255;
            }
        REQUIRE(writer->writeVideoRgba(rgba.data(), 640 * 4));
    }
    REQUIRE(writer->finish());
    return path;
}

} // namespace

TEST_CASE("a plane is followed in perspective, its four corners", "[apple]")
{
    const int frames = 45;
    const std::string path = writePlaneVideo(frames);
    const Quad start = planeCorners(0);
    Quad fractions{};
    for (std::size_t i = 0; i < 8; ++i)
        fractions[i] = start[i] / (i % 2 == 0 ? 640.0 : 360.0);
    const auto plane = trackPlane(path, 0, 2 * kTicksPerSecond, fractions);
    REQUIRE(plane);
    CHECK(int(plane->size()) >= frames - 2);
    // Each corner within a few pixels of where it really is, frame after frame.
    double worst = 0.0;
    for (std::size_t n = 0; n < plane->size(); ++n) {
        const Quad truth = planeCorners(int(n));
        REQUIRE((*plane)[n].hasCorners());
        for (std::size_t i = 0; i < 8; ++i)
            worst = std::max(worst, std::abs((*plane)[n].corners[i] * (i % 2 == 0 ? 640.0 : 360.0) - truth[i]));
    }
    INFO("worst corner error " << worst << " px");
    CHECK(worst < 3.0);
    // The sample's centre and turn follow the corners.
    const TrackSample& last = plane->back();
    CHECK(last.x == Approx((last.corners[0] + last.corners[2] + last.corners[4] + last.corners[6]) / 4));
    // No surface to follow: said so.
    std::string error;
    CHECK_FALSE(trackPlane(path, 0, kTicksPerSecond, {0.1, 0.1, 0.5, 0.1, 0.9, 0.1, 0.1, 0.5}, {}, &error));
    CHECK_FALSE(error.empty());
    fs::remove(path);
}
