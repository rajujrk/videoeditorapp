#include "io/ProjectSerializer.h"
#include "model/ColourMatch.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <random>

using namespace tve;

namespace {

// A dim picture with a blue cast: values spread over a narrow range, blue raised.
std::vector<std::array<float, 3>> castPicture(float r, float g, float b, float spread)
{
    std::mt19937 random(7);
    std::uniform_real_distribution<float> u(0.0f, 1.0f);
    std::vector<std::array<float, 3>> pixels;
    for (int i = 0; i < 5000; ++i) {
        const float t = u(random);
        pixels.push_back({r + spread * t, g + spread * t, b + spread * t});
    }
    return pixels;
}

std::vector<std::array<float, 3>> graded(const ColorGrade& grade, std::vector<std::array<float, 3>> pixels)
{
    const auto c = gradeCoefficients(grade);
    const auto curves = bakeCurves(grade);
    for (auto& p : pixels)
        p = applyGrade(c, curves, p);
    return pixels;
}

double castOf(const ColourStats& s) { return std::abs(s.channels[2][2] - s.channels[0][2]); }

} // namespace

TEST_CASE("auto colour takes a cast off and fills the levels", "[colour]")
{
    const auto pixels = castPicture(0.15f, 0.2f, 0.35f, 0.3f);
    const ColourStats before = colourStats(pixels);
    CHECK(castOf(before) > 0.15);
    const ColorGrade grade = withMatchingCurves({}, before, autoColourTarget(before));
    REQUIRE_FALSE(validateGrade(grade));
    CHECK(grade.master.empty()); // only the channel curves
    const ColourStats after = colourStats(graded(grade, pixels));
    CHECK(castOf(after) < 0.06);
    CHECK(after.luma[4] - after.luma[0] > (before.luma[4] - before.luma[0]) * 1.8);
    CHECK(after.luma[0] < 0.12);
    CHECK(after.luma[4] > 0.85);
}

TEST_CASE("colour match makes one picture's colours like another's", "[colour]")
{
    const auto warm = castPicture(0.5f, 0.35f, 0.2f, 0.4f);
    const auto cold = castPicture(0.2f, 0.3f, 0.45f, 0.2f);
    const ColourStats reference = colourStats(warm);
    const ColorGrade grade = withMatchingCurves({}, colourStats(cold), reference);
    REQUIRE_FALSE(validateGrade(grade));
    const ColourStats after = colourStats(graded(grade, cold));
    for (std::size_t c = 0; c < 3; ++c)
        for (std::size_t q = 0; q < 5; ++q)
            CHECK(after.channels[c][q] == Catch::Approx(reference.channels[c][q]).margin(0.04));
    // Its other settings stay.
    ColorGrade mine;
    mine.exposure = 0.5;
    mine.master = {{0, 0}, {1, 0.9}};
    const ColorGrade kept = withMatchingCurves(mine, colourStats(cold), reference);
    CHECK(kept.exposure == 0.5);
    CHECK(kept.master == mine.master);
}

TEST_CASE("HDR wheels move only their range of tones and are saved", "[colour]")
{
    ColorGrade grade;
    grade.zones[5] = {0.0, 0.0, -0.6}; // specular darker
    const auto c = gradeCoefficients(grade);
    const auto curves = bakeCurves(grade);
    REQUIRE(c.zonesActive);
    CHECK(applyGrade(c, curves, {0.98f, 0.98f, 0.98f})[0] < 0.9f);
    CHECK(applyGrade(c, curves, {0.3f, 0.3f, 0.3f})[0] == Catch::Approx(0.3f).margin(1e-4)); // shadows untouched
    CHECK(toneZoneWeight(2, 0.32f) == Catch::Approx(1.0f));
    CHECK(toneZoneWeight(2, 0.6f) == 0.0f);
    // Halfway between two centres the zones share it.
    const float between = (kToneZoneCentres[2] + kToneZoneCentres[3]) / 2;
    CHECK(toneZoneWeight(2, between) + toneZoneWeight(3, between) > 0.8f);
    ColorGrade bad;
    bad.zones[1].master = 3;
    CHECK(validateGrade(bad));
    Project project;
    Clip clip{.id = 1, .duration = kTicksPerSecond};
    clip.color = grade;
    project.tracks.push_back(Track{.id = 2, .clips = {clip}});
    project.setNextId(3);
    CHECK(projectFromJson(projectToJson(project)).tracks == project.tracks);
}
