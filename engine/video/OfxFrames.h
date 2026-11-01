#pragma once

#include "decode/DecodedFrame.h"
#include "model/Project.h"
#include "video/OfxHost.h"

#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

typedef struct OpaqueVTPixelTransferSession* VTPixelTransferSessionRef;

namespace tve {

struct VideoLayer;

// Clips' pictures through their OpenFX effects, on the playback (and export) thread: each
// clip's enabled OpenFX effects in their order, on its own decoded picture — before its
// colour, key, masks and our GPU effects — giving a BGRA frame in their place. Each clip has
// its own plug-in instances. A plug-in that can't be opened or fails is passed over.
class OfxFrames {
public:
    OfxFrames();
    ~OfxFrames();
    OfxFrames(const OfxFrames&) = delete;
    OfxFrames& operator=(const OfxFrames&) = delete;

    // A layer's picture (and an incoming clip's, in a transition, and those of a compound
    // clip's layers) through its OpenFX effects, which leave its effect stack.
    void apply(VideoLayer& layer);
    // Drops the instances of clips not in `live`.
    void keepOnly(const std::set<Id>& live);

    // Whether the last picture rendered with Metal (for tests).
    bool lastUsedMetal() const { return m_lastMetal; }

private:
    struct Running {
        std::string id;
        std::unique_ptr<OfxEffect> effect; // null when it couldn't be opened
    };
    DecodedFrame process(Id clipId, const DecodedFrame& frame, std::vector<ClipEffect>& effects, double seconds);
    // The picture into m_pixels (straight RGBA, 0..1, top row first); false if it can't be.
    bool toRgba(CVPixelBufferRef source, int width, int height);

    std::map<Id, std::vector<Running>> m_clips;
    VTPixelTransferSessionRef m_transfer = nullptr;
    std::vector<float> m_pixels;
    bool m_lastMetal = false;
};

} // namespace tve
