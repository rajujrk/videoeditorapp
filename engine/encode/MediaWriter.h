#pragma once

#include "model/Project.h"
#include "model/Time.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace tve {

// What file it is. Mp4, Mov and Gif carry pictures; M4a, Wav, Flac and Mp3 only sound.
enum class Container { Mp4, Mov, Gif, M4a, Wav, Flac, Mp3 };
enum class VideoCodec { H264, Hevc, ProRes, Gif };
enum class ProResProfile { Proxy, Lt, Standard, Hq, P4444 };
// Pcm32Float: 32-bit floating-point PCM, the mix as it is (peaks over 0 dBFS kept, not clipped).
enum class AudioCodec { Aac, Pcm16, Pcm24, Flac, Mp3, Pcm32Float };
// How pictures go in: NV12 planes (H.264, HEVC), P010 planes (HDR HEVC) or 8-bit RGBA rows
// (ProRes, GIF).
enum class PictureInput { None, Nv12, P010, Rgba };

struct MediaSettings {
    Container container = Container::Mp4;
    int width = 1920;  // even, except for GIF
    int height = 1080; // even, except for GIF
    FrameRate frameRate{30, 1};
    VideoCodec codec = VideoCodec::H264;
    ProResProfile proRes = ProResProfile::Hq;
    // ProRes 4444 only: keep the pictures' transparency (straight-alpha RGBA in, an alpha
    // channel in the file).
    bool alpha = false;
    std::int64_t videoBitrate = 0; // bits per second (H.264, HEVC); 0 picks one from size and frame rate
    // H.264, HEVC: no second of pictures over this many bits (0: no ceiling). With videoBitrate
    // the same, the pictures cannot pass videoBitrate × length.
    std::int64_t maxVideoBitrate = 0;
    std::int64_t audioBitrate = 192'000; // AAC and MP3
    bool audio = true; // false: no sound track (GIF never has one)
    AudioCodec audioCodec = AudioCodec::Aac;
    int keyframeInterval = 0;      // frames between keyframes; 0: about two seconds
    std::vector<Chapter> chapters; // in order; MP4, MOV and M4A write them as the file's chapters, MP3 as ID3 chapters
    // HDR (PQ or HLG): 10-bit HEVC (Main 10), Rec.2020, tagged so, with the mastering display
    // (Rec.2020 primaries, D65, 0.0001–1000 nits) and, for PQ, the content light levels
    // measured as it is written. SDR: Rec.709.
    ProjectColour colour = ProjectColour::Sdr;
    // 360° video: equirectangular pictures, an MP4 marked so (Spherical Video V2: its sv3d
    // and st3d boxes), as 360° players and sites read it.
    bool spherical = false;

    bool hasVideo() const { return container == Container::Mp4 || container == Container::Mov || container == Container::Gif; }
    bool hasAudio() const { return audio && container != Container::Gif; }
};

// Nullopt if the container, codecs and size go together; otherwise why not.
std::optional<std::string> validateSettings(const MediaSettings& settings);
PictureInput pictureInput(const MediaSettings& settings);
// "mp4", "mov", "gif", "m4a", "wav", "flac" or "mp3".
const char* fileExtension(Container container);

// Picture bits per second for size estimates (and the bitrate H.264/HEVC use when
// videoBitrate is 0): 0.15 bit per pixel per frame for H.264 (9.3 Mbit/s for 1080p30), two
// thirds of that for HEVC (a quarter more for HDR); ProRes at its published rates scaled from
// 1080p30; GIF a rough guess.
std::int64_t defaultVideoBitrate(const MediaSettings& settings);
// Sound bits per second (0 without sound): AAC's and MP3's bitrate, PCM's exact rate, FLAC's typical one.
std::int64_t audioBitrate(const MediaSettings& settings);

// Writes a video or sound file: H.264 or HEVC (VideoToolbox) in MP4 or MOV; ProRes in MOV
// (VideoToolbox, or FFmpeg's encoder where VideoToolbox has none); an animated GIF (a palette
// of its own for each frame, ordered dithering, looping); sound as AAC, 16- or 24-bit PCM,
// FLAC or MP3 (LAME, with its gapless header) at 48 kHz stereo; MP4, MOV and M4A with the index at the front (fast start) and any
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
    // One HDR frame in P010 (pictureInput P010): 10-bit codes in the high bits of 16-bit
    // samples, BT.2020 non-constant luminance, video range; strides in samples.
    bool writeVideoP010(const std::uint16_t* luma, int lumaStride, const std::uint16_t* chroma, int chromaStride);
    // One frame as 8-bit RGBA rows (pictureInput Rgba), BT.709 primaries; opaque unless the
    // settings keep transparency.
    bool writeVideoRgba(const std::uint8_t* rgba, int stride);
    // Interleaved stereo float samples at 48 kHz.
    bool writeAudio(const float* samples, int frames);

    // Flushes the encoders and writes the index. Without it the file is incomplete.
    bool finish();

    std::int64_t videoFramesWritten() const;
    // A PQ file's content light levels so far, in nits: the brightest pixel (its brightest
    // channel) and the brightest frame average (MaxCLL, MaxFALL).
    std::pair<double, double> contentLightLevels() const;
    // The picture encoder in use, e.g. "prores_videotoolbox" (empty without pictures).
    std::string videoEncoder() const;
    std::string lastError() const;

private:
    struct Impl;
    explicit MediaWriter(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> m_impl;
};

} // namespace tve
