#include "model/Colour.h"

#include <algorithm>
#include <cmath>

namespace tve {

namespace {

using Mat3 = std::array<double, 9>;

Mat3 multiply(const Mat3& a, const Mat3& b)
{
    Mat3 out{};
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c)
            for (int k = 0; k < 3; ++k)
                out[std::size_t(r * 3 + c)] += a[std::size_t(r * 3 + k)] * b[std::size_t(k * 3 + c)];
    return out;
}

Mat3 inverse(const Mat3& m)
{
    const double a = m[0], b = m[1], c = m[2], d = m[3], e = m[4], f = m[5], g = m[6], h = m[7], i = m[8];
    const double det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
    return {(e * i - f * h) / det, (c * h - b * i) / det, (b * f - c * e) / det,
            (f * g - d * i) / det, (a * i - c * g) / det, (c * d - a * f) / det,
            (d * h - e * g) / det, (b * g - a * h) / det, (a * e - b * d) / det};
}

// Linear RGB to XYZ from the primaries' and D65's chromaticities.
Mat3 toXyz(Primaries p)
{
    struct Xy {
        double x, y;
    };
    std::array<Xy, 3> rgb;
    switch (p) {
    case Primaries::Rec709: rgb = {{{0.640, 0.330}, {0.300, 0.600}, {0.150, 0.060}}}; break;
    case Primaries::DisplayP3: rgb = {{{0.680, 0.320}, {0.265, 0.690}, {0.150, 0.060}}}; break;
    case Primaries::Rec2020: rgb = {{{0.708, 0.292}, {0.170, 0.797}, {0.131, 0.046}}}; break;
    }
    const Xy white{0.3127, 0.3290};
    Mat3 columns{};
    for (int i = 0; i < 3; ++i) {
        const Xy c = rgb[std::size_t(i)];
        columns[std::size_t(i)] = c.x / c.y;
        columns[std::size_t(3 + i)] = 1.0;
        columns[std::size_t(6 + i)] = (1.0 - c.x - c.y) / c.y;
    }
    const std::array<double, 3> w{white.x / white.y, 1.0, (1.0 - white.x - white.y) / white.y};
    const Mat3 inv = inverse(columns);
    std::array<double, 3> s{};
    for (int r = 0; r < 3; ++r)
        for (int k = 0; k < 3; ++k)
            s[std::size_t(r)] += inv[std::size_t(r * 3 + k)] * w[std::size_t(k)];
    Mat3 out{};
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c)
            out[std::size_t(r * 3 + c)] = columns[std::size_t(r * 3 + c)] * s[std::size_t(c)];
    return out;
}

constexpr double kM1 = 2610.0 / 16384.0, kM2 = 2523.0 / 4096.0 * 128.0;
constexpr double kC1 = 3424.0 / 4096.0, kC2 = 2413.0 / 4096.0 * 32.0, kC3 = 2392.0 / 4096.0 * 32.0;
constexpr double kHlgA = 0.17883277, kHlgB = 0.28466892, kHlgC = 0.55991073;
constexpr double kHlgGamma = 1.2; // system gamma for a 1000-nit display

double hlgInverseOetf(double v)
{
    v = std::clamp(v, 0.0, 1.0);
    return v <= 0.5 ? v * v / 3.0 : (std::exp((v - kHlgC) / kHlgA) + kHlgB) / 12.0;
}
double hlgOetf(double e)
{
    e = std::max(e, 0.0);
    return e <= 1.0 / 12.0 ? std::sqrt(3.0 * e) : kHlgA * std::log(12.0 * e - kHlgB) + kHlgC;
}
double luminance2020(const std::array<double, 3>& rgb)
{
    return 0.2627 * rgb[0] + 0.6780 * rgb[1] + 0.0593 * rgb[2];
}

} // namespace

ColourSpace workingSpace(ProjectColour colour)
{
    switch (colour) {
    case ProjectColour::Sdr: return {Primaries::Rec709, Transfer::Sdr};
    case ProjectColour::HdrPq: return {Primaries::Rec2020, Transfer::Pq};
    case ProjectColour::HdrHlg: return {Primaries::Rec2020, Transfer::Hlg};
    }
    return {};
}

std::string toString(ProjectColour colour)
{
    return colour == ProjectColour::HdrPq ? "hdr-pq" : colour == ProjectColour::HdrHlg ? "hdr-hlg" : "sdr";
}
ProjectColour projectColourFrom(const std::string& text)
{
    return text == "hdr-pq" ? ProjectColour::HdrPq : text == "hdr-hlg" ? ProjectColour::HdrHlg : ProjectColour::Sdr;
}
std::string toString(Primaries p)
{
    return p == Primaries::DisplayP3 ? "p3" : p == Primaries::Rec2020 ? "2020" : "709";
}
std::string toString(Transfer t)
{
    return t == Transfer::Pq ? "pq" : t == Transfer::Hlg ? "hlg" : t == Transfer::Linear ? "linear" : "sdr";
}
Primaries primariesFrom(const std::string& text)
{
    return text == "p3" ? Primaries::DisplayP3 : text == "2020" ? Primaries::Rec2020 : Primaries::Rec709;
}
Transfer transferFrom(const std::string& text)
{
    return text == "pq" ? Transfer::Pq : text == "hlg" ? Transfer::Hlg : text == "linear" ? Transfer::Linear : Transfer::Sdr;
}

std::array<double, 9> primariesMatrix(Primaries from, Primaries to)
{
    if (from == to)
        return {1, 0, 0, 0, 1, 0, 0, 0, 1};
    return multiply(inverse(toXyz(to)), toXyz(from));
}

double decodeTransfer(Transfer transfer, double v)
{
    switch (transfer) {
    case Transfer::Sdr: return std::pow(std::clamp(v, 0.0, 1.0), 2.4);
    case Transfer::Linear: return v;
    case Transfer::Pq: {
        const double p = std::pow(std::clamp(v, 0.0, 1.0), 1.0 / kM2);
        const double nits = 10000.0 * std::pow(std::max(p - kC1, 0.0) / (kC2 - kC3 * p), 1.0 / kM1);
        return nits / kReferenceWhiteNits;
    }
    case Transfer::Hlg: return hlgInverseOetf(v) * kHdrPeakNits / kReferenceWhiteNits; // (grey, no OOTF)
    }
    return v;
}

double encodeTransfer(Transfer transfer, double light)
{
    switch (transfer) {
    case Transfer::Sdr: return std::pow(std::clamp(light, 0.0, 1.0), 1.0 / 2.4);
    case Transfer::Linear: return light;
    case Transfer::Pq: return pqFromNits(light * kReferenceWhiteNits);
    case Transfer::Hlg: return hlgOetf(light * kReferenceWhiteNits / kHdrPeakNits);
    }
    return light;
}

std::array<double, 3> decodeHlg(const std::array<double, 3>& signal)
{
    // Scene light, then the OOTF of a 1000-nit display: Fd = Lw · Ys^(γ-1) · E.
    std::array<double, 3> scene{hlgInverseOetf(signal[0]), hlgInverseOetf(signal[1]), hlgInverseOetf(signal[2])};
    const double gain = std::pow(std::max(luminance2020(scene), 1e-9), kHlgGamma - 1.0) * kHdrPeakNits / kReferenceWhiteNits;
    return {scene[0] * gain, scene[1] * gain, scene[2] * gain};
}

std::array<double, 3> encodeHlg(const std::array<double, 3>& light)
{
    // Back through the OOTF: E = Fd/Lw · Yd^((1-γ)/γ).
    std::array<double, 3> display{light[0] * kReferenceWhiteNits / kHdrPeakNits, light[1] * kReferenceWhiteNits / kHdrPeakNits,
                                  light[2] * kReferenceWhiteNits / kHdrPeakNits};
    const double gain = std::pow(std::max(luminance2020(display), 1e-9), (1.0 - kHlgGamma) / kHlgGamma);
    return {hlgOetf(display[0] * gain), hlgOetf(display[1] * gain), hlgOetf(display[2] * gain)};
}

double rollOff(double x, double peak)
{
    if (peak <= 0.0)
        return x;
    const double knee = 0.75 * peak;
    if (x <= knee)
        return x;
    const double room = peak - knee;
    return knee + room * (1.0 - std::exp(-(x - knee) / room));
}

double pqFromNits(double nits)
{
    const double y = std::pow(std::clamp(nits / 10000.0, 0.0, 1.0), kM1);
    return std::pow((kC1 + kC2 * y) / (1.0 + kC3 * y), kM2);
}

double nitsFromPq(double signal)
{
    return decodeTransfer(Transfer::Pq, signal) * kReferenceWhiteNits;
}

ColourConversion conversion(ColourSpace from, ColourSpace to, double displayPeak)
{
    ColourConversion c;
    c.identity = from == to;
    c.from = from.transfer;
    c.to = to.transfer;
    const auto m = primariesMatrix(from.primaries, to.primaries);
    for (std::size_t i = 0; i < 9; ++i)
        c.matrix[i] = float(m[i]);
    // Brighter than the destination can show: rolled off to fit.
    const double sourcePeak = isHdr(from.transfer) ? kHdrPeakNits / kReferenceWhiteNits : 1.0;
    const double destinationPeak = to.transfer == Transfer::Sdr     ? 1.0
                                 : to.transfer == Transfer::Linear ? std::max(displayPeak, 1.0)
                                                                   : kHdrPeakNits / kReferenceWhiteNits;
    c.targetPeak = sourcePeak > destinationPeak ? float(destinationPeak) : 0.0f;
    return c;
}

std::array<double, 3> convert(const ColourConversion& c, std::array<double, 3> rgb)
{
    if (c.identity)
        return rgb;
    std::array<double, 3> light = c.from == Transfer::Hlg
                                      ? decodeHlg(rgb)
                                      : std::array<double, 3>{decodeTransfer(c.from, rgb[0]), decodeTransfer(c.from, rgb[1]),
                                                              decodeTransfer(c.from, rgb[2])};
    std::array<double, 3> out{};
    for (int r = 0; r < 3; ++r) {
        for (int k = 0; k < 3; ++k)
            out[std::size_t(r)] += double(c.matrix[std::size_t(r * 3 + k)]) * light[std::size_t(k)];
        out[std::size_t(r)] = std::max(out[std::size_t(r)], 0.0); // out of the destination's gamut: clipped
    }
    if (c.targetPeak > 0.0f) {
        const double brightest = std::max({out[0], out[1], out[2]});
        if (brightest > 1e-9) {
            const double scale = rollOff(brightest, c.targetPeak) / brightest;
            for (double& v : out)
                v *= scale;
        }
    }
    if (c.to == Transfer::Hlg)
        return encodeHlg(out);
    return {encodeTransfer(c.to, out[0]), encodeTransfer(c.to, out[1]), encodeTransfer(c.to, out[2])};
}

} // namespace tve
