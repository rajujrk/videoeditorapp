#include "analysis/ContentVision.h"
#include "analysis/VisionSupport.h"

#include <algorithm>

namespace tve {

using namespace vision;

namespace {

constexpr std::size_t kMostLabels = 12;
constexpr float kTextThreshold = 0.4f;

NSArray<VNRequest*>* contentRequests(VNClassifyImageRequest** classify, VNRecognizeTextRequest** recognize)
{
    *classify = [[[VNClassifyImageRequest alloc] init] autorelease];
    *recognize = [[[VNRecognizeTextRequest alloc] init] autorelease];
    (*recognize).recognitionLevel = VNRequestTextRecognitionLevelAccurate;
    (*recognize).usesLanguageCorrection = YES;
    (*recognize).minimumTextHeight = 0.02f; // smaller than this is not worth searching for
    if (@available(macOS 13.0, *))
        (*recognize).automaticallyDetectsLanguage = YES;
    return @[*classify, *recognize];
}

ContentFrame frameFrom(Ticks time, VNClassifyImageRequest* classify, VNRecognizeTextRequest* recognize)
{
    ContentFrame frame{time, {}, {}};
    for (VNClassificationObservation* label in classify.results) {
        if (label.confidence < kLabelThreshold)
            continue;
        frame.labels.push_back({std::string(label.identifier.UTF8String), label.confidence});
    }
    std::ranges::stable_sort(frame.labels, [](const ContentLabel& a, const ContentLabel& b) { return a.confidence > b.confidence; });
    if (frame.labels.size() > kMostLabels)
        frame.labels.resize(kMostLabels);
    for (VNRecognizedTextObservation* line in recognize.results) {
        VNRecognizedText* best = [line topCandidates:1].firstObject;
        if (!best || best.confidence < kTextThreshold)
            continue;
        const std::string text = best.string.UTF8String;
        if (text.find_first_not_of(" \t") == std::string::npos)
            continue;
        const CGRect r = line.boundingBox; // origin bottom left
        frame.text.push_back({text, r.origin.x + r.size.width / 2, 1.0 - (r.origin.y + r.size.height / 2), r.size.width,
                              r.size.height, best.confidence});
    }
    std::ranges::stable_sort(frame.text, [](const ScreenText& a, const ScreenText& b) { return a.y < b.y; });
    return frame;
}

} // namespace

ContentFrame describeFrame(CVPixelBufferRef frame, Ticks time)
{
    VNClassifyImageRequest* classify = nil;
    VNRecognizeTextRequest* recognize = nil;
    @autoreleasepool {
        NSArray<VNRequest*>* requests = contentRequests(&classify, &recognize);
        if (!perform(frame, requests))
            return {time, {}, {}};
        return frameFrom(time, classify, recognize);
    }
}

std::optional<ContentFrame> describeImage(const std::string& path, std::string* error)
{
    @autoreleasepool {
        NSURL* url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:path.c_str()]];
        VNClassifyImageRequest* classify = nil;
        VNRecognizeTextRequest* recognize = nil;
        NSArray<VNRequest*>* requests = contentRequests(&classify, &recognize);
        VNImageRequestHandler* handler = [[[VNImageRequestHandler alloc] initWithURL:url options:@{}] autorelease];
        NSError* failure = nil;
        if (![handler performRequests:requests error:&failure]) {
            if (error)
                *error = TVE_TR("core", "Could not look at this picture.");
            return std::nullopt;
        }
        return frameFrom(0, classify, recognize);
    }
}

std::optional<ContentIndex> analyzeContent(const std::string& path, bool still, const AnalysisProgress& progress,
                                           std::string* error)
{
    ContentIndex index;
    if (still) {
        auto frame = describeImage(path, error);
        if (!frame)
            return std::nullopt;
        index.frames.push_back(std::move(*frame));
        return index;
    }
    // The first frame at each whole second (or a hair before: timestamps wobble).
    constexpr Ticks spacing = ContentIndex::kSpacing;
    Ticks next = 0;
    const bool ok = eachFrame(path, 0, progress, error, [&](const DecodedFrame& frame) {
        if (frame.pts() < next - spacing / 50)
            return;
        index.frames.push_back(describeFrame(frame.pixelBuffer(), frame.pts()));
        while (next - spacing / 50 <= frame.pts())
            next += spacing;
    });
    if (!ok)
        return std::nullopt;
    return index;
}

std::optional<ContentIndex> loadContent(const AnalysisCache& cache, const std::string& fingerprint)
{
    const auto json = readJson(cachePath(cache, fingerprint, "content"), ContentIndex::kVersion);
    if (!json)
        return std::nullopt;
    try {
        ContentIndex index;
        for (const auto& f : json->at("frames")) {
            ContentFrame frame{f.at("t").get<Ticks>(), {}, {}};
            for (const auto& l : f.at("labels"))
                frame.labels.push_back({l.at(0).get<std::string>(), l.at(1).get<float>()});
            for (const auto& t : f.at("text"))
                frame.text.push_back({t.at(0).get<std::string>(), t.at(1).get<double>(), t.at(2).get<double>(),
                                      t.at(3).get<double>(), t.at(4).get<double>(), t.at(5).get<float>()});
            index.frames.push_back(std::move(frame));
        }
        return index;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

bool saveContent(const AnalysisCache& cache, const std::string& fingerprint, const ContentIndex& index)
{
    nlohmann::json frames = nlohmann::json::array();
    for (const ContentFrame& f : index.frames) {
        nlohmann::json labels = nlohmann::json::array();
        for (const ContentLabel& l : f.labels)
            labels.push_back({l.name, l.confidence});
        nlohmann::json text = nlohmann::json::array();
        for (const ScreenText& t : f.text)
            text.push_back({t.text, t.x, t.y, t.width, t.height, t.confidence});
        frames.push_back({{"t", f.time}, {"labels", labels}, {"text", text}});
    }
    return writeJson(cachePath(cache, fingerprint, "content"), {{"version", ContentIndex::kVersion}, {"frames", frames}});
}

} // namespace tve
