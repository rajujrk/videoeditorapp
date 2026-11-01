#include "audio/plugins/AudioPlugin.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <vector>

using namespace tve;
using Catch::Approx;

// Apple's own Audio Units, on every Mac: AUDelay and AUPeakLimiter.
namespace {
constexpr const char* kDelay = "au:aufx/dely/appl";
constexpr const char* kLimiter = "au:aufx/lmtr/appl";

std::string paramNamed(const AudioPlugin& plugin, const std::string& name)
{
    for (const PluginParameter& p : plugin.parameters())
        if (p.name == name)
            return p.id;
    return {};
}
} // namespace

TEST_CASE("Audio Units on this Mac are listed")
{
    const auto units = listAudioUnits();
    const auto delay = std::ranges::find(units, std::string(kDelay), &PluginDescription::id);
    REQUIRE(delay != units.end());
    CHECK(delay->name == "AUDelay");
    CHECK(delay->format == "AU");
    CHECK(delay->problem.empty());
}

TEST_CASE("an Audio Unit processes sound, with its parameters set as 0..1")
{
    std::string error;
    auto delay = AudioPlugin::open(kDelay, &error);
    REQUIRE(delay);
    INFO(error);
    const std::string mix = paramNamed(*delay, "Dry/Wet Mix");
    const std::string time = paramNamed(*delay, "Delay Time");
    const std::string feedback = paramNamed(*delay, "Feedback");
    REQUIRE(!mix.empty());
    REQUIRE(!time.empty());
    // All wet, 0.1 s (of 0..2 s), no feedback.
    delay->setParameter(mix, 1.0);
    delay->setParameter(time, 0.05);
    delay->setParameter(feedback, (0.0 - -99.9) / (99.9 - -99.9));
    CHECK(delay->parameter(time) == Approx(0.05).margin(1e-4));
    CHECK(delay->displayValue(time, 0.05) == "0.1 Secs");

    // An impulse comes out 4800 samples later.
    std::vector<float> sound(48000 * 2, 0.0f);
    sound[0] = sound[1] = 1.0f;
    delay->process(sound.data(), 48000);
    int loudest = 0;
    for (int i = 1; i < 48000; ++i)
        if (std::abs(sound[std::size_t(i) * 2]) > std::abs(sound[std::size_t(loudest) * 2]))
            loudest = i;
    CHECK(std::abs(loudest - 4800) <= 8); // (its delay line interpolates)
    CHECK(std::abs(sound[0]) < 0.01f);

    // Its state, into another instance.
    const std::string state = delay->saveState();
    REQUIRE(!state.empty());
    auto again = AudioPlugin::open(kDelay);
    REQUIRE(again);
    CHECK(again->parameter(time) != Approx(0.05).margin(1e-3));
    REQUIRE(again->loadState(state));
    CHECK(again->parameter(time) == Approx(0.05).margin(1e-4));
    CHECK_FALSE(again->loadState("not a state"));
}

TEST_CASE("plug-ins report latency and tail; unknown ones don't open")
{
    auto limiter = AudioPlugin::open(kLimiter);
    REQUIRE(limiter);
    CHECK(limiter->latency() > 0);
    auto delay = AudioPlugin::open(kDelay);
    REQUIRE(delay);
    CHECK(delay->tail() > 0.0);
    std::string error;
    CHECK_FALSE(AudioPlugin::open("au:aufx/none/nope", &error));
    CHECK(error.find("isn't installed") != std::string::npos);
    CHECK_FALSE(AudioPlugin::open("ladspa:x", &error));
}

// VST3, with the test plug-in (tests/vst3/TestGain.cpp: gain 0..1 → ×0..2, 64 samples of
// latency) found in the build's tests/vst3 folder by the real out-of-process scanner.
namespace {
constexpr const char* kTestGain = "vst3:5A3C1E427B9D4F608E21C3A4D05B6F17";

void useTestPlugins()
{
    setenv("TVE_VST3_PATH", TVE_TEST_VST3_DIR, 1);
    setenv("TVE_PLUGIN_SCANNER", TVE_PLUGIN_SCANNER_PATH, 1);
}
} // namespace

TEST_CASE("VST3 plug-ins are found by a scan in another process")
{
    useTestPlugins();
    const auto all = listPlugins();
    const auto gain = std::ranges::find(all, std::string(kTestGain), &PluginDescription::id);
    REQUIRE(gain != all.end());
    CHECK(gain->name == "TVE Test Gain");
    CHECK(gain->vendor == "TechieVideoEditor tests");
    CHECK(gain->format == "VST3");
    CHECK(gain->problem.empty());
    // Audio Units are listed too.
    CHECK(std::ranges::any_of(all, [](const PluginDescription& p) { return p.id == kDelay; }));
}

TEST_CASE("a VST3 plug-in processes sound, reports its latency and keeps its state")
{
    useTestPlugins();
    std::string error;
    auto gain = AudioPlugin::open(kTestGain, &error);
    INFO(error);
    REQUIRE(gain);
    const auto params = gain->parameters();
    REQUIRE(params.size() == 1);
    CHECK(params[0].name == "Gain");
    CHECK(params[0].defaultValue == Approx(0.5));
    CHECK(gain->latency() == 64);
    CHECK(gain->displayValue(params[0].id, 0.5).find("x") != std::string::npos);

    // Twice as loud, 64 samples late.
    gain->setParameter(params[0].id, 1.0);
    std::vector<float> sound(2048 * 2, 0.0f);
    sound[0] = sound[1] = 0.25f;
    gain->process(sound.data(), 2048);
    CHECK(sound[0] == 0.0f);
    CHECK(sound[64 * 2] == Approx(0.5f));
    CHECK(sound[64 * 2 + 1] == Approx(0.5f));

    // Its state, into another instance.
    gain->setParameter(params[0].id, 0.25);
    const std::string state = gain->saveState();
    auto again = AudioPlugin::open(kTestGain);
    REQUIRE(again);
    REQUIRE(again->loadState(state));
    CHECK(again->parameter(params[0].id) == Approx(0.25));
    std::fill(sound.begin(), sound.end(), 0.0f);
    sound[0] = 1.0f;
    again->process(sound.data(), 2048);
    CHECK(sound[64 * 2] == Approx(0.5f));
    CHECK_FALSE(again->loadState("TVE3"));
}

TEST_CASE("a bundle that isn't a plug-in is listed with what's wrong")
{
    const auto folder = std::filesystem::temp_directory_path() / "tve_vst3_broken";
    std::filesystem::remove_all(folder);
    std::filesystem::create_directories(folder / "Broken.vst3" / "Contents" / "MacOS");
    setenv("TVE_VST3_PATH", folder.c_str(), 1);
    setenv("TVE_PLUGIN_SCANNER", TVE_PLUGIN_SCANNER_PATH, 1);
    const auto all = listPlugins();
    const auto broken = std::ranges::find(all, std::string("Broken"), &PluginDescription::name);
    REQUIRE(broken != all.end());
    CHECK(!broken->problem.empty());
    std::string error;
    CHECK_FALSE(AudioPlugin::open(broken->id, &error));
    std::filesystem::remove_all(folder);
    useTestPlugins();
}
