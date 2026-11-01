#include "video/OfxHost.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <filesystem>

using namespace tve;
using Catch::Approx;

// The test plug-in (tests/ofx/TestInvert.mm): inverts (tinted, by an amount) or keeps the
// picture, an offset adding to red and green; CPU and Metal. Found by the real scanner.
namespace {
constexpr const char* kInvert = "ofx:com.techievideoeditor.tests.Invert";

void useTestPlugins()
{
    setenv("TVE_OFX_PATH", TVE_TEST_OFX_DIR, 1);
    setenv("TVE_PLUGIN_SCANNER", TVE_PLUGIN_SCANNER_PATH, 1);
}

// A 4 × 2 picture: top row red, bottom row half-grey; all opaque.
std::vector<float> picture()
{
    std::vector<float> rgba(4 * 2 * 4);
    for (int x = 0; x < 4; ++x) {
        float* top = &rgba[std::size_t(x) * 4];
        float* bottom = &rgba[std::size_t(4 + x) * 4];
        top[0] = 1, top[1] = 0, top[2] = 0, top[3] = 1;
        bottom[0] = bottom[1] = bottom[2] = 0.5f, bottom[3] = 1;
    }
    return rgba;
}
} // namespace

TEST_CASE("OpenFX plug-ins are found and described in another process")
{
    useTestPlugins();
    const auto all = listOfxPlugins();
    const auto invert = std::ranges::find(all, std::string(kInvert), &OfxPluginInfo::id);
    REQUIRE(invert != all.end());
    CHECK(invert->name == "TVE Test Invert");
    CHECK(invert->group == "TVE/Tests");
    CHECK(invert->metal);
    CHECK(invert->problem.empty());
    REQUIRE(invert->parameters.size() == 4); // the string one isn't offered
    CHECK(invert->parameters[0].name == "amount");
    CHECK(invert->parameters[0].defaults == std::vector<double>{1.0});
    CHECK(invert->parameters[1].type == "rgb");
    CHECK(invert->parameters[2].options == std::vector<std::string>{"Invert", "Keep"});
    CHECK(invert->parameters[3].type == "double2d");
    CHECK(invert->parameters[3].min == -1.0);
}

TEST_CASE("an OpenFX plug-in renders on the CPU and with Metal alike")
{
    useTestPlugins();
    std::string error;
    auto effect = OfxEffect::open(kInvert, &error);
    INFO(error);
    REQUIRE(effect);
    for (const bool metal : {false, true}) {
        INFO((metal ? "Metal" : "CPU"));
        // Inverted: red → cyan, the top row stays the top row.
        auto rgba = picture();
        REQUIRE(effect->render(rgba, 4, 2, 0.0, {}, metal));
        CHECK(effect->usedMetal() == metal);
        CHECK(rgba[0] == Approx(0.0f));
        CHECK(rgba[1] == Approx(1.0f));
        CHECK(rgba[2] == Approx(1.0f));
        CHECK(rgba[3] == Approx(1.0f));
        CHECK(rgba[4 * 4] == Approx(0.5f));
        // Half the amount, tinted red only, offset green by 0.25 ("offset#1").
        rgba = picture();
        REQUIRE(effect->render(rgba, 4, 2, 1.0,
                               {{"amount", {0.5f}}, {"tint", {1.0f, 0.0f, 0.0f}}, {"offset#1", {0.25f}}}, metal));
        CHECK(rgba[0] == Approx(0.5f));        // 1 + (0 - 1) × 0.5
        CHECK(rgba[1] == Approx(0.25f));       // 0 + (0 - 0) × 0.5, + 0.25
        CHECK(rgba[2] == Approx(0.0f));
        // Kept as it is (the choice).
        rgba = picture();
        REQUIRE(effect->render(rgba, 4, 2, 2.0, {{"mode", {1.0f}}}, metal));
        CHECK(rgba == picture());
    }
}

TEST_CASE("OpenFX plug-ins that aren't there, or bundles that aren't plug-ins")
{
    std::string error;
    CHECK_FALSE(OfxEffect::open("ofx:com.example.none", &error));
    CHECK(error.find("isn't installed") != std::string::npos);
    const auto folder = std::filesystem::temp_directory_path() / "tve_ofx_broken";
    std::filesystem::remove_all(folder);
    std::filesystem::create_directories(folder / "Broken.ofx.bundle" / "Contents" / "MacOS");
    setenv("TVE_OFX_PATH", folder.c_str(), 1);
    setenv("TVE_PLUGIN_SCANNER", TVE_PLUGIN_SCANNER_PATH, 1);
    const auto all = listOfxPlugins();
    REQUIRE(all.size() == 1);
    CHECK(all[0].name == "Broken");
    CHECK(!all[0].problem.empty());
    std::filesystem::remove_all(folder);
    useTestPlugins();
}
