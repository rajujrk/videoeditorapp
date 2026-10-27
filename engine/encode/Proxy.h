#pragma once

#include <functional>
#include <string>
#include <utility>

namespace tve {

// A proxy is a lighter copy of a video for editing: smaller, H.264 with frequent keyframes
// (cheap to seek and to play backwards), video only, with the same frames at the same times.
struct ProxyOptions {
    int maxHeight = 720;
    int keyframeInterval = 10;
};

// Proxy picture size for a source: the source's shape, at most `maxHeight` high (the
// shorter side for portrait), even.
std::pair<int, int> proxySize(int width, int height, int maxHeight);

// Writes a proxy of `source` to `destination` (via a temporary file renamed at the end).
// `progress` gets 0–1 as frames are done; returning false cancels. On failure or cancel
// nothing is left at `destination` and `error` says why (empty when cancelled).
bool makeProxy(const std::string& source, const std::string& destination, const ProxyOptions& options,
               const std::function<bool(double)>& progress, std::string* error);

} // namespace tve
