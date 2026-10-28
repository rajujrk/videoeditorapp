// The audio processors, the audio effect packages and the loudness meter, on synthetic sound.
#include "audio/AudioEffects.h"
#include "audio/dsp/Loudness.h"
#include "audio/dsp/Processors.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <numbers>
#include <set>

using namespace tve;
using Catch::Approx;

namespace {

constexpr int kRate = 48000;

// A stereo sine of `seconds` at `amplitude` (both channels).
std::vector<float> sine(double frequency, double seconds, double amplitude)
{
    std::vector<float> s(std::size_t(seconds * kRate) * 2);
    for (std::size_t i = 0; i < s.size() / 2; ++i) {
        const float v = float(amplitude * std::sin(2.0 * std::numbers::pi * frequency * double(i) / kRate));
        s[i * 2] = s[i * 2 + 1] = v;
    }
    return s;
}

float peakOf(const std::vector<float>& s, std::size_t from = 0, std::size_t to = SIZE_MAX)
{
    float p = 0.0f;
    for (std::size_t i = from * 2; i < std::min(to * 2, s.size()); ++i)
        p = std::max(p, std::abs(s[i]));
    return p;
}

double rmsOf(const std::vector<float>& s, std::size_t from, std::size_t to)
{
    double sum = 0.0;
    for (std::size_t i = from * 2; i < to * 2; ++i)
        sum += double(s[i]) * s[i];
    return std::sqrt(sum / double((to - from) * 2));
}

// Power of the left channel at one frequency (Goertzel).
double powerAt(const std::vector<float>& s, double frequency, std::size_t from, std::size_t to)
{
    const double k = 2.0 * std::cos(2.0 * std::numbers::pi * frequency / kRate);
    double a = 0.0, b = 0.0;
    for (std::size_t i = from; i < to; ++i) {
        const double c = s[i * 2] + k * a - b;
        b = a;
        a = c;
    }
    return a * a + b * b - k * a * b;
}

// Runs one processor with `settings` over `sound` in 1024-frame blocks.
std::vector<float> through(const char* processor, dsp::Settings settings, std::vector<float> sound)
{
    const dsp::ProcessorInfo* info = dsp::findProcessor(processor);
    REQUIRE(info);
    auto p = info->make();
    p->configure(info->complete(settings));
    for (std::size_t at = 0; at < sound.size() / 2; at += 1024)
        p->process(sound.data() + at * 2, int(std::min<std::size_t>(1024, sound.size() / 2 - at)));
    return sound;
}

std::vector<float> throughEffect(const std::string& id, std::vector<float> sound,
                                 std::map<std::string, std::vector<float>> values = {})
{
    AudioEffectChain chain;
    chain.setEffects({ClipEffect{id, true, std::move(values), {}}});
    for (std::size_t at = 0; at < sound.size() / 2; at += 1024)
        chain.process(sound.data() + at * 2, int(std::min<std::size_t>(1024, sound.size() / 2 - at)));
    return sound;
}

} // namespace

TEST_CASE("the built-in audio effects load and all run", "[audio-dsp]")
{
    const AudioEffectLibrary& library = AudioEffectLibrary::builtIn();
    std::string errors;
    for (const std::string& e : library.errors())
        errors += e + "\n";
    INFO(errors);
    CHECK(library.errors().empty());
    CHECK(library.effects().size() == 40);
    const std::set<std::string> categories{"eq", "dynamics", "space", "echo", "modulation", "pitch", "voice"};
    std::set<std::string> seen;
    // Speech-like test sound: two tones and a little noise.
    std::vector<float> input = sine(220, 1.0, 0.3);
    const std::vector<float> high = sine(3300, 1.0, 0.1);
    for (std::size_t i = 0; i < input.size(); ++i)
        input[i] += high[i] + 0.02f * float(std::sin(double(i) * 12.9898) * 43758.5453 - std::floor(std::sin(double(i) * 12.9898) * 43758.5453) - 0.5);
    for (const EffectManifest& effect : library.effects()) {
        INFO(effect.id);
        CHECK(effect.license == "TVE-Original");
        REQUIRE(effect.categories.size() == 1);
        CHECK(categories.contains(effect.categories[0]));
        seen.insert(effect.categories[0]);
        const auto out = throughEffect(effect.id, input);
        bool finite = true;
        float difference = 0.0f;
        for (std::size_t i = 0; i < out.size(); ++i) {
            finite = finite && std::isfinite(out[i]);
            difference = std::max(difference, std::abs(out[i] - input[i]));
        }
        CHECK(finite);
        CHECK(peakOf(out) < 2.0f);
        // Every effect changes this sound at its defaults, except those with nothing to do to
        // it: a flat EQ, gates (it is loud) and the widener (it is the same on both sides).
        static const std::set<std::string> neutral{"tve.audio.graphic_eq", "tve.audio.noise_gate", "tve.audio.expander",
                                                   "tve.audio.stereo_widener"};
        if (!neutral.contains(effect.id))
            CHECK(difference > 0.01f);
    }
    CHECK(seen == categories);
}

TEST_CASE("broken audio effect packages are left out with a reason", "[audio-dsp]")
{
    const auto make = [](const std::string& chain, const std::string& params = "[]") {
        return R"({"id": "tve.audio.x", "version": 1, "kind": "audio-effect", "title": {"en": "X"}, "params": )" + params
             + R"(, "chain": )" + chain + R"(, "license": "TVE-Original"})";
    };
    const AudioEffectLibrary library = AudioEffectLibrary::fromManifests({
        {"unknown", make(R"([{"processor": "teleporter"}])")},
        {"setting", make(R"([{"processor": "eq", "settings": {"b3k": 2}}])")},
        {"unused", make(R"([{"processor": "eq"}])",
                        R"([{"id": "semitones", "type": "float", "title": {"en": "S"}, "min": 0, "max": 1, "default": 0}])")},
        {"fine", make(R"([{"processor": "eq"}])",
                      R"([{"id": "mix", "type": "float", "title": {"en": "M"}, "min": 0, "max": 1, "default": 1}])")},
    });
    REQUIRE(library.errors().size() == 3);
    CHECK(library.errors()[0] == "unknown: unknown processor \"teleporter\"");
    CHECK(library.errors()[1] == "setting: processor \"eq\" has no setting \"b3k\"");
    CHECK(library.errors()[2] == "unused: param \"semitones\" sets nothing in the chain");
    CHECK(library.effects().size() == 1);
}

TEST_CASE("equalizer bands and cuts", "[audio-dsp]")
{
    // +12 dB at 1 kHz: four times the amplitude.
    const auto boosted = through("eq", {{"b1k", 12.0f}}, sine(1000, 0.5, 0.1));
    CHECK(peakOf(boosted, 12000) == Approx(0.4).epsilon(0.05));
    // A 4th-order low cut at 400 Hz all but removes 50 Hz and keeps 2 kHz.
    CHECK(peakOf(through("eq", {{"lowCut", 400.0f}}, sine(50, 0.5, 0.5)), 12000) < 0.01f);
    CHECK(peakOf(through("eq", {{"lowCut", 400.0f}}, sine(2000, 0.5, 0.5)), 12000) == Approx(0.5).epsilon(0.02));
    CHECK(peakOf(through("eq", {{"highCut", 1000.0f}}, sine(8000, 0.5, 0.5)), 12000) < 0.01f);
}

TEST_CASE("dynamics: compressor, limiter and gate", "[audio-dsp]")
{
    // 0 dBFS through a 4:1 compressor at -20 dB: 15 dB of reduction.
    const auto squashed = through("compressor", {{"threshold", -20.0f}, {"ratio", 4.0f}, {"knee", 0.0f}, {"attack", 1.0f}},
                                  sine(200, 1.0, 1.0));
    CHECK(20.0 * std::log10(peakOf(squashed, 24000)) == Approx(-15.0).margin(1.0));
    // The limiter's ceiling holds even for a sudden peak.
    std::vector<float> spike = sine(200, 0.5, 0.2);
    for (std::size_t i = 12000; i < 12200; ++i)
        spike[i * 2] = spike[i * 2 + 1] = 1.5f;
    CHECK(peakOf(through("limiter", {{"ceiling", -3.0f}}, spike)) <= dsp::dbToGain(-3.0) + 1e-4f);
    // The gate silences quiet sound and lets loud sound through.
    CHECK(peakOf(through("gate", {{"threshold", -40.0f}}, sine(300, 1.0, 0.003)), 24000) < 0.0001f);
    CHECK(peakOf(through("gate", {{"threshold", -40.0f}}, sine(300, 1.0, 0.3)), 24000) == Approx(0.3).epsilon(0.02));
}

TEST_CASE("reverb and echo ring on after the sound stops", "[audio-dsp]")
{
    std::vector<float> click(std::size_t(kRate) * 2 * 2, 0.0f);
    click[0] = click[1] = 1.0f;
    const dsp::ProcessorInfo& reverb = *dsp::findProcessor("reverb");
    auto r = reverb.make();
    r->configure(reverb.complete({{"size", 0.8f}}));
    CHECK(r->tail() > 1.0);
    std::vector<float> wet = click;
    r->process(wet.data(), int(wet.size() / 2));
    CHECK(peakOf(wet, kRate / 2, kRate) > 0.001f); // still sounding half a second later
    // An echo at 250 ms, then another at 500 ms, quieter.
    const auto echoed = through("delay", {{"time", 250.0f}, {"feedback", 0.5f}, {"level", 1.0f}}, click);
    CHECK(peakOf(echoed, 1, 11000) < 0.001f);
    const float first = peakOf(echoed, 11990, 12010);
    const float second = peakOf(echoed, 23990, 24010);
    CHECK(first > 0.5f);
    CHECK(second > 0.1f);
    CHECK(second < first);
}

TEST_CASE("pitch shifting keeps the timing and moves the frequency", "[audio-dsp]")
{
    // An octave up, the energy moves from 440 Hz to 880 Hz; down, to 220 Hz.
    const auto octave = through("pitch", {{"semitones", 12.0f}}, sine(440, 1.0, 0.5));
    CHECK(powerAt(octave, 880, 4800, 48000) > 20.0 * powerAt(octave, 440, 4800, 48000));
    const auto lower = through("pitch", {{"semitones", -12.0f}}, sine(440, 1.0, 0.5));
    CHECK(powerAt(lower, 220, 4800, 48000) > 20.0 * powerAt(lower, 440, 4800, 48000));
    CHECK(rmsOf(octave, 4800, 48000) == Approx(0.5 / std::numbers::sqrt2).epsilon(0.15));
    // No shift is no change at all.
    const auto same = sine(440, 0.2, 0.5);
    CHECK(through("pitch", {{"semitones", 0.0f}}, same) == same);
}

TEST_CASE("a mix param blends an effect with the dry sound", "[audio-dsp]")
{
    const auto dry = sine(500, 0.5, 0.4);
    const auto full = throughEffect("tve.audio.telephone", dry);
    const auto none = throughEffect("tve.audio.telephone", dry, {{"mix", {0.0f}}});
    CHECK(none == dry);
    const auto half = throughEffect("tve.audio.telephone", dry, {{"mix", {0.5f}}});
    for (std::size_t i = 4000; i < 4100; ++i)
        CHECK(half[i] == Approx((dry[i] + full[i]) * 0.5f).margin(1e-5));
}

TEST_CASE("loudness meter reads the EBU reference tone", "[audio-dsp]")
{
    // A 1 kHz sine peaking at -23 dBFS on both channels is -23 LUFS (EBU Tech 3341).
    dsp::LoudnessMeter meter;
    const auto tone = sine(1000, 20.0, std::pow(10.0, -23.0 / 20.0));
    meter.process(tone.data(), int(tone.size() / 2));
    CHECK(meter.integrated() == Approx(-23.0).margin(0.1));
    CHECK(meter.momentary() == Approx(-23.0).margin(0.1));
    CHECK(meter.shortTerm() == Approx(-23.0).margin(0.1));
    CHECK(meter.peak() == Approx(std::pow(10.0, -23.0 / 20.0)).epsilon(0.01));
    // Silence is ignored by the gate: 10 s of tone then 10 s of silence still reads -23.
    const std::vector<float> silence(tone.size() / 2, 0.0f);
    meter.process(silence.data(), int(silence.size() / 2));
    CHECK(meter.integrated() == Approx(-23.0).margin(0.1));
    CHECK(std::isinf(meter.momentary()));
    // 6 dB louder is 6 LU more.
    dsp::LoudnessMeter louder;
    const auto loud = sine(1000, 5.0, std::pow(10.0, -17.0 / 20.0));
    louder.process(loud.data(), int(loud.size() / 2));
    CHECK(louder.integrated() == Approx(-17.0).margin(0.1));
    dsp::LoudnessMeter empty;
    CHECK(std::isinf(empty.integrated()));
}
