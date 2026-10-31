#include "analysis/MediaAnalysis.h"

#include "decode/AudioDecoder.h"
#include "decode/Thumbnail.h"
#include "decode/VideoDecoder.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <numbers>
#include <numeric>

namespace tve {

namespace {

constexpr Ticks kHopTicks = kTicksPerSecond / 100; // AudioFeatures::kHop, exactly
constexpr int kHop48 = 480;                       // 10 ms at 48 kHz
constexpr int kDecimation = 4;                    // 48 kHz → 12 kHz for the spectrum
constexpr int kFft = 512;                         // 43 ms at 12 kHz
constexpr int kFftHop = kHop48 / kDecimation;     // 10 ms at 12 kHz

// In-place radix-2 FFT.
void fft(std::vector<std::complex<float>>& data)
{
    const std::size_t n = data.size();
    for (std::size_t i = 1, j = 0; i < n; ++i) {
        std::size_t bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap(data[i], data[j]);
    }
    for (std::size_t length = 2; length <= n; length <<= 1) {
        const float angle = -2.0f * std::numbers::pi_v<float> / float(length);
        const std::complex<float> step(std::cos(angle), std::sin(angle));
        for (std::size_t i = 0; i < n; i += length) {
            std::complex<float> w(1.0f, 0.0f);
            for (std::size_t k = 0; k < length / 2; ++k) {
                const std::complex<float> a = data[i + k];
                const std::complex<float> b = data[i + k + length / 2] * w;
                data[i + k] = a + b;
                data[i + k + length / 2] = a - b;
                w *= step;
            }
        }
    }
}

// Onset strength of the 12 kHz signal: how much the log spectrum rises from one 10 ms frame
// to the next (30 Hz – 5.5 kHz).
class OnsetMeter {
public:
    OnsetMeter() : m_window(kFft), m_previous(kFft / 2, 0.0f), m_spectrum(kFft)
    {
        for (int i = 0; i < kFft; ++i)
            m_window[std::size_t(i)] = 0.5f - 0.5f * std::cos(2.0f * std::numbers::pi_v<float> * float(i) / float(kFft));
    }

    // Samples of the 12 kHz signal arrive here; frames come out as soon as they are whole.
    void push(float sample, std::vector<float>& onset)
    {
        m_signal.push_back(sample);
        ++m_received;
        while (frameEnd(m_frames) <= m_received)
            onset.push_back(frame());
    }
    // The frames left at the end, completed with silence, until there are `count`.
    void finish(std::vector<float>& onset, std::size_t count)
    {
        while (onset.size() < count)
            onset.push_back(frame());
    }

private:
    // Frame k is centred on sample k × hop.
    std::size_t frameEnd(std::size_t k) const { return k * kFftHop + kFft / 2; }

    float frame()
    {
        const std::int64_t centre = std::int64_t(m_frames) * kFftHop;
        for (int i = 0; i < kFft; ++i) {
            const std::int64_t index = centre - kFft / 2 + i - std::int64_t(m_dropped);
            const float value = index >= 0 && index < std::int64_t(m_signal.size()) ? m_signal[std::size_t(index)] : 0.0f;
            m_spectrum[std::size_t(i)] = {value * m_window[std::size_t(i)], 0.0f};
        }
        fft(m_spectrum);
        float flux = 0.0f;
        for (int bin = 2; bin < 236; ++bin) {
            const float level = std::log1p(100.0f * std::abs(m_spectrum[std::size_t(bin)]));
            flux += std::max(0.0f, level - m_previous[std::size_t(bin)]);
            m_previous[std::size_t(bin)] = level;
        }
        ++m_frames;
        // Forget samples no frame needs any more.
        const std::int64_t keepFrom = std::int64_t(m_frames) * kFftHop - kFft / 2;
        if (keepFrom - std::int64_t(m_dropped) > 65536) {
            const std::size_t drop = std::size_t(keepFrom) - m_dropped;
            m_signal.erase(m_signal.begin(), m_signal.begin() + std::ptrdiff_t(drop));
            m_dropped += drop;
        }
        return m_frames == 1 ? 0.0f : flux;
    }

    std::vector<float> m_window;
    std::vector<float> m_previous;
    std::vector<std::complex<float>> m_spectrum;
    std::vector<float> m_signal;
    std::size_t m_dropped = 0;  // samples forgotten from the front
    std::size_t m_received = 0; // samples pushed in all
    std::size_t m_frames = 0;
};

Ticks frameTime(std::size_t frame)
{
    return Ticks(frame) * kHopTicks;
}

std::size_t frameAt(Ticks time)
{
    return std::size_t(std::max<Ticks>(0, time) / kHopTicks);
}

float median(std::vector<float> values)
{
    if (values.empty())
        return 0.0f;
    const auto middle = values.begin() + std::ptrdiff_t(values.size() / 2);
    std::nth_element(values.begin(), middle, values.end());
    return *middle;
}

std::string safeName(const std::string& fingerprint)
{
    std::string name = fingerprint;
    for (char& c : name)
        if (!std::isalnum(static_cast<unsigned char>(c)))
            c = '-';
    return name;
}

constexpr char kMagic[4] = {'T', 'V', 'E', 'A'};

template <typename T>
void writeValue(std::ofstream& out, const T& value)
{
    out.write(reinterpret_cast<const char*>(&value), sizeof value);
}
template <typename T>
bool readValue(std::ifstream& in, T& value)
{
    return bool(in.read(reinterpret_cast<char*>(&value), sizeof value));
}
template <typename T>
void writeVector(std::ofstream& out, const std::vector<T>& values)
{
    writeValue(out, std::uint64_t(values.size()));
    out.write(reinterpret_cast<const char*>(values.data()), std::streamsize(values.size() * sizeof(T)));
}
template <typename T>
bool readVector(std::ifstream& in, std::vector<T>& values)
{
    std::uint64_t count = 0;
    if (!readValue(in, count) || count > (1ull << 32))
        return false;
    values.resize(std::size_t(count));
    return bool(in.read(reinterpret_cast<char*>(values.data()), std::streamsize(values.size() * sizeof(T))));
}

} // namespace

// --- Measuring ---

std::optional<AudioFeatures> analyzeAudio(const std::string& path, const AnalysisProgress& progress, std::string* error)
{
    auto decoder = AudioDecoder::open(path, error);
    if (!decoder)
        return std::nullopt;
    AudioFeatures features;
    features.duration = decoder->info().duration;
    const double total = std::max(1.0, toSeconds(features.duration));
    OnsetMeter onset;
    std::vector<float> buffer(std::size_t(audio::kSampleRate) * audio::kChannels);
    double squares = 0.0;
    int inHop = 0;
    float decimated = 0.0f;
    int inDecimation = 0;
    std::int64_t samples = 0;
    for (;;) {
        const int read = decoder->read(buffer.data(), audio::kSampleRate);
        if (read <= 0)
            break;
        for (int i = 0; i < read; ++i) {
            const float mono = 0.5f * (buffer[std::size_t(i) * 2] + buffer[std::size_t(i) * 2 + 1]);
            squares += double(mono) * mono;
            if (++inHop == kHop48) {
                features.loudness.push_back(float(std::max(-120.0, 10.0 * std::log10(squares / kHop48 + 1e-12))));
                squares = 0.0;
                inHop = 0;
            }
            decimated += mono;
            if (++inDecimation == kDecimation) {
                onset.push(decimated / kDecimation, features.onset);
                decimated = 0.0f;
                inDecimation = 0;
            }
        }
        samples += read;
        if (progress && !progress(std::min(1.0, double(samples) / audio::kSampleRate / total)))
            return std::nullopt;
    }
    if (inHop > 0)
        features.loudness.push_back(float(std::max(-120.0, 10.0 * std::log10(squares / inHop + 1e-12))));
    onset.finish(features.onset, features.loudness.size());
    features.onset.resize(features.loudness.size());
    if (features.loudness.empty()) {
        if (error)
            *error = "the file has no sound";
        return std::nullopt;
    }
    if (features.duration <= 0)
        features.duration = frameTime(features.loudness.size());
    return features;
}

std::optional<VideoFeatures> analyzeVideo(const std::string& path, const AnalysisProgress& progress, std::string* error)
{
    auto decoder = VideoDecoder::open(path, error);
    if (!decoder)
        return std::nullopt;
    const double total = std::max(1e-3, toSeconds(decoder->info().duration));
    VideoFeatures features;
    std::vector<float> previousHistogram;
    std::vector<float> previousLuma;
    while (auto frame = decoder->nextFrame()) {
        const auto picture = pictureOf(*frame, 18);
        if (!picture)
            continue;
        // A 4 × 4 × 4 colour histogram and the luma of each pixel.
        const std::size_t pixels = std::size_t(picture->width) * std::size_t(picture->height);
        std::vector<float> histogram(64, 0.0f);
        std::vector<float> luma(pixels);
        for (std::size_t i = 0; i < pixels; ++i) {
            const std::uint8_t* p = &picture->pixels[i * 4];
            histogram[std::size_t((p[0] >> 6) * 16 + (p[1] >> 6) * 4 + (p[2] >> 6))] += 1.0f / float(pixels);
            luma[i] = (0.2126f * p[0] + 0.7152f * p[1] + 0.0722f * p[2]) / 255.0f;
        }
        float change = 0.0f;
        if (previousHistogram.size() == histogram.size() && previousLuma.size() == luma.size()) {
            float histogramDifference = 0.0f;
            for (std::size_t i = 0; i < histogram.size(); ++i)
                histogramDifference += std::abs(histogram[i] - previousHistogram[i]);
            float pixelDifference = 0.0f;
            for (std::size_t i = 0; i < luma.size(); ++i)
                pixelDifference += std::abs(luma[i] - previousLuma[i]);
            pixelDifference /= float(luma.size());
            change = 0.6f * 0.5f * histogramDifference + 0.4f * std::min(1.0f, pixelDifference / 0.25f);
        }
        features.times.push_back(frame->pts());
        features.change.push_back(change);
        previousHistogram = std::move(histogram);
        previousLuma = std::move(luma);
        if (progress && features.times.size() % 30 == 0
            && !progress(std::min(1.0, toSeconds(features.times.back()) / total)))
            return std::nullopt;
    }
    if (features.times.empty()) {
        if (error)
            *error = "the file has no pictures";
        return std::nullopt;
    }
    return features;
}

// --- Lining up ---

std::optional<AudioOffset> findAudioOffset(const AudioFeatures& reference, const AudioFeatures& other, double maxLag)
{
    // Each onset curve with its mean taken off and scaled to one, over at most ten minutes.
    const auto prepare = [](const std::vector<float>& onset) {
        std::vector<double> v(onset.begin(), onset.begin() + std::ptrdiff_t(std::min<std::size_t>(onset.size(), 60000)));
        if (v.empty())
            return v;
        const double mean = std::accumulate(v.begin(), v.end(), 0.0) / double(v.size());
        double spread = 0.0;
        for (double& x : v) {
            x -= mean;
            spread += x * x;
        }
        spread = std::sqrt(spread / double(v.size()));
        if (spread < 1e-9)
            return std::vector<double>{};
        for (double& x : v)
            x /= spread;
        return v;
    };
    const std::vector<double> a = prepare(reference.onset);
    const std::vector<double> b = prepare(other.onset);
    if (a.size() < 100 || b.size() < 100)
        return std::nullopt;
    const int reach = int(std::lround(maxLag / AudioFeatures::kHop));
    // corr(lag) = mean over k of a[k + lag] · b[k] (the overlap), for every lag in reach.
    std::vector<double> corr(std::size_t(2 * reach + 1), -1e9);
    for (int lag = -reach; lag <= reach; ++lag) {
        const std::ptrdiff_t from = std::max<std::ptrdiff_t>(0, -lag);
        const std::ptrdiff_t to = std::min<std::ptrdiff_t>(std::ptrdiff_t(b.size()), std::ptrdiff_t(a.size()) - lag);
        if (to - from < 200) // at least two seconds in common
            continue;
        double sum = 0.0;
        for (std::ptrdiff_t k = from; k < to; ++k)
            sum += a[std::size_t(k + lag)] * b[std::size_t(k)];
        corr[std::size_t(lag + reach)] = sum / double(to - from);
    }
    const auto best = std::max_element(corr.begin(), corr.end());
    if (*best <= 0.0)
        return std::nullopt;
    const std::ptrdiff_t at = best - corr.begin();
    // A parabola through the peak and its neighbours: a fraction of a step.
    double fraction = 0.0;
    if (at > 0 && at + 1 < std::ptrdiff_t(corr.size())) {
        const double l = corr[std::size_t(at - 1)], c = *best, r = corr[std::size_t(at + 1)];
        const double curve = l - 2.0 * c + r;
        if (curve < 0.0)
            fraction = std::clamp(0.5 * (l - r) / curve, -0.5, 0.5);
    }
    // How far it stands out from the best match a quarter of a second away or more. At the
    // edge of the lags searched, the real peak may lie beyond: no confidence.
    double second = 0.0;
    for (std::size_t i = 0; i < corr.size(); ++i)
        if (std::abs(std::ptrdiff_t(i) - at) > 25)
            second = std::max(second, corr[i]);
    const bool atEdge = at == 0 || at + 1 == std::ptrdiff_t(corr.size());
    AudioOffset result;
    result.offset = (double(at - reach) + fraction) * AudioFeatures::kHop;
    result.confidence = atEdge ? 0.0 : std::clamp(1.0 - std::max(0.0, second) / *best, 0.0, 1.0);
    return result;
}

// --- Detecting ---

std::vector<TimeRange> findSilences(const AudioFeatures& features, const SilenceOptions& options, Ticks from, Ticks to)
{
    std::vector<TimeRange> silences;
    const std::size_t first = std::min(frameAt(from), features.loudness.size());
    const std::size_t last = std::min(features.loudness.size(), std::size_t((std::max<Ticks>(to, 0) + kHopTicks - 1) / kHopTicks));
    const Ticks padding = Ticks(options.padding * kTicksPerSecond);
    const Ticks minimum = Ticks(options.minDuration * kTicksPerSecond);
    std::size_t i = first;
    while (i < last) {
        if (features.loudness[i] >= options.thresholdDb) {
            ++i;
            continue;
        }
        std::size_t end = i;
        while (end < last && features.loudness[end] < options.thresholdDb)
            ++end;
        const Ticks start = std::max(from, frameTime(i));
        const Ticks stop = std::min(to, frameTime(end));
        if (stop - start >= minimum) {
            const TimeRange range{start + padding, stop - padding};
            if (range.length() > 0)
                silences.push_back(range);
        }
        i = end;
    }
    return silences;
}

std::vector<TimeRange> findSpeech(const AudioFeatures& features, const SpeechOptions& options)
{
    std::vector<TimeRange> speech;
    const std::size_t hold = std::size_t(std::lround(options.hold / AudioFeatures::kHop));
    for (std::size_t i = 0; i < features.loudness.size(); ++i) {
        if (features.loudness[i] < options.thresholdDb)
            continue;
        const Ticks start = frameTime(i);
        const Ticks end = frameTime(i + 1);
        if (!speech.empty() && start - speech.back().end <= Ticks(hold) * kHopTicks)
            speech.back().end = end;
        else
            speech.push_back({start, end});
    }
    const Ticks minimum = Ticks(options.minDuration * kTicksPerSecond);
    std::erase_if(speech, [&](const TimeRange& r) { return r.length() < minimum; });
    return speech;
}

Beats findBeats(const AudioFeatures& features)
{
    Beats beats;
    const std::size_t n = features.onset.size();
    constexpr double rate = 1.0 / AudioFeatures::kHop; // frames per second
    if (n < std::size_t(rate * 2))
        return beats;
    // Onset strength above its own half-second average, in units of its spread.
    std::vector<float> o(n);
    const std::size_t half = std::size_t(rate / 4);
    double sum = 0.0;
    std::vector<double> prefix(n + 1, 0.0);
    for (std::size_t i = 0; i < n; ++i)
        prefix[i + 1] = prefix[i] + features.onset[i];
    for (std::size_t i = 0; i < n; ++i) {
        const std::size_t a = i > half ? i - half : 0;
        const std::size_t b = std::min(n, i + half + 1);
        o[i] = std::max(0.0f, features.onset[i] - float((prefix[b] - prefix[a]) / double(b - a)));
        sum += double(o[i]) * o[i];
    }
    const double spread = std::sqrt(sum / double(n));
    if (spread < 1e-6)
        return beats; // nothing happens: no beat
    for (float& v : o)
        v = float(v / spread);

    // Tempo: the strongest repetition of the onsets between 60 and 200 BPM, leaning towards
    // 120 BPM (a log-Gaussian, one octave wide) so halves and doubles lose.
    const std::size_t minLag = std::size_t(rate * 60.0 / 200.0);
    const std::size_t maxLag = std::min(n / 2, std::size_t(rate * 60.0 / 60.0));
    std::vector<double> score(maxLag + 2, 0.0);
    std::size_t best = 0;
    for (std::size_t lag = minLag; lag <= maxLag; ++lag) {
        double r = 0.0;
        for (std::size_t t = 0; t + lag < n; ++t)
            r += double(o[t]) * o[t + lag];
        r /= double(n - lag);
        const double bpm = rate * 60.0 / double(lag);
        score[lag] = r * std::exp(-0.5 * std::pow(std::log2(bpm / 120.0), 2));
        if (best == 0 || score[lag] > score[best])
            best = lag;
    }
    double period = double(best);
    if (best > minLag && best < maxLag) {
        const double a = score[best - 1], b = score[best], c = score[best + 1];
        const double denominator = a - 2 * b + c;
        if (std::abs(denominator) > 1e-12)
            period += std::clamp(0.5 * (a - c) / denominator, -0.5, 0.5);
    }
    beats.bpm = rate * 60.0 / period;

    // Beat tracking by dynamic programming: each beat's score is its (smoothed) onset
    // strength plus the best earlier beat's, less a penalty for straying from the period.
    std::vector<double> local(n, 0.0);
    const int radius = int(std::ceil(period));
    for (std::size_t t = 0; t < n; ++t) {
        double v = 0.0;
        for (int k = -radius; k <= radius; ++k) {
            const std::int64_t i = std::int64_t(t) + k;
            if (i >= 0 && i < std::int64_t(n))
                v += o[std::size_t(i)] * std::exp(-0.5 * std::pow(k * 32.0 / period, 2));
        }
        local[t] = v;
    }
    constexpr double kTightness = 100.0;
    std::vector<double> cumulative(n, 0.0);
    std::vector<std::int64_t> back(n, -1);
    const std::int64_t earliest = std::int64_t(std::lround(2.0 * period));
    const std::int64_t latest = std::max<std::int64_t>(1, std::lround(period / 2.0));
    for (std::size_t t = 0; t < n; ++t) {
        double bestPrevious = 0.0;
        std::int64_t from = -1;
        for (std::int64_t gap = latest; gap <= earliest; ++gap) {
            const std::int64_t tau = std::int64_t(t) - gap;
            if (tau < 0)
                break;
            const double candidate = cumulative[std::size_t(tau)] - kTightness * std::pow(std::log(double(gap) / period), 2);
            if (from < 0 || candidate > bestPrevious) {
                bestPrevious = candidate;
                from = tau;
            }
        }
        cumulative[t] = local[t] + (from >= 0 ? std::max(0.0, bestPrevious) : 0.0);
        back[t] = from >= 0 && bestPrevious > 0.0 ? from : -1;
    }
    // The last beat: the last strong peak of the cumulative score.
    std::vector<std::size_t> peaks;
    for (std::size_t t = 1; t + 1 < n; ++t)
        if (cumulative[t] > cumulative[t - 1] && cumulative[t] >= cumulative[t + 1])
            peaks.push_back(t);
    if (peaks.empty())
        return beats;
    std::vector<float> peakValues;
    for (std::size_t t : peaks)
        peakValues.push_back(float(cumulative[t]));
    const float bar = 0.5f * median(peakValues);
    std::size_t last = peaks.back();
    for (auto it = peaks.rbegin(); it != peaks.rend(); ++it)
        if (cumulative[*it] >= bar) {
            last = *it;
            break;
        }
    std::vector<std::size_t> frames;
    for (std::int64_t t = std::int64_t(last); t >= 0; t = back[std::size_t(t)])
        frames.push_back(std::size_t(t));
    std::ranges::reverse(frames);
    // Leave out beats in the quiet before the music starts and after it ends.
    double energy = 0.0;
    for (double v : local)
        energy += v * v;
    const double threshold = 0.5 * std::sqrt(energy / double(n));
    while (!frames.empty() && local[frames.front()] < threshold)
        frames.erase(frames.begin());
    while (!frames.empty() && local[frames.back()] < threshold)
        frames.pop_back();
    for (std::size_t f : frames)
        beats.times.push_back(frameTime(f));
    return beats;
}

std::vector<Ticks> findSceneCuts(const VideoFeatures& features, const SceneOptions& options)
{
    std::vector<Ticks> cuts;
    if (features.times.size() < 2)
        return cuts;
    const Ticks minimum = Ticks(options.minScene * kTicksPerSecond);
    Ticks lastCut = features.times.front();
    for (std::size_t i = 1; i < features.times.size(); ++i) {
        // How much the shot changes by itself: the median change over the second before.
        std::vector<float> recent;
        for (std::size_t j = i - 1; j > 0 && features.times[i] - features.times[j] <= kTicksPerSecond; --j)
            recent.push_back(features.change[j]);
        const float usual = median(std::move(recent));
        const float change = features.change[i];
        if (change > options.threshold && change > 2.5f * usual + 0.05f && features.times[i] - lastCut >= minimum) {
            cuts.push_back(features.times[i]);
            lastCut = features.times[i];
        }
    }
    return cuts;
}

// --- Cache ---

std::string AnalysisCache::pathFor(const std::string& fingerprint, const char* kind) const
{
    return (std::filesystem::path(m_directory) / (safeName(fingerprint) + "." + kind)).string();
}

std::optional<AudioFeatures> AnalysisCache::loadAudio(const std::string& fingerprint) const
{
    std::ifstream in(pathFor(fingerprint, "audio"), std::ios::binary);
    char magic[4];
    std::int32_t version = 0;
    AudioFeatures features;
    if (!in || !in.read(magic, 4) || std::memcmp(magic, kMagic, 4) != 0 || !readValue(in, version)
        || version != AudioFeatures::kVersion || !readValue(in, features.duration) || !readVector(in, features.loudness)
        || !readVector(in, features.onset) || features.onset.size() != features.loudness.size())
        return std::nullopt;
    return features;
}

std::optional<VideoFeatures> AnalysisCache::loadVideo(const std::string& fingerprint) const
{
    std::ifstream in(pathFor(fingerprint, "video"), std::ios::binary);
    char magic[4];
    std::int32_t version = 0;
    VideoFeatures features;
    if (!in || !in.read(magic, 4) || std::memcmp(magic, kMagic, 4) != 0 || !readValue(in, version)
        || version != VideoFeatures::kVersion || !readVector(in, features.times) || !readVector(in, features.change)
        || features.change.size() != features.times.size())
        return std::nullopt;
    return features;
}

bool AnalysisCache::save(const std::string& fingerprint, const AudioFeatures& features) const
{
    std::error_code ignored;
    std::filesystem::create_directories(m_directory, ignored);
    std::ofstream out(pathFor(fingerprint, "audio"), std::ios::binary | std::ios::trunc);
    out.write(kMagic, 4);
    writeValue(out, std::int32_t(AudioFeatures::kVersion));
    writeValue(out, features.duration);
    writeVector(out, features.loudness);
    writeVector(out, features.onset);
    return bool(out);
}

bool AnalysisCache::save(const std::string& fingerprint, const VideoFeatures& features) const
{
    std::error_code ignored;
    std::filesystem::create_directories(m_directory, ignored);
    std::ofstream out(pathFor(fingerprint, "video"), std::ios::binary | std::ios::trunc);
    out.write(kMagic, 4);
    writeValue(out, std::int32_t(VideoFeatures::kVersion));
    writeVector(out, features.times);
    writeVector(out, features.change);
    return bool(out);
}

} // namespace tve
