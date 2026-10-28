#include "decode/AudioDecoder.h"
#include "decode/Thumbnail.h"
#include "decode/VideoDecoder.h"
#include "encode/Mp4Writer.h"

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
    Mp4Settings settings;
    settings.width = 320;
    settings.height = 180;
    settings.codec = codec;
    std::string error;
    auto writer = Mp4Writer::open(path, settings, &error);
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
    Mp4Settings settings;
    settings.width = 160;
    settings.height = 90;
    settings.audio = false;
    settings.chapters = {{0, kSec, "Opening"}, {kSec, 3 * kSec / 2, "The middle"}, {3 * kSec / 2, 2 * kSec, "Ending"}};
    std::string error;
    auto writer = Mp4Writer::open(path, settings, &error);
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
    Mp4Settings settings;
    settings.width = 321;
    std::string error;
    CHECK_FALSE(Mp4Writer::open(tempFile("tve_odd.mp4"), settings, &error));
    CHECK(error.find("even") != std::string::npos);
    CHECK_FALSE(Mp4Writer::open("/nonexistent/dir/out.mp4", Mp4Settings{}, &error));
    CHECK(error.find("/nonexistent/dir/out.mp4") != std::string::npos);
}

TEST_CASE("default bitrates scale with picture size and codec")
{
    Mp4Settings hd;
    CHECK(defaultVideoBitrate(hd) == 9'331'200); // 0.15 bit per pixel per frame at 1080p30
    hd.codec = VideoCodec::Hevc;
    CHECK(defaultVideoBitrate(hd) == 6'220'800);
}
