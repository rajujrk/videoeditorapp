#include "decode/VideoDecoder.h"

#include <algorithm>
#include <mutex>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/hwcontext.h>
#include <libavutil/pixdesc.h>
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

    int streamIndex = -1;
    AVRational timeBase{};
    std::int64_t startTime = 0;
    bool inputDone = false;
    Ticks lastPts = 0;
    Ticks discardBefore = -1; // after a seek: drop frames that end at or before this time
    VideoStreamInfo info;

    ~Impl()
    {
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
    CVPixelBufferRef convertToNv12();
    CVPixelBufferRef convertToBgra();
    bool ensurePool(int pixelFormat);
};

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

    if (frame->format == AV_PIX_FMT_VIDEOTOOLBOX) {
        info.hardwareAccelerated = true;
        auto buffer = reinterpret_cast<CVPixelBufferRef>(frame->data[3]);
        CVPixelBufferRetain(buffer);
        return DecodedFrame(buffer, pts);
    }

    info.hardwareAccelerated = false;
    CVPixelBufferRef buffer = info.hasAlpha ? convertToBgra() : convertToNv12();
    if (!buffer)
        return std::nullopt;
    return DecodedFrame(buffer, pts);
}

bool VideoDecoder::Impl::ensurePool(int pixelFormat)
{
    if (!pool || poolWidth != frame->width || poolHeight != frame->height) {
        if (pool)
            CVPixelBufferPoolRelease(pool);
        pool = createPool(frame->width, frame->height, pixelFormat);
        poolWidth = frame->width;
        poolHeight = frame->height;
    }
    return pool != nullptr;
}

CVPixelBufferRef VideoDecoder::Impl::convertToBgra()
{
    if (!ensurePool(kCVPixelFormatType_32BGRA))
        return nullptr;
    sws = sws_getCachedContext(sws, frame->width, frame->height, static_cast<AVPixelFormat>(frame->format),
                               frame->width, frame->height, AV_PIX_FMT_BGRA, SWS_BILINEAR, nullptr, nullptr, nullptr);
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
    sws_scale(sws, frame->data, frame->linesize, 0, frame->height, planes, strides);
    CVPixelBufferUnlockBaseAddress(buffer, 0);
    return buffer;
}

CVPixelBufferRef VideoDecoder::Impl::convertToNv12()
{
    if (!ensurePool(kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange))
        return nullptr;

    const CFStringRef matrix = matrixAttachment(frame->colorspace, frame->height);
    sws = sws_getCachedContext(sws, frame->width, frame->height, static_cast<AVPixelFormat>(frame->format),
                               frame->width, frame->height, AV_PIX_FMT_NV12, SWS_BILINEAR, nullptr, nullptr, nullptr);
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

    d->codec = avcodec_alloc_context3(decoder);
    if (!d->codec || avcodec_parameters_to_context(d->codec, stream->codecpar) < 0)
        return fail("cannot configure decoder");

    // Transparency: VideoToolbox would hand back NV12 without it, so such pictures decode in
    // software.
    const AVPixFmtDescriptor* sourceFormat = av_pix_fmt_desc_get(static_cast<AVPixelFormat>(stream->codecpar->format));
    d->info.hasAlpha = sourceFormat && (sourceFormat->flags & AV_PIX_FMT_FLAG_ALPHA);
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
            continue;
        }
        if (d.packet->stream_index == d.streamIndex)
            avcodec_send_packet(d.codec, d.packet); // a corrupt packet is skipped, not fatal
        av_packet_unref(d.packet);
    }
}

bool VideoDecoder::seekToStart()
{
    return seek(0);
}

bool VideoDecoder::seek(Ticks target)
{
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
    const std::int64_t timestamp = d.startTime + av_rescale_q(target, kTickTimeBase, d.timeBase);
    if (av_seek_frame(d.format, d.streamIndex, timestamp, AVSEEK_FLAG_BACKWARD) < 0)
        return false;
    avcodec_flush_buffers(d.codec);
    d.inputDone = false;
    d.lastPts = target;
    d.discardBefore = -1;
    return true;
}

std::optional<Ticks> VideoDecoder::keyframeBefore(Ticks target) const
{
    const Impl& d = *m_impl;
    AVStream* stream = d.format->streams[d.streamIndex];
    const std::int64_t timestamp = d.startTime + av_rescale_q(std::max<Ticks>(target, 0), kTickTimeBase, d.timeBase);
    const AVIndexEntry* entry = avformat_index_get_entry_from_timestamp(stream, timestamp, AVSEEK_FLAG_BACKWARD);
    if (!entry)
        return std::nullopt;
    return av_rescale_q(entry->timestamp - d.startTime, d.timeBase, kTickTimeBase);
}

} // namespace tve
