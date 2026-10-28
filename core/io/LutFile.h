#pragma once

#include <array>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace tve {

// A 3D colour lookup table: size³ output colours over a cube of inputs, red changing
// fastest, then green, then blue (the .cube order).
struct Lut3d {
    int size = 0;
    std::vector<float> rgb; // size³ × 3
    std::array<float, 3> domainMin{0, 0, 0};
    std::array<float, 3> domainMax{1, 1, 1};
    std::string title;

    // The output colour stored for grid point (r, g, b).
    std::array<float, 3> at(int r, int g, int b) const;
    // Trilinear lookup of an input colour (what the GPU does).
    std::array<float, 3> lookup(std::array<float, 3> rgbIn) const;
};

inline constexpr int kMinLutSize = 2;
inline constexpr int kMaxLutSize = 129;

class LutError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Adobe/Resolve .cube (3D tables; TITLE, LUT_3D_SIZE, DOMAIN_MIN/MAX).
Lut3d parseCubeLut(std::string_view text);
// Autodesk .3dl: an optional input mesh line, then integer rows with blue changing fastest,
// scaled by their bit depth (10, 12 or 16 bits, from the largest value).
Lut3d parse3dlLut(std::string_view text);
// Either, by file extension. Throws LutError (or for an unreadable file).
Lut3d loadLut(const std::filesystem::path& path);

} // namespace tve
