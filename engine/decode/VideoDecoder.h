#pragma once

#include "decode/DecodedFrame.h"
#include "model/Time.h"

#include <memory>
#include <optional>
#include <string>

namespace tve {

struct VideoStreamInfo {
    int width = 0;
    int height = 0;
    FrameRate frameRate;
    Ticks duration = 0; // 0 if the container does not say
    std::string codecName;
    bool hardwareAccelerated = false;
};

// Decodes the best video stream of a file. Uses VideoToolbox when the codec supports it
// and falls back to FFmpeg's software decoders, converting their output to NV12 so every
// frame reaches the renderer in the same GPU-friendly form.
class VideoDecoder {
public:
    static std::unique_ptr<VideoDecoder> open(const std::string& path, std::string* error = nullptr);
    ~VideoDecoder();

    VideoDecoder(const VideoDecoder&) = delete;
    VideoDecoder& operator=(const VideoDecoder&) = delete;

    const VideoStreamInfo& info() const;

    // Returns the next frame in presentation order, with pts relative to the stream start,
    // or nullopt at end of stream or on an unrecoverable error.
    std::optional<DecodedFrame> nextFrame();

    bool seekToStart();

private:
    struct Impl;
    explicit VideoDecoder(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> m_impl;
};

} // namespace tve
