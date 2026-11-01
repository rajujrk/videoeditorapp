#include "video/OfxFrames.h"

#include "playback/CompositeFrame.h"

#import <CoreVideo/CoreVideo.h>
#import <VideoToolbox/VideoToolbox.h>

#include <algorithm>
#include <cmath>

namespace tve {

namespace {

CVPixelBufferRef makeBgra(int width, int height)
{
    NSDictionary* attributes = @{(id)kCVPixelBufferIOSurfacePropertiesKey: @{},
                                 (id)kCVPixelBufferMetalCompatibilityKey: @YES};
    CVPixelBufferRef buffer = nullptr;
    CVPixelBufferCreate(kCFAllocatorDefault, std::size_t(width), std::size_t(height), kCVPixelFormatType_32BGRA,
                        (CFDictionaryRef)attributes, &buffer);
    return buffer;
}

} // namespace

OfxFrames::OfxFrames()
{
    VTPixelTransferSessionCreate(kCFAllocatorDefault, &m_transfer);
}

OfxFrames::~OfxFrames()
{
    if (m_transfer) {
        VTPixelTransferSessionInvalidate(m_transfer);
        CFRelease(m_transfer);
    }
}

void OfxFrames::keepOnly(const std::set<Id>& live)
{
    std::erase_if(m_clips, [&](const auto& entry) { return !live.contains(entry.first); });
}

void OfxFrames::apply(VideoLayer& layer)
{
    if (layer.frame)
        layer.frame = process(layer.clipId, layer.frame, layer.effects, layer.clipSeconds);
    else
        std::erase_if(layer.effects, [](const ClipEffect& e) { return isOfxEffect(e.effectId); });
    if (layer.transition) {
        LayerTransition& t = *layer.transition;
        if (t.frame)
            t.frame = process(t.clipId, t.frame, t.effects, t.clipSeconds);
        else
            std::erase_if(t.effects, [](const ClipEffect& e) { return isOfxEffect(e.effectId); });
    }
    if (layer.nested)
        for (VideoLayer& inner : layer.nested->layers)
            apply(inner);
}

namespace {

struct YuvLayout {
    bool yuv = false;
    bool fullRange = false;
    bool wide = false; // 16-bit words (10-bit samples in their high bits)
};

YuvLayout yuvLayout(OSType format)
{
    switch (format) {
    case kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange:
    case kCVPixelFormatType_422YpCbCr8BiPlanarVideoRange:
    case kCVPixelFormatType_444YpCbCr8BiPlanarVideoRange:
        return {true, false, false};
    case kCVPixelFormatType_420YpCbCr8BiPlanarFullRange:
    case kCVPixelFormatType_422YpCbCr8BiPlanarFullRange:
    case kCVPixelFormatType_444YpCbCr8BiPlanarFullRange:
        return {true, true, false};
    case kCVPixelFormatType_420YpCbCr10BiPlanarVideoRange:
    case kCVPixelFormatType_422YpCbCr10BiPlanarVideoRange:
    case kCVPixelFormatType_444YpCbCr10BiPlanarVideoRange:
    case kCVPixelFormatType_422YpCbCr16BiPlanarVideoRange:
    case kCVPixelFormatType_444YpCbCr16BiPlanarVideoRange:
        return {true, false, true};
    case kCVPixelFormatType_420YpCbCr10BiPlanarFullRange:
    case kCVPixelFormatType_422YpCbCr10BiPlanarFullRange:
    case kCVPixelFormatType_444YpCbCr10BiPlanarFullRange:
        return {true, true, true};
    default:
        return {};
    }
}

} // namespace

bool OfxFrames::toRgba(CVPixelBufferRef source, int width, int height)
{
    const OSType format = CVPixelBufferGetPixelFormatType(source);
    const YuvLayout layout = yuvLayout(format);
    if (layout.yuv) {
        // The compositor's matrix (FrameCompositor.mm: yuvToRgbMatrix).
        double kr = 0.2126, kb = 0.0722;
        if (CFTypeRef matrix = CVBufferCopyAttachment(source, kCVImageBufferYCbCrMatrixKey, nullptr)) {
            if (CFEqual(matrix, kCVImageBufferYCbCrMatrix_ITU_R_601_4))
                kr = 0.299, kb = 0.114;
            else if (CFEqual(matrix, kCVImageBufferYCbCrMatrix_ITU_R_2020))
                kr = 0.2627, kb = 0.0593;
            CFRelease(matrix);
        }
        const double kg = 1.0 - kr - kb;
        const double ys = layout.fullRange ? 1.0 : 255.0 / 219.0, yo = layout.fullRange ? 0.0 : -16.0 / 219.0;
        const double cs = layout.fullRange ? 1.0 : 255.0 / 224.0, co = layout.fullRange ? -128.0 / 255.0 : -128.0 / 224.0;
        const double rCr = 2.0 * (1.0 - kr), bCb = 2.0 * (1.0 - kb);
        const double gCb = 2.0 * kb * (1.0 - kb) / kg, gCr = 2.0 * kr * (1.0 - kr) / kg;
        CVPixelBufferLockBaseAddress(source, kCVPixelBufferLock_ReadOnly);
        const auto* luma = static_cast<const std::uint8_t*>(CVPixelBufferGetBaseAddressOfPlane(source, 0));
        const auto* chroma = static_cast<const std::uint8_t*>(CVPixelBufferGetBaseAddressOfPlane(source, 1));
        const std::size_t lumaStride = CVPixelBufferGetBytesPerRowOfPlane(source, 0);
        const std::size_t chromaStride = CVPixelBufferGetBytesPerRowOfPlane(source, 1);
        const int chromaWidth = int(CVPixelBufferGetWidthOfPlane(source, 1));
        const int chromaHeight = int(CVPixelBufferGetHeightOfPlane(source, 1));
        const double scale = layout.wide ? 1.0 / 65535.0 : 1.0 / 255.0;
        for (int y = 0; y < height; ++y) {
            const int cy = std::min(chromaHeight - 1, y * chromaHeight / height);
            float* out = m_pixels.data() + std::size_t(y) * std::size_t(width) * 4;
            for (int x = 0; x < width; ++x) {
                const int cx = std::min(chromaWidth - 1, x * chromaWidth / width);
                double Y, Cb, Cr;
                if (layout.wide) {
                    Y = reinterpret_cast<const std::uint16_t*>(luma + std::size_t(y) * lumaStride)[x] * scale;
                    const auto* c = reinterpret_cast<const std::uint16_t*>(chroma + std::size_t(cy) * chromaStride);
                    Cb = c[cx * 2] * scale;
                    Cr = c[cx * 2 + 1] * scale;
                } else {
                    Y = luma[std::size_t(y) * lumaStride + std::size_t(x)] * scale;
                    const std::uint8_t* c = chroma + std::size_t(cy) * chromaStride;
                    Cb = c[cx * 2] * scale;
                    Cr = c[cx * 2 + 1] * scale;
                }
                out[x * 4] = float(std::clamp(ys * Y + rCr * cs * Cr + yo + rCr * co, 0.0, 1.0));
                out[x * 4 + 1] = float(std::clamp(ys * Y - gCb * cs * Cb - gCr * cs * Cr + yo - (gCb + gCr) * co, 0.0, 1.0));
                out[x * 4 + 2] = float(std::clamp(ys * Y + bCb * cs * Cb + yo + bCb * co, 0.0, 1.0));
                out[x * 4 + 3] = 1.0f;
            }
        }
        CVPixelBufferUnlockBaseAddress(source, kCVPixelBufferLock_ReadOnly);
        return true;
    }

    // BGRA as it is; anything else through VideoToolbox first.
    CVPixelBufferRef bgra = source;
    bool converted = false;
    if (format != kCVPixelFormatType_32BGRA) {
        bgra = makeBgra(width, height);
        if (bgra)
            CVBufferPropagateAttachments(source, bgra);
        if (!bgra || VTPixelTransferSessionTransferImage(m_transfer, source, bgra) != noErr) {
            if (bgra)
                CVPixelBufferRelease(bgra);
            return false;
        }
        converted = true;
    }
    CVPixelBufferLockBaseAddress(bgra, kCVPixelBufferLock_ReadOnly);
    const auto* base = static_cast<const std::uint8_t*>(CVPixelBufferGetBaseAddress(bgra));
    const std::size_t stride = CVPixelBufferGetBytesPerRow(bgra);
    for (int y = 0; y < height; ++y) {
        const std::uint8_t* row = base + std::size_t(y) * stride;
        float* out = m_pixels.data() + std::size_t(y) * std::size_t(width) * 4;
        for (int x = 0; x < width; ++x) {
            out[x * 4] = float(row[x * 4 + 2]) / 255.0f;
            out[x * 4 + 1] = float(row[x * 4 + 1]) / 255.0f;
            out[x * 4 + 2] = float(row[x * 4]) / 255.0f;
            out[x * 4 + 3] = float(row[x * 4 + 3]) / 255.0f;
        }
    }
    CVPixelBufferUnlockBaseAddress(bgra, kCVPixelBufferLock_ReadOnly);
    if (converted)
        CVPixelBufferRelease(bgra);
    return true;
}

DecodedFrame OfxFrames::process(Id clipId, const DecodedFrame& frame, std::vector<ClipEffect>& effects, double seconds)
{
    std::vector<ClipEffect> ofx;
    for (ClipEffect& e : effects)
        if (isOfxEffect(e.effectId) && e.enabled)
            ofx.push_back(e);
    std::erase_if(effects, [](const ClipEffect& e) { return isOfxEffect(e.effectId); });
    if (ofx.empty())
        return frame.share();

    // This clip's instances, in the stack's order (reopened when the stack changes).
    auto& running = m_clips[clipId];
    std::vector<std::string> ids;
    for (const ClipEffect& e : ofx)
        ids.push_back(e.effectId);
    const bool same = running.size() == ids.size()
                      && std::ranges::equal(running, ids, [](const Running& r, const std::string& id) { return r.id == id; });
    if (!same) {
        running.clear();
        for (const std::string& id : ids)
            running.push_back({id, OfxEffect::open(id)});
    }
    if (std::ranges::none_of(running, [](const Running& r) { return r.effect != nullptr; }))
        return frame.share();

    // The picture as straight RGBA, 0..1, top row first: YCbCr turned to RGB exactly as the
    // compositor would (its matrix, from the frame's tags), so a plug-in changes only what it
    // does to the picture.
    CVPixelBufferRef source = frame.pixelBuffer();
    const int width = int(CVPixelBufferGetWidth(source));
    const int height = int(CVPixelBufferGetHeight(source));
    m_pixels.resize(std::size_t(width) * std::size_t(height) * 4);
    if (!toRgba(source, width, height))
        return frame.share();

    // Through each plug-in in turn (the clip's values as at this frame).
    bool any = false;
    for (std::size_t i = 0; i < running.size(); ++i) {
        if (!running[i].effect)
            continue;
        if (running[i].effect->render(m_pixels, width, height, seconds, ofx[i].values)) {
            any = true;
            m_lastMetal = running[i].effect->usedMetal();
        }
    }
    if (!any)
        return frame.share();

    CVPixelBufferRef result = makeBgra(width, height);
    if (!result)
        return frame.share();
    CVBufferPropagateAttachments(source, result);
    CVPixelBufferLockBaseAddress(result, 0);
    {
        auto* base = static_cast<std::uint8_t*>(CVPixelBufferGetBaseAddress(result));
        const std::size_t stride = CVPixelBufferGetBytesPerRow(result);
        const auto byte = [](float v) { return std::uint8_t(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)); };
        for (int y = 0; y < height; ++y) {
            std::uint8_t* row = base + std::size_t(y) * stride;
            const float* in = m_pixels.data() + std::size_t(y) * std::size_t(width) * 4;
            for (int x = 0; x < width; ++x) {
                row[x * 4] = byte(in[x * 4 + 2]);
                row[x * 4 + 1] = byte(in[x * 4 + 1]);
                row[x * 4 + 2] = byte(in[x * 4]);
                row[x * 4 + 3] = byte(in[x * 4 + 3]);
            }
        }
    }
    CVPixelBufferUnlockBaseAddress(result, 0);
    return DecodedFrame(result, frame.pts());
}

} // namespace tve
