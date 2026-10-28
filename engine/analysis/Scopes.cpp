#include "analysis/Scopes.h"

#include <algorithm>
#include <cmath>

namespace tve {

namespace {

float clamp01(float v)
{
    return std::clamp(v, 0.0f, 1.0f);
}

int level(float v, int levels)
{
    return std::min(levels - 1, int(clamp01(v) * float(levels - 1) + 0.5f));
}

void finish(ScopeGrid& grid)
{
    grid.peak = grid.counts.empty() ? 0 : *std::ranges::max_element(grid.counts);
}

} // namespace

float scopeLuma(float r, float g, float b)
{
    return 0.2126f * r + 0.7152f * g + 0.0722f * b;
}

Histogram histogram(const ScopeImage& image)
{
    Histogram h;
    const std::size_t pixels = std::size_t(image.width) * std::size_t(image.height);
    for (std::size_t i = 0; i < pixels; ++i) {
        const float r = image.rgb[i * 3], g = image.rgb[i * 3 + 1], b = image.rgb[i * 3 + 2];
        ++h.bins[0][std::size_t(level(r, 256))];
        ++h.bins[1][std::size_t(level(g, 256))];
        ++h.bins[2][std::size_t(level(b, 256))];
        ++h.bins[3][std::size_t(level(scopeLuma(r, g, b), 256))];
    }
    for (const auto& channel : h.bins)
        h.peak = std::max(h.peak, *std::ranges::max_element(channel));
    return h;
}

ScopeGrid waveform(const ScopeImage& image, ScopeChannel channel, int columns, int levels)
{
    ScopeGrid grid{columns, levels, std::vector<std::uint32_t>(std::size_t(columns) * std::size_t(levels), 0), 0};
    if (image.width <= 0 || image.height <= 0)
        return grid;
    for (int y = 0; y < image.height; ++y) {
        for (int x = 0; x < image.width; ++x) {
            const std::size_t i = (std::size_t(y) * std::size_t(image.width) + std::size_t(x)) * 3;
            const float r = image.rgb[i], g = image.rgb[i + 1], b = image.rgb[i + 2];
            const float v = channel == ScopeChannel::Red     ? r
                          : channel == ScopeChannel::Green   ? g
                          : channel == ScopeChannel::Blue    ? b
                                                             : scopeLuma(r, g, b);
            const int column = std::min(columns - 1, x * columns / image.width);
            ++grid.counts[std::size_t(level(v, levels)) * std::size_t(columns) + std::size_t(column)];
        }
    }
    finish(grid);
    return grid;
}

std::array<float, 2> vectorscopePosition(float r, float g, float b, int size)
{
    // Rec. 709 colour differences, scaled so the primaries and secondaries (Cb, Cr up to
    // ±0.5) reach the edge of the circle.
    r = clamp01(r);
    g = clamp01(g);
    b = clamp01(b);
    const float y = scopeLuma(r, g, b);
    const float cb = (b - y) / 1.8556f;
    const float cr = (r - y) / 1.5748f;
    const float half = float(size) / 2.0f;
    return {half + cb * float(size), half + cr * float(size)};
}

ScopeGrid vectorscope(const ScopeImage& image, int size)
{
    ScopeGrid grid{size, size, std::vector<std::uint32_t>(std::size_t(size) * std::size_t(size), 0), 0};
    const std::size_t pixels = std::size_t(image.width) * std::size_t(image.height);
    for (std::size_t i = 0; i < pixels; ++i) {
        const auto p = vectorscopePosition(image.rgb[i * 3], image.rgb[i * 3 + 1], image.rgb[i * 3 + 2], size);
        const int x = std::clamp(int(p[0]), 0, size - 1);
        const int y = std::clamp(int(p[1]), 0, size - 1);
        ++grid.counts[std::size_t(y) * std::size_t(size) + std::size_t(x)];
    }
    finish(grid);
    return grid;
}

} // namespace tve
