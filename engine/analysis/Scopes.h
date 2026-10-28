#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace tve {

// A picture for the scopes: RGB floats (0..1, values outside are clamped), row by row.
struct ScopeImage {
    int width = 0;
    int height = 0;
    std::vector<float> rgb; // width × height × 3
};

// Counts in a grid: `columns` across, `rows` up (row 0 is the bottom: black, or the lowest
// value).
struct ScopeGrid {
    int columns = 0;
    int rows = 0;
    std::vector<std::uint32_t> counts; // rows × columns, bottom row first
    std::uint32_t peak = 0;            // largest count, for scaling brightness

    std::uint32_t at(int column, int row) const { return counts[std::size_t(row) * std::size_t(columns) + std::size_t(column)]; }
};

enum class ScopeChannel { Red, Green, Blue, Luma };

// Rec. 709 luma of a gamma-encoded colour.
float scopeLuma(float r, float g, float b);

// How many pixels have each level (256 bins) of red, green, blue and luma.
struct Histogram {
    std::array<std::array<std::uint32_t, 256>, 4> bins{}; // red, green, blue, luma
    std::uint32_t peak = 0;
};
Histogram histogram(const ScopeImage& image);

// Waveform: for each image column (`columns` across, the picture squeezed or stretched to
// fit), how many pixels have each level of `channel`.
ScopeGrid waveform(const ScopeImage& image, ScopeChannel channel, int columns = 256, int levels = 256);

// Vectorscope: pixels placed by their colour difference (Rec. 709 Cb across, Cr up),
// neutral in the centre and fully saturated colours on a circle of radius size / 2.
ScopeGrid vectorscope(const ScopeImage& image, int size = 256);

// Where a colour lands on the vectorscope, in the grid's coordinates (0..size).
std::array<float, 2> vectorscopePosition(float r, float g, float b, int size = 256);

} // namespace tve
