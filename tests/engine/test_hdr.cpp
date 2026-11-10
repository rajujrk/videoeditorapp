// HDR through the engine: 10-bit HEVC files with their tags and mastering metadata, read
// back by FFmpeg and by our decoder (whose frames say their colour space); and the frame
// mirror that feeds the HDR viewer.
#include "decode/ColourTags.h"
#include "decode/Thumbnail.h"
#include "decode/VideoDecoder.h"
#include "encode/MediaWriter.h"
#include "playback/FrameMirror.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/mastering_display_metadata.h>
}

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <vector>

using namespace tve;
using Catch::Approx;

namespace {

std::string tempFile(const char* name)
{
    return (std::filesystem::temp_directory_path() / name).string();
}

// A 10-bit code in P010's high bits.
std::uint16_t p010(double code)
{
    return std::uint16_t(std::lround(code)) << 6;
}

// Left half bright (1000 nits for PQ), right half reference white (203 nits), neutral.
struct P010Frame {
    std::vector<std::uint16_t> luma;
    std::vector<std::uint16_t> chroma;
    P010Frame(int w, int h, double bright, double white) : luma(std::size_t(w * h)), chroma(std::size_t(w * h / 2), p010(512))
    {
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
                luma[std::size_t(y * w + x)] = p010(64 + 876 * (x < w / 2 ? bright : white));
    }
};

std::string writeHdr(ProjectColour colour, const char* name, double bright, double white)
{
    const std::string path = tempFile(name);
    MediaSettings settings;
    settings.width = 320;
    settings.height = 180;
    settings.codec = VideoCodec::Hevc;
    settings.colour = colour;
    settings.audio = false;
    std::string error;
    auto writer = MediaWriter::open(path, settings, &error);
    INFO(error);
    REQUIRE(writer);
    CHECK(pictureInput(settings) == PictureInput::P010);
    const P010Frame frame(320, 180, bright, white);
    for (int i = 0; i < 15; ++i)
        REQUIRE(writer->writeVideoP010(frame.luma.data(), 320, frame.chroma.data(), 320));
    REQUIRE(writer->finish());
    if (colour == ProjectColour::HdrPq) {
        const auto [maxCll, maxFall] = writer->contentLightLevels();
        CHECK(maxCll == Approx(1000.0).epsilon(0.02));
        CHECK(maxFall == Approx((1000.0 + 203.0) / 2).epsilon(0.03));
    }
    return path;
}

struct Probe {
    AVFormatContext* format = nullptr;
    AVCodecParameters* par = nullptr;
    explicit Probe(const std::string& path)
    {
        REQUIRE(avformat_open_input(&format, path.c_str(), nullptr, nullptr) == 0);
        REQUIRE(avformat_find_stream_info(format, nullptr) >= 0);
        const int index = av_find_best_stream(format, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
        REQUIRE(index >= 0);
        par = format->streams[index]->codecpar;
    }
    ~Probe() { avformat_close_input(&format); }
    const AVPacketSideData* side(AVPacketSideDataType type) const
    {
        return av_packet_side_data_get(par->coded_side_data, par->nb_coded_side_data, type);
    }
};

} // namespace

TEST_CASE("a PQ project's HEVC is 10-bit Rec.2020 PQ with its mastering display and light levels", "[apple]")
{
    const std::string path = writeHdr(ProjectColour::HdrPq, "tve-hdr-pq.mp4", pqFromNits(1000), pqFromNits(203));
    {
        const Probe probe(path);
        CHECK(probe.par->codec_id == AV_CODEC_ID_HEVC);
        CHECK(probe.par->profile == AV_PROFILE_HEVC_MAIN_10);
        CHECK(probe.par->format == AV_PIX_FMT_YUV420P10LE);
        CHECK(probe.par->color_primaries == AVCOL_PRI_BT2020);
        CHECK(probe.par->color_trc == AVCOL_TRC_SMPTE2084);
        CHECK(probe.par->color_space == AVCOL_SPC_BT2020_NCL);
        const AVPacketSideData* mastering = probe.side(AV_PKT_DATA_MASTERING_DISPLAY_METADATA);
        REQUIRE(mastering);
        const auto* display = reinterpret_cast<const AVMasteringDisplayMetadata*>(mastering->data);
        CHECK(display->has_luminance);
        CHECK(av_q2d(display->max_luminance) == Approx(1000.0));
        CHECK(av_q2d(display->min_luminance) == Approx(0.0001));
        CHECK(av_q2d(display->display_primaries[1][1]) == Approx(0.797)); // Rec.2020 green
        CHECK(av_q2d(display->white_point[0]) == Approx(0.3127));
        const AVPacketSideData* light = probe.side(AV_PKT_DATA_CONTENT_LIGHT_LEVEL);
        REQUIRE(light);
        const auto* levels = reinterpret_cast<const AVContentLightMetadata*>(light->data);
        CHECK(levels->MaxCLL == Approx(1000).epsilon(0.02));
        CHECK(levels->MaxFALL == Approx(600).epsilon(0.03));
    }

    // Our decoder: the stream and every frame say PQ Rec.2020; 10 bits come back.
    std::string error;
    auto decoder = VideoDecoder::open(path, &error);
    INFO(error);
    REQUIRE(decoder);
    CHECK(decoder->info().colour == ColourSpace{Primaries::Rec2020, Transfer::Pq});
    auto frame = decoder->nextFrame();
    REQUIRE(frame);
    CHECK(bufferColour(frame->pixelBuffer()) == ColourSpace{Primaries::Rec2020, Transfer::Pq});
    // And its YCbCr matrix, BT.2020's (the renderer turns it into RGB by it).
    CFTypeRef matrix = CVBufferCopyAttachment(frame->pixelBuffer(), kCVImageBufferYCbCrMatrixKey, nullptr);
    REQUIRE(matrix);
    CHECK(CFEqual(matrix, kCVImageBufferYCbCrMatrix_ITU_R_2020));
    CFRelease(matrix);
    const OSType format = CVPixelBufferGetPixelFormatType(frame->pixelBuffer());
    CHECK((format == kCVPixelFormatType_420YpCbCr10BiPlanarVideoRange
           || format == kCVPixelFormatType_420YpCbCr10BiPlanarFullRange));
    CVPixelBufferLockBaseAddress(frame->pixelBuffer(), kCVPixelBufferLock_ReadOnly);
    const auto* row = static_cast<const std::uint16_t*>(CVPixelBufferGetBaseAddressOfPlane(frame->pixelBuffer(), 0))
                    + (CVPixelBufferGetBytesPerRowOfPlane(frame->pixelBuffer(), 0) / 2) * 90;
    const double brightCode = double(row[40] >> 6);
    const double whiteCode = double(row[280] >> 6);
    CVPixelBufferUnlockBaseAddress(frame->pixelBuffer(), kCVPixelBufferLock_ReadOnly);
    CHECK(brightCode == Approx(64 + 876 * pqFromNits(1000)).margin(4));
    CHECK(whiteCode == Approx(64 + 876 * pqFromNits(203)).margin(4));
    // Its thumbnail is SDR: graphics white near SDR white, the highlight rolled off above it.
    const auto thumbnail = pictureOf(*frame, 18);
    REQUIRE(thumbnail);
    const auto at = [&](int x) { return int(thumbnail->pixels[std::size_t(9 * thumbnail->width + x) * 4 + 1]); };
    const double white = encodeTransfer(Transfer::Sdr, rollOff(1.0, 1.0)) * 255;
    CHECK(at(thumbnail->width - 4) == Approx(white).margin(4));
    CHECK(at(3) > at(thumbnail->width - 4));
    CHECK(at(3) >= 250);
    std::filesystem::remove(path);
}

TEST_CASE("an HLG project's HEVC is tagged HLG, with its mastering display", "[apple]")
{
    const std::string path = writeHdr(ProjectColour::HdrHlg, "tve-hdr-hlg.mov", 0.9, 0.75);
    {
        const Probe probe(path);
        CHECK(probe.par->profile == AV_PROFILE_HEVC_MAIN_10);
        CHECK(probe.par->color_trc == AVCOL_TRC_ARIB_STD_B67);
        CHECK(probe.par->color_primaries == AVCOL_PRI_BT2020);
        CHECK(probe.side(AV_PKT_DATA_MASTERING_DISPLAY_METADATA));
        CHECK_FALSE(probe.side(AV_PKT_DATA_CONTENT_LIGHT_LEVEL)); // (PQ's measure)
    }
    auto decoder = VideoDecoder::open(path);
    REQUIRE(decoder);
    CHECK(decoder->info().colour == ColourSpace{Primaries::Rec2020, Transfer::Hlg});
    std::filesystem::remove(path);
}

TEST_CASE("HDR goes out only as HEVC; SDR stays Rec.709 8-bit")
{
    MediaSettings settings;
    settings.colour = ProjectColour::HdrPq;
    settings.codec = VideoCodec::H264;
    CHECK(validateSettings(settings) == "HDR goes out as 10-bit HEVC");
    settings.container = Container::Mov;
    settings.codec = VideoCodec::ProRes;
    CHECK(validateSettings(settings));
    settings.colour = ProjectColour::Sdr;
    CHECK_FALSE(validateSettings(settings));
    settings.container = Container::Mp4;
    settings.codec = VideoCodec::Hevc;
    CHECK(pictureInput(settings) == PictureInput::Nv12);
    const std::int64_t sdrRate = defaultVideoBitrate(settings);
    settings.colour = ProjectColour::HdrHlg;
    CHECK(defaultVideoBitrate(settings) == std::int64_t(double(sdrRate) * 1.25 + 0.5));
}

TEST_CASE("files' colour tags read as colour spaces, untagged as SDR Rec.709")
{
    CHECK(colourFromTags(AVCOL_PRI_BT709, AVCOL_TRC_BT709) == ColourSpace{});
    CHECK(colourFromTags(AVCOL_PRI_UNSPECIFIED, AVCOL_TRC_UNSPECIFIED) == ColourSpace{});
    CHECK(colourFromTags(AVCOL_PRI_SMPTE432, AVCOL_TRC_IEC61966_2_1) == ColourSpace{Primaries::DisplayP3, Transfer::Sdr});
    CHECK(colourFromTags(AVCOL_PRI_BT2020, AVCOL_TRC_SMPTE2084) == ColourSpace{Primaries::Rec2020, Transfer::Pq});
    CHECK(colourFromTags(AVCOL_PRI_BT2020, AVCOL_TRC_ARIB_STD_B67) == ColourSpace{Primaries::Rec2020, Transfer::Hlg});
    // And on pixel buffers, both ways.
    CVPixelBufferRef buffer = nullptr;
    REQUIRE(CVPixelBufferCreate(nullptr, 16, 16, kCVPixelFormatType_32BGRA, nullptr, &buffer) == kCVReturnSuccess);
    CHECK(bufferColour(buffer) == ColourSpace{});
    for (const ColourSpace colour : {ColourSpace{Primaries::Rec2020, Transfer::Hlg}, ColourSpace{Primaries::DisplayP3, Transfer::Sdr},
                                     ColourSpace{Primaries::Rec2020, Transfer::Pq}, ColourSpace{}}) {
        tagBuffer(buffer, colour);
        CHECK(bufferColour(buffer) == colour);
    }
    CVPixelBufferRelease(buffer);
}

TEST_CASE("a mirror shows its leader's frames by the leader's clock, never holding it up")
{
    struct Leader final : FrameSource {
        FrameQueue frames{4};
        PlaybackClock ticking;
        PlaybackCounters numbers;
        FrameQueue& queue() override { return frames; }
        PlaybackClock& clock() override { return ticking; }
        PlaybackCounters& counters() override { return numbers; }
        bool hardwareAccelerated() const override { return true; }
        Size canvasSize() const override { return {1280, 720}; }
    };
    auto leader = std::make_shared<Leader>();
    auto other = std::make_shared<Leader>();
    FrameMirrors mirrors;
    auto mirror = std::make_shared<MirrorSource>(leader);
    mirrors.add(mirror);
    CHECK(&mirror->clock() == &leader->clock());
    CHECK(mirror->canvasSize().width == 1280);

    // Each frame the leader takes is offered; a full mirror drops its oldest, not blocking.
    for (Ticks t = 0; t < 10; ++t)
        mirrors.offer(leader.get(), CompositeFrame{t * 1000, {}, ProjectColour::HdrPq});
    mirrors.offer(other.get(), CompositeFrame{99'000, {}}); // another source's: not this mirror's
    int skipped = 0;
    auto latest = mirror->queue().takeLatestDue(1'000'000, skipped);
    REQUIRE(latest);
    CHECK(latest->time == 9000);
    CHECK(latest->colour == ProjectColour::HdrPq);
    CHECK(skipped == 2); // three kept of ten

    // Gone mirrors are forgotten.
    mirror.reset();
    mirrors.offer(leader.get(), CompositeFrame{});
}

TEST_CASE("a frame queue offer never blocks: the oldest frame makes room")
{
    BasicFrameQueue<CompositeFrame> queue(2);
    CHECK(queue.offer(CompositeFrame{1, {}}));
    CHECK(queue.offer(CompositeFrame{2, {}}));
    CHECK(queue.offer(CompositeFrame{3, {}}));
    CHECK(queue.frontPts() == 2);
    queue.stop();
    CHECK_FALSE(queue.offer(CompositeFrame{4, {}}));
}
