#pragma once

#include <array>
#include <string>

namespace tve {

// Colour management: what colour space a picture is in, the space a project works in, and
// how to convert between them (colour.frag does it on the GPU; the same maths is here for
// tests and the CPU). Light is "relative" linear light, 1 being reference white — SDR's
// white, and HDR's graphics white at 203 nits (BT.2408).

enum class Primaries { Rec709, DisplayP3, Rec2020 };
// SDR: BT.1886's 2.4 power (images' sRGB is taken as the same); Linear: relative light as
// it is (an EDR display's extended linear output); PQ (SMPTE ST 2084) and HLG (ARIB
// STD-B67, shown as on a 1000-nit display).
enum class Transfer { Sdr, Linear, Pq, Hlg };

struct ColourSpace {
    Primaries primaries = Primaries::Rec709;
    Transfer transfer = Transfer::Sdr;

    friend bool operator==(const ColourSpace&, const ColourSpace&) = default;
};

// A project works in SDR Rec.709 (the default), or HDR Rec.2020 with PQ or HLG: its effects
// and grade work on the picture encoded so, and it exports so.
enum class ProjectColour { Sdr, HdrPq, HdrHlg };

ColourSpace workingSpace(ProjectColour colour);
inline bool isHdr(Transfer t)
{
    return t == Transfer::Pq || t == Transfer::Hlg;
}
inline bool isHdr(ProjectColour c)
{
    return c != ProjectColour::Sdr;
}

inline constexpr double kReferenceWhiteNits = 203.0;
// How bright HDR media is taken to get (HLG by definition; PQ as mastered, typically).
inline constexpr double kHdrPeakNits = 1000.0;

// "sdr", "hdr-pq", "hdr-hlg" and back; "709", "p3", "2020"; "sdr", "linear", "pq", "hlg".
std::string toString(ProjectColour colour);
ProjectColour projectColourFrom(const std::string& text);
std::string toString(Primaries primaries);
std::string toString(Transfer transfer);
Primaries primariesFrom(const std::string& text);
Transfer transferFrom(const std::string& text);

// A conversion for colour.frag: the source transfer decoded to relative light, the linear
// primaries matrix (row-major), highlights above a knee rolled off towards `targetPeak` (by
// the brightest channel, keeping hue; 0: none), then the destination transfer.
struct ColourConversion {
    bool identity = true;
    Transfer from = Transfer::Sdr;
    Transfer to = Transfer::Sdr;
    std::array<float, 9> matrix{1, 0, 0, 0, 1, 0, 0, 0, 1};
    float targetPeak = 0.0f;
};

// From `from` to `to`; `displayPeak` (relative) is how bright the destination can go when
// it is Linear (an EDR display's headroom; 1 for SDR).
ColourConversion conversion(ColourSpace from, ColourSpace to, double displayPeak = 1.0);

// Linear RGB in one set of primaries to another (row-major 3 × 3, D65 white).
std::array<double, 9> primariesMatrix(Primaries from, Primaries to);

// The transfer functions, relative light.
double decodeTransfer(Transfer transfer, double signal);
double encodeTransfer(Transfer transfer, double light);
// HLG is decoded and encoded with its system gamma on the picture's Rec.2020 luminance.
std::array<double, 3> decodeHlg(const std::array<double, 3>& signal);
std::array<double, 3> encodeHlg(const std::array<double, 3>& light);
// The highlight roll-off: linear up to 0.75 × `peak`, then easing towards `peak`.
double rollOff(double light, double peak);
// A whole conversion of one RGB value (what colour.frag does to each pixel).
std::array<double, 3> convert(const ColourConversion& conversion, std::array<double, 3> rgb);

// PQ signal (0..1) for an absolute brightness, and back (for scopes in nits).
double pqFromNits(double nits);
double nitsFromPq(double signal);

} // namespace tve
