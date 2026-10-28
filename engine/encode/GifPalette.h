#pragma once

#include <array>
#include <cstdint>

namespace tve {

// One frame's colours for a GIF: at most 256, as 0xAARRGGBB (what FFmpeg's PAL8 frames hold).
using GifPalette = std::array<std::uint32_t, 256>;

// Reduces an opaque RGBA frame to a palette of its own (median cut over its colours at 5 bits
// per channel) and writes each pixel's palette index, ordered (4 × 4 Bayer) dithering
// smoothing gradients. Returns how many palette entries are used; the rest are black.
int quantizeFrame(const std::uint8_t* rgba, int width, int height, int stride, std::uint8_t* indices, int indexStride,
                  GifPalette& palette);

} // namespace tve
