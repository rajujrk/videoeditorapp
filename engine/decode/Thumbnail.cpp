#include "decode/Thumbnail.h"

#include "decode/VideoDecoder.h"

#include <algorithm>
#include <cmath>

namespace tve {

namespace {

std::uint8_t toByte(float value)
{
    return std::uint8_t(std::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f);
}

} // namespace

std::optional<RgbaImage> extractThumbnail(const std::string& path, Ticks time, int height)
{
    if (height <= 0)
        return std::nullopt;
    auto decoder = VideoDecoder::open(path);
    if (!decoder)
        return std::nullopt;
    return extractThumbnail(*decoder, time, height);
}

std::optional<RgbaImage> extractThumbnail(VideoDecoder& decoder, Ticks time, int height)
{
    if (height <= 0)
        return std::nullopt;
    // Requests at or past the end (rounded tile times) show the last frame.
    if (const auto& info = decoder.info(); info.duration > 0)
        time = std::min(time, info.duration - frameDuration(info.frameRate));
    if (!decoder.seek(std::max<Ticks>(time, 0)))
        return std::nullopt;
    auto frame = decoder.nextFrame();
    if (!frame || !frame->pixelBuffer())
        return std::nullopt;

    CVPixelBufferRef buffer = frame->pixelBuffer();
    const OSType format = CVPixelBufferGetPixelFormatType(buffer);
    // Two-plane YUV at 4:2:0, 4:2:2 (ProRes 422) or 4:4:4 (ProRes 4444), 8, 10 or 16 bits.
    const bool tenBit = format == kCVPixelFormatType_422YpCbCr16BiPlanarVideoRange
                     || format == kCVPixelFormatType_444YpCbCr16BiPlanarVideoRange
                     || format == kCVPixelFormatType_420YpCbCr10BiPlanarVideoRange
                     || format == kCVPixelFormatType_420YpCbCr10BiPlanarFullRange
                     || format == kCVPixelFormatType_422YpCbCr10BiPlanarVideoRange
                     || format == kCVPixelFormatType_422YpCbCr10BiPlanarFullRange
                     || format == kCVPixelFormatType_444YpCbCr10BiPlanarVideoRange
                     || format == kCVPixelFormatType_444YpCbCr10BiPlanarFullRange;
    const bool fullRange = format == kCVPixelFormatType_420YpCbCr8BiPlanarFullRange
                        || format == kCVPixelFormatType_420YpCbCr10BiPlanarFullRange
                        || format == kCVPixelFormatType_422YpCbCr8BiPlanarFullRange
                        || format == kCVPixelFormatType_422YpCbCr10BiPlanarFullRange
                        || format == kCVPixelFormatType_444YpCbCr8BiPlanarFullRange
                        || format == kCVPixelFormatType_444YpCbCr10BiPlanarFullRange;
    const bool eightBit = format == kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange
                       || format == kCVPixelFormatType_420YpCbCr8BiPlanarFullRange
                       || format == kCVPixelFormatType_422YpCbCr8BiPlanarVideoRange
                       || format == kCVPixelFormatType_422YpCbCr8BiPlanarFullRange
                       || format == kCVPixelFormatType_444YpCbCr8BiPlanarVideoRange
                       || format == kCVPixelFormatType_444YpCbCr8BiPlanarFullRange;
    const bool bgra = format == kCVPixelFormatType_32BGRA;
    if (!tenBit && !eightBit && !bgra)
        return std::nullopt;

    const int sourceW = int(CVPixelBufferGetWidth(buffer));
    const int sourceH = int(CVPixelBufferGetHeight(buffer));
    RgbaImage image;
    image.height = height;
    image.width = std::max(1, int(std::lround(double(height) * sourceW / sourceH)));
    image.pixels.resize(std::size_t(image.width) * std::size_t(image.height) * 4);

    if (bgra) {
        // Pictures with transparency (straight alpha) keep it.
        CVPixelBufferLockBaseAddress(buffer, kCVPixelBufferLock_ReadOnly);
        const auto* base = static_cast<const std::uint8_t*>(CVPixelBufferGetBaseAddress(buffer));
        const std::size_t stride = CVPixelBufferGetBytesPerRow(buffer);
        for (int y = 0; y < image.height; ++y) {
            const int sy = std::min(sourceH - 1, int((double(y) + 0.5) * sourceH / image.height));
            for (int x = 0; x < image.width; ++x) {
                const int sx = std::min(sourceW - 1, int((double(x) + 0.5) * sourceW / image.width));
                const std::uint8_t* in = base + std::size_t(sy) * stride + std::size_t(sx) * 4;
                std::uint8_t* px = image.pixels.data() + (std::size_t(y) * std::size_t(image.width) + std::size_t(x)) * 4;
                px[0] = in[2];
                px[1] = in[1];
                px[2] = in[0];
                px[3] = in[3];
            }
        }
        CVPixelBufferUnlockBaseAddress(buffer, kCVPixelBufferLock_ReadOnly);
        return image;
    }

    // BT.709 for HD and up, BT.601 below.
    const bool hd = sourceH > 576;
    const float kr = hd ? 0.2126f : 0.299f;
    const float kb = hd ? 0.0722f : 0.114f;
    const float kg = 1.0f - kr - kb;

    CVPixelBufferLockBaseAddress(buffer, kCVPixelBufferLock_ReadOnly);
    const auto* luma = static_cast<const std::uint8_t*>(CVPixelBufferGetBaseAddressOfPlane(buffer, 0));
    const auto* chroma = static_cast<const std::uint8_t*>(CVPixelBufferGetBaseAddressOfPlane(buffer, 1));
    const std::size_t lumaStride = CVPixelBufferGetBytesPerRowOfPlane(buffer, 0);
    const std::size_t chromaStride = CVPixelBufferGetBytesPerRowOfPlane(buffer, 1);
    const int chromaW = int(CVPixelBufferGetWidthOfPlane(buffer, 1));
    const int chromaH = int(CVPixelBufferGetHeightOfPlane(buffer, 1));
    const auto sample = [&](const std::uint8_t* row, int index) -> float {
        if (tenBit)
            return float(reinterpret_cast<const std::uint16_t*>(row)[index]) / 65535.0f;
        return float(row[index]) / 255.0f;
    };

    for (int y = 0; y < image.height; ++y) {
        const int sy = std::min(sourceH - 1, int((double(y) + 0.5) * sourceH / image.height));
        const std::uint8_t* lumaRow = luma + std::size_t(sy) * lumaStride;
        const std::uint8_t* chromaRow = chroma + std::size_t(std::min(chromaH - 1, sy * chromaH / sourceH)) * chromaStride;
        for (int x = 0; x < image.width; ++x) {
            const int sx = std::min(sourceW - 1, int((double(x) + 0.5) * sourceW / image.width));
            float yv = sample(lumaRow, sx);
            const int cx = std::min(chromaW - 1, sx * chromaW / sourceW);
            float cb = sample(chromaRow, cx * 2);
            float cr = sample(chromaRow, cx * 2 + 1);
            if (fullRange) {
                cb -= 0.5f;
                cr -= 0.5f;
            } else {
                yv = (yv - 16.0f / 255.0f) * 255.0f / 219.0f;
                cb = (cb - 128.0f / 255.0f) * 255.0f / 224.0f;
                cr = (cr - 128.0f / 255.0f) * 255.0f / 224.0f;
            }
            const float r = yv + 2.0f * (1.0f - kr) * cr;
            const float b = yv + 2.0f * (1.0f - kb) * cb;
            const float g = yv - (2.0f * kb * (1.0f - kb) * cb + 2.0f * kr * (1.0f - kr) * cr) / kg;
            std::uint8_t* px = image.pixels.data() + (std::size_t(y) * std::size_t(image.width) + std::size_t(x)) * 4;
            px[0] = toByte(r);
            px[1] = toByte(g);
            px[2] = toByte(b);
            px[3] = 255;
        }
    }
    CVPixelBufferUnlockBaseAddress(buffer, kCVPixelBufferLock_ReadOnly);
    return image;
}

} // namespace tve
