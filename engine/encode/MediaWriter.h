#pragma once

#include "model/Project.h"
#include "model/Time.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace tve {

// What file it is. Mp4, Mov and Gif carry pictures; M4a, Wav and Flac only sound.
enum class Container { Mp4, Mov, Gif, M4a, Wav, Flac };
enum class VideoCodec { H264, Hevc, ProRes, Gif };
enum class ProResProfile { Proxy, Lt, Standard, Hq, P4444 };
enum class AudioCodec { Aac, Pcm16, Pcm24, Flac };
// How pictures go in: NV12 planes (H.264, HEVC) or 8-bit RGBA rows (ProRes, GIF).
enum class PictureInput { None, Nv12, Rgba };

struct MediaSettings {
    Container container = Container::Mp4;
    int width = 1920;  // even, except for GIF
    int height = 1080; // even, except for GIF
    FrameRate frameRate{30, 1};
    VideoCodec codec = VideoCodec::H264;
    ProResProfile proRes = ProResProfile::Hq;
    std::int64_t videoBitrate = 0; // bits per second (H.264, HEVC); 0 picks one from size and frame rate
    std::int64_t audioBitrate = 192'000; // AAC
    bool audio = true; // false: no sound track (GIF never has one)
    AudioCodec audioCodec = AudioCodec::Aac;
    int keyframeInterval = 0;      // frames between keyframes; 0: about two seconds
    std::vector<Chapter> chapters; // in order; MP4, MOV and M4A write them as the file's chapters

    bool hasVideo() const { return container == Container::Mp4 || container == Container::Mov || container == Container::Gif; }
    bool hasAudio() const { return audio && container != Container::Gif; }
};

// Nullopt if the container, codecs and size go together; otherwise why not.
std::optional<std::string> validateSettings(const MediaSettings& settings);
PictureInput pictureInput(const MediaSettings& settings);
// "mp4", "mov", "gif", "m4a", "wav" or "flac".
const char* fileExtension(Container container);

// Picture bits per second for size estimates (and the bitrate H.264/HEVC use when
// videoBitrate is 0): 0.15 bit per pixel per frame for H.264 (9.3 Mbit/s for 1080p30), two
// thirds of that for HEVC; ProRes at its published rates scaled from 1080p30; GIF a rough guess.
std::int64_t defaultVideoBitrate(const MediaSettings& settings);
// Sound bits per second (0 without sound): AAC's bitrate, PCM's exact rate, FLAC's typical one.
std::int64_t audioBitrate(const MediaSettings& settings);

// Writes a video or sound file: H.264 or HEVC (VideoToolbox) in MP4 or MOV; ProRes in MOV
// (VideoToolbox, or FFmpeg's encoder where VideoToolbox has none); an animated GIF (a palette
// of its own for each frame, ordered dithering, looping); sound as AAC, 16- or 24-bit PCM or
// FLAC at 48 kHz stereo; MP4, MOV and M4A with the index at the front (fast start) and any
// chapters. Pictures go in one per frame period, in order; sound in any amount at a time.
// Not thread-safe.
class MediaWriter {
public:
    ~MediaWriter();
    MediaWriter(const MediaWriter&) = delete;
    MediaWriter& operator=(const MediaWriter&) = delete;

    static std::unique_ptr<MediaWriter> open(const std::string& path, const MediaSettings& settings,
                                             std::string* error = nullptr);

    const MediaSettings& settings() const;

    // One frame in NV12 (pictureInput Nv12): a full-size luma plane and a half-size
    // interleaved CbCr plane (BT.709, video range).
    bool writeVideo(const std::uint8_t* luma, int lumaStride, const std::uint8_t* chroma, int chromaStride);
    // One frame as 8-bit RGBA rows (pictureInput Rgba), BT.709 primaries, opaque.
    bool writeVideoRgba(const std::uint8_t* rgba, int stride);
    // Interleaved stereo float samples at 48 kHz.
    bool writeAudio(const float* samples, int frames);

    // Flushes the encoders and writes the index. Without it the file is incomplete.
    bool finish();

    std::int64_t videoFramesWritten() const;
    // The picture encoder in use, e.g. "prores_videotoolbox" (empty without pictures).
    std::string videoEncoder() const;
    std::string lastError() const;

private:
    struct Impl;
    explicit MediaWriter(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> m_impl;
};

} // namespace tve
