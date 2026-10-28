#pragma once

#include <algorithm>
#include <cmath>
#include <numbers>

namespace tve::dsp {

// Second-order filter coefficients, normalized (a0 = 1).
struct BiquadCoefficients {
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
};

// One channel of a second-order filter (transposed direct form II). New coefficients keep
// the state, so settings can change while it runs.
struct Biquad {
    BiquadCoefficients c;
    double z1 = 0.0;
    double z2 = 0.0;

    float process(float x)
    {
        const double y = c.b0 * x + z1;
        z1 = c.b1 * x - c.a1 * y + z2;
        z2 = c.b2 * x - c.a2 * y;
        return float(y);
    }
    void reset() { z1 = z2 = 0.0; }
};

// The usual audio-EQ shapes (bilinear transform of the analogue prototypes).
namespace biquad {

inline BiquadCoefficients normalized(double b0, double b1, double b2, double a0, double a1, double a2)
{
    return {b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0};
}

// A bell around `frequency` raising or cutting by `gainDb`.
inline BiquadCoefficients peaking(double frequency, double gainDb, double q, double sampleRate)
{
    const double a = std::pow(10.0, gainDb / 40.0);
    const double w = 2.0 * std::numbers::pi * frequency / sampleRate;
    const double alpha = std::sin(w) / (2.0 * q);
    const double cosw = std::cos(w);
    return normalized(1.0 + alpha * a, -2.0 * cosw, 1.0 - alpha * a, 1.0 + alpha / a, -2.0 * cosw, 1.0 - alpha / a);
}

inline BiquadCoefficients lowPass(double frequency, double q, double sampleRate)
{
    const double w = 2.0 * std::numbers::pi * frequency / sampleRate;
    const double alpha = std::sin(w) / (2.0 * q);
    const double cosw = std::cos(w);
    return normalized((1.0 - cosw) / 2.0, 1.0 - cosw, (1.0 - cosw) / 2.0, 1.0 + alpha, -2.0 * cosw, 1.0 - alpha);
}

inline BiquadCoefficients highPass(double frequency, double q, double sampleRate)
{
    const double w = 2.0 * std::numbers::pi * frequency / sampleRate;
    const double alpha = std::sin(w) / (2.0 * q);
    const double cosw = std::cos(w);
    return normalized((1.0 + cosw) / 2.0, -(1.0 + cosw), (1.0 + cosw) / 2.0, 1.0 + alpha, -2.0 * cosw, 1.0 - alpha);
}

// Q of the two sections of a fourth-order Butterworth filter (24 dB per octave).
inline constexpr double kButterworth4[2] = {0.5411961, 1.3065630};

} // namespace biquad

inline float dbToGain(double db)
{
    return float(std::pow(10.0, db / 20.0));
}

inline double gainToDb(double gain)
{
    return 20.0 * std::log10(std::max(gain, 1e-9));
}

// Per-sample smoothing coefficient for a time constant in milliseconds.
inline double smoothing(double milliseconds, double sampleRate)
{
    return std::exp(-1.0 / std::max(milliseconds * 0.001 * sampleRate, 1.0));
}

} // namespace tve::dsp
