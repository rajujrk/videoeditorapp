#include "decode/ColourTags.h"

extern "C" {
#include <libavutil/pixfmt.h>
}

namespace tve {

ColourSpace colourFromTags(int primaries, int transfer)
{
    ColourSpace colour;
    switch (primaries) {
    case AVCOL_PRI_BT2020: colour.primaries = Primaries::Rec2020; break;
    case AVCOL_PRI_SMPTE432: colour.primaries = Primaries::DisplayP3; break;
    default: break;
    }
    switch (transfer) {
    case AVCOL_TRC_SMPTE2084: colour.transfer = Transfer::Pq; break;
    case AVCOL_TRC_ARIB_STD_B67: colour.transfer = Transfer::Hlg; break;
    case AVCOL_TRC_LINEAR: colour.transfer = Transfer::Linear; break;
    default: break;
    }
    return colour;
}

void tagBuffer(CVPixelBufferRef buffer, ColourSpace colour)
{
    if (!buffer)
        return;
    const CFStringRef primaries = colour.primaries == Primaries::Rec2020     ? kCVImageBufferColorPrimaries_ITU_R_2020
                                : colour.primaries == Primaries::DisplayP3 ? kCVImageBufferColorPrimaries_P3_D65
                                                                           : kCVImageBufferColorPrimaries_ITU_R_709_2;
    const CFStringRef transfer = colour.transfer == Transfer::Pq       ? kCVImageBufferTransferFunction_SMPTE_ST_2084_PQ
                               : colour.transfer == Transfer::Hlg    ? kCVImageBufferTransferFunction_ITU_R_2100_HLG
                               : colour.transfer == Transfer::Linear ? kCVImageBufferTransferFunction_Linear
                                                                     : kCVImageBufferTransferFunction_ITU_R_709_2;
    CVBufferSetAttachment(buffer, kCVImageBufferColorPrimariesKey, primaries, kCVAttachmentMode_ShouldPropagate);
    CVBufferSetAttachment(buffer, kCVImageBufferTransferFunctionKey, transfer, kCVAttachmentMode_ShouldPropagate);
}

ColourSpace bufferColour(CVPixelBufferRef buffer)
{
    ColourSpace colour;
    if (!buffer)
        return colour;
    if (CFTypeRef primaries = CVBufferCopyAttachment(buffer, kCVImageBufferColorPrimariesKey, nullptr)) {
        if (CFEqual(primaries, kCVImageBufferColorPrimaries_ITU_R_2020))
            colour.primaries = Primaries::Rec2020;
        else if (CFEqual(primaries, kCVImageBufferColorPrimaries_P3_D65))
            colour.primaries = Primaries::DisplayP3;
        CFRelease(primaries);
    }
    if (CFTypeRef transfer = CVBufferCopyAttachment(buffer, kCVImageBufferTransferFunctionKey, nullptr)) {
        if (CFEqual(transfer, kCVImageBufferTransferFunction_SMPTE_ST_2084_PQ))
            colour.transfer = Transfer::Pq;
        else if (CFEqual(transfer, kCVImageBufferTransferFunction_ITU_R_2100_HLG))
            colour.transfer = Transfer::Hlg;
        else if (CFEqual(transfer, kCVImageBufferTransferFunction_Linear))
            colour.transfer = Transfer::Linear;
        CFRelease(transfer);
    }
    return colour;
}

} // namespace tve
