#pragma once

#include "model/Time.h"

#include <cstdint>
#include <memory>
#include <string>

namespace tve {

enum class VideoCodec { H264, Hevc };

struct Mp4Settings {
    int width = 1920;  // even
    int height = 1080; // even
    FrameRate frameRate{30, 1};
    VideoCodec codec = VideoCodec::H264;
    std::int64_t videoBitrate = 0; // bits per second; 0 picks one from size and frame rate
    std::int64_t audioBitrate = 192'000;
    bool audio = true;         // false: a video-only file
    int keyframeInterval = 0;  // frames between keyframes; 0: about two seconds
};

// Bitrate used when Mp4Settings::videoBitrate is 0: 0.15 bit per pixel per frame for H.264
// (9.3 Mbit/s for 1080p30), two thirds of that for HEVC.
std::int64_t defaultVideoBitrate(const Mp4Settings& settings);

// Writes an MP4 file: H.264 or HEVC video encoded by VideoToolbox, and (unless turned off)
// AAC stereo sound at 48 kHz, with the index at the front (fast start). Frames go in in order, one per frame
// period; sound in any amount at a time. Not thread-safe.
class Mp4Writer {
public:
    ~Mp4Writer();
    Mp4Writer(const Mp4Writer&) = delete;
    Mp4Writer& operator=(const Mp4Writer&) = delete;

    static std::unique_ptr<Mp4Writer> open(const std::string& path, const Mp4Settings& settings,
                                           std::string* error = nullptr);

    const Mp4Settings& settings() const;

    // One video frame in NV12: a full-size luma plane and a half-size interleaved CbCr
    // plane (BT.709, video range).
    bool writeVideo(const std::uint8_t* luma, int lumaStride, const std::uint8_t* chroma, int chromaStride);
    // Interleaved stereo float samples at 48 kHz.
    bool writeAudio(const float* samples, int frames);

    // Flushes the encoders and writes the index. Without it the file is incomplete.
    bool finish();

    std::int64_t videoFramesWritten() const;
    std::string lastError() const;

private:
    struct Impl;
    explicit Mp4Writer(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> m_impl;
};

} // namespace tve
