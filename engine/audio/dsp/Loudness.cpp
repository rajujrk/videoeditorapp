#include "audio/dsp/Loudness.h"

#include "audio/dsp/Processors.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

namespace tve::dsp {

namespace {

constexpr int kSubBlock = int(kSampleRate / 10); // 100 ms
constexpr double kMinusInfinity = -std::numeric_limits<double>::infinity();

// The K-weighting curve's two stages (BS.1770): a high shelf of about +4 dB above 1.7 kHz
// for the head, and a high-pass at 38 Hz, derived for any sample rate.
BiquadCoefficients shelf(double fs)
{
    const double f0 = 1681.974450955533;
    const double gainDb = 3.999843853973347;
    const double q = 0.7071752369554196;
    const double k = std::tan(std::numbers::pi * f0 / fs);
    const double vh = std::pow(10.0, gainDb / 20.0);
    const double vb = std::pow(vh, 0.4996667741545416);
    const double a0 = 1.0 + k / q + k * k;
    return {(vh + vb * k / q + k * k) / a0, 2.0 * (k * k - vh) / a0, (vh - vb * k / q + k * k) / a0,
            2.0 * (k * k - 1.0) / a0, (1.0 - k / q + k * k) / a0};
}

BiquadCoefficients rlbHighPass(double fs)
{
    const double f0 = 38.13547087602444;
    const double q = 0.5003270373238773;
    const double k = std::tan(std::numbers::pi * f0 / fs);
    const double a0 = 1.0 + k / q + k * k;
    return {1.0, -2.0, 1.0, 2.0 * (k * k - 1.0) / a0, (1.0 - k / q + k * k) / a0};
}

} // namespace

LoudnessMeter::LoudnessMeter()
{
    for (Biquad& b : m_shelf)
        b.c = shelf(kSampleRate);
    for (Biquad& b : m_highPass)
        b.c = rlbHighPass(kSampleRate);
}

void LoudnessMeter::reset()
{
    for (Biquad& b : m_shelf)
        b.reset();
    for (Biquad& b : m_highPass)
        b.reset();
    m_sum = 0.0;
    m_count = 0;
    m_subBlocks.clear();
    m_blocks.clear();
    m_peak = 0.0f;
}

double LoudnessMeter::toLufs(double meanSquare)
{
    return meanSquare > 0.0 ? -0.691 + 10.0 * std::log10(meanSquare) : kMinusInfinity;
}

void LoudnessMeter::process(const float* stereo, int frames)
{
    for (int i = 0; i < frames; ++i) {
        for (int c = 0; c < 2; ++c) {
            const float x = stereo[i * 2 + c];
            m_peak = std::max(m_peak, std::abs(x));
            const double y = m_highPass[c].process(m_shelf[c].process(x));
            m_sum += y * y; // left and right weigh 1 each
        }
        if (++m_count == kSubBlock) {
            m_subBlocks.push_back(m_sum / kSubBlock);
            if (m_subBlocks.size() > 30)
                m_subBlocks.pop_front();
            if (m_subBlocks.size() >= 4)
                m_blocks.push_back(meanOfLast(4));
            m_sum = 0.0;
            m_count = 0;
        }
    }
}

double LoudnessMeter::meanOfLast(std::size_t subBlocks) const
{
    const std::size_t n = std::min(subBlocks, m_subBlocks.size());
    if (n == 0)
        return 0.0;
    return std::accumulate(m_subBlocks.end() - std::ptrdiff_t(n), m_subBlocks.end(), 0.0) / double(n);
}

double LoudnessMeter::momentary() const
{
    return m_subBlocks.size() >= 4 ? toLufs(meanOfLast(4)) : kMinusInfinity;
}

double LoudnessMeter::shortTerm() const
{
    return m_subBlocks.size() >= 4 ? toLufs(meanOfLast(30)) : kMinusInfinity;
}

double LoudnessMeter::integrated() const
{
    // Absolute gate at -70 LUFS, then a relative gate 10 LU below the gated average.
    const auto averageAbove = [&](double gateLufs) {
        double sum = 0.0;
        int n = 0;
        for (const double block : m_blocks) {
            if (toLufs(block) > gateLufs) {
                sum += block;
                ++n;
            }
        }
        return n > 0 ? sum / n : 0.0;
    };
    const double absolute = averageAbove(-70.0);
    if (absolute <= 0.0)
        return kMinusInfinity;
    return toLufs(averageAbove(toLufs(absolute) - 10.0));
}

} // namespace tve::dsp
