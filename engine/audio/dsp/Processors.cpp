#include "audio/dsp/Processors.h"

#include "audio/dsp/Biquad.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <random>

namespace tve::dsp {

namespace {

constexpr double kRate = kSampleRate;
constexpr double kTwoPi = 2.0 * std::numbers::pi;

float get(const Settings& s, std::string_view id)
{
    const auto it = s.find(id);
    return it == s.end() ? 0.0f : it->second;
}

// A fractional delay line for one channel.
class DelayLine {
public:
    void resize(std::size_t samples)
    {
        m_buffer.assign(samples, 0.0f);
        m_write = 0;
    }
    void clear() { std::ranges::fill(m_buffer, 0.0f); }
    void push(float x)
    {
        m_buffer[m_write] = x;
        m_write = (m_write + 1) % m_buffer.size();
    }
    // The sample `delay` samples ago (≥ 1), linearly interpolated.
    float read(double delay) const
    {
        const double size = double(m_buffer.size());
        delay = std::clamp(delay, 1.0, size - 2.0);
        double position = double(m_write) - delay;
        if (position < 0.0)
            position += size;
        const auto i0 = std::size_t(position);
        const std::size_t i1 = (i0 + 1) % m_buffer.size();
        const float t = float(position - double(i0));
        return m_buffer[i0] + (m_buffer[i1] - m_buffer[i0]) * t;
    }
    std::size_t size() const { return m_buffer.size(); }

private:
    std::vector<float> m_buffer{0.0f, 0.0f, 0.0f, 0.0f};
    std::size_t m_write = 0;
};

// --- eq: ten-band graphic equalizer, low and high cuts, output gain ---

constexpr std::array<double, 10> kBandFrequencies{31.25, 62.5, 125, 250, 500, 1000, 2000, 4000, 8000, 16000};
constexpr std::array<const char*, 10> kBandIds{"b31", "b62", "b125", "b250", "b500", "b1k", "b2k", "b4k", "b8k", "b16k"};

class Equalizer final : public Processor {
public:
    void configure(const Settings& s) override
    {
        for (std::size_t i = 0; i < kBandIds.size(); ++i) {
            const double gain = get(s, kBandIds[i]);
            m_bands[i].on = std::abs(gain) > 0.01;
            if (m_bands[i].on)
                m_bands[i].set(biquad::peaking(kBandFrequencies[i], gain, 1.41, kRate));
        }
        const double low = get(s, "lowCut");
        const double high = get(s, "highCut");
        for (int k = 0; k < 2; ++k) {
            m_lowCut[k].on = low > 20.5;
            if (m_lowCut[k].on)
                m_lowCut[k].set(biquad::highPass(low, biquad::kButterworth4[k], kRate));
            m_highCut[k].on = high < 19999.0;
            if (m_highCut[k].on)
                m_highCut[k].set(biquad::lowPass(high, biquad::kButterworth4[k], kRate));
        }
        m_gain = dbToGain(get(s, "gain"));
    }
    void process(float* x, int frames) override
    {
        for (int i = 0; i < frames; ++i) {
            for (int c = 0; c < 2; ++c) {
                float v = x[i * 2 + c];
                for (Stage* stages : {m_lowCut.data(), m_highCut.data()}) {
                    for (int k = 0; k < 2; ++k)
                        if (stages[k].on)
                            v = stages[k].channel[c].process(v);
                }
                for (Stage& band : m_bands)
                    if (band.on)
                        v = band.channel[c].process(v);
                x[i * 2 + c] = v * m_gain;
            }
        }
    }
    void reset() override
    {
        for (Stage& s : m_bands)
            s.reset();
        for (int k = 0; k < 2; ++k) {
            m_lowCut[k].reset();
            m_highCut[k].reset();
        }
    }

private:
    struct Stage {
        bool on = false;
        Biquad channel[2];
        void set(const BiquadCoefficients& c)
        {
            channel[0].c = c;
            channel[1].c = c;
        }
        void reset()
        {
            channel[0].reset();
            channel[1].reset();
        }
    };
    std::array<Stage, 10> m_bands;
    std::array<Stage, 2> m_lowCut;
    std::array<Stage, 2> m_highCut;
    float m_gain = 1.0f;
};

// --- compressor: stereo-linked, soft knee ---

class Compressor final : public Processor {
public:
    void configure(const Settings& s) override
    {
        m_threshold = get(s, "threshold");
        m_ratio = std::max(1.0f, get(s, "ratio"));
        m_knee = get(s, "knee");
        m_attack = smoothing(get(s, "attack"), kRate);
        m_release = smoothing(get(s, "release"), kRate);
        m_makeup = get(s, "makeup");
    }
    void process(float* x, int frames) override
    {
        for (int i = 0; i < frames; ++i) {
            const double level = gainToDb(std::max(std::abs(x[i * 2]), std::abs(x[i * 2 + 1])));
            const double over = level - m_threshold;
            const double slope = 1.0 / m_ratio - 1.0;
            double reduction = 0.0;
            if (2.0 * over >= m_knee)
                reduction = slope * over;
            else if (m_knee > 0.0 && 2.0 * std::abs(over) < m_knee)
                reduction = slope * (over + m_knee / 2.0) * (over + m_knee / 2.0) / (2.0 * m_knee);
            const double k = reduction < m_envelope ? m_attack : m_release;
            m_envelope = k * m_envelope + (1.0 - k) * reduction;
            const float gain = dbToGain(m_envelope + m_makeup);
            x[i * 2] *= gain;
            x[i * 2 + 1] *= gain;
        }
    }
    void reset() override { m_envelope = 0.0; }

private:
    double m_threshold = -18, m_ratio = 4, m_knee = 6, m_attack = 0, m_release = 0, m_makeup = 0;
    double m_envelope = 0.0; // gain reduction, dB (≤ 0)
};

// --- limiter: 2 ms look-ahead, nothing above the ceiling ---

class Limiter final : public Processor {
public:
    static constexpr int kLookahead = 96;

    Limiter()
    {
        for (DelayLine& line : m_delay)
            line.resize(kLookahead + 2);
        m_needed.fill(1.0f);
    }
    void configure(const Settings& s) override
    {
        m_ceiling = dbToGain(get(s, "ceiling"));
        m_release = smoothing(get(s, "release"), kRate);
    }
    void process(float* x, int frames) override
    {
        for (int i = 0; i < frames; ++i) {
            const float peak = std::max(std::abs(x[i * 2]), std::abs(x[i * 2 + 1]));
            m_needed[m_at] = peak > m_ceiling ? m_ceiling / peak : 1.0f;
            m_at = (m_at + 1) % kLookahead;
            const float target = *std::ranges::min_element(m_needed);
            m_gain = target < m_gain ? target : float(m_release * m_gain + (1.0 - m_release) * target);
            for (int c = 0; c < 2; ++c) {
                m_delay[c].push(x[i * 2 + c]);
                x[i * 2 + c] = std::clamp(m_delay[c].read(kLookahead) * m_gain, -m_ceiling, m_ceiling);
            }
        }
    }
    void reset() override
    {
        for (DelayLine& line : m_delay)
            line.clear();
        m_needed.fill(1.0f);
        m_gain = 1.0f;
    }

private:
    DelayLine m_delay[2];
    std::array<float, kLookahead> m_needed{};
    std::size_t m_at = 0;
    float m_gain = 1.0f;
    float m_ceiling = 0.9f;
    double m_release = 0.0;
};

// --- gate: noise gate and downward expander ---

class Gate final : public Processor {
public:
    void configure(const Settings& s) override
    {
        m_threshold = get(s, "threshold");
        m_ratio = std::max(1.0f, get(s, "ratio"));
        m_range = get(s, "range");
        m_attack = smoothing(get(s, "attack"), kRate);
        m_release = smoothing(get(s, "release"), kRate);
        m_detectorDecay = smoothing(10.0, kRate);
    }
    void process(float* x, int frames) override
    {
        for (int i = 0; i < frames; ++i) {
            const double peak = std::max(std::abs(x[i * 2]), std::abs(x[i * 2 + 1]));
            m_level = std::max(peak, m_level * m_detectorDecay);
            const double db = gainToDb(m_level);
            const double target = db < m_threshold ? std::max<double>(m_range, (db - m_threshold) * (m_ratio - 1.0)) : 0.0;
            const double k = target > m_envelope ? m_attack : m_release; // opens fast, closes slowly
            m_envelope = k * m_envelope + (1.0 - k) * target;
            const float gain = dbToGain(m_envelope);
            x[i * 2] *= gain;
            x[i * 2 + 1] *= gain;
        }
    }
    void reset() override
    {
        m_level = 0.0;
        m_envelope = 0.0;
    }

private:
    double m_threshold = -45, m_ratio = 10, m_range = -60, m_attack = 0, m_release = 0, m_detectorDecay = 0;
    double m_level = 0.0;
    double m_envelope = 0.0;
};

// --- reverb: parallel damped combs into series all-passes, per channel ---

class Reverb final : public Processor {
public:
    void configure(const Settings& s) override
    {
        const double size = get(s, "size");
        m_feedback = 0.72 + 0.26 * size;
        m_damping = 0.05 + 0.45 * get(s, "damping");
        m_width = get(s, "width");
        m_preDelay = get(s, "preDelay") * 0.001 * kRate;
        const double scale = 0.6 + 0.9 * size;
        if (std::abs(scale - m_scale) > 1e-6) {
            m_scale = scale;
            for (int c = 0; c < 2; ++c) {
                for (std::size_t k = 0; k < kCombs.size(); ++k)
                    m_combs[c][k].resize(std::size_t((kCombs[k] + c * kSpread) * scale) + 1);
                for (std::size_t k = 0; k < kAllPasses.size(); ++k)
                    m_allPasses[c][k].assign(std::size_t(kAllPasses[k] + c * kSpread), 0.0f);
            }
            reset();
        }
        if (m_pre.size() < std::size_t(0.25 * kRate))
            m_pre.resize(std::size_t(0.25 * kRate));
    }
    void process(float* x, int frames) override
    {
        for (int i = 0; i < frames; ++i) {
            m_pre.push((x[i * 2] + x[i * 2 + 1]) * 0.5f);
            const float in = (m_preDelay >= 1.0 ? m_pre.read(m_preDelay) : (x[i * 2] + x[i * 2 + 1]) * 0.5f) * 0.02f;
            float wet[2];
            for (int c = 0; c < 2; ++c) {
                float sum = 0.0f;
                for (Comb& comb : m_combs[c])
                    sum += comb.process(in, float(m_feedback), float(m_damping));
                for (std::size_t k = 0; k < kAllPasses.size(); ++k) {
                    std::vector<float>& buffer = m_allPasses[c][k];
                    std::size_t& at = m_allPassAt[c][k];
                    const float delayed = buffer[at];
                    buffer[at] = sum + delayed * 0.5f;
                    sum = delayed - sum;
                    at = (at + 1) % buffer.size();
                }
                wet[c] = sum * 3.0f;
            }
            const float a = float((1.0 + m_width) / 2.0);
            const float b = float((1.0 - m_width) / 2.0);
            x[i * 2] = wet[0] * a + wet[1] * b;
            x[i * 2 + 1] = wet[1] * a + wet[0] * b;
        }
    }
    void reset() override
    {
        for (int c = 0; c < 2; ++c) {
            for (Comb& comb : m_combs[c])
                comb.clear();
            for (std::size_t k = 0; k < kAllPasses.size(); ++k) {
                std::ranges::fill(m_allPasses[c][k], 0.0f);
                m_allPassAt[c][k] = 0;
            }
        }
        m_pre.clear();
    }
    double tail() const override
    {
        // Time to fall 60 dB: three times the decay per round trip of an average comb.
        double average = 0.0;
        for (const int length : kCombs)
            average += length * m_scale;
        average /= double(kCombs.size()) * kRate;
        return std::min(8.0, 3.0 * average / -std::log10(m_feedback) + m_preDelay / kRate);
    }

private:
    // Comb and all-pass lengths in samples (our own tunings: mutually prime-ish, 28–39 ms).
    static constexpr std::array<int, 8> kCombs{1361, 1433, 1499, 1571, 1637, 1709, 1777, 1847};
    static constexpr std::array<int, 4> kAllPasses{599, 467, 373, 241};
    static constexpr int kSpread = 27; // right channel slightly longer, for stereo

    struct Comb {
        std::vector<float> buffer{0.0f};
        std::size_t at = 0;
        float store = 0.0f;
        void resize(std::size_t n)
        {
            buffer.assign(n, 0.0f);
            at = 0;
        }
        void clear()
        {
            std::ranges::fill(buffer, 0.0f);
            store = 0.0f;
            at = 0;
        }
        float process(float in, float feedback, float damping)
        {
            const float out = buffer[at];
            store = out * (1.0f - damping) + store * damping;
            buffer[at] = in + store * feedback;
            at = (at + 1) % buffer.size();
            return out;
        }
    };

    std::array<std::array<Comb, 8>, 2> m_combs;
    std::array<std::array<std::vector<float>, 4>, 2> m_allPasses;
    std::array<std::array<std::size_t, 4>, 2> m_allPassAt{};
    DelayLine m_pre;
    double m_feedback = 0.85, m_damping = 0.25, m_width = 1.0, m_preDelay = 0.0, m_scale = -1.0;
};

// --- delay: echoes, optionally bouncing left and right, darker each time ---

class Delay final : public Processor {
public:
    Delay()
    {
        for (DelayLine& line : m_lines)
            line.resize(std::size_t(2.1 * kRate));
    }
    void configure(const Settings& s) override
    {
        m_time = get(s, "time") * 0.001 * kRate;
        m_feedback = get(s, "feedback");
        m_pingPong = get(s, "pingPong") > 0.5f;
        m_level = get(s, "level");
        m_tone = std::exp(-kTwoPi * (800.0 + 15000.0 * get(s, "tone")) / kRate);
    }
    void process(float* x, int frames) override
    {
        for (int i = 0; i < frames; ++i) {
            const float echo[2] = {m_lines[0].read(m_time), m_lines[1].read(m_time)};
            for (int c = 0; c < 2; ++c)
                m_filtered[c] = float((1.0 - m_tone) * echo[c] + m_tone * m_filtered[c]);
            const float fb = float(m_feedback);
            if (m_pingPong) {
                m_lines[0].push((x[i * 2] + x[i * 2 + 1]) * 0.5f + m_filtered[1] * fb);
                m_lines[1].push(m_filtered[0] * fb);
            } else {
                m_lines[0].push(x[i * 2] + m_filtered[0] * fb);
                m_lines[1].push(x[i * 2 + 1] + m_filtered[1] * fb);
            }
            x[i * 2] += echo[0] * float(m_level);
            x[i * 2 + 1] += echo[1] * float(m_level);
        }
    }
    void reset() override
    {
        for (DelayLine& line : m_lines)
            line.clear();
        m_filtered[0] = m_filtered[1] = 0.0f;
    }
    double tail() const override
    {
        const double seconds = m_time / kRate;
        const double repeats = m_feedback > 0.01 ? std::log(0.001) / std::log(m_feedback) : 1.0;
        return std::min(12.0, seconds * (repeats + 1.0));
    }

private:
    DelayLine m_lines[2];
    float m_filtered[2] = {0.0f, 0.0f};
    double m_time = 14400, m_feedback = 0.35, m_level = 0.5, m_tone = 0.0;
    bool m_pingPong = false;
};

// --- chorus (and flanger): a delay swept by a slow sine, a quarter turn apart per side ---

class Chorus final : public Processor {
public:
    Chorus()
    {
        for (DelayLine& line : m_lines)
            line.resize(std::size_t(0.1 * kRate));
    }
    void configure(const Settings& s) override
    {
        m_rate = get(s, "rate");
        m_depth = get(s, "depth") * 0.001 * kRate;
        m_delay = get(s, "delay") * 0.001 * kRate;
        m_feedback = get(s, "feedback");
    }
    void process(float* x, int frames) override
    {
        const double step = kTwoPi * m_rate / kRate;
        for (int i = 0; i < frames; ++i) {
            for (int c = 0; c < 2; ++c) {
                const double d = m_delay + m_depth * 0.5 * (1.0 + std::sin(m_phase + c * std::numbers::pi / 2.0));
                const float wet = m_lines[c].read(d);
                m_lines[c].push(x[i * 2 + c] + wet * float(m_feedback));
                x[i * 2 + c] = (x[i * 2 + c] + wet) * 0.7f;
            }
            m_phase = std::fmod(m_phase + step, kTwoPi);
        }
    }
    void reset() override
    {
        for (DelayLine& line : m_lines)
            line.clear();
        m_phase = 0.0;
    }

private:
    DelayLine m_lines[2];
    double m_rate = 0.8, m_depth = 144, m_delay = 576, m_feedback = 0.0, m_phase = 0.0;
};

// --- widener: mid/side width and an optional short (Haas) delay on the right ---

class Widener final : public Processor {
public:
    Widener() { m_right.resize(std::size_t(0.05 * kRate)); }
    void configure(const Settings& s) override
    {
        m_width = get(s, "width");
        m_haas = get(s, "haas") * 0.001 * kRate;
    }
    void process(float* x, int frames) override
    {
        for (int i = 0; i < frames; ++i) {
            const float mid = (x[i * 2] + x[i * 2 + 1]) * 0.5f;
            const float side = (x[i * 2] - x[i * 2 + 1]) * 0.5f * float(m_width);
            const float right = mid - side;
            m_right.push(right);
            x[i * 2] = mid + side;
            x[i * 2 + 1] = m_haas >= 1.0 ? m_right.read(m_haas) : right;
        }
    }
    void reset() override { m_right.clear(); }

private:
    DelayLine m_right;
    double m_width = 1.5, m_haas = 0.0;
};

// --- autopan: the sound circling from side to side ---

class AutoPan final : public Processor {
public:
    void configure(const Settings& s) override
    {
        m_rate = get(s, "rate");
        m_depth = get(s, "depth");
    }
    void process(float* x, int frames) override
    {
        const double step = kTwoPi * m_rate / kRate;
        for (int i = 0; i < frames; ++i) {
            const double pan = m_depth * std::sin(m_phase);
            const double angle = (pan + 1.0) * std::numbers::pi / 4.0; // constant power, unity in the middle
            const float mono = (x[i * 2] + x[i * 2 + 1]) * 0.5f;
            x[i * 2] = mono * float(std::cos(angle) * std::numbers::sqrt2);
            x[i * 2 + 1] = mono * float(std::sin(angle) * std::numbers::sqrt2);
            m_phase = std::fmod(m_phase + step, kTwoPi);
        }
    }
    void reset() override { m_phase = 0.0; }

private:
    double m_rate = 0.25, m_depth = 1.0, m_phase = 0.0;
};

// --- pitch: two read heads sliding through a short delay, crossfaded (granular shifting) ---

class PitchShifter final : public Processor {
public:
    PitchShifter()
    {
        for (DelayLine& line : m_lines)
            line.resize(std::size_t(0.25 * kRate));
    }
    void configure(const Settings& s) override
    {
        m_ratio = std::pow(2.0, get(s, "semitones") / 12.0);
        m_window = std::max(64.0, get(s, "window") * 0.001 * kRate);
    }
    void process(float* x, int frames) override
    {
        if (std::abs(m_ratio - 1.0) < 1e-4)
            return;
        // The read heads move through the delay at `ratio` while it fills at 1, so the delay
        // changes by (1 − ratio) per sample; each head restarts when it runs out of window.
        const double step = (1.0 - m_ratio) / m_window;
        for (int i = 0; i < frames; ++i) {
            for (int c = 0; c < 2; ++c)
                m_lines[c].push(x[i * 2 + c]);
            float out[2] = {0.0f, 0.0f};
            for (const double offset : {0.0, 0.5}) {
                double p = m_phase + offset;
                p -= std::floor(p);
                const float weight = float(std::pow(std::sin(std::numbers::pi * p), 2.0));
                for (int c = 0; c < 2; ++c)
                    out[c] += m_lines[c].read(1.0 + p * m_window) * weight;
            }
            x[i * 2] = out[0];
            x[i * 2 + 1] = out[1];
            m_phase += step;
            m_phase -= std::floor(m_phase);
        }
    }
    void reset() override
    {
        for (DelayLine& line : m_lines)
            line.clear();
        m_phase = 0.0;
    }

private:
    DelayLine m_lines[2];
    double m_ratio = 1.0, m_window = 2400, m_phase = 0.0;
};

// --- distortion: soft clipping, then a tone filter ---

class Distortion final : public Processor {
public:
    void configure(const Settings& s) override
    {
        m_drive = dbToGain(get(s, "drive"));
        m_output = dbToGain(get(s, "output"));
        const auto c = biquad::lowPass(get(s, "tone"), 0.707, kRate);
        m_tone[0].c = c;
        m_tone[1].c = c;
    }
    void process(float* x, int frames) override
    {
        for (int i = 0; i < frames * 2; ++i)
            x[i] = m_tone[i % 2].process(std::tanh(x[i] * m_drive)) * m_output;
    }
    void reset() override
    {
        m_tone[0].reset();
        m_tone[1].reset();
    }

private:
    float m_drive = 4.0f, m_output = 0.25f;
    Biquad m_tone[2];
};

// --- ringmod: multiplied by a sine (metallic, robotic) ---

class RingModulator final : public Processor {
public:
    void configure(const Settings& s) override { m_frequency = get(s, "frequency"); }
    void process(float* x, int frames) override
    {
        const double step = kTwoPi * m_frequency / kRate;
        for (int i = 0; i < frames; ++i) {
            const float m = float(std::sin(m_phase));
            x[i * 2] *= m;
            x[i * 2 + 1] *= m;
            m_phase = std::fmod(m_phase + step, kTwoPi);
        }
    }
    void reset() override { m_phase = 0.0; }

private:
    double m_frequency = 50.0, m_phase = 0.0;
};

// --- noise: hiss added under the sound (radios, old recordings) ---

class Noise final : public Processor {
public:
    void configure(const Settings& s) override
    {
        m_level = dbToGain(get(s, "level"));
        const auto c = biquad::lowPass(get(s, "tone"), 0.707, kRate);
        m_tone[0].c = c;
        m_tone[1].c = c;
    }
    void process(float* x, int frames) override
    {
        std::uniform_real_distribution<float> uniform(-1.0f, 1.0f);
        for (int i = 0; i < frames * 2; ++i)
            x[i] += m_tone[i % 2].process(uniform(m_random)) * m_level;
    }
    void reset() override
    {
        m_random.seed(7);
        m_tone[0].reset();
        m_tone[1].reset();
    }

private:
    float m_level = 0.01f;
    Biquad m_tone[2];
    std::minstd_rand m_random{7};
};

template <typename T>
std::unique_ptr<Processor> make()
{
    return std::make_unique<T>();
}

} // namespace

const ParamInfo* ProcessorInfo::find(std::string_view paramId) const
{
    const auto it = std::ranges::find(params, paramId, &ParamInfo::id);
    return it == params.end() ? nullptr : &*it;
}

Settings ProcessorInfo::complete(const Settings& settings) const
{
    Settings result;
    for (const ParamInfo& p : params) {
        const auto it = settings.find(p.id);
        const float v = it == settings.end() || !std::isfinite(it->second) ? p.value : it->second;
        result[p.id] = std::clamp(v, p.min, p.max);
    }
    return result;
}

const std::vector<ProcessorInfo>& processors()
{
    static const std::vector<ProcessorInfo> all = [] {
        std::vector<ParamInfo> eq;
        for (const char* band : kBandIds)
            eq.push_back({band, -18.0f, 18.0f, 0.0f});
        eq.push_back({"lowCut", 20.0f, 2000.0f, 20.0f});
        eq.push_back({"highCut", 500.0f, 20000.0f, 20000.0f});
        eq.push_back({"gain", -24.0f, 24.0f, 0.0f});
        return std::vector<ProcessorInfo>{
            {"eq", eq, make<Equalizer>},
            {"compressor",
             {{"threshold", -60, 0, -18}, {"ratio", 1, 20, 4}, {"attack", 0.1f, 200, 10}, {"release", 10, 2000, 150},
              {"knee", 0, 24, 6}, {"makeup", 0, 24, 0}},
             make<Compressor>},
            {"limiter", {{"ceiling", -24, 0, -1}, {"release", 5, 1000, 80}}, make<Limiter>},
            {"gate",
             {{"threshold", -90, 0, -45}, {"ratio", 1, 20, 10}, {"range", -90, 0, -60}, {"attack", 0.1f, 50, 2},
              {"release", 10, 1000, 120}},
             make<Gate>},
            {"reverb", {{"size", 0, 1, 0.5f}, {"damping", 0, 1, 0.5f}, {"width", 0, 1, 1}, {"preDelay", 0, 200, 10}},
             make<Reverb>},
            {"delay",
             {{"time", 1, 2000, 300}, {"feedback", 0, 0.95f, 0.35f}, {"pingPong", 0, 1, 0}, {"tone", 0, 1, 0.6f},
              {"level", 0, 1, 0.5f}},
             make<Delay>},
            {"chorus", {{"rate", 0.05f, 10, 0.8f}, {"depth", 0, 10, 3}, {"delay", 1, 30, 12}, {"feedback", -0.9f, 0.9f, 0}},
             make<Chorus>},
            {"widener", {{"width", 0, 2, 1.5f}, {"haas", 0, 30, 0}}, make<Widener>},
            {"autopan", {{"rate", 0.02f, 10, 0.25f}, {"depth", 0, 1, 1}}, make<AutoPan>},
            {"pitch", {{"semitones", -24, 24, 0}, {"window", 20, 100, 50}}, make<PitchShifter>},
            {"distortion", {{"drive", 0, 40, 12}, {"tone", 200, 16000, 6000}, {"output", -36, 12, -6}}, make<Distortion>},
            {"ringmod", {{"frequency", 1, 2000, 50}}, make<RingModulator>},
            {"noise", {{"level", -90, -10, -40}, {"tone", 200, 16000, 5000}}, make<Noise>},
        };
    }();
    return all;
}

const ProcessorInfo* findProcessor(std::string_view id)
{
    const auto& all = processors();
    const auto it = std::ranges::find(all, id, &ProcessorInfo::id);
    return it == all.end() ? nullptr : &*it;
}

} // namespace tve::dsp
