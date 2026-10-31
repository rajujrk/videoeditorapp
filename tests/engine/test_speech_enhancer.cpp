#include "audio/dsp/Denoise.h"
#include "decode/AudioDecoder.h"
#include "encode/WavWriter.h"
#include "speech/SpeechEnhancer.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <numbers>
#include <numeric>
#include <random>
#include <sstream>

#include <unistd.h>

using namespace tve;
namespace fs = std::filesystem;

namespace {

std::string sample(const char* name)
{
    return (fs::path(TVE_SAMPLES_DIR) / name).string();
}

std::string scratch(const std::string& name)
{
    return (fs::temp_directory_path() / ("tve_enhance_" + std::to_string(::getpid()) + "_" + name)).string();
}

// The left channel of a file, in the engine format.
std::vector<float> readLeft(const std::string& path)
{
    auto decoder = AudioDecoder::open(path);
    REQUIRE(decoder);
    std::vector<float> left;
    std::vector<float> chunk(4800 * 2);
    for (int n; (n = decoder->read(chunk.data(), 4800)) > 0;) {
        for (int i = 0; i < n; ++i)
            left.push_back(chunk[std::size_t(i) * 2]);
    }
    return left;
}

void writeMono(const std::string& path, const std::vector<float>& samples)
{
    auto writer = WavWriter::open(path, audio::kSampleRate, 1);
    REQUIRE(writer);
    REQUIRE(writer->write(samples.data(), std::int64_t(samples.size())));
    REQUIRE(writer->finish());
}

std::vector<float> whiteNoise(std::size_t n, float rms, unsigned seed = 7)
{
    std::mt19937 random(seed);
    std::normal_distribution<float> gauss(0.0f, rms);
    std::vector<float> noise(n);
    for (float& v : noise)
        v = gauss(random);
    return noise;
}

// Level of x[from, to) in dB (RMS).
double levelDb(const std::vector<float>& x, double from, double to, std::size_t offset = 0)
{
    const auto a = std::size_t(from * audio::kSampleRate) + offset;
    const auto b = std::min(x.size(), std::size_t(to * audio::kSampleRate) + offset);
    double sum = 0.0;
    for (std::size_t i = a; i < b; ++i)
        sum += double(x[i]) * x[i];
    return 10.0 * std::log10(sum / double(b - a) + 1e-20);
}

// Talking (speech_en.wav: words, a pause 2.62–4.33 s, words) with steady noise 5 dB under
// the speech's level, so both the pause and the words have something to clean.
struct NoisySpeech {
    std::vector<float> clean;
    std::vector<float> noisy;
};

NoisySpeech noisySpeech()
{
    if (!fs::exists(sample("speech_en.wav")))
        SKIP("speech_en.wav missing (run tools/make_test_media.sh)");
    NoisySpeech s;
    s.clean = readLeft(sample("speech_en.wav"));
    const double speechDb = levelDb(s.clean, 0.3, 2.4);
    const auto noise = whiteNoise(s.clean.size(), float(std::pow(10.0, (speechDb - 5.0) / 20.0)));
    s.noisy.resize(s.clean.size());
    for (std::size_t i = 0; i < s.clean.size(); ++i)
        s.noisy[i] = s.clean[i] + noise[i];
    return s;
}

std::string modelDir()
{
    const std::string dir = sample("models/deepfilternet3");
    if (!fs::exists(fs::path(dir) / "enc.onnx"))
        SKIP("DeepFilterNet3 missing (run tools/fetch_models.sh)");
    return dir;
}

} // namespace

TEST_CASE("DeepFilterNet's settings, ERB bands and normalisation rate")
{
    const std::string dir = modelDir();
    std::ifstream ini(fs::path(dir) / "config.ini");
    std::stringstream text;
    text << ini.rdbuf();
    const auto config = DeepFilterConfig::parse(text.str());
    REQUIRE(config);
    CHECK(config->fftSize == 960);
    CHECK(config->hopSize == 480);
    CHECK(config->nbErb == 32);
    CHECK(config->nbDf == 96);
    CHECK(config->dfOrder == 5);
    CHECK(config->dfLookahead == 2);
    CHECK(config->convLookahead == 2);
    CHECK(config->normAlpha() == Catch::Approx(0.99f));

    // 32 bands covering the 481 bins, at least two bins wide, widening with frequency.
    const std::vector<int> widths = config->erbWidths();
    REQUIRE(widths.size() == 32);
    CHECK(std::accumulate(widths.begin(), widths.end(), 0) == 481);
    for (std::size_t i = 0; i < widths.size(); ++i) {
        CHECK(widths[i] >= 2);
        if (i > 0 && i + 1 < widths.size())
            CHECK(widths[i] >= widths[i - 1]);
    }
    CHECK(widths.front() == 2);
    CHECK(widths.back() > 40);

    // What the app cannot run is refused.
    CHECK_FALSE(DeepFilterConfig::parse("[df]\nsr = 16000\n"));
    CHECK_FALSE(DeepFilterConfig::parse("[df]\nfft_size = banana\n"));
}

TEST_CASE("DeepFilterNet's spectrum gives the sound back, one hop late")
{
    const DeepFilterConfig config;
    DeepFilterStft analysis(config), synthesis(config);
    const auto x = whiteNoise(48000, 0.3f);
    std::vector<std::complex<float>> spectrum(std::size_t(analysis.bins()));
    std::vector<float> y(x.size());
    for (std::size_t at = 0; at + 480 <= x.size(); at += 480) {
        analysis.analysis(x.data() + at, spectrum.data());
        synthesis.synthesis(spectrum.data(), y.data() + at);
    }
    double worst = 0.0;
    for (std::size_t i = 0; i + 480 < x.size(); ++i)
        worst = std::max(worst, double(std::abs(y[i + 480] - x[i])));
    CHECK(worst < 1e-5);

    // The scale matches DeepFilterNet's: the windowed DFT times 2·hop / N².
    DeepFilterStft fresh(config);
    std::vector<float> sine(960);
    for (int i = 0; i < 960; ++i)
        sine[std::size_t(i)] = float(std::sin(2.0 * std::numbers::pi * 1000.0 * i / 48000.0) + 0.3 * std::cos(0.01 * i * i));
    fresh.analysis(sine.data(), spectrum.data());
    fresh.analysis(sine.data() + 480, spectrum.data());
    for (const int k : {0, 3, 20, 200, 480}) {
        std::complex<double> sum = 0.0;
        for (int n = 0; n < 960; ++n) {
            const double s = std::sin(0.5 * std::numbers::pi * (n + 0.5) / 480.0);
            const double w = std::sin(0.5 * std::numbers::pi * s * s);
            sum += sine[std::size_t(n)] * w * std::polar(1.0, -2.0 * std::numbers::pi * k * n / 960.0);
        }
        sum *= 960.0 / (960.0 * 960.0);
        CHECK(spectrum[std::size_t(k)].real() == Catch::Approx(sum.real()).margin(1e-4));
        CHECK(spectrum[std::size_t(k)].imag() == Catch::Approx(sum.imag()).margin(1e-4));
    }
}

TEST_CASE("live noise reduction (RNNoise) is 30 ms late and quietens noise")
{
    auto denoiser = dsp::makeDenoiser();
    const dsp::ProcessorInfo* info = dsp::findProcessor("denoise");
    REQUIRE(info);

    SECTION("amount 0 is the input, exactly 1440 samples later")
    {
        denoiser->configure(info->complete({{"amount", 0.0f}}));
        const auto noise = whiteNoise(9600, 0.1f);
        std::vector<float> stereo(noise.size() * 2);
        for (std::size_t i = 0; i < noise.size(); ++i)
            stereo[i * 2] = stereo[i * 2 + 1] = noise[i];
        denoiser->process(stereo.data(), int(noise.size()));
        const auto latency = std::size_t(dsp::kDenoiseLatency);
        for (std::size_t i = 0; i < latency; ++i)
            REQUIRE(stereo[i * 2] == 0.0f);
        for (std::size_t i = latency; i < noise.size(); ++i)
            REQUIRE(stereo[i * 2] == Catch::Approx(noise[i - latency]).margin(1e-6));
    }

    SECTION("noisy speech: the pause much quieter, the words kept")
    {
        const NoisySpeech s = noisySpeech();
        denoiser->configure(info->complete({}));
        std::vector<float> stereo(s.noisy.size() * 2);
        for (std::size_t i = 0; i < s.noisy.size(); ++i)
            stereo[i * 2] = stereo[i * 2 + 1] = s.noisy[i];
        // In odd-sized blocks, as playback hands them over.
        for (std::size_t at = 0; at < s.noisy.size(); at += 1021)
            denoiser->process(stereo.data() + at * 2, int(std::min<std::size_t>(1021, s.noisy.size() - at)));
        std::vector<float> out(s.noisy.size());
        for (std::size_t i = 0; i < out.size(); ++i)
            out[i] = stereo[i * 2];
        const double pauseBefore = levelDb(s.noisy, 2.9, 4.1);
        const double pauseAfter = levelDb(out, 2.9, 4.1, dsp::kDenoiseLatency);
        CHECK(pauseBefore - pauseAfter > 12.0);
        // The words lose little of their level.
        CHECK(levelDb(out, 0.3, 2.4, dsp::kDenoiseLatency) > levelDb(s.clean, 0.3, 2.4) - 4.0);

        // "quiet" lowers what is left between words further.
        auto quieter = dsp::makeDenoiser();
        quieter->configure(info->complete({{"quiet", -20.0f}}));
        for (std::size_t i = 0; i < s.noisy.size(); ++i)
            stereo[i * 2] = stereo[i * 2 + 1] = s.noisy[i];
        quieter->process(stereo.data(), int(s.noisy.size()));
        for (std::size_t i = 0; i < out.size(); ++i)
            out[i] = stereo[i * 2];
        CHECK(levelDb(out, 2.9, 4.1, dsp::kDenoiseLatency) < pauseAfter - 6.0);

        // reset() starts over: silence in, silence out.
        quieter->reset();
        std::vector<float> silence(4800 * 2, 0.0f);
        quieter->process(silence.data(), 4800);
        CHECK(std::ranges::all_of(silence, [](float v) { return v == 0.0f; }));
    }
}

TEST_CASE("speech enhancement (DeepFilterNet3) cleans noisy speech in place")
{
    const std::string dir = modelDir();
    const NoisySpeech s = noisySpeech();
    std::string error;
    auto enhancer = SpeechEnhancer::load(dir, &error);
    REQUIRE(enhancer);

    const auto started = std::chrono::steady_clock::now();
    const auto out = enhancer->enhance(s.noisy, {}, &error);
    const double took = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    REQUIRE(out);
    REQUIRE(out->size() == s.noisy.size());
    // Faster than real time by far (7 s of sound).
    CHECK(took < 3.0);

    // The pause: the noise nearly gone. The words: closer to the clean speech than the
    // noisy input was (by its error), with the same timing (no lag).
    const double pauseBefore = levelDb(s.noisy, 2.9, 4.1);
    const double pauseAfter = levelDb(*out, 2.9, 4.1);
    CHECK(pauseBefore - pauseAfter > 25.0);
    const auto errorDb = [&](const std::vector<float>& y, int lag) {
        std::vector<float> e(s.clean.size(), 0.0f);
        for (std::size_t i = 0; i < e.size(); ++i) {
            const auto j = std::ptrdiff_t(i) + lag;
            e[i] = (j >= 0 && j < std::ptrdiff_t(y.size()) ? y[std::size_t(j)] : 0.0f) - s.clean[i];
        }
        return levelDb(e, 0.3, 2.4);
    };
    CHECK(errorDb(s.noisy, 0) - errorDb(*out, 0) > 6.0);
    CHECK(errorDb(*out, 0) < errorDb(*out, 480) - 3.0);
    CHECK(errorDb(*out, 0) < errorDb(*out, -480) - 3.0);

    // Cancelling stops it.
    CHECK_FALSE(enhancer->enhance(s.noisy, [](double) { return false; }, &error));
}

TEST_CASE("enhancing a file keeps its length and mixes back to a strength")
{
    const std::string dir = modelDir();
    const NoisySpeech s = noisySpeech();
    const std::string noisyPath = scratch("noisy.wav");
    const std::string fullPath = scratch("full.wav");
    const std::string limitedPath = scratch("limited.wav");
    writeMono(noisyPath, s.noisy);

    auto enhancer = SpeechEnhancer::load(dir);
    REQUIRE(enhancer);
    std::vector<double> reported;
    std::string error;
    REQUIRE(enhancer->enhanceFile(noisyPath, fullPath, [&](double p) { reported.push_back(p); return true; }, &error));
    CHECK(std::ranges::is_sorted(reported));
    CHECK(reported.back() > 0.9);
    auto decoder = AudioDecoder::open(fullPath);
    REQUIRE(decoder);
    CHECK(decoder->info().sourceChannels == 1); // the same on both sides: cleaned once
    const std::vector<float> full = readLeft(fullPath);
    REQUIRE(full.size() == s.noisy.size());

    // Lowered by at most 12 dB: a quarter of the original under the cleaned sound.
    REQUIRE(limitEnhancement(noisyPath, fullPath, 12.0, limitedPath, {}, &error));
    const std::vector<float> limited = readLeft(limitedPath);
    REQUIRE(limited.size() == full.size());
    const float keep = float(std::pow(10.0, -12.0 / 20.0));
    for (std::size_t i = 0; i < limited.size(); i += 997)
        REQUIRE(limited[i] == Catch::Approx(full[i] * (1.0f - keep) + s.noisy[i] * keep).margin(1e-5));
    CHECK(levelDb(limited, 2.9, 4.1) == Catch::Approx(levelDb(s.noisy, 2.9, 4.1) - 12.0).margin(1.5));

    // What cannot be cleaned says why.
    CHECK_FALSE(enhancer->enhanceFile(scratch("missing.wav"), scratch("never.wav"), {}, &error));
    CHECK_FALSE(error.empty());
    CHECK_FALSE(SpeechEnhancer::load(scratch("no-model"), &error));
    CHECK(error.find("not there") != std::string::npos);
    for (const auto& p : {noisyPath, fullPath, limitedPath})
        fs::remove(p);
}
