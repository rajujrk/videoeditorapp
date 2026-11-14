#include "analysis/DepthVision.h"
#include "analysis/VisionSupport.h"

#import <CoreML/CoreML.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <mutex>

namespace tve {

using namespace vision;

namespace {

std::mutex g_mutex;
VNCoreMLModel* g_model = nil; // retained
std::string g_path;

} // namespace

std::uint8_t DepthMap::at(double u, double v) const
{
    if (!*this)
        return 0;
    const int cx = std::clamp(int(std::lround(u * (width - 1))), 0, width - 1);
    const int cy = std::clamp(int(std::lround(v * (height - 1))), 0, height - 1);
    int sum = 0, n = 0;
    for (int y = std::max(0, cy - 2); y <= std::min(height - 1, cy + 2); ++y)
        for (int x = std::max(0, cx - 2); x <= std::min(width - 1, cx + 2); ++x) {
            sum += pixels[std::size_t(y * width + x)];
            ++n;
        }
    return std::uint8_t(sum / std::max(1, n));
}

std::uint8_t DepthMap::percentile(double fraction) const
{
    if (!*this)
        return 0;
    std::array<std::size_t, 256> counts{};
    for (std::uint8_t p : pixels)
        ++counts[p];
    const std::size_t wanted = std::size_t(std::clamp(fraction, 0.0, 1.0) * double(pixels.size()));
    std::size_t seen = 0;
    for (int value = 0; value < 256; ++value) {
        seen += counts[std::size_t(value)];
        if (seen > wanted)
            return std::uint8_t(value);
    }
    return 255;
}

bool setDepthModel(const std::string& packagePath, std::string* error)
{
    std::lock_guard lock(g_mutex);
    if (packagePath == g_path && (g_model || packagePath.empty()))
        return true;
    if (g_model) {
        [g_model release];
        g_model = nil;
    }
    g_path.clear();
    if (packagePath.empty())
        return true;
    @autoreleasepool {
        namespace fs = std::filesystem;
        std::error_code ec;
        const fs::path package(packagePath);
        if (!fs::is_directory(package, ec)) {
            if (error)
                *error = TVE_TR("core", "The depth model is not there.");
            return false;
        }
        // Compiled once, beside the package (again if the package is newer).
        fs::path compiled = package;
        compiled.replace_extension(".mlmodelc");
        if (!fs::is_directory(compiled, ec) || fs::last_write_time(compiled, ec) < fs::last_write_time(package, ec)) {
            NSError* failure = nil;
            NSURL* made = [MLModel compileModelAtURL:[NSURL fileURLWithPath:@(packagePath.c_str())] error:&failure];
            if (!made) {
                if (error)
                    *error = TVE_TR("core", "The depth model could not be prepared.");
                return false;
            }
            fs::remove_all(compiled, ec);
            fs::rename(fs::path(made.path.UTF8String), compiled, ec);
            if (ec) { // (another volume: copy instead)
                fs::copy(fs::path(made.path.UTF8String), compiled, fs::copy_options::recursive, ec);
                fs::remove_all(fs::path(made.path.UTF8String), ec);
            }
        }
        MLModelConfiguration* configuration = [[[MLModelConfiguration alloc] init] autorelease];
        configuration.computeUnits = MLComputeUnitsAll;
        NSError* failure = nil;
        MLModel* model = [MLModel modelWithContentsOfURL:[NSURL fileURLWithPath:@(compiled.c_str())]
                                           configuration:configuration
                                                   error:&failure];
        VNCoreMLModel* vision = model ? [VNCoreMLModel modelForMLModel:model error:&failure] : nil;
        if (!vision) {
            if (error)
                *error = TVE_TR("core", "The depth model could not be loaded.");
            return false;
        }
        g_model = [vision retain];
        g_path = packagePath;
    }
    return true;
}

bool hasDepthModel()
{
    std::lock_guard lock(g_mutex);
    return g_model != nil;
}

DepthMap estimateDepth(CVPixelBufferRef frame)
{
    std::lock_guard lock(g_mutex);
    DepthMap map;
    if (!g_model || !frame)
        return map;
    @autoreleasepool {
        VNCoreMLRequest* request = [[[VNCoreMLRequest alloc] initWithModel:g_model] autorelease];
        request.imageCropAndScaleOption = VNImageCropAndScaleOptionScaleFill;
        if (!perform(frame, @[request]))
            return map;
        VNPixelBufferObservation* result = request.results.firstObject;
        if (![result isKindOfClass:[VNPixelBufferObservation class]])
            return map;
        CVPixelBufferRef depth = result.pixelBuffer;
        const OSType format = CVPixelBufferGetPixelFormatType(depth);
        if (format != kCVPixelFormatType_OneComponent16Half && format != kCVPixelFormatType_OneComponent32Float)
            return map;
        CVPixelBufferLockBaseAddress(depth, kCVPixelBufferLock_ReadOnly);
        const int width = int(CVPixelBufferGetWidth(depth));
        const int height = int(CVPixelBufferGetHeight(depth));
        const std::size_t stride = CVPixelBufferGetBytesPerRow(depth);
        const auto* base = static_cast<const std::uint8_t*>(CVPixelBufferGetBaseAddress(depth));
        std::vector<float> values(std::size_t(width * height));
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x) {
                const std::uint8_t* row = base + std::size_t(y) * stride;
                values[std::size_t(y * width + x)] = format == kCVPixelFormatType_OneComponent16Half
                                                        ? float(reinterpret_cast<const _Float16*>(row)[x])
                                                        : reinterpret_cast<const float*>(row)[x];
            }
        CVPixelBufferUnlockBaseAddress(depth, kCVPixelBufferLock_ReadOnly);
        // The model says how near (larger: nearer), on its own scale: spread over 0..255 from
        // its 2nd to its 98th percentile in this frame.
        std::vector<float> sorted = values;
        const std::size_t lo = sorted.size() / 50, hi = sorted.size() - 1 - sorted.size() / 50;
        std::nth_element(sorted.begin(), sorted.begin() + std::ptrdiff_t(lo), sorted.end());
        const float low = sorted[lo];
        std::nth_element(sorted.begin(), sorted.begin() + std::ptrdiff_t(hi), sorted.end());
        const float high = sorted[hi];
        const float span = std::max(1e-6f, high - low);
        map.width = width;
        map.height = height;
        map.pixels.resize(values.size());
        for (std::size_t i = 0; i < values.size(); ++i)
            map.pixels[i] = std::uint8_t(std::clamp((values[i] - low) / span, 0.0f, 1.0f) * 255.0f + 0.5f);
    }
    return map;
}

std::uint8_t focusNearness(const DepthMap& depth, const DepthOfField& settings, const std::vector<FaceBox>& faces)
{
    if (!depth)
        return 255;
    if (!settings.autoFocus)
        return depth.at(settings.focusX, settings.focusY);
    if (!faces.empty()) {
        const FaceBox& face = *std::ranges::max_element(faces, {}, [](const FaceBox& f) { return f.width * f.height; });
        return depth.at(face.x, face.y);
    }
    return depth.percentile(0.9);
}

} // namespace tve
