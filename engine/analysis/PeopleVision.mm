#include "analysis/PeopleVision.h"

#include "decode/VideoDecoder.h"

#import <Foundation/Foundation.h>
#import <Vision/Vision.h>
#include <VideoToolbox/VideoToolbox.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>

namespace tve {

namespace {

constexpr int kFacesVersion = 1;
constexpr int kSubjectVersion = 1;

// Vision reads 8-bit 4:2:0 and BGRA; anything else (10-bit P010) is converted to BGRA first.
// Returns a buffer with a retain the caller releases.
CVPixelBufferRef readable(CVPixelBufferRef frame)
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
bool perform(CVPixelBufferRef frame, NSArray<VNRequest*>* requests)
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

// Vision's normalized rectangles have their origin at the bottom left.
FaceBox boxOf(CGRect r)
{
    return {r.origin.x + r.size.width / 2, 1.0 - (r.origin.y + r.size.height / 2), r.size.width, r.size.height};
}

std::vector<FaceBox> facesFrom(VNDetectFaceRectanglesRequest* request)
{
    std::vector<FaceBox> faces;
    for (VNFaceObservation* face in request.results)
        faces.push_back(boxOf(face.boundingBox));
    return faces;
}

std::optional<FaceBox> subjectFrom(const std::vector<FaceBox>& faces, VNGenerateAttentionBasedSaliencyImageRequest* saliency)
{
    if (!faces.empty()) {
        double left = 1, top = 1, right = 0, bottom = 0;
        for (const FaceBox& f : faces) {
            left = std::min(left, f.x - f.width / 2);
            right = std::max(right, f.x + f.width / 2);
            top = std::min(top, f.y - f.height / 2);
            bottom = std::max(bottom, f.y + f.height / 2);
        }
        // Faces far apart cannot all be kept in a narrower frame: follow the largest.
        if (right - left > 0.5)
            return *std::ranges::max_element(faces, {}, [](const FaceBox& f) { return f.width * f.height; });
        return FaceBox{(left + right) / 2, (top + bottom) / 2, right - left, bottom - top};
    }
    VNSaliencyImageObservation* observation = saliency.results.firstObject;
    VNRectangleObservation* best = nil;
    for (VNRectangleObservation* object in observation.salientObjects) {
        if (!best || object.confidence > best.confidence)
            best = object;
    }
    if (best)
        return boxOf(best.boundingBox);
    return std::nullopt;
}

std::string cachePath(const AnalysisCache& cache, const std::string& fingerprint, const char* kind)
{
    std::string name = fingerprint;
    std::ranges::replace(name, ':', '_');
    return (std::filesystem::path(cache.directory()) / (name + "." + kind + ".json")).string();
}

std::optional<nlohmann::json> readJson(const std::string& path, int version)
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

bool writeJson(const std::string& path, const nlohmann::json& json)
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

nlohmann::json boxJson(const FaceBox& b)
{
    return {b.x, b.y, b.width, b.height};
}

FaceBox boxFrom(const nlohmann::json& j)
{
    return {j.at(0).get<double>(), j.at(1).get<double>(), j.at(2).get<double>(), j.at(3).get<double>()};
}

// Every frame of a file, `perFrame` on those at least `spacing` apart.
template <typename F>
bool eachFrame(const std::string& path, Ticks spacing, const AnalysisProgress& progress, std::string* error, F perFrame)
{
    auto decoder = VideoDecoder::open(path, error);
    if (!decoder) {
        if (error && error->empty())
            *error = "This file has no pictures to look at.";
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

} // namespace

PersonMatte segmentPeople(CVPixelBufferRef frame, SegmentationQuality quality)
{
    PersonMatte matte;
    if (!frame)
        return matte;
    @autoreleasepool {
        VNGeneratePersonSegmentationRequest* request = [[VNGeneratePersonSegmentationRequest alloc] init];
        request.qualityLevel = quality == SegmentationQuality::Fast       ? VNGeneratePersonSegmentationRequestQualityLevelFast
                               : quality == SegmentationQuality::Accurate ? VNGeneratePersonSegmentationRequestQualityLevelAccurate
                                                                          : VNGeneratePersonSegmentationRequestQualityLevelBalanced;
        request.outputPixelFormat = kCVPixelFormatType_OneComponent8;
        if (perform(frame, @[request])) {
            VNPixelBufferObservation* observation = request.results.firstObject;
            CVPixelBufferRef mask = observation.pixelBuffer;
            if (mask && CVPixelBufferLockBaseAddress(mask, kCVPixelBufferLock_ReadOnly) == kCVReturnSuccess) {
                matte.width = int(CVPixelBufferGetWidth(mask));
                matte.height = int(CVPixelBufferGetHeight(mask));
                const auto* base = static_cast<const std::uint8_t*>(CVPixelBufferGetBaseAddress(mask));
                const std::size_t stride = CVPixelBufferGetBytesPerRow(mask);
                matte.pixels.resize(std::size_t(matte.width) * std::size_t(matte.height));
                for (int y = 0; y < matte.height; ++y)
                    std::copy_n(base + std::size_t(y) * stride, matte.width,
                                matte.pixels.begin() + std::ptrdiff_t(y) * matte.width);
                CVPixelBufferUnlockBaseAddress(mask, kCVPixelBufferLock_ReadOnly);
            }
        }
        [request release];
    }
    return matte;
}

PersonMatte segmentSubject(CVPixelBufferRef frame, const FaceBox& box)
{
    PersonMatte matte;
    if (!frame)
        return matte;
    @autoreleasepool {
        VNGenerateForegroundInstanceMaskRequest* request = [[VNGenerateForegroundInstanceMaskRequest alloc] init];
        if (perform(frame, @[request])) {
            VNInstanceMaskObservation* observation = request.results.firstObject;
            CVPixelBufferRef labels = observation.instanceMask; // one byte per pixel: 0 background, 1… instances
            if (labels && CVPixelBufferLockBaseAddress(labels, kCVPixelBufferLock_ReadOnly) == kCVReturnSuccess) {
                const int w = int(CVPixelBufferGetWidth(labels)), h = int(CVPixelBufferGetHeight(labels));
                const auto* base = static_cast<const std::uint8_t*>(CVPixelBufferGetBaseAddress(labels));
                const std::size_t stride = CVPixelBufferGetBytesPerRow(labels);
                // How much of each instance lies in the box, and how big each is.
                std::array<int, 256> inside{}, total{};
                const int x0 = int((box.x - box.width / 2) * w), x1 = int((box.x + box.width / 2) * w);
                const int y0 = int((box.y - box.height / 2) * h), y1 = int((box.y + box.height / 2) * h);
                for (int y = 0; y < h; ++y)
                    for (int x = 0; x < w; ++x) {
                        const std::uint8_t label = base[std::size_t(y) * stride + std::size_t(x)];
                        if (label == 0)
                            continue;
                        ++total[label];
                        if (x >= x0 && x < x1 && y >= y0 && y < y1)
                            ++inside[label];
                    }
                // The instances mostly inside the box; failing that, the one it covers most.
                std::array<bool, 256> kept{};
                int best = 0;
                bool any = false;
                for (int l = 1; l < 256; ++l) {
                    if (inside[std::size_t(l)] > inside[std::size_t(best)])
                        best = l;
                    if (total[std::size_t(l)] > 0 && inside[std::size_t(l)] * 2 >= total[std::size_t(l)]) {
                        kept[std::size_t(l)] = true;
                        any = true;
                    }
                }
                if (!any && best > 0) {
                    kept[std::size_t(best)] = true;
                    any = true;
                }
                if (any) {
                    matte.width = w;
                    matte.height = h;
                    matte.pixels.resize(std::size_t(w) * std::size_t(h));
                    for (int y = 0; y < h; ++y)
                        for (int x = 0; x < w; ++x)
                            matte.pixels[std::size_t(y) * std::size_t(w) + std::size_t(x)] =
                                kept[base[std::size_t(y) * stride + std::size_t(x)]] ? 255 : 0;
                }
                CVPixelBufferUnlockBaseAddress(labels, kCVPixelBufferLock_ReadOnly);
            }
        }
        [request release];
    }
    return matte;
}

std::vector<FaceBox> detectFaces(CVPixelBufferRef frame)
{
    std::vector<FaceBox> faces;
    if (!frame)
        return faces;
    @autoreleasepool {
        VNDetectFaceRectanglesRequest* request = [[VNDetectFaceRectanglesRequest alloc] init];
        if (perform(frame, @[request]))
            faces = facesFrom(request);
        [request release];
    }
    return faces;
}

std::optional<FaceBox> findSubjectIn(CVPixelBufferRef frame)
{
    std::optional<FaceBox> subject;
    if (!frame)
        return subject;
    @autoreleasepool {
        VNDetectFaceRectanglesRequest* faces = [[VNDetectFaceRectanglesRequest alloc] init];
        VNGenerateAttentionBasedSaliencyImageRequest* saliency = [[VNGenerateAttentionBasedSaliencyImageRequest alloc] init];
        if (perform(frame, @[faces, saliency]))
            subject = subjectFrom(facesFrom(faces), saliency);
        [faces release];
        [saliency release];
    }
    return subject;
}

std::optional<std::vector<FaceFrame>> findFaces(const std::string& path, const AnalysisProgress& progress, std::string* error)
{
    std::vector<FaceFrame> frames;
    const bool ok = eachFrame(path, kTicksPerSecond / 30, progress, error,
                              [&](const DecodedFrame& frame) { frames.push_back({frame.pts(), detectFaces(frame.pixelBuffer())}); });
    if (!ok)
        return std::nullopt;
    return frames;
}

std::optional<std::vector<SubjectSample>> findSubject(const std::string& path, const AnalysisProgress& progress,
                                                      std::string* error)
{
    std::vector<SubjectSample> samples;
    const bool ok = eachFrame(path, kTicksPerSecond / 10, progress, error, [&](const DecodedFrame& frame) {
        if (auto subject = findSubjectIn(frame.pixelBuffer()))
            samples.push_back({frame.pts(), *subject});
    });
    if (!ok)
        return std::nullopt;
    return samples;
}

std::optional<std::vector<FaceFrame>> loadFaces(const AnalysisCache& cache, const std::string& fingerprint)
{
    const auto json = readJson(cachePath(cache, fingerprint, "faces"), kFacesVersion);
    if (!json)
        return std::nullopt;
    try {
        std::vector<FaceFrame> frames;
        for (const auto& f : json->at("frames")) {
            FaceFrame frame{f.at("t").get<Ticks>(), {}};
            for (const auto& b : f.at("faces"))
                frame.faces.push_back(boxFrom(b));
            frames.push_back(std::move(frame));
        }
        return frames;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

bool saveFaces(const AnalysisCache& cache, const std::string& fingerprint, const std::vector<FaceFrame>& frames)
{
    nlohmann::json list = nlohmann::json::array();
    for (const FaceFrame& f : frames) {
        nlohmann::json faces = nlohmann::json::array();
        for (const FaceBox& b : f.faces)
            faces.push_back(boxJson(b));
        list.push_back({{"t", f.time}, {"faces", faces}});
    }
    return writeJson(cachePath(cache, fingerprint, "faces"), {{"version", kFacesVersion}, {"frames", list}});
}

std::optional<std::vector<SubjectSample>> loadSubject(const AnalysisCache& cache, const std::string& fingerprint)
{
    const auto json = readJson(cachePath(cache, fingerprint, "subject"), kSubjectVersion);
    if (!json)
        return std::nullopt;
    try {
        std::vector<SubjectSample> samples;
        for (const auto& s : json->at("samples"))
            samples.push_back({s.at("t").get<Ticks>(), boxFrom(s.at("box"))});
        return samples;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

bool saveSubject(const AnalysisCache& cache, const std::string& fingerprint, const std::vector<SubjectSample>& samples)
{
    nlohmann::json list = nlohmann::json::array();
    for (const SubjectSample& s : samples)
        list.push_back({{"t", s.time}, {"box", boxJson(s.box)}});
    return writeJson(cachePath(cache, fingerprint, "subject"), {{"version", kSubjectVersion}, {"samples", list}});
}

} // namespace tve
