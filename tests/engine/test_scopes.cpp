#include "analysis/Scopes.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace tve;

namespace {

// Columns of solid colours, `height` pixels tall.
ScopeImage columns(const std::vector<std::array<float, 3>>& colors, int height = 4)
{
    ScopeImage image{int(colors.size()), height, {}};
    for (int y = 0; y < height; ++y)
        for (const auto& c : colors)
            image.rgb.insert(image.rgb.end(), c.begin(), c.end());
    return image;
}

} // namespace

TEST_CASE("histogram counts each level", "[scopes]")
{
    const ScopeImage image = columns({{0, 0, 0}, {1, 1, 1}, {1, 0, 0}, {0.5f, 0.5f, 0.5f}});
    const Histogram h = histogram(image);
    CHECK(h.bins[0][255] == 8); // white and red
    CHECK(h.bins[0][0] == 4);   // black
    CHECK(h.bins[0][128] == 4); // grey (0.5 → 128)
    CHECK(h.bins[1][0] == 8);   // black and red
    CHECK(h.bins[3][255] == 4); // white only
    CHECK(h.bins[3][54] == 4);  // red's luma 0.2126
    CHECK(h.peak == 8);
}

TEST_CASE("waveform keeps each column's levels", "[scopes]")
{
    // Left half black, right half white: two flat lines.
    const ScopeImage image = columns({{0, 0, 0}, {0, 0, 0}, {1, 1, 1}, {1, 1, 1}});
    const ScopeGrid luma = waveform(image, ScopeChannel::Luma, 4, 256);
    CHECK(luma.at(0, 0) == 4);
    CHECK(luma.at(1, 0) == 4);
    CHECK(luma.at(2, 255) == 4);
    CHECK(luma.at(3, 255) == 4);
    CHECK(luma.at(2, 0) == 0);
    CHECK(luma.peak == 4);
    // Squeezed into fewer columns, neighbours share one.
    const ScopeGrid squeezed = waveform(image, ScopeChannel::Luma, 2, 16);
    CHECK(squeezed.at(0, 0) == 8);
    CHECK(squeezed.at(1, 15) == 8);
    // Per channel (the parade): red shows only where there is red.
    const ScopeGrid red = waveform(columns({{1, 0, 0}, {0, 0, 1}}), ScopeChannel::Red, 2, 2);
    CHECK(red.at(0, 1) == 4);
    CHECK(red.at(1, 0) == 4);
    CHECK(waveform(ScopeImage{}, ScopeChannel::Luma, 8, 8).peak == 0);
}

TEST_CASE("vectorscope places colours by hue and saturation", "[scopes]")
{
    const auto grey = vectorscopePosition(0.5f, 0.5f, 0.5f);
    CHECK(grey[0] == Catch::Approx(128.0f));
    CHECK(grey[1] == Catch::Approx(128.0f));
    // Pure red: Cr at its maximum, Cb slightly negative.
    const auto red = vectorscopePosition(1, 0, 0);
    CHECK(red[1] == Catch::Approx(256.0f).margin(0.5));
    CHECK(red[0] < 128.0f);
    // Pure blue: Cb at its maximum.
    const auto blue = vectorscopePosition(0, 0, 1);
    CHECK(blue[0] == Catch::Approx(256.0f).margin(0.5));
    // Opposite colours land opposite each other.
    const auto cyan = vectorscopePosition(0, 1, 1);
    CHECK(cyan[0] - 128.0f == Catch::Approx(128.0f - red[0]).margin(1e-3));
    CHECK(cyan[1] - 128.0f == Catch::Approx(128.0f - red[1]).margin(1e-3));

    const ScopeGrid grid = vectorscope(columns({{0.5f, 0.5f, 0.5f}, {0.2f, 0.2f, 0.2f}}), 256);
    CHECK(grid.at(128, 128) == 8); // every grey in the centre
    CHECK(grid.peak == 8);
}
