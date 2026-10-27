#include "encode/Proxy.h"

#include "decode/VideoDecoder.h"
#include "encode/Mp4Writer.h"

extern "C" {
#include <libswscale/swscale.h>
}

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <vector>

#include <CoreVideo/CoreVideo.h>

namespace tve {

std::pair<int, int> proxySize(int width, int height, int maxHeight)
{
    const auto even = [](double v) { return std::max(2, int(std::lround(v / 2.0)) * 2); };
    const int shortSide = std::min(width, height);
    if (shortSide <= maxHeight)
        return {even(width), even(height)};
    const double scale = double(maxHeight) / double(shortSide);
    return {even(width * scale), even(height * scale)};
}

bool makeProxy(const std::string& source, const std::string& destination, const ProxyOptions& options,
               const std::function<bool(double)>& progress, std::string* error)
{
    const auto fail = [&](const std::string& message) {
        if (error)
            *error = message;
        return false;
    };
    std::string openError;
    auto decoder = VideoDecoder::open(source, &openError);
    if (!decoder)
        return fail(openError);
    const VideoStreamInfo& info = decoder->info();
    const auto [width, height] = proxySize(info.width, info.height, options.maxHeight);

    Mp4Settings settings;
    settings.width = width;
    settings.height = height;
    settings.frameRate = info.frameRate;
    settings.codec = VideoCodec::H264;
    settings.audio = false;
    settings.keyframeInterval = options.keyframeInterval;
    const std::string temporary = destination + ".part";
    std::filesystem::create_directories(std::filesystem::path(destination).parent_path());
    auto writer = Mp4Writer::open(temporary, settings, &openError);
    if (!writer)
        return fail(openError);
    const auto abandon = [&](const std::string& message) {
        writer.reset();
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return fail(message);
    };

    std::vector<std::uint8_t> luma(std::size_t(width) * std::size_t(height));
    std::vector<std::uint8_t> chroma(std::size_t(width) * std::size_t(height) / 2);
    SwsContext* scaler = nullptr;
    const double expected = std::max(1.0, toSeconds(info.duration) * info.frameRate.num / info.frameRate.den);
    std::int64_t frames = 0;
    while (auto frame = decoder->nextFrame()) {
        CVPixelBufferRef buffer = frame->pixelBuffer();
        if (!buffer)
            continue;
        const OSType format = CVPixelBufferGetPixelFormatType(buffer);
        const bool tenBit = format == kCVPixelFormatType_420YpCbCr10BiPlanarVideoRange
                         || format == kCVPixelFormatType_420YpCbCr10BiPlanarFullRange;
        const bool fullRange = format == kCVPixelFormatType_420YpCbCr8BiPlanarFullRange
                            || format == kCVPixelFormatType_420YpCbCr10BiPlanarFullRange;
        const int sourceW = int(CVPixelBufferGetWidth(buffer));
        const int sourceH = int(CVPixelBufferGetHeight(buffer));
        scaler = sws_getCachedContext(scaler, sourceW, sourceH, tenBit ? AV_PIX_FMT_P010LE : AV_PIX_FMT_NV12, width,
                                      height, AV_PIX_FMT_NV12, SWS_BILINEAR, nullptr, nullptr, nullptr);
        if (!scaler)
            return abandon("cannot scale the pictures");
        // BT.709 in and out; video range out (what the writer tags).
        const int* bt709 = sws_getCoefficients(SWS_CS_ITU709);
        sws_setColorspaceDetails(scaler, bt709, fullRange ? 1 : 0, bt709, 0, 0, 1 << 16, 1 << 16);

        CVPixelBufferLockBaseAddress(buffer, kCVPixelBufferLock_ReadOnly);
        const std::uint8_t* planes[] = {static_cast<const std::uint8_t*>(CVPixelBufferGetBaseAddressOfPlane(buffer, 0)),
                                        static_cast<const std::uint8_t*>(CVPixelBufferGetBaseAddressOfPlane(buffer, 1))};
        const int strides[] = {int(CVPixelBufferGetBytesPerRowOfPlane(buffer, 0)),
                               int(CVPixelBufferGetBytesPerRowOfPlane(buffer, 1))};
        std::uint8_t* out[] = {luma.data(), chroma.data()};
        const int outStrides[] = {width, width};
        sws_scale(scaler, planes, strides, 0, sourceH, out, outStrides);
        CVPixelBufferUnlockBaseAddress(buffer, kCVPixelBufferLock_ReadOnly);

        if (!writer->writeVideo(luma.data(), width, chroma.data(), width))
            return abandon(writer->lastError());
        ++frames;
        if (progress && !progress(std::min(1.0, double(frames) / expected))) {
            sws_freeContext(scaler);
            return abandon(std::string()); // cancelled
        }
    }
    sws_freeContext(scaler);
    if (frames == 0)
        return abandon("no pictures in " + source);
    if (!writer->finish())
        return abandon(writer->lastError());
    writer.reset();
    std::error_code renameError;
    std::filesystem::rename(temporary, destination, renameError);
    if (renameError)
        return abandon("cannot save " + destination);
    if (progress)
        progress(1.0);
    return true;
}

} // namespace tve
