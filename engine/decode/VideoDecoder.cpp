#include "decode/VideoDecoder.h"

#include "decode/ColourTags.h"
#include "decode/ImageFile.h"
#include "model/ImageSequence.h"

#include <algorithm>
#include <cstring>
#include <deque>
#include <vector>
#include <mutex>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/hwcontext.h>
#include <libavutil/intreadwrite.h>
#include <libavutil/pixdesc.h>
#include <libavutil/spherical.h>
#include <libswscale/swscale.h>
}

namespace tve {

namespace {

constexpr AVRational kTickTimeBase{1, static_cast<int>(kTicksPerSecond)};

std::string avError(int code)
{
    char buffer[AV_ERROR_MAX_STRING_SIZE]{};
    av_strerror(code, buffer, sizeof buffer);
    return buffer;
}

AVPixelFormat pickPixelFormat(AVCodecContext* context, const AVPixelFormat* formats)
{
    if (context->hw_device_ctx) {
        for (const AVPixelFormat* f = formats; *f != AV_PIX_FMT_NONE; ++f) {
            if (*f == AV_PIX_FMT_VIDEOTOOLBOX)
                return *f;
        }
    }
    // VideoToolbox cannot decode this stream (profile, bit depth…): use software.
    for (const AVPixelFormat* f = formats; *f != AV_PIX_FMT_NONE; ++f) {
        if (!(av_pix_fmt_desc_get(*f)->flags & AV_PIX_FMT_FLAG_HWACCEL))
            return *f;
    }
    return AV_PIX_FMT_NONE;
}

CFStringRef matrixAttachment(AVColorSpace space, int height)
{
    switch (space) {
    case AVCOL_SPC_BT709:
        return kCVImageBufferYCbCrMatrix_ITU_R_709_2;
    case AVCOL_SPC_BT2020_NCL:
    case AVCOL_SPC_BT2020_CL:
        return kCVImageBufferYCbCrMatrix_ITU_R_2020;
    case AVCOL_SPC_BT470BG:
    case AVCOL_SPC_SMPTE170M:
        return kCVImageBufferYCbCrMatrix_ITU_R_601_4;
    default:
        return height > 576 ? kCVImageBufferYCbCrMatrix_ITU_R_709_2 : kCVImageBufferYCbCrMatrix_ITU_R_601_4;
    }
}

int swsColorspace(CFStringRef matrix)
{
    if (CFEqual(matrix, kCVImageBufferYCbCrMatrix_ITU_R_709_2))
        return SWS_CS_ITU709;
    if (CFEqual(matrix, kCVImageBufferYCbCrMatrix_ITU_R_2020))
        return SWS_CS_BT2020;
    return SWS_CS_ITU601;
}

CVPixelBufferPoolRef createPool(int width, int height, int pixelFormat)
{
    CFNumberRef format = CFNumberCreate(nullptr, kCFNumberIntType, &pixelFormat);
    CFNumberRef w = CFNumberCreate(nullptr, kCFNumberIntType, &width);
    CFNumberRef h = CFNumberCreate(nullptr, kCFNumberIntType, &height);
    CFDictionaryRef ioSurface = CFDictionaryCreate(nullptr, nullptr, nullptr, 0, &kCFTypeDictionaryKeyCallBacks,
                                                   &kCFTypeDictionaryValueCallBacks);
    const void* keys[] = {kCVPixelBufferPixelFormatTypeKey, kCVPixelBufferWidthKey, kCVPixelBufferHeightKey,
                          kCVPixelBufferIOSurfacePropertiesKey, kCVPixelBufferMetalCompatibilityKey};
    const void* values[] = {format, w, h, ioSurface, kCFBooleanTrue};
    CFDictionaryRef attributes = CFDictionaryCreate(nullptr, keys, values, 5, &kCFTypeDictionaryKeyCallBacks,
                                                    &kCFTypeDictionaryValueCallBacks);
    CVPixelBufferPoolRef pool = nullptr;
    CVPixelBufferPoolCreate(nullptr, nullptr, attributes, &pool);
    CFRelease(attributes);
    CFRelease(ioSurface);
    CFRelease(h);
    CFRelease(w);
    CFRelease(format);
    return pool;
}

// The same layout with an alpha plane (the planes of a picture and its separate alpha).
AVPixelFormat withAlphaPlane(int format)
{
    switch (format) {
    case AV_PIX_FMT_YUV420P: return AV_PIX_FMT_YUVA420P;
    case AV_PIX_FMT_YUV422P: return AV_PIX_FMT_YUVA422P;
    case AV_PIX_FMT_YUV444P: return AV_PIX_FMT_YUVA444P;
    case AV_PIX_FMT_YUV420P10LE: return AV_PIX_FMT_YUVA420P10LE;
    case AV_PIX_FMT_YUV422P10LE: return AV_PIX_FMT_YUVA422P10LE;
    case AV_PIX_FMT_YUV444P10LE: return AV_PIX_FMT_YUVA444P10LE;
    default: return AV_PIX_FMT_NONE;
    }
}

// An HEVC stream with more layers than its base one (its VPS says so): an alpha layer, as
// Apple's "HEVC with alpha" writes, or a second view. The parameter sets are in the
// extradata, as an hvcC record or as Annex B start codes.
bool hevcHasLayers(const std::uint8_t* data, int size)
{
    const auto layersOf = [](const std::uint8_t* nal, int length) {
        // NAL header (2 bytes), then vps_video_parameter_set_id (4 bits), two flags, and
        // vps_max_layers_minus1 (6 bits).
        if (length < 4 || ((nal[0] >> 1) & 0x3f) != 32)
            return false;
        return (((nal[2] & 0x03) << 4) | (nal[3] >> 4)) > 0;
    };
    if (!data || size < 4)
        return false;
    if (data[0] == 1 && size > 23) { // hvcC
        int at = 23;
        const int arrays = data[22];
        for (int a = 0; a < arrays && at + 3 <= size; ++a) {
            const int count = AV_RB16(data + at + 1);
            at += 3;
            for (int n = 0; n < count && at + 2 <= size; ++n) {
                const int length = AV_RB16(data + at);
                at += 2;
                if (at + length > size)
                    return false;
                if (layersOf(data + at, length))
                    return true;
                at += length;
            }
        }
        return false;
    }
    for (int i = 0; i + 3 < size; ++i) // Annex B
        if (data[i] == 0 && data[i + 1] == 0 && data[i + 2] == 1 && layersOf(data + i + 3, size - i - 3))
            return true;
    return false;
}

} // namespace

struct VideoDecoder::Impl {
    AVFormatContext* format = nullptr;
    AVCodecContext* codec = nullptr;
    AVBufferRef* hwDevice = nullptr;
    AVPacket* packet = nullptr;
    AVFrame* frame = nullptr;
    SwsContext* sws = nullptr;
    CVPixelBufferPoolRef pool = nullptr;
    int poolWidth = 0;
    int poolHeight = 0;
    int poolFormat = 0;

    int streamIndex = -1;
    AVRational timeBase{};
    std::int64_t startTime = 0;
    bool inputDone = false;
    Ticks lastPts = 0;
    Ticks discardBefore = -1; // after a seek: drop frames that end at or before this time
    VideoStreamInfo info;

    // VP9 with transparency (WebM): the alpha is a second VP9 stream, a picture per frame in
    // its blocks' additional data, decoded alongside; its pictures' luma is the alpha.
    AVCodecContext* alphaCodec = nullptr;
    AVPacket* alphaPacket = nullptr;
    std::deque<AVFrame*> alphaFrames; // decoded, waiting for their picture (matched by pts)

    // An image sequence (model/ImageSequence.h): a picture file a frame, read by ImageIO,
    // with no FFmpeg context at all. Opaque pictures become NV12, like any video; ones with
    // transparency BGRA.
    std::vector<std::string> sequenceFiles;
    std::size_t sequenceNext = 0;
    std::vector<std::uint8_t> sequencePixels; // one picture as BGRA, before NV12
    bool isSequence() const { return !sequenceFiles.empty(); }
    std::optional<DecodedFrame> sequenceFrame();

    ~Impl()
    {
        clearAlpha();
        av_packet_free(&alphaPacket);
        avcodec_free_context(&alphaCodec);
        sws_freeContext(sws);
        if (pool)
            CVPixelBufferPoolRelease(pool);
        av_frame_free(&frame);
        av_packet_free(&packet);
        avcodec_free_context(&codec);
        av_buffer_unref(&hwDevice);
        avformat_close_input(&format);
    }

    Ticks framePts() const;
    std::optional<DecodedFrame> takeFrame(Ticks pts);
    CVPixelBufferRef convertToYuv();
    CVPixelBufferRef convertToBgra();
    bool ensurePool(int pixelFormat);
    bool ensurePool(int pixelFormat, int width, int height);
    // The alpha stream: fed a packet's additional data, drained into alphaFrames.
    void sendAlpha(const AVPacket* source);
    void receiveAlpha();
    const AVFrame* alphaFor(std::int64_t pts);
    void clearAlpha();
    // The pixel format of the stream's first picture (decoded, then back to the start).
    AVPixelFormat firstPictureFormat();
};

void VideoDecoder::Impl::sendAlpha(const AVPacket* source)
{
    std::size_t size = 0;
    const std::uint8_t* data = av_packet_get_side_data(source, AV_PKT_DATA_MATROSKA_BLOCKADDITIONAL, &size);
    // The BlockAddID (8 bytes, big-endian) comes first; 1 is the alpha.
    if (!data || size <= 8 || AV_RB64(data) != 1)
        return;
    if (av_new_packet(alphaPacket, int(size - 8)) < 0)
        return;
    std::memcpy(alphaPacket->data, data + 8, size - 8);
    alphaPacket->pts = source->pts;
    alphaPacket->dts = source->dts;
    alphaPacket->flags = source->flags;
    avcodec_send_packet(alphaCodec, alphaPacket);
    av_packet_unref(alphaPacket);
    receiveAlpha();
}

void VideoDecoder::Impl::receiveAlpha()
{
    while (true) {
        AVFrame* decoded = av_frame_alloc();
        if (!decoded || avcodec_receive_frame(alphaCodec, decoded) < 0) {
            av_frame_free(&decoded);
            return;
        }
        alphaFrames.push_back(decoded);
        while (alphaFrames.size() > 64) { // (pictures that never came)
            av_frame_free(&alphaFrames.front());
            alphaFrames.pop_front();
        }
    }
}

const AVFrame* VideoDecoder::Impl::alphaFor(std::int64_t pts)
{
    // Earlier ones belong to pictures already gone (or dropped after a seek).
    while (!alphaFrames.empty() && alphaFrames.front()->pts != AV_NOPTS_VALUE && alphaFrames.front()->pts < pts) {
        av_frame_free(&alphaFrames.front());
        alphaFrames.pop_front();
    }
    for (AVFrame* candidate : alphaFrames)
        if (candidate->pts == pts)
            return candidate;
    return nullptr;
}

void VideoDecoder::Impl::clearAlpha()
{
    for (AVFrame*& f : alphaFrames)
        av_frame_free(&f);
    alphaFrames.clear();
}

AVPixelFormat VideoDecoder::Impl::firstPictureFormat()
{
    AVPixelFormat found = AV_PIX_FMT_NONE;
    AVPacket* probe = av_packet_alloc();
    AVFrame* picture = av_frame_alloc();
    for (int packets = 0; probe && picture && found == AV_PIX_FMT_NONE && packets < 300; ++packets) {
        const bool more = av_read_frame(format, probe) >= 0;
        if (more && probe->stream_index != streamIndex) {
            av_packet_unref(probe);
            continue;
        }
        avcodec_send_packet(codec, more ? probe : nullptr);
        av_packet_unref(probe);
        if (avcodec_receive_frame(codec, picture) == 0) {
            found = static_cast<AVPixelFormat>(picture->format);
            av_frame_unref(picture);
        }
        if (!more)
            break;
    }
    av_frame_free(&picture);
    av_packet_free(&probe);
    av_seek_frame(format, streamIndex, startTime, AVSEEK_FLAG_BACKWARD);
    avcodec_flush_buffers(codec);
    return found;
}

Ticks VideoDecoder::Impl::framePts() const
{
    std::int64_t timestamp = frame->best_effort_timestamp;
    if (timestamp == AV_NOPTS_VALUE)
        timestamp = frame->pts;
    return timestamp == AV_NOPTS_VALUE ? lastPts + frameDuration(info.frameRate)
                                       : av_rescale_q(timestamp - startTime, timeBase, kTickTimeBase);
}

std::optional<DecodedFrame> VideoDecoder::Impl::takeFrame(Ticks pts)
{

    // Each picture says its colour space (the frame's own tags, else the stream's).
    const ColourSpace tagged = frame->color_primaries != AVCOL_PRI_UNSPECIFIED || frame->color_trc != AVCOL_TRC_UNSPECIFIED
                                   ? colourFromTags(frame->color_primaries, frame->color_trc)
                                   : info.colour;
    if (frame->format == AV_PIX_FMT_VIDEOTOOLBOX) {
        info.hardwareAccelerated = true;
        auto buffer = reinterpret_cast<CVPixelBufferRef>(frame->data[3]);
        CVPixelBufferRetain(buffer);
        tagBuffer(buffer, tagged);
        return DecodedFrame(buffer, pts);
    }

    info.hardwareAccelerated = false;
    CVPixelBufferRef buffer = info.hasAlpha ? convertToBgra() : convertToYuv();
    if (!buffer)
        return std::nullopt;
    tagBuffer(buffer, tagged);
    return DecodedFrame(buffer, pts);
}

bool VideoDecoder::Impl::ensurePool(int pixelFormat)
{
    return ensurePool(pixelFormat, frame->width, frame->height);
}

bool VideoDecoder::Impl::ensurePool(int pixelFormat, int width, int height)
{
    if (!pool || poolWidth != width || poolHeight != height || poolFormat != pixelFormat) {
        if (pool)
            CVPixelBufferPoolRelease(pool);
        pool = createPool(width, height, pixelFormat);
        poolWidth = width;
        poolHeight = height;
        poolFormat = pixelFormat;
    }
    return pool != nullptr;
}

std::optional<DecodedFrame> VideoDecoder::Impl::sequenceFrame()
{
    if (sequenceNext >= sequenceFiles.size())
        return std::nullopt;
    const std::size_t index = sequenceNext++;
    const Ticks pts = Ticks(index) * frameDuration(info.frameRate);
    lastPts = pts;
    const int width = info.width;
    const int height = info.height;
    CVPixelBufferRef buffer = nullptr;
    if (info.hasAlpha) {
        // Drawn straight into the frame.
        if (!ensurePool(kCVPixelFormatType_32BGRA, width, height)
            || CVPixelBufferPoolCreatePixelBuffer(nullptr, pool, &buffer) != kCVReturnSuccess)
            return std::nullopt;
        CVPixelBufferLockBaseAddress(buffer, 0);
        const bool ok = imagefile::decodeInto(sequenceFiles[index], width, height, imagefile::Layout::Bgra,
                                              static_cast<std::uint8_t*>(CVPixelBufferGetBaseAddress(buffer)),
                                              CVPixelBufferGetBytesPerRow(buffer));
        CVPixelBufferUnlockBaseAddress(buffer, 0);
        if (!ok) { // an unreadable frame shows black and clear, not the end of the clip
            CVPixelBufferLockBaseAddress(buffer, 0);
            std::memset(CVPixelBufferGetBaseAddress(buffer), 0, CVPixelBufferGetBytesPerRow(buffer) * std::size_t(height));
            CVPixelBufferUnlockBaseAddress(buffer, 0);
        }
    } else {
        const std::size_t stride = std::size_t(width) * 4;
        sequencePixels.resize(stride * std::size_t(height));
        if (!imagefile::decodeInto(sequenceFiles[index], width, height, imagefile::Layout::Bgra, sequencePixels.data(),
                                   stride))
            std::fill(sequencePixels.begin(), sequencePixels.end(), std::uint8_t(0));
        if (!ensurePool(kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange, width, height)
            || CVPixelBufferPoolCreatePixelBuffer(nullptr, pool, &buffer) != kCVReturnSuccess)
            return std::nullopt;
        // sRGB pictures as BT.709 video range, as the compositor reads untagged NV12.
        sws = sws_getCachedContext(sws, width, height, AV_PIX_FMT_BGRA, width, height, AV_PIX_FMT_NV12, SWS_BILINEAR,
                                   nullptr, nullptr, nullptr);
        if (!sws) {
            CVPixelBufferRelease(buffer);
            return std::nullopt;
        }
        const int* bt709 = sws_getCoefficients(SWS_CS_ITU709);
        sws_setColorspaceDetails(sws, bt709, 1, bt709, 0, 0, 1 << 16, 1 << 16);
        const std::uint8_t* source[] = {sequencePixels.data()};
        const int sourceStrides[] = {int(stride)};
        CVPixelBufferLockBaseAddress(buffer, 0);
        std::uint8_t* planes[] = {static_cast<std::uint8_t*>(CVPixelBufferGetBaseAddressOfPlane(buffer, 0)),
                                  static_cast<std::uint8_t*>(CVPixelBufferGetBaseAddressOfPlane(buffer, 1))};
        const int strides[] = {static_cast<int>(CVPixelBufferGetBytesPerRowOfPlane(buffer, 0)),
                               static_cast<int>(CVPixelBufferGetBytesPerRowOfPlane(buffer, 1))};
        sws_scale(sws, source, sourceStrides, 0, height, planes, strides);
        CVPixelBufferUnlockBaseAddress(buffer, 0);
        CVBufferSetAttachment(buffer, kCVImageBufferYCbCrMatrixKey, kCVImageBufferYCbCrMatrix_ITU_R_709_2,
                              kCVAttachmentMode_ShouldPropagate);
    }
    tagBuffer(buffer, info.colour);
    return DecodedFrame(buffer, pts);
}

CVPixelBufferRef VideoDecoder::Impl::convertToBgra()
{
    if (!ensurePool(kCVPixelFormatType_32BGRA))
        return nullptr;
    // A VP9 picture's separate alpha joins its planes as a fourth.
    AVPixelFormat source = static_cast<AVPixelFormat>(frame->format);
    const std::uint8_t* data[4] = {frame->data[0], frame->data[1], frame->data[2], frame->data[3]};
    int lines[4] = {frame->linesize[0], frame->linesize[1], frame->linesize[2], frame->linesize[3]};
    if (alphaCodec) {
        const AVFrame* alpha = alphaFor(frame->pts);
        const AVPixelFormat joined = withAlphaPlane(frame->format);
        if (alpha && joined != AV_PIX_FMT_NONE && alpha->format == frame->format && alpha->width == frame->width
            && alpha->height == frame->height) {
            source = joined;
            data[3] = alpha->data[0];
            lines[3] = alpha->linesize[0];
        }
    }
    sws = sws_getCachedContext(sws, frame->width, frame->height, source, frame->width, frame->height, AV_PIX_FMT_BGRA,
                               SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!sws)
        return nullptr;
    const CFStringRef matrix = matrixAttachment(frame->colorspace, frame->height);
    const int* coefficients = sws_getCoefficients(swsColorspace(matrix));
    sws_setColorspaceDetails(sws, coefficients, frame->color_range == AVCOL_RANGE_JPEG ? 1 : 0, coefficients, 1, 0,
                             1 << 16, 1 << 16);
    CVPixelBufferRef buffer = nullptr;
    if (CVPixelBufferPoolCreatePixelBuffer(nullptr, pool, &buffer) != kCVReturnSuccess)
        return nullptr;
    CVPixelBufferLockBaseAddress(buffer, 0);
    std::uint8_t* planes[] = {static_cast<std::uint8_t*>(CVPixelBufferGetBaseAddress(buffer))};
    const int strides[] = {static_cast<int>(CVPixelBufferGetBytesPerRow(buffer))};
    sws_scale(sws, data, lines, 0, frame->height, planes, strides);
    CVPixelBufferUnlockBaseAddress(buffer, 0);
    return buffer;
}

// NV12, or P010 for more than 8 bits a sample (HDR would band in 8).
CVPixelBufferRef VideoDecoder::Impl::convertToYuv()
{
    const bool tenBit = info.highBitDepth;
    if (!ensurePool(tenBit ? kCVPixelFormatType_420YpCbCr10BiPlanarVideoRange : kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange))
        return nullptr;

    const CFStringRef matrix = matrixAttachment(frame->colorspace, frame->height);
    sws = sws_getCachedContext(sws, frame->width, frame->height, static_cast<AVPixelFormat>(frame->format),
                               frame->width, frame->height, tenBit ? AV_PIX_FMT_P010LE : AV_PIX_FMT_NV12, SWS_BILINEAR,
                               nullptr, nullptr, nullptr);
    if (!sws)
        return nullptr;
    const int* coefficients = sws_getCoefficients(swsColorspace(matrix));
    const int sourceFullRange = frame->color_range == AVCOL_RANGE_JPEG ? 1 : 0;
    sws_setColorspaceDetails(sws, coefficients, sourceFullRange, coefficients, 0, 0, 1 << 16, 1 << 16);

    CVPixelBufferRef buffer = nullptr;
    if (CVPixelBufferPoolCreatePixelBuffer(nullptr, pool, &buffer) != kCVReturnSuccess)
        return nullptr;

    CVPixelBufferLockBaseAddress(buffer, 0);
    std::uint8_t* planes[] = {static_cast<std::uint8_t*>(CVPixelBufferGetBaseAddressOfPlane(buffer, 0)),
                              static_cast<std::uint8_t*>(CVPixelBufferGetBaseAddressOfPlane(buffer, 1))};
    const int strides[] = {static_cast<int>(CVPixelBufferGetBytesPerRowOfPlane(buffer, 0)),
                           static_cast<int>(CVPixelBufferGetBytesPerRowOfPlane(buffer, 1))};
    sws_scale(sws, frame->data, frame->linesize, 0, frame->height, planes, strides);
    CVPixelBufferUnlockBaseAddress(buffer, 0);

    CVBufferSetAttachment(buffer, kCVImageBufferYCbCrMatrixKey, matrix, kCVAttachmentMode_ShouldPropagate);
    return buffer;
}

VideoDecoder::VideoDecoder(std::unique_ptr<Impl> impl) : m_impl(std::move(impl)) {}

VideoDecoder::~VideoDecoder() = default;

std::unique_ptr<VideoDecoder> VideoDecoder::open(const std::string& path, std::string* error)
{
    auto fail = [&](std::string message) -> std::unique_ptr<VideoDecoder> {
        if (error)
            *error = std::move(message);
        return nullptr;
    };

    // FFmpeg logs warnings (e.g. about JPEG-range pixel formats) to stderr by default.
    static std::once_flag quietLogs;
    std::call_once(quietLogs, [] { av_log_set_level(AV_LOG_ERROR); });

    auto d = std::make_unique<Impl>();

    // The first frame of a registered image sequence plays the whole run.
    if (const auto registered = registeredImageSequence(path)) {
        d->sequenceFiles = sequenceFrameFiles(path, registered->sequence);
        if (d->sequenceFiles.empty())
            return fail("no frames of the image sequence " + path);
        const auto first = imagefile::probe(d->sequenceFiles.front());
        if (!first)
            return fail("cannot read " + d->sequenceFiles.front());
        // Even sizes: NV12 halves them for its colour plane.
        d->info.width = first->alpha ? first->width : std::max(2, first->width & ~1);
        d->info.height = first->alpha ? first->height : std::max(2, first->height & ~1);
        d->info.frameRate = registered->rate.num > 0 && registered->rate.den > 0 ? registered->rate : FrameRate{30, 1};
        d->info.duration = Ticks(d->sequenceFiles.size()) * frameDuration(d->info.frameRate);
        d->info.codecName = "image sequence";
        d->info.hasAlpha = first->alpha;
        return std::unique_ptr<VideoDecoder>(new VideoDecoder(std::move(d)));
    }

    if (int rc = avformat_open_input(&d->format, path.c_str(), nullptr, nullptr); rc < 0)
        return fail("cannot open " + path + ": " + avError(rc));
    if (int rc = avformat_find_stream_info(d->format, nullptr); rc < 0)
        return fail("cannot read stream info: " + avError(rc));

    const AVCodec* decoder = nullptr;
    d->streamIndex = av_find_best_stream(d->format, AVMEDIA_TYPE_VIDEO, -1, -1, &decoder, 0);
    if (d->streamIndex < 0 || !decoder)
        return fail("no decodable video stream in " + path);

    AVStream* stream = d->format->streams[d->streamIndex];
    d->timeBase = stream->time_base;
    d->startTime = stream->start_time == AV_NOPTS_VALUE ? 0 : stream->start_time;

    // Transparency: VideoToolbox would hand back NV12 without it, so such pictures decode in
    // software. The stream's format says so, or (its format saying only the picture) a VP9
    // stream's alpha mode, or an HEVC stream's alpha layer, found by decoding its first
    // picture when it has layers beyond the base one.
    const AVPixFmtDescriptor* sourceFormat = av_pix_fmt_desc_get(static_cast<AVPixelFormat>(stream->codecpar->format));
    d->info.hasAlpha = sourceFormat && (sourceFormat->flags & AV_PIX_FMT_FLAG_ALPHA);
    const AVDictionaryEntry* alphaMode = av_dict_get(stream->metadata, "alpha_mode", nullptr, 0);
    const bool vp9Alpha = decoder->id == AV_CODEC_ID_VP9 && alphaMode && std::strcmp(alphaMode->value, "1") == 0;
    const bool layered = decoder->id == AV_CODEC_ID_HEVC && !d->info.hasAlpha
                         && hevcHasLayers(stream->codecpar->extradata, stream->codecpar->extradata_size);
    d->info.hasAlpha = d->info.hasAlpha || vp9Alpha;
    if (vp9Alpha) {
        const AVCodec* alphaDecoder = avcodec_find_decoder(AV_CODEC_ID_VP9);
        d->alphaCodec = alphaDecoder ? avcodec_alloc_context3(alphaDecoder) : nullptr;
        d->alphaPacket = av_packet_alloc();
        if (!d->alphaCodec || !d->alphaPacket)
            return fail("cannot configure decoder");
        d->alphaCodec->pkt_timebase = stream->time_base;
        // Slice threads only: each alpha picture comes out as its packet goes in, ready
        // before (or with) the picture it belongs to.
        d->alphaCodec->thread_count = 0;
        d->alphaCodec->thread_type = FF_THREAD_SLICE;
        if (avcodec_open2(d->alphaCodec, alphaDecoder, nullptr) < 0)
            return fail("cannot open the transparency decoder");
    }
    if (layered) {
        // Decoded in software to look (an alpha layer is not VideoToolbox's to keep).
        d->codec = avcodec_alloc_context3(decoder);
        if (!d->codec || avcodec_parameters_to_context(d->codec, stream->codecpar) < 0)
            return fail("cannot configure decoder");
        d->codec->pkt_timebase = stream->time_base;
        d->codec->thread_count = 0;
        if (int rc = avcodec_open2(d->codec, decoder, nullptr); rc < 0)
            return fail("cannot open decoder: " + avError(rc));
        const AVPixFmtDescriptor* first = av_pix_fmt_desc_get(d->firstPictureFormat());
        d->info.hasAlpha = first && (first->flags & AV_PIX_FMT_FLAG_ALPHA);
        avcodec_free_context(&d->codec); // set up again below, as for any stream
    }

    d->codec = avcodec_alloc_context3(decoder);
    if (!d->codec || avcodec_parameters_to_context(d->codec, stream->codecpar) < 0)
        return fail("cannot configure decoder");
    d->info.highBitDepth = sourceFormat && sourceFormat->comp[0].depth > 8;
    d->info.colour = colourFromTags(stream->codecpar->color_primaries, stream->codecpar->color_trc);
    if (const AVPacketSideData* sphere = av_packet_side_data_get(stream->codecpar->coded_side_data,
                                                                 stream->codecpar->nb_coded_side_data, AV_PKT_DATA_SPHERICAL))
        d->info.spherical = reinterpret_cast<const AVSphericalMapping*>(sphere->data)->projection == AV_SPHERICAL_EQUIRECTANGULAR;
    for (int i = 0; !d->info.hasAlpha; ++i) {
        const AVCodecHWConfig* config = avcodec_get_hw_config(decoder, i);
        if (!config)
            break;
        if (config->device_type == AV_HWDEVICE_TYPE_VIDEOTOOLBOX
            && (config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX)) {
            if (av_hwdevice_ctx_create(&d->hwDevice, AV_HWDEVICE_TYPE_VIDEOTOOLBOX, nullptr, nullptr, 0) >= 0)
                d->codec->hw_device_ctx = av_buffer_ref(d->hwDevice);
            break;
        }
    }
    d->codec->get_format = pickPixelFormat;
    d->codec->thread_count = 0; // software fallback: one thread per core
    // VideoToolbox does the decoding; frame threading would only add a frame of delay per
    // thread and make every seek refill that pipeline. Slice threads still serve a fallback.
    if (d->codec->hw_device_ctx)
        d->codec->thread_type = FF_THREAD_SLICE;

    if (int rc = avcodec_open2(d->codec, decoder, nullptr); rc < 0)
        return fail("cannot open decoder: " + avError(rc));

    d->packet = av_packet_alloc();
    d->frame = av_frame_alloc();

    const AVRational rate = av_guess_frame_rate(d->format, stream, nullptr);
    d->info.width = d->codec->width;
    d->info.height = d->codec->height;
    d->info.frameRate = rate.num > 0 && rate.den > 0 ? FrameRate{rate.num, rate.den} : FrameRate{30, 1};
    if (stream->duration != AV_NOPTS_VALUE)
        d->info.duration = av_rescale_q(stream->duration, d->timeBase, kTickTimeBase);
    else if (d->format->duration != AV_NOPTS_VALUE)
        d->info.duration = av_rescale_q(d->format->duration, AV_TIME_BASE_Q, kTickTimeBase);
    d->info.codecName = decoder->name;
    d->info.hardwareAccelerated = d->codec->hw_device_ctx != nullptr;

    return std::unique_ptr<VideoDecoder>(new VideoDecoder(std::move(d)));
}

const VideoStreamInfo& VideoDecoder::info() const
{
    return m_impl->info;
}

std::optional<DecodedFrame> VideoDecoder::nextFrame()
{
    Impl& d = *m_impl;
    if (d.isSequence())
        return d.sequenceFrame();
    while (true) {
        const int rc = avcodec_receive_frame(d.codec, d.frame);
        if (rc == 0) {
            const Ticks pts = d.framePts();
            d.lastPts = pts;
            // After a seek, frames before the target are decoded (they are needed as
            // references) but not returned. A frame is kept if it is still on screen at
            // the target time.
            if (d.discardBefore >= 0 && pts + frameDuration(d.info.frameRate) <= d.discardBefore) {
                av_frame_unref(d.frame);
                continue;
            }
            d.discardBefore = -1;
            auto decoded = d.takeFrame(pts);
            av_frame_unref(d.frame);
            if (decoded)
                return decoded;
            continue;
        }
        if (rc != AVERROR(EAGAIN) || d.inputDone)
            return std::nullopt; // end of stream or decoder error

        if (av_read_frame(d.format, d.packet) < 0) {
            d.inputDone = true;
            avcodec_send_packet(d.codec, nullptr); // drain buffered frames
            if (d.alphaCodec) {
                avcodec_send_packet(d.alphaCodec, nullptr);
                d.receiveAlpha();
            }
            continue;
        }
        if (d.packet->stream_index == d.streamIndex) {
            if (d.alphaCodec)
                d.sendAlpha(d.packet); // first: its picture is ready when the frame is
            avcodec_send_packet(d.codec, d.packet); // a corrupt packet is skipped, not fatal
        }
        av_packet_unref(d.packet);
    }
}

bool VideoDecoder::seekToStart()
{
    return seek(0);
}

bool VideoDecoder::seek(Ticks target)
{
    if (m_impl->isSequence())
        return seekToKeyframe(target);
    // Land on the keyframe at or before the target, then decode forward to it.
    if (!seekToKeyframe(target))
        return false;
    m_impl->discardBefore = target > 0 ? target : -1;
    return true;
}

bool VideoDecoder::seekToKeyframe(Ticks target)
{
    Impl& d = *m_impl;
    target = std::max<Ticks>(target, 0);
    if (d.isSequence()) { // every frame a keyframe: the one on screen at the target
        d.sequenceNext = std::min<std::size_t>(std::size_t(target / frameDuration(d.info.frameRate)), d.sequenceFiles.size());
        d.lastPts = target;
        return true;
    }
    const std::int64_t timestamp = d.startTime + av_rescale_q(target, kTickTimeBase, d.timeBase);
    if (av_seek_frame(d.format, d.streamIndex, timestamp, AVSEEK_FLAG_BACKWARD) < 0)
        return false;
    avcodec_flush_buffers(d.codec);
    if (d.alphaCodec) {
        avcodec_flush_buffers(d.alphaCodec);
        d.clearAlpha();
    }
    d.inputDone = false;
    d.lastPts = target;
    d.discardBefore = -1;
    return true;
}

std::optional<Ticks> VideoDecoder::keyframeBefore(Ticks target) const
{
    const Impl& d = *m_impl;
    if (d.isSequence()) {
        const Ticks frame = frameDuration(d.info.frameRate);
        return std::max<Ticks>(target, 0) / frame * frame;
    }
    AVStream* stream = d.format->streams[d.streamIndex];
    const std::int64_t timestamp = d.startTime + av_rescale_q(std::max<Ticks>(target, 0), kTickTimeBase, d.timeBase);
    const AVIndexEntry* entry = avformat_index_get_entry_from_timestamp(stream, timestamp, AVSEEK_FLAG_BACKWARD);
    if (!entry)
        return std::nullopt;
    return av_rescale_q(entry->timestamp - d.startTime, d.timeBase, kTickTimeBase);
}

} // namespace tve
