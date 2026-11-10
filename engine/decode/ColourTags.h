#pragma once

#include "model/Colour.h"

#include <CoreVideo/CoreVideo.h>

namespace tve {

// A picture's colour space as its file tags it (FFmpeg's AVColorPrimaries and
// AVColorTransferCharacteristic values): untagged and unknown values read as SDR Rec.709.
ColourSpace colourFromTags(int primaries, int transfer);

// The same on a pixel buffer, as CoreVideo attachments (kCVImageBufferColorPrimariesKey,
// kCVImageBufferTransferFunctionKey) that propagate with it, and back.
void tagBuffer(CVPixelBufferRef buffer, ColourSpace colour);
ColourSpace bufferColour(CVPixelBufferRef buffer);

} // namespace tve
