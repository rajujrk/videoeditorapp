#include "decode/AudioDecoder.h"
#include "decode/Thumbnail.h"
#include "decode/VideoDecoder.h"
#include "encode/GifPalette.h"
#include "encode/MediaWriter.h"

extern "C" {
#include <libavformat/avformat.h>
}

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <filesystem>
#include <numbers>
#include <vector>

using namespace tve;

namespace {

constexpr Ticks kSec = kTicksPerSecond;

std::string tempFile(const char* name)
{
    return (std::filesystem::temp_directory_path() / name).string();
}

// NV12 frame of one BT.709 video-range colour.
struct Nv12 {
    std::vector<std::uint8_t> luma;
    std::vector<std::uint8_t> chroma;
    Nv12(int w, int h, std::uint8_t y, std::uint8_t cb, std::uint8_t cr)
        : luma(std::size_t(w * h), y), chroma(std::size_t(w * h / 2))
    {
        for (std::size_t i = 0; i < chroma.size(); i += 2) {
            chroma[i] = cb;
            chroma[i + 1] = cr;
        }
    }
};

// 1 s red, 1 s green at 30 fps with a 440 Hz tone; checks it reads back.
void roundTrip(VideoCodec codec, const char* name)
{
    const std::string path = tempFile(name);
    MediaSettings settings;
    settings.width = 320;
    settings.height = 180;
    settings.codec = codec;
    std::string error;
    auto writer = MediaWriter::open(path, settings, &error);
    INFO(error);
    REQUIRE(writer);

    const Nv12 red(320, 180, 63, 102, 240);
    const Nv12 green(320, 180, 173, 42, 26);
    std::vector<float> tone(std::size_t(audio::kSampleRate / 30) * 2);
    std::int64_t sample = 0;
    for (int frame = 0; frame < 60; ++frame) {
        const Nv12& picture = frame < 30 ? red : green;
        REQUIRE(writer->writeVideo(picture.luma.data(), 320, picture.chroma.data(), 320));
        for (std::size_t i = 0; i < tone.size() / 2; ++i, ++sample) {
            const float v = 0.5f * float(std::sin(2.0 * std::numbers::pi * 440.0 * double(sample) / audio::kSampleRate));
            tone[i * 2] = v;
            tone[i * 2 + 1] = v;
        }
        REQUIRE(writer->writeAudio(tone.data(), int(tone.size() / 2)));
    }
    REQUIRE(writer->finish());
    CHECK(writer->videoFramesWritten() == 60);

    auto decoder = VideoDecoder::open(path, &error);
    INFO(error);
    REQUIRE(decoder);
    CHECK(decoder->info().width == 320);
    CHECK(decoder->info().height == 180);
    int frames = 0;
    while (decoder->nextFrame())
        ++frames;
    CHECK(frames == 60);

    auto first = extractThumbnail(path, kSec / 2, 90);
    auto second = extractThumbnail(path, 3 * kSec / 2, 90);
    REQUIRE(first);
    REQUIRE(second);
    const auto* a = &first->pixels[std::size_t((45 * first->width + 80) * 4)];
    const auto* b = &second->pixels[std::size_t((45 * second->width + 80) * 4)];
    CHECK(a[0] > 200);
    CHECK(a[1] < 50);
    CHECK(b[1] > 200);
    CHECK(b[0] < 50);

    auto sound = AudioDecoder::open(path, &error);
    INFO(error);
    REQUIRE(sound);
    REQUIRE(sound->seek(kSec / 2));
    std::vector<float> read(4800 * 2);
    REQUIRE(sound->read(read.data(), 4800) == 4800);
    float peak = 0;
    for (float v : read)
        peak = std::max(peak, std::abs(v));
    CHECK(peak == Catch::Approx(0.5f).margin(0.03f));
    std::filesystem::remove(path);
}

// A frame of one colour as RGBA rows.
std::vector<std::uint8_t> solidRgba(int w, int h, std::uint8_t r, std::uint8_t g, std::uint8_t b)
{
    std::vector<std::uint8_t> rgba(std::size_t(w * h * 4));
    for (std::size_t i = 0; i < rgba.size(); i += 4) {
        rgba[i] = r;
        rgba[i + 1] = g;
        rgba[i + 2] = b;
        rgba[i + 3] = 255;
    }
    return rgba;
}

// Writes 1 s red and 1 s green as RGBA (with a tone when the file has sound) and checks the
// pictures read back: frame count, size, and colours half way through each second.
void roundTripRgba(MediaSettings settings, const char* name, int expectedFrames)
{
    const std::string path = tempFile(name);
    std::string error;
    auto writer = MediaWriter::open(path, settings, &error);
    INFO(error);
    REQUIRE(writer);
    // Apple silicon encodes ProRes in hardware; the software encoder is only a fallback.
    if (settings.codec == VideoCodec::ProRes)
        CHECK(writer->videoEncoder() == "prores_videotoolbox");
    const int w = settings.width;
    const int h = settings.height;
    const auto red = solidRgba(w, h, 220, 20, 20);
    const auto green = solidRgba(w, h, 20, 200, 40);
    const int fps = settings.frameRate.num / settings.frameRate.den;
    std::vector<float> tone(std::size_t(audio::kSampleRate / fps) * 2, 0.25f);
    for (int frame = 0; frame < 2 * fps; ++frame) {
        REQUIRE(writer->writeVideoRgba((frame < fps ? red : green).data(), w * 4));
        if (settings.hasAudio())
            REQUIRE(writer->writeAudio(tone.data(), int(tone.size() / 2)));
    }
    REQUIRE(writer->finish());

    auto decoder = VideoDecoder::open(path, &error);
    INFO(error);
    REQUIRE(decoder);
    CHECK(decoder->info().width == w);
    CHECK(decoder->info().height == h);
    int frames = 0;
    while (decoder->nextFrame())
        ++frames;
    CHECK(frames == expectedFrames);
    auto first = extractThumbnail(path, kSec / 2, h / 2 * 2);
    auto second = extractThumbnail(path, 3 * kSec / 2, h / 2 * 2);
    REQUIRE(first);
    REQUIRE(second);
    const auto* a = &first->pixels[std::size_t(((first->height / 2) * first->width + first->width / 2) * 4)];
    const auto* b = &second->pixels[std::size_t(((second->height / 2) * second->width + second->width / 2) * 4)];
    CHECK(a[0] > 180);
    CHECK(a[1] < 60);
    CHECK(b[1] > 160);
    CHECK(b[0] < 60);
    std::filesystem::remove(path);
}

// Writes one second of a 440 Hz tone at half scale and reads it back.
void soundRoundTrip(Container container, AudioCodec codec, const char* name, const char* expectedCodec)
{
    const std::string path = tempFile(name);
    MediaSettings settings;
    settings.container = container;
    settings.audioCodec = codec;
    std::string error;
    auto writer = MediaWriter::open(path, settings, &error);
    INFO(name << ": " << error);
    REQUIRE(writer);
    CHECK_FALSE(writer->writeVideoRgba(nullptr, 0)); // no pictures in a sound file
    std::vector<float> tone(std::size_t(audio::kSampleRate) * 2);
    for (std::size_t i = 0; i < tone.size() / 2; ++i) {
        const float v = 0.5f * float(std::sin(2.0 * std::numbers::pi * 440.0 * double(i) / audio::kSampleRate));
        tone[i * 2] = v;
        tone[i * 2 + 1] = v;
    }
    // In uneven pieces, as the mixer's chunks come.
    for (std::size_t at = 0; at < tone.size() / 2;) {
        const int frames = int(std::min<std::size_t>(1000, tone.size() / 2 - at));
        REQUIRE(writer->writeAudio(tone.data() + at * 2, frames));
        at += std::size_t(frames);
    }
    REQUIRE(writer->finish());

    AVFormatContext* format = nullptr;
    REQUIRE(avformat_open_input(&format, path.c_str(), nullptr, nullptr) == 0);
    REQUIRE(avformat_find_stream_info(format, nullptr) >= 0);
    REQUIRE(format->nb_streams == 1);
    CHECK(std::string(avcodec_get_name(format->streams[0]->codecpar->codec_id)) == expectedCodec);
    avformat_close_input(&format);

    auto sound = AudioDecoder::open(path, &error);
    INFO(error);
    REQUIRE(sound);
    CHECK(toSeconds(sound->info().duration) == Catch::Approx(1.0).margin(0.05));
    REQUIRE(sound->seek(kSec / 2));
    std::vector<float> read(4800 * 2);
    REQUIRE(sound->read(read.data(), 4800) == 4800);
    float peak = 0;
    for (float v : read)
        peak = std::max(peak, std::abs(v));
    // PCM and FLAC are exact to their bits; AAC close.
    CHECK(peak == Catch::Approx(0.5f).margin(codec == AudioCodec::Aac ? 0.03f : 0.001f));
    std::filesystem::remove(path);
}

} // namespace

TEST_CASE("an H.264 MP4 reads back with its pictures and sound")
{
    roundTrip(VideoCodec::H264, "tve_writer_h264.mp4");
}

TEST_CASE("an HEVC MP4 reads back with its pictures and sound")
{
    roundTrip(VideoCodec::Hevc, "tve_writer_hevc.mp4");
}

TEST_CASE("chapters are written into the file")
{
    const std::string path = tempFile("tve_writer_chapters.mp4");
    MediaSettings settings;
    settings.width = 160;
    settings.height = 90;
    settings.audio = false;
    settings.chapters = {{0, kSec, "Opening"}, {kSec, 3 * kSec / 2, "The middle"}, {3 * kSec / 2, 2 * kSec, "Ending"}};
    std::string error;
    auto writer = MediaWriter::open(path, settings, &error);
    INFO(error);
    REQUIRE(writer);
    const Nv12 grey(160, 90, 126, 128, 128);
    for (int frame = 0; frame < 60; ++frame)
        REQUIRE(writer->writeVideo(grey.luma.data(), 160, grey.chroma.data(), 160));
    REQUIRE(writer->finish());
    writer.reset();

    AVFormatContext* format = nullptr;
    REQUIRE(avformat_open_input(&format, path.c_str(), nullptr, nullptr) == 0);
    REQUIRE(avformat_find_stream_info(format, nullptr) >= 0);
    REQUIRE(format->nb_chapters == 3);
    const char* titles[] = {"Opening", "The middle", "Ending"};
    const double starts[] = {0.0, 1.0, 1.5};
    for (unsigned i = 0; i < 3; ++i) {
        const AVChapter* chapter = format->chapters[i];
        INFO("chapter " << i);
        const AVDictionaryEntry* title = av_dict_get(chapter->metadata, "title", nullptr, 0);
        REQUIRE(title);
        CHECK(std::string(title->value) == titles[i]);
        CHECK(double(chapter->start) * av_q2d(chapter->time_base) == Catch::Approx(starts[i]).margin(0.01));
    }
    // The chapter track holds text, not pictures: the video still reads back whole.
    avformat_close_input(&format);
    auto decoder = VideoDecoder::open(path, &error);
    REQUIRE(decoder);
    int frames = 0;
    while (decoder->nextFrame())
        ++frames;
    CHECK(frames == 60);
    std::filesystem::remove(path);
}

TEST_CASE("odd sizes and unwritable paths are refused")
{
    MediaSettings settings;
    settings.width = 321;
    std::string error;
    CHECK_FALSE(MediaWriter::open(tempFile("tve_odd.mp4"), settings, &error));
    CHECK(error.find("even") != std::string::npos);
    CHECK_FALSE(MediaWriter::open("/nonexistent/dir/out.mp4", MediaSettings{}, &error));
    CHECK(error.find("/nonexistent/dir/out.mp4") != std::string::npos);
}

TEST_CASE("default bitrates scale with picture size and codec")
{
    MediaSettings hd;
    CHECK(defaultVideoBitrate(hd) == 9'331'200); // 0.15 bit per pixel per frame at 1080p30
    hd.codec = VideoCodec::Hevc;
    CHECK(defaultVideoBitrate(hd) == 6'220'800);
}

TEST_CASE("ProRes in a MOV reads back with its pictures and sound")
{
    for (const ProResProfile profile : {ProResProfile::Proxy, ProResProfile::Hq, ProResProfile::P4444}) {
        INFO("profile " << int(profile));
        MediaSettings settings;
        settings.container = Container::Mov;
        settings.codec = VideoCodec::ProRes;
        settings.proRes = profile;
        settings.audioCodec = AudioCodec::Pcm24;
        settings.width = 320;
        settings.height = 180;
        CHECK(pictureInput(settings) == PictureInput::Rgba);
        roundTripRgba(settings, "tve_writer_prores.mov", 60);
        // Bigger profiles hold more.
        CHECK(defaultVideoBitrate(settings) > 0);
    }
    MediaSettings proxy;
    proxy.codec = VideoCodec::ProRes;
    proxy.proRes = ProResProfile::Proxy;
    MediaSettings hq = proxy;
    hq.proRes = ProResProfile::Hq;
    CHECK(defaultVideoBitrate(hq) > 4 * defaultVideoBitrate(proxy));
}

TEST_CASE("an H.264 MOV reads back")
{
    MediaSettings settings;
    settings.container = Container::Mov;
    settings.width = 320;
    settings.height = 180;
    std::string error;
    auto writer = MediaWriter::open(tempFile("tve_writer_h264.mov"), settings, &error);
    INFO(error);
    REQUIRE(writer);
    const Nv12 grey(320, 180, 126, 128, 128);
    for (int frame = 0; frame < 30; ++frame)
        REQUIRE(writer->writeVideo(grey.luma.data(), 320, grey.chroma.data(), 320));
    CHECK_FALSE(writer->writeVideoRgba(grey.luma.data(), 320)); // H.264 takes NV12
    REQUIRE(writer->finish());
    auto decoder = VideoDecoder::open(tempFile("tve_writer_h264.mov"), &error);
    REQUIRE(decoder);
    CHECK(decoder->info().codecName == "h264");
    std::filesystem::remove(tempFile("tve_writer_h264.mov"));
}

TEST_CASE("an animated GIF keeps its colours and timing")
{
    MediaSettings settings;
    settings.container = Container::Gif;
    settings.codec = VideoCodec::Gif;
    settings.width = 161; // any size
    settings.height = 91;
    settings.frameRate = {10, 1};
    CHECK_FALSE(settings.hasAudio());
    roundTripRgba(settings, "tve_writer.gif", 20);
}

TEST_CASE("a GIF palette follows the frame's colours")
{
    // A horizontal red-to-blue gradient over grey bars: more colours than a palette holds.
    constexpr int w = 256;
    constexpr int h = 64;
    std::vector<std::uint8_t> rgba(std::size_t(w * h * 4));
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            std::uint8_t* p = &rgba[std::size_t((y * w + x) * 4)];
            const bool bar = y >= 48;
            p[0] = bar ? std::uint8_t(x) : std::uint8_t(255 - x);
            p[1] = bar ? std::uint8_t(x) : std::uint8_t(y * 2);
            p[2] = bar ? std::uint8_t(x) : std::uint8_t(x);
            p[3] = 255;
        }
    std::vector<std::uint8_t> indices(std::size_t(w * h));
    GifPalette palette;
    const int used = quantizeFrame(rgba.data(), w, h, w * 4, indices.data(), w, palette);
    CHECK(used == 256);
    // Averaged over 4 × 4 blocks (what dithering evens out), each block keeps its colour.
    double worst = 0;
    for (int by = 0; by < h; by += 4)
        for (int bx = 0; bx < w; bx += 4) {
            double error[3] = {0, 0, 0};
            for (int y = by; y < by + 4; ++y)
                for (int x = bx; x < bx + 4; ++x) {
                    const std::uint32_t c = palette[indices[std::size_t(y * w + x)]];
                    const std::uint8_t* p = &rgba[std::size_t((y * w + x) * 4)];
                    error[0] += double((c >> 16) & 255) - p[0];
                    error[1] += double((c >> 8) & 255) - p[1];
                    error[2] += double(c & 255) - p[2];
                }
            for (double e : error)
                worst = std::max(worst, std::abs(e / 16));
        }
    CHECK(worst < 10);

    // Two colours need two entries, exactly.
    const auto two = solidRgba(8, 8, 10, 200, 30);
    std::vector<std::uint8_t> twoIndices(64);
    CHECK(quantizeFrame(two.data(), 8, 8, 32, twoIndices.data(), 8, palette) == 1);
    CHECK((palette[twoIndices[0]] & 0xFFFFFF) == ((12u << 16) | (204u << 8) | 28u)); // its 5-bit bin's centre
}

TEST_CASE("sound-only files read back: M4A, WAV, FLAC")
{
    soundRoundTrip(Container::M4a, AudioCodec::Aac, "tve_writer.m4a", "aac");
    soundRoundTrip(Container::Wav, AudioCodec::Pcm16, "tve_writer16.wav", "pcm_s16le");
    soundRoundTrip(Container::Wav, AudioCodec::Pcm24, "tve_writer24.wav", "pcm_s24le");
    soundRoundTrip(Container::Flac, AudioCodec::Flac, "tve_writer.flac", "flac");
}

TEST_CASE("formats that do not go together are refused")
{
    std::string error;
    const auto refused = [&](MediaSettings settings, const char* why) {
        INFO(why);
        CHECK(validateSettings(settings));
        CHECK_FALSE(MediaWriter::open(tempFile("tve_refused.out"), settings, &error));
    };
    MediaSettings proresMp4;
    proresMp4.codec = VideoCodec::ProRes;
    refused(proresMp4, "ProRes needs MOV");
    MediaSettings pcmMp4;
    pcmMp4.audioCodec = AudioCodec::Pcm16;
    refused(pcmMp4, "MP4 carries AAC");
    MediaSettings flacWav;
    flacWav.container = Container::Wav;
    flacWav.audioCodec = AudioCodec::Flac;
    refused(flacWav, "WAV carries PCM");
    MediaSettings silentWav;
    silentWav.container = Container::Wav;
    silentWav.audioCodec = AudioCodec::Pcm16;
    silentWav.audio = false;
    refused(silentWav, "a sound file needs sound");
    MediaSettings hugeGif;
    hugeGif.container = Container::Gif;
    hugeGif.codec = VideoCodec::Gif;
    hugeGif.width = 5000;
    refused(hugeGif, "GIFs are at most 4096 across");
    CHECK(std::string(fileExtension(Container::M4a)) == "m4a");

    MediaSettings wav;
    wav.container = Container::Wav;
    wav.audioCodec = AudioCodec::Pcm24;
    CHECK_FALSE(validateSettings(wav));
    CHECK(pictureInput(wav) == PictureInput::None);
    CHECK(audioBitrate(wav) == 2'304'000);
}
