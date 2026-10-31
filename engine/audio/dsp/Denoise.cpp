#include "audio/dsp/Denoise.h"

#include <rnnoise.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>

namespace tve::dsp {

namespace {

constexpr int kFrame = 480;        // rnnoise_get_frame_size() at 48 kHz
constexpr float kScale = 32768.0f; // RNNoise works on 16-bit sample values

struct StateDeleter {
    void operator()(DenoiseState* state) const { std::free(state); }
};

class Denoiser final : public Processor {
public:
    Denoiser()
    {
        for (auto& state : m_states) {
            // Allocated once, so reset() can start it over without allocating.
            state.reset(static_cast<DenoiseState*>(std::malloc(std::size_t(rnnoise_get_size()))));
            rnnoise_init(state.get(), nullptr);
        }
    }
    void configure(const Settings& s) override
    {
        const auto get = [&](std::string_view id) {
            const auto it = s.find(id);
            return it == s.end() ? 0.0f : it->second;
        };
        m_amount = std::clamp(get("amount"), 0.0f, 1.0f);
        m_quiet = std::pow(10.0f, std::min(0.0f, get("quiet")) / 20.0f);
    }
    void process(float* x, int frames) override
    {
        for (int i = 0; i < frames; ++i) {
            for (int c = 0; c < 2; ++c) {
                const float in = x[i * 2 + c] * kScale;
                m_in[c][m_fill] = in;
                // Out: what came in kDenoiseLatency samples ago (a frame here, two in RNNoise),
                // cleaned, blended with itself as it was.
                const float dry = m_dry[c][m_dryAt];
                m_dry[c][m_dryAt] = in;
                const float wet = m_out[c][m_fill];
                const float gain = m_gainFrom + (m_gainTo - m_gainFrom) * (float(m_fill) / kFrame);
                x[i * 2 + c] = (dry + (wet * gain - dry) * m_amount) / kScale;
            }
            m_dryAt = (m_dryAt + 1) % kDenoiseLatency;
            if (++m_fill == kFrame) {
                m_fill = 0;
                float voice = 0.0f;
                for (int c = 0; c < 2; ++c) {
                    voice = std::max(voice, rnnoise_process_frame(m_states[c].get(), m_out[c].data(), m_in[c].data()));
                }
                // Between words (no voice heard in this frame or the one before: RNNoise
                // hears a frame ahead of what it gives out) the "quiet" reduction, ramped
                // across the frame.
                const float heard = std::max(voice, m_lastVoice);
                m_lastVoice = voice;
                m_gainFrom = m_gainTo;
                m_gainTo = heard >= 0.5f ? 1.0f : m_quiet + (1.0f - m_quiet) * heard * 2.0f;
            }
        }
    }
    void reset() override
    {
        for (auto& state : m_states)
            rnnoise_init(state.get(), nullptr);
        for (int c = 0; c < 2; ++c) {
            m_in[c].fill(0.0f);
            m_out[c].fill(0.0f);
            m_dry[c].fill(0.0f);
        }
        m_fill = 0;
        m_dryAt = 0;
        m_lastVoice = 0.0f;
        m_gainFrom = m_gainTo = 1.0f;
    }
    double tail() const override { return double(kDenoiseLatency) / kSampleRate; }

private:
    std::array<std::unique_ptr<DenoiseState, StateDeleter>, 2> m_states;
    std::array<std::array<float, kFrame>, 2> m_in{}, m_out{};
    std::array<std::array<float, kDenoiseLatency>, 2> m_dry{}; // the input, delayed to match
    int m_fill = 0;
    int m_dryAt = 0;
    float m_amount = 1.0f;
    float m_quiet = 1.0f;
    float m_lastVoice = 0.0f;
    float m_gainFrom = 1.0f, m_gainTo = 1.0f;
};

} // namespace

std::unique_ptr<Processor> makeDenoiser()
{
    return std::make_unique<Denoiser>();
}

} // namespace tve::dsp
