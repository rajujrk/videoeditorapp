#pragma once

// What the Vision analyses share (Objective-C++ only): frames Vision can read, running
// requests, walking a file's frames, and JSON kept in the analysis cache.

#include "analysis/MediaAnalysis.h"
#include "decode/VideoDecoder.h"
#include "model/Translate.h"

#import <Foundation/Foundation.h>
#import <Vision/Vision.h>
#include <CoreVideo/CoreVideo.h>
#include <VideoToolbox/VideoToolbox.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <string>

namespace tve::vision {

// Vision reads 8-bit 4:2:0 and BGRA; anything else (10-bit P010) is converted to BGRA first.
// Returns a buffer with a retain the caller releases.
inline CVPixelBufferRef readable(CVPixelBufferRef frame)
{
    switch (CVPixelBufferGetPixelFormatType(frame)) {
    case kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange:
    case kCVPixelFormatType_420YpCbCr8BiPlanarFullRange:
    case kCVPixelFormatType_32BGRA:
        return CVPixelBufferRetain(frame);
    default:
        break;
    }
    VTPixelTransferSessionRef session = nullptr;
    if (VTPixelTransferSessionCreate(kCFAllocatorDefault, &session) != noErr)
        return nullptr;
    CVPixelBufferRef converted = nullptr;
    NSDictionary* attributes = @{(id)kCVPixelBufferIOSurfacePropertiesKey: @{}};
    if (CVPixelBufferCreate(kCFAllocatorDefault, CVPixelBufferGetWidth(frame), CVPixelBufferGetHeight(frame),
                            kCVPixelFormatType_32BGRA, (__bridge CFDictionaryRef)attributes, &converted)
            == kCVReturnSuccess
        && VTPixelTransferSessionTransferImage(session, frame, converted) != noErr) {
        CVPixelBufferRelease(converted);
        converted = nullptr;
    }
    VTPixelTransferSessionInvalidate(session);
    CFRelease(session);
    return converted;
}

// Runs requests on a frame; false if Vision failed.
inline bool perform(CVPixelBufferRef frame, NSArray<VNRequest*>* requests)
{
    CVPixelBufferRef buffer = readable(frame);
    if (!buffer)
        return false;
    VNImageRequestHandler* handler = [[VNImageRequestHandler alloc] initWithCVPixelBuffer:buffer options:@{}];
    NSError* error = nil;
    const BOOL ok = [handler performRequests:requests error:&error];
    [handler release];
    CVPixelBufferRelease(buffer);
    return ok;
}

inline std::string cachePath(const AnalysisCache& cache, const std::string& fingerprint, const char* kind)
{
    std::string name = fingerprint;
    std::ranges::replace(name, ':', '_');
    return (std::filesystem::path(cache.directory()) / (name + "." + kind + ".json")).string();
}

inline std::optional<nlohmann::json> readJson(const std::string& path, int version)
{
    std::ifstream in(path);
    if (!in)
        return std::nullopt;
    try {
        nlohmann::json json = nlohmann::json::parse(in);
        if (json.value("version", 0) != version)
            return std::nullopt;
        return json;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

inline bool writeJson(const std::string& path, const nlohmann::json& json)
{
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
    std::ofstream out(path + ".part");
    out << json.dump();
    out.close();
    if (!out)
        return false;
    std::filesystem::rename(path + ".part", path, ec);
    return !ec;
}

// Every frame of a file, `perFrame` on those at least `spacing` apart.
template <typename F>
bool eachFrame(const std::string& path, Ticks spacing, const AnalysisProgress& progress, std::string* error, F perFrame)
{
    auto decoder = VideoDecoder::open(path, error);
    if (!decoder) {
        if (error && error->empty())
            *error = TVE_TR("core", "This file has no pictures to look at.");
        return false;
    }
    const double total = std::max(1e-3, toSeconds(decoder->info().duration));
    Ticks next = std::numeric_limits<Ticks>::min();
    int count = 0;
    while (auto frame = decoder->nextFrame()) {
        if (frame->pts() < next)
            continue;
        next = frame->pts() + spacing - spacing / 10; // a little early, for timestamps that wobble
        @autoreleasepool {
            perFrame(*frame);
        }
        if (progress && ++count % 10 == 0 && !progress(std::min(1.0, toSeconds(frame->pts()) / total)))
            return false;
    }
    return true;
}

} // namespace tve::vision
