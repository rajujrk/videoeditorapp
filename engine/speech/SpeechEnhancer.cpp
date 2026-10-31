#include "speech/SpeechEnhancer.h"

#include "decode/AudioDecoder.h"
#include "encode/WavWriter.h"

#include <Accelerate/Accelerate.h>
#include <onnxruntime/onnxruntime_cxx_api.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <map>
#include <numbers>
#include <sstream>

// The pipeline around the network follows DeepFilterNet's own (libDF and df/deepfilternet3.py,
// MIT / Apache-2.0): the same window and scaling, ERB bands, feature normalisation, a
// two-frame look-ahead for the features, the ERB mask on all bins and the deep filter
// (five frames, two of them ahead) on the lowest nbDf.

namespace tve {

namespace {

using Complex = std::complex<float>;

double freqToErb(double hz)
{
    return 9.265 * std::log1p(hz / (24.7 * 9.265));
}

double erbToFreq(double erb)
{
    return 24.7 * 9.265 * (std::exp(erb / 9.265) - 1.0);
}

std::string trimmed(std::string s)
{
    const auto notSpace = [](unsigned char c) { return !std::isspace(c); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), notSpace));
    s.erase(std::find_if(s.rbegin(), s.rend(), notSpace).base(), s.end());
    return s;
}

void fail(std::string* error, std::string message)
{
    if (error)
        *error = std::move(message);
}

// ONNX Runtime's environment: one per process.
Ort::Env& ortEnv()
{
    static Ort::Env env(ORT_LOGGING_LEVEL_ERROR, "tve"); // (it warns of an "unknown CPU vendor" on Apple chips)
    return env;
}

// A tensor that reads `data` in place (which must outlive it).
Ort::Value view(float* data, const std::vector<std::int64_t>& shape)
{
    static const Ort::MemoryInfo cpu = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    std::size_t count = 1;
    for (const std::int64_t d : shape)
        count *= std::size_t(d);
    return Ort::Value::CreateTensor<float>(cpu, data, count, shape.data(), shape.size());
}

Ort::Value view(Ort::Value& value)
{
    return view(value.GetTensorMutableData<float>(), value.GetTensorTypeAndShapeInfo().GetShape());
}

} // namespace

// --- config ---

std::optional<DeepFilterConfig> DeepFilterConfig::parse(const std::string& ini, std::string* error)
{
    std::map<std::string, std::string> values;
    std::istringstream lines(ini);
    for (std::string line; std::getline(lines, line);) {
        line = trimmed(line);
        const auto eq = line.find('=');
        if (line.empty() || line[0] == '[' || line[0] == '#' || line[0] == ';' || eq == std::string::npos)
            continue;
        values[trimmed(line.substr(0, eq))] = trimmed(line.substr(eq + 1));
    }
    DeepFilterConfig c;
    try {
        const auto read = [&](const char* key, auto& into) {
            const auto it = values.find(key);
            if (it == values.end())
                return;
            if constexpr (std::is_same_v<std::decay_t<decltype(into)>, double>)
                into = std::stod(it->second);
            else
                into = std::stoi(it->second);
        };
        read("sr", c.sampleRate);
        read("fft_size", c.fftSize);
        read("hop_size", c.hopSize);
        read("nb_erb", c.nbErb);
        read("nb_df", c.nbDf);
        read("min_nb_erb_freqs", c.minNbErbFreqs);
        read("df_order", c.dfOrder);
        read("df_lookahead", c.dfLookahead);
        read("conv_lookahead", c.convLookahead);
        read("norm_tau", c.normTau);
    } catch (const std::exception&) {
        fail(error, "The model's settings could not be read.");
        return std::nullopt;
    }
    const bool fits = c.sampleRate == audio::kSampleRate && c.hopSize > 0 && c.fftSize >= 2 * c.hopSize
                   && c.fftSize % 2 == 0 && c.nbErb > 1 && c.nbDf > 1 && c.nbDf <= c.fftSize / 2 + 1
                   && c.dfOrder > 0 && c.dfLookahead >= 0 && c.dfLookahead < c.dfOrder && c.convLookahead >= 0
                   && c.normTau > 0.0;
    if (!fits) {
        fail(error, "This model's settings are not ones the app can run (it needs 48 kHz).");
        return std::nullopt;
    }
    return c;
}

float DeepFilterConfig::normAlpha() const
{
    // exp(-hop / sr / tau), rounded to the fewest decimals (from 3) that keep it below 1.
    const double a = std::exp(-double(hopSize) / sampleRate / normTau);
    for (int digits = 3;; ++digits) {
        const double scale = std::pow(10.0, digits);
        const double rounded = std::round(a * scale) / scale;
        if (rounded < 1.0)
            return float(rounded);
    }
}

std::vector<int> DeepFilterConfig::erbWidths() const
{
    const float freqWidth = float(sampleRate) / float(fftSize);
    const float erbLow = float(freqToErb(0.0));
    const float erbHigh = float(freqToErb(double(sampleRate / 2)));
    const float step = (erbHigh - erbLow) / float(nbErb);
    std::vector<int> widths(std::size_t(nbErb), 0);
    int previous = 0;
    int over = 0;
    for (int i = 1; i <= nbErb; ++i) {
        const float f = float(erbToFreq(double(erbLow + float(i) * step)));
        const int bin = int(std::round(f / freqWidth));
        int count = bin - previous - over;
        if (count < minNbErbFreqs) {
            over = minNbErbFreqs - count; // borrowed from the bands above
            count = minNbErbFreqs;
        } else {
            over = 0;
        }
        widths[std::size_t(i - 1)] = count;
        previous = bin;
    }
    widths.back() += 1; // the Nyquist bin
    int total = 0;
    for (const int w : widths)
        total += w;
    const int tooMany = total - (fftSize / 2 + 1);
    if (tooMany > 0)
        widths.back() -= tooMany;
    return widths;
}

// --- STFT ---

struct DeepFilterStft::Fft {
    vDSP_DFT_Setup forward = nullptr;
    vDSP_DFT_Setup inverse = nullptr;
    std::vector<float> inReal, inImag, outReal, outImag;

    explicit Fft(int n)
        : inReal(std::size_t(n / 2)), inImag(std::size_t(n / 2)), outReal(std::size_t(n / 2)), outImag(std::size_t(n / 2))
    {
        forward = vDSP_DFT_zrop_CreateSetup(nullptr, vDSP_Length(n), vDSP_DFT_FORWARD);
        inverse = vDSP_DFT_zrop_CreateSetup(forward, vDSP_Length(n), vDSP_DFT_INVERSE);
    }
    ~Fft()
    {
        if (inverse)
            vDSP_DFT_DestroySetup(inverse);
        if (forward)
            vDSP_DFT_DestroySetup(forward);
    }
};

DeepFilterStft::DeepFilterStft(const DeepFilterConfig& config)
    : m_fft(config.fftSize), m_hop(config.hopSize), m_window(std::size_t(config.fftSize)),
      m_analysisMemory(std::size_t(config.fftSize - config.hopSize)),
      m_synthesisMemory(std::size_t(config.fftSize - config.hopSize)), m_buffer(std::size_t(config.fftSize)),
      m_transform(std::make_unique<Fft>(config.fftSize))
{
    // Vorbis: sin(π/2 · sin²(π (n + ½) / N)), power-complementary at half overlap.
    const double half = m_fft / 2;
    for (int i = 0; i < m_fft; ++i) {
        const double s = std::sin(0.5 * std::numbers::pi * (i + 0.5) / half);
        m_window[std::size_t(i)] = float(std::sin(0.5 * std::numbers::pi * s * s));
    }
}

DeepFilterStft::~DeepFilterStft() = default;

void DeepFilterStft::analysis(const float* input, Complex* spectrum)
{
    const int keep = m_fft - m_hop;
    for (int i = 0; i < keep; ++i)
        m_buffer[std::size_t(i)] = m_analysisMemory[std::size_t(i)] * m_window[std::size_t(i)];
    for (int i = 0; i < m_hop; ++i)
        m_buffer[std::size_t(keep + i)] = input[i] * m_window[std::size_t(keep + i)];
    const int shift = keep - m_hop;
    if (shift > 0)
        std::rotate(m_analysisMemory.begin(), m_analysisMemory.begin() + m_hop, m_analysisMemory.end());
    std::copy(input, input + m_hop, m_analysisMemory.begin() + shift);

    Fft& t = *m_transform;
    for (int k = 0; k < m_fft / 2; ++k) {
        t.inReal[std::size_t(k)] = m_buffer[std::size_t(2 * k)];
        t.inImag[std::size_t(k)] = m_buffer[std::size_t(2 * k + 1)];
    }
    vDSP_DFT_Execute(t.forward, t.inReal.data(), t.inImag.data(), t.outReal.data(), t.outImag.data());
    // vDSP gives twice the DFT; DeepFilterNet scales it by 2·hop / N².
    const float scale = 0.5f * float(2 * m_hop) / float(m_fft * m_fft);
    spectrum[0] = {t.outReal[0] * scale, 0.0f};
    spectrum[m_fft / 2] = {t.outImag[0] * scale, 0.0f};
    for (int k = 1; k < m_fft / 2; ++k)
        spectrum[k] = {t.outReal[std::size_t(k)] * scale, t.outImag[std::size_t(k)] * scale};
}

void DeepFilterStft::synthesis(const Complex* spectrum, float* output)
{
    Fft& t = *m_transform;
    t.inReal[0] = spectrum[0].real();
    t.inImag[0] = spectrum[m_fft / 2].real();
    for (int k = 1; k < m_fft / 2; ++k) {
        t.inReal[std::size_t(k)] = spectrum[k].real();
        t.inImag[std::size_t(k)] = spectrum[k].imag();
    }
    // Unnormalised (N times the signal), which the analysis scaling undoes.
    vDSP_DFT_Execute(t.inverse, t.inReal.data(), t.inImag.data(), t.outReal.data(), t.outImag.data());
    for (int k = 0; k < m_fft / 2; ++k) {
        m_buffer[std::size_t(2 * k)] = t.outReal[std::size_t(k)] * m_window[std::size_t(2 * k)];
        m_buffer[std::size_t(2 * k + 1)] = t.outImag[std::size_t(k)] * m_window[std::size_t(2 * k + 1)];
    }
    for (int i = 0; i < m_hop; ++i)
        output[i] = m_buffer[std::size_t(i)] + m_synthesisMemory[std::size_t(i)];
    const int overlap = m_fft - 2 * m_hop;
    if (overlap > 0)
        std::rotate(m_synthesisMemory.begin(), m_synthesisMemory.begin() + m_hop, m_synthesisMemory.end());
    for (int i = 0; i < overlap; ++i)
        m_synthesisMemory[std::size_t(i)] += m_buffer[std::size_t(m_hop + i)];
    for (int i = 0; i < m_hop; ++i)
        m_synthesisMemory[std::size_t(overlap + i)] = m_buffer[std::size_t(m_hop + overlap + i)];
}

// --- the enhancer ---

struct SpeechEnhancer::Models {
    Ort::Session encoder{nullptr};
    Ort::Session erbDecoder{nullptr};
    Ort::Session dfDecoder{nullptr};
};

SpeechEnhancer::SpeechEnhancer(DeepFilterConfig config, std::unique_ptr<Models> models)
    : m_config(config), m_models(std::move(models))
{
}

SpeechEnhancer::~SpeechEnhancer() = default;

std::unique_ptr<SpeechEnhancer> SpeechEnhancer::load(const std::string& directory, std::string* error)
{
    namespace fs = std::filesystem;
    const fs::path dir(directory);
    std::ifstream ini(dir / "config.ini");
    if (!ini) {
        fail(error, "The speech enhancement model is not there (" + directory + ").");
        return nullptr;
    }
    std::stringstream text;
    text << ini.rdbuf();
    const std::optional<DeepFilterConfig> config = DeepFilterConfig::parse(text.str(), error);
    if (!config)
        return nullptr;
    auto models = std::make_unique<Models>();
    try {
        Ort::SessionOptions options;
        options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        options.SetIntraOpNumThreads(0);
        models->encoder = Ort::Session(ortEnv(), (dir / "enc.onnx").c_str(), options);
        models->erbDecoder = Ort::Session(ortEnv(), (dir / "erb_dec.onnx").c_str(), options);
        models->dfDecoder = Ort::Session(ortEnv(), (dir / "df_dec.onnx").c_str(), options);
    } catch (const Ort::Exception& e) {
        fail(error, std::string("The speech enhancement model could not be loaded: ") + e.what());
        return nullptr;
    }
    return std::unique_ptr<SpeechEnhancer>(new SpeechEnhancer(*config, std::move(models)));
}

std::optional<std::vector<float>> SpeechEnhancer::enhance(const std::vector<float>& samples,
                                                           const AnalysisProgress& progress, std::string* error)
{
    const DeepFilterConfig& c = m_config;
    const int hop = c.hopSize;
    const int bins = c.fftSize / 2 + 1;
    const std::size_t length = samples.size();
    // Room for the STFT's delay (fftSize - hopSize) at the end, in whole hops.
    const std::size_t frames = (length + std::size_t(c.fftSize) + std::size_t(hop) - 1) / std::size_t(hop);
    std::vector<float> input(frames * std::size_t(hop), 0.0f);
    std::copy(samples.begin(), samples.end(), input.begin());

    // The noisy spectrum and the two features: ERB band levels (dB, mean-normalised) and
    // the lowest nbDf bins as they are (unit-normalised).
    const std::vector<int> widths = c.erbWidths();
    const float alpha = c.normAlpha();
    std::vector<Complex> spectrum(frames * std::size_t(bins));
    std::vector<float> featErb(frames * std::size_t(c.nbErb));
    std::vector<Complex> featSpec(frames * std::size_t(c.nbDf));
    {
        DeepFilterStft stft(c);
        std::vector<float> meanState(std::size_t(c.nbErb));
        for (int b = 0; b < c.nbErb; ++b)
            meanState[std::size_t(b)] = -60.0f + float(b) * (-30.0f / float(c.nbErb - 1));
        std::vector<float> unitState(std::size_t(c.nbDf));
        for (int f = 0; f < c.nbDf; ++f)
            unitState[std::size_t(f)] = 0.001f + float(f) * (-0.0009f / float(c.nbDf - 1));
        for (std::size_t t = 0; t < frames; ++t) {
            Complex* x = spectrum.data() + t * std::size_t(bins);
            stft.analysis(input.data() + t * std::size_t(hop), x);
            int bin = 0;
            for (int b = 0; b < c.nbErb; ++b) {
                float power = 0.0f;
                for (int i = 0; i < widths[std::size_t(b)]; ++i, ++bin)
                    power += std::norm(x[bin]);
                const float db = 10.0f * std::log10(power / float(widths[std::size_t(b)]) + 1e-10f);
                float& state = meanState[std::size_t(b)];
                state = db * (1.0f - alpha) + state * alpha;
                featErb[t * std::size_t(c.nbErb) + std::size_t(b)] = (db - state) / 40.0f;
            }
            for (int f = 0; f < c.nbDf; ++f) {
                float& state = unitState[std::size_t(f)];
                state = std::abs(x[f]) * (1.0f - alpha) + state * alpha;
                featSpec[t * std::size_t(c.nbDf) + std::size_t(f)] = x[f] / std::sqrt(state);
            }
            if (progress && t % 2000 == 0 && !progress(0.1 * double(t) / double(frames)))
                return std::nullopt;
        }
    }

    // The network, over spans of frames. Position p is fed the features of frame p +
    // convLookahead (zeros past the end), so its outputs belong to frame p. A span starts a
    // few seconds early so the recurrent layers have settled; those outputs are dropped.
    constexpr std::size_t kSpan = 1500;
    constexpr std::size_t kWarmUp = 300;
    const std::size_t taps = std::size_t(c.dfOrder) * 2;
    std::vector<float> mask(frames * std::size_t(c.nbErb));
    std::vector<float> coefs(frames * std::size_t(c.nbDf) * taps);
    const std::size_t lookahead = std::size_t(c.convLookahead);
    try {
        for (std::size_t from = 0; from < frames; from += kSpan) {
            const std::size_t to = std::min(frames, from + kSpan);
            const std::size_t first = from > kWarmUp ? from - kWarmUp : 0;
            const auto n = std::int64_t(to - first);
            std::vector<float> erbIn(std::size_t(n) * std::size_t(c.nbErb), 0.0f);
            std::vector<float> specIn(2 * std::size_t(n) * std::size_t(c.nbDf), 0.0f);
            for (std::int64_t p = 0; p < n; ++p) {
                const std::size_t t = first + std::size_t(p) + lookahead;
                if (t >= frames)
                    break;
                std::copy_n(featErb.begin() + std::ptrdiff_t(t * std::size_t(c.nbErb)), c.nbErb,
                            erbIn.begin() + std::ptrdiff_t(std::size_t(p) * std::size_t(c.nbErb)));
                for (int f = 0; f < c.nbDf; ++f) {
                    const Complex v = featSpec[t * std::size_t(c.nbDf) + std::size_t(f)];
                    specIn[std::size_t(p) * std::size_t(c.nbDf) + std::size_t(f)] = v.real();
                    specIn[(std::size_t(n) + std::size_t(p)) * std::size_t(c.nbDf) + std::size_t(f)] = v.imag();
                }
            }
            std::array<Ort::Value, 2> encIn{view(erbIn.data(), {1, 1, n, c.nbErb}),
                                            view(specIn.data(), {1, 2, n, c.nbDf})};
            static const std::array<const char*, 2> encInNames{"feat_erb", "feat_spec"};
            static const std::array<const char*, 7> encOutNames{"e0", "e1", "e2", "e3", "emb", "c0", "lsnr"};
            std::vector<Ort::Value> enc = m_models->encoder.Run(Ort::RunOptions{nullptr}, encInNames.data(), encIn.data(),
                                                                encIn.size(), encOutNames.data(), encOutNames.size());

            std::array<Ort::Value, 5> erbDecIn{view(enc[4]), view(enc[3]), view(enc[2]), view(enc[1]), view(enc[0])};
            static const std::array<const char*, 5> erbDecInNames{"emb", "e3", "e2", "e1", "e0"};
            static const std::array<const char*, 1> erbDecOutNames{"m"};
            std::vector<Ort::Value> m = m_models->erbDecoder.Run(Ort::RunOptions{nullptr}, erbDecInNames.data(),
                                                                 erbDecIn.data(), erbDecIn.size(),
                                                                 erbDecOutNames.data(), erbDecOutNames.size());

            std::array<Ort::Value, 2> dfDecIn{view(enc[4]), view(enc[5])};
            static const std::array<const char*, 2> dfDecInNames{"emb", "c0"};
            static const std::array<const char*, 1> dfDecOutNames{"coefs"};
            std::vector<Ort::Value> df = m_models->dfDecoder.Run(Ort::RunOptions{nullptr}, dfDecInNames.data(),
                                                                 dfDecIn.data(), dfDecIn.size(), dfDecOutNames.data(),
                                                                 dfDecOutNames.size());

            const float* gains = m[0].GetTensorData<float>(); // [1, 1, n, nbErb]
            const float* taps0 = df[0].GetTensorData<float>(); // [1, n, nbDf, dfOrder · 2]
            const std::size_t skip = from - first;
            std::copy_n(gains + skip * std::size_t(c.nbErb), (to - from) * std::size_t(c.nbErb),
                        mask.begin() + std::ptrdiff_t(from * std::size_t(c.nbErb)));
            std::copy_n(taps0 + skip * std::size_t(c.nbDf) * taps, (to - from) * std::size_t(c.nbDf) * taps,
                        coefs.begin() + std::ptrdiff_t(from * std::size_t(c.nbDf) * taps));
            if (progress && !progress(0.1 + 0.8 * double(to) / double(frames)))
                return std::nullopt;
        }
    } catch (const Ort::Exception& e) {
        fail(error, std::string("Speech enhancement failed: ") + e.what());
        return std::nullopt;
    }

    // Each frame: the ERB gains on every bin, then the lowest nbDf bins replaced by the deep
    // filter over the noisy spectrum (frames t - (order - 1 - lookahead) … t + lookahead).
    std::vector<float> output(frames * std::size_t(hop));
    DeepFilterStft stft(c);
    std::vector<Complex> enhanced(static_cast<std::size_t>(bins));
    const int back = c.dfOrder - 1 - c.dfLookahead;
    for (std::size_t t = 0; t < frames; ++t) {
        const Complex* x = spectrum.data() + t * std::size_t(bins);
        const float* g = mask.data() + t * std::size_t(c.nbErb);
        int bin = 0;
        for (int b = 0; b < c.nbErb; ++b) {
            for (int i = 0; i < widths[std::size_t(b)]; ++i, ++bin)
                enhanced[std::size_t(bin)] = x[bin] * g[b];
        }
        const float* w = coefs.data() + t * std::size_t(c.nbDf) * taps;
        for (int f = 0; f < c.nbDf; ++f) {
            Complex sum = 0.0f;
            for (int k = 0; k < c.dfOrder; ++k) {
                const std::ptrdiff_t s = std::ptrdiff_t(t) - back + k;
                if (s < 0 || s >= std::ptrdiff_t(frames))
                    continue;
                const Complex tap(w[std::size_t(f) * taps + std::size_t(2 * k)],
                                  w[std::size_t(f) * taps + std::size_t(2 * k + 1)]);
                sum += spectrum[std::size_t(s) * std::size_t(bins) + std::size_t(f)] * tap;
            }
            enhanced[std::size_t(f)] = sum;
        }
        stft.synthesis(enhanced.data(), output.data() + t * std::size_t(hop));
        if (progress && t % 2000 == 0 && !progress(0.9 + 0.1 * double(t) / double(frames)))
            return std::nullopt;
    }
    const std::size_t delay = std::size_t(c.fftSize - hop);
    return std::vector<float>(output.begin() + std::ptrdiff_t(delay), output.begin() + std::ptrdiff_t(delay + length));
}

namespace {

// The whole sound of a file in the engine format, as its two channels.
bool readChannels(const std::string& path, std::array<std::vector<float>, 2>& channels, int* sourceChannels,
                  const AnalysisProgress& progress, double progressShare, std::string* error)
{
    std::string why;
    std::unique_ptr<AudioDecoder> decoder = AudioDecoder::open(path, &why);
    if (!decoder) {
        fail(error, why.empty() ? "This has no sound to work on." : why);
        return false;
    }
    if (sourceChannels)
        *sourceChannels = decoder->info().sourceChannels;
    const double total = std::max(1.0, double(audio::samplesFromTicks(decoder->info().duration)));
    std::vector<float> chunk(std::size_t(audio::kSampleRate) * audio::kChannels);
    for (;;) {
        const int read = decoder->read(chunk.data(), audio::kSampleRate);
        for (int i = 0; i < read; ++i) {
            channels[0].push_back(chunk[std::size_t(i) * 2]);
            channels[1].push_back(chunk[std::size_t(i) * 2 + 1]);
        }
        if (read < audio::kSampleRate)
            break;
        if (progress && !progress(progressShare * std::min(1.0, double(channels[0].size()) / total)))
            return false;
    }
    if (channels[0].empty()) {
        fail(error, "This has no sound to work on.");
        return false;
    }
    return true;
}

bool writeWav(const std::string& path, const std::array<std::vector<float>, 2>& channels, int count, std::string* error)
{
    std::filesystem::create_directories(std::filesystem::path(path).parent_path());
    const std::string part = path + ".part";
    std::unique_ptr<WavWriter> writer = WavWriter::open(part, audio::kSampleRate, count, error);
    if (!writer)
        return false;
    const std::size_t length = channels[0].size();
    std::vector<float> interleaved;
    constexpr std::size_t kChunk = 48000;
    for (std::size_t at = 0; at < length; at += kChunk) {
        const std::size_t n = std::min(kChunk, length - at);
        interleaved.resize(n * std::size_t(count));
        for (std::size_t i = 0; i < n; ++i) {
            for (int ch = 0; ch < count; ++ch)
                interleaved[i * std::size_t(count) + std::size_t(ch)] = channels[std::size_t(ch)][at + i];
        }
        if (!writer->write(interleaved.data(), std::int64_t(n))) {
            fail(error, "The cleaned sound could not be written.");
            return false;
        }
    }
    if (!writer->finish()) {
        fail(error, "The cleaned sound could not be written.");
        return false;
    }
    std::error_code ec;
    std::filesystem::rename(part, path, ec);
    if (ec) {
        fail(error, "The cleaned sound could not be written.");
        return false;
    }
    return true;
}

} // namespace

bool SpeechEnhancer::enhanceFile(const std::string& mediaPath, const std::string& outputPath,
                                 const AnalysisProgress& progress, std::string* error)
{
    std::array<std::vector<float>, 2> channels;
    int sourceChannels = 0;
    if (!readChannels(mediaPath, channels, &sourceChannels, progress, 0.05, error))
        return false;
    // Mono (or a stereo file with the same sound on both sides): clean it once.
    bool alike = true;
    for (std::size_t i = 0; i < channels[0].size() && alike; ++i)
        alike = std::abs(channels[0][i] - channels[1][i]) < 1e-5f;
    const int count = alike ? 1 : 2;
    for (int ch = 0; ch < count; ++ch) {
        const double base = 0.05 + 0.9 * ch / count;
        const AnalysisProgress part = [&](double p) { return !progress || progress(base + 0.9 / count * p); };
        std::optional<std::vector<float>> clean = enhance(channels[std::size_t(ch)], part, error);
        if (!clean)
            return false;
        channels[std::size_t(ch)] = std::move(*clean);
    }
    if (!writeWav(outputPath, channels, count, error))
        return false;
    if (progress)
        progress(1.0);
    return true;
}

bool limitEnhancement(const std::string& originalPath, const std::string& enhancedPath, double limitDb,
                      const std::string& outputPath, const AnalysisProgress& progress, std::string* error)
{
    std::array<std::vector<float>, 2> original, enhanced;
    int count = 0;
    const auto half = [&](double from) {
        return [&progress, from](double p) { return !progress || progress(from + 0.45 * p); };
    };
    if (!readChannels(originalPath, original, nullptr, half(0.0), 1.0, error)
        || !readChannels(enhancedPath, enhanced, &count, half(0.45), 1.0, error))
        return false;
    // Lowered by at most limitDb: the original at that level under the cleaned sound.
    const float keep = limitDb >= 100.0 ? 0.0f : float(std::pow(10.0, -std::abs(limitDb) / 20.0));
    count = std::clamp(count, 1, 2);
    for (int ch = 0; ch < count; ++ch) {
        std::vector<float>& e = enhanced[std::size_t(ch)];
        const std::vector<float>& o = original[std::size_t(ch)];
        for (std::size_t i = 0; i < e.size(); ++i)
            e[i] = e[i] * (1.0f - keep) + (i < o.size() ? o[i] : 0.0f) * keep;
    }
    return writeWav(outputPath, enhanced, count, error);
}

} // namespace tve
