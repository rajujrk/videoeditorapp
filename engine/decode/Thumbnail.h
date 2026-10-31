#pragma once

#include "model/Time.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace tve {

struct RgbaImage {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> pixels; // width × height × 4, top row first
};

// Decodes the frame shown at `time` in a video file and scales it to `height` pixels high,
// keeping its aspect ratio. For small previews (media bin, timeline strips): nearest
// sampling, BT.601/709 by size.
std::optional<RgbaImage> extractThumbnail(const std::string& path, Ticks time, int height);

class VideoDecoder;
class DecodedFrame;
// Same, from an open decoder (reusing one saves opening the file and hardware session).
std::optional<RgbaImage> extractThumbnail(VideoDecoder& decoder, Ticks time, int height);
// A decoded frame scaled to `height` pixels high, the same way (for analysing every frame).
std::optional<RgbaImage> pictureOf(const DecodedFrame& frame, int height);

} // namespace tve
