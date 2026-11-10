// 360° video through the engine: an MP4 marked as spherical (Spherical Video V2) read back
// by FFmpeg and by our decoder.
#include "decode/VideoDecoder.h"
#include "encode/MediaWriter.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/spherical.h>
#include <libavutil/stereo3d.h>
}

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <vector>

using namespace tve;

namespace {

std::string write(const char* name, bool spherical, Container container = Container::Mp4)
{
    const std::string path = (std::filesystem::temp_directory_path() / name).string();
    MediaSettings settings;
    settings.container = container;
    settings.width = 512;
    settings.height = 256;
    settings.spherical = spherical;
    settings.audio = false;
    std::string error;
    auto writer = MediaWriter::open(path, settings, &error);
    INFO(error);
    REQUIRE(writer);
    const std::vector<std::uint8_t> luma(512 * 256, 120), chroma(512 * 128, 128);
    for (int i = 0; i < 10; ++i)
        REQUIRE(writer->writeVideo(luma.data(), 512, chroma.data(), 512));
    REQUIRE(writer->finish());
    return path;
}

const AVPacketSideData* sideData(AVFormatContext* format, AVPacketSideDataType type)
{
    const AVCodecParameters* par = format->streams[0]->codecpar;
    return av_packet_side_data_get(par->coded_side_data, par->nb_coded_side_data, type);
}

} // namespace

TEST_CASE("a 360° MP4 is marked equirectangular and mono, and reads back as 360°", "[apple]")
{
    const std::string path = write("tve-360.mp4", true);
    AVFormatContext* format = nullptr;
    REQUIRE(avformat_open_input(&format, path.c_str(), nullptr, nullptr) == 0);
    REQUIRE(avformat_find_stream_info(format, nullptr) >= 0);
    const AVPacketSideData* sphere = sideData(format, AV_PKT_DATA_SPHERICAL);
    REQUIRE(sphere);
    CHECK(reinterpret_cast<const AVSphericalMapping*>(sphere->data)->projection == AV_SPHERICAL_EQUIRECTANGULAR);
    const AVPacketSideData* stereo = sideData(format, AV_PKT_DATA_STEREO3D);
    REQUIRE(stereo);
    CHECK(reinterpret_cast<const AVStereo3D*>(stereo->data)->type == AV_STEREO3D_2D);
    avformat_close_input(&format);

    auto decoder = VideoDecoder::open(path);
    REQUIRE(decoder);
    CHECK(decoder->info().spherical);
    std::filesystem::remove(path);
}

TEST_CASE("flat video isn't 360°, and 360° goes out only as MP4", "[apple]")
{
    const std::string path = write("tve-flat.mp4", false);
    auto decoder = VideoDecoder::open(path);
    REQUIRE(decoder);
    CHECK_FALSE(decoder->info().spherical);
    std::filesystem::remove(path);

    MediaSettings settings;
    settings.container = Container::Mov;
    settings.spherical = true;
    CHECK(validateSettings(settings) == "360° video goes out as MP4");
}
