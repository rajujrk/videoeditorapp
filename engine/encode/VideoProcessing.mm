#include "encode/VideoProcessing.h"

#include "decode/VideoDecoder.h"
#include "encode/MediaWriter.h"

#import <Foundation/Foundation.h>
#import <VideoToolbox/VideoToolbox.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <deque>
#include <filesystem>

namespace tve {

namespace {

void fail(std::string* error, std::string message)
{
    if (error)
        *error = std::move(message);
}

CMTime timeOf(Ticks t)
{
    return CMTimeMake(t / 1000, int32_t(kTicksPerSecond / 1000));
}

// A pixel buffer (retained) of a format and size, usable by Metal and the frame processors.
CVPixelBufferRef makeBuffer(int width, int height, OSType format, NSDictionary* attributes = nil)
{
    NSMutableDictionary* a = attributes ? [[attributes mutableCopy] autorelease] : [NSMutableDictionary dictionary];
    a[(id)kCVPixelBufferIOSurfacePropertiesKey] = @{};
    a[(id)kCVPixelBufferMetalCompatibilityKey] = @YES;
    [a removeObjectForKey:(id)kCVPixelBufferWidthKey];
    [a removeObjectForKey:(id)kCVPixelBufferHeightKey];
    [a removeObjectForKey:(id)kCVPixelBufferPixelFormatTypeKey];
    CVPixelBufferRef buffer = nullptr;
    if (CVPixelBufferCreate(kCFAllocatorDefault, std::size_t(width), std::size_t(height), format, (CFDictionaryRef)a, &buffer)
        != kCVReturnSuccess)
        return nullptr;
    return buffer;
}

// Format and size conversion (and scaling) between pixel buffers.
class Transfer {
public:
    Transfer() { VTPixelTransferSessionCreate(kCFAllocatorDefault, &m_session); }
    ~Transfer()
    {
        if (m_session) {
            VTPixelTransferSessionInvalidate(m_session);
            CFRelease(m_session);
        }
    }
    bool operator()(CVPixelBufferRef from, CVPixelBufferRef to)
    {
        return m_session && VTPixelTransferSessionTransferImage(m_session, from, to) == noErr;
    }

private:
    VTPixelTransferSessionRef m_session = nullptr;
};

// The copy being written: HEVC in a MOV at four times the usual bitrate, keyframes every
// half second (cheap to seek), pictures only; frames go in as any pixel buffer.
class Copy {
public:
    bool open(const std::string& output, int width, int height, FrameRate rate, std::string* error)
    {
        m_part = output + ".part.mov";
        MediaSettings settings;
        settings.container = Container::Mov;
        settings.codec = VideoCodec::Hevc;
        settings.width = width & ~1;
        settings.height = height & ~1;
        settings.frameRate = rate;
        settings.audio = false;
        settings.keyframeInterval = std::max(1, rate.num / std::max(1, rate.den) / 2);
        settings.videoBitrate = 4 * defaultVideoBitrate(settings);
        std::filesystem::create_directories(std::filesystem::path(output).parent_path());
        m_writer = MediaWriter::open(m_part, settings, error);
        m_nv12 = makeBuffer(settings.width, settings.height, kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange);
        return m_writer && m_nv12;
    }
    ~Copy()
    {
        if (m_nv12)
            CVPixelBufferRelease(m_nv12);
        if (!m_done) {
            m_writer.reset();
            std::error_code ec;
            std::filesystem::remove(m_part, ec);
        }
    }
    bool write(CVPixelBufferRef frame)
    {
        if (!m_transfer(frame, m_nv12) || CVPixelBufferLockBaseAddress(m_nv12, kCVPixelBufferLock_ReadOnly) != kCVReturnSuccess)
            return false;
        const bool ok = m_writer->writeVideo(static_cast<const std::uint8_t*>(CVPixelBufferGetBaseAddressOfPlane(m_nv12, 0)),
                                             int(CVPixelBufferGetBytesPerRowOfPlane(m_nv12, 0)),
                                             static_cast<const std::uint8_t*>(CVPixelBufferGetBaseAddressOfPlane(m_nv12, 1)),
                                             int(CVPixelBufferGetBytesPerRowOfPlane(m_nv12, 1)));
        CVPixelBufferUnlockBaseAddress(m_nv12, kCVPixelBufferLock_ReadOnly);
        return ok;
    }
    bool finish(const std::string& output, std::string* error)
    {
        if (!m_writer->finish()) {
            fail(error, "The processed copy could not be written.");
            return false;
        }
        m_writer.reset();
        std::error_code ec;
        std::filesystem::rename(m_part, output, ec);
        if (ec) {
            fail(error, "The processed copy could not be kept.");
            return false;
        }
        m_done = true;
        return true;
    }

private:
    std::unique_ptr<MediaWriter> m_writer;
    CVPixelBufferRef m_nv12 = nullptr;
    Transfer m_transfer;
    std::string m_part;
    bool m_done = false;
};

// Opens the source at `from`; its first frame's size and rate.
std::unique_ptr<VideoDecoder> openAt(const std::string& path, Ticks from, std::string* error)
{
    auto decoder = VideoDecoder::open(path, error);
    if (!decoder || !decoder->seek(from)) {
        if (error && error->empty())
            *error = "This file has no pictures to work on.";
        return nullptr;
    }
    return decoder;
}

bool reportProgress(const AnalysisProgress& progress, Ticks at, Ticks from, Ticks to)
{
    return !progress || progress(std::clamp(double(at - from) / double(std::max<Ticks>(1, to - from)), 0.0, 1.0));
}

} // namespace

// --- upscaling ---

namespace {

std::atomic<bool> g_downloading{false};

VTSuperResolutionScalerConfiguration* upscaleConfiguration(int width, int height) // retained, or nil
{
    if (!VTSuperResolutionScalerConfiguration.isSupported)
        return nil;
    return [[VTSuperResolutionScalerConfiguration alloc]
        initWithFrameWidth:width
               frameHeight:height
               scaleFactor:4
                 inputType:VTSuperResolutionScalerConfigurationInputTypeVideo
        usePrecomputedFlow:NO
     qualityPrioritization:VTSuperResolutionScalerConfigurationQualityPrioritizationNormal
                  revision:VTSuperResolutionScalerConfiguration.defaultRevision];
}

} // namespace

UpscaleModel upscaleModelStatus()
{
    @autoreleasepool {
        VTSuperResolutionScalerConfiguration* c = upscaleConfiguration(1280, 720);
        if (!c)
            return UpscaleModel::Unsupported;
        const auto status = c.configurationModelStatus;
        [c release];
        if (status == VTSuperResolutionScalerConfigurationModelStatusReady)
            return UpscaleModel::Ready;
        if (status == VTSuperResolutionScalerConfigurationModelStatusDownloading || g_downloading)
            return UpscaleModel::Downloading;
        return UpscaleModel::NeedsDownload;
    }
}

double upscaleModelProgress()
{
    @autoreleasepool {
        VTSuperResolutionScalerConfiguration* c = upscaleConfiguration(1280, 720);
        const double p = c ? c.configurationModelPercentageAvailable : 0.0;
        [c release];
        return p;
    }
}

void downloadUpscaleModel(std::function<void(bool, std::string)> done)
{
    VTSuperResolutionScalerConfiguration* c = upscaleConfiguration(1280, 720);
    if (!c) {
        done(false, "Upscaling is not available on this Mac.");
        return;
    }
    g_downloading = true;
    auto callback = std::make_shared<std::function<void(bool, std::string)>>(std::move(done));
    [c downloadConfigurationModelWithCompletionHandler:^(NSError* error) {
        g_downloading = false;
        // (It can report an error and still leave the model ready: the status decides.)
        const bool ready = upscaleModelStatus() == UpscaleModel::Ready;
        (*callback)(ready, ready || !error ? std::string() : std::string(error.localizedDescription.UTF8String));
    }];
    [c release];
}

bool upscaleVideo(const std::string& path, Ticks from, Ticks to, int scale, const std::string& output,
                  const AnalysisProgress& progress, std::string* error)
{
    auto decoder = openAt(path, from, error);
    if (!decoder)
        return false;
    const VideoStreamInfo info = decoder->info();
    if (info.width > 1920 || info.height > 1080) {
        fail(error, "Upscaling works on pictures up to 1920 × 1080.");
        return false;
    }
    scale = scale >= 4 ? 4 : 2;
    while (scale > 2 && std::max(info.width, info.height) * scale > 7680)
        scale /= 2;
    @autoreleasepool {
        VTSuperResolutionScalerConfiguration* configuration = upscaleConfiguration(info.width, info.height);
        if (!configuration) {
            fail(error, "Upscaling is not available on this Mac.");
            return false;
        }
        [configuration autorelease];
        if (configuration.configurationModelStatus != VTSuperResolutionScalerConfigurationModelStatusReady) {
            fail(error, "Download Apple's upscaling model first.");
            return false;
        }
        VTFrameProcessor* processor = [[[VTFrameProcessor alloc] init] autorelease];
        NSError* failure = nil;
        if (![processor startSessionWithConfiguration:configuration error:&failure]) {
            fail(error, std::string("Upscaling could not start: ") + failure.localizedDescription.UTF8String);
            return false;
        }
        Copy copy;
        if (!copy.open(output, info.width * scale, info.height * scale, info.frameRate, error)) {
            [processor endSession];
            return false;
        }
        const OSType format = OSType([configuration.frameSupportedPixelFormats.firstObject unsignedIntValue]);
        Transfer transfer;
        CVPixelBufferRef previousSource = nullptr, previousOutput = nullptr;
        bool ok = true;
        while (ok) {
            auto frame = decoder->nextFrame();
            if (!frame || frame->pts() >= to)
                break;
            @autoreleasepool {
                CVPixelBufferRef source = makeBuffer(info.width, info.height, format, configuration.sourcePixelBufferAttributes);
                CVPixelBufferRef result = makeBuffer(info.width * 4, info.height * 4, format,
                                                     configuration.destinationPixelBufferAttributes);
                ok = source && result && transfer(frame->pixelBuffer(), source);
                if (ok) {
                    const CMTime t = timeOf(frame->pts());
                    VTFrameProcessorFrame* in = [[[VTFrameProcessorFrame alloc] initWithBuffer:source presentationTimeStamp:t] autorelease];
                    VTFrameProcessorFrame* out = [[[VTFrameProcessorFrame alloc] initWithBuffer:result presentationTimeStamp:t] autorelease];
                    VTFrameProcessorFrame* before =
                        previousSource ? [[[VTFrameProcessorFrame alloc] initWithBuffer:previousSource
                                                                presentationTimeStamp:CMTimeSubtract(t, CMTimeMake(1, 600))] autorelease]
                                       : nil;
                    VTFrameProcessorFrame* beforeOut =
                        previousOutput ? [[[VTFrameProcessorFrame alloc] initWithBuffer:previousOutput
                                                                presentationTimeStamp:CMTimeSubtract(t, CMTimeMake(1, 600))] autorelease]
                                       : nil;
                    VTSuperResolutionScalerParameters* parameters = [[[VTSuperResolutionScalerParameters alloc]
                        initWithSourceFrame:in
                              previousFrame:before
                        previousOutputFrame:beforeOut
                                opticalFlow:nil
                             submissionMode:VTSuperResolutionScalerParametersSubmissionModeSequential
                           destinationFrame:out] autorelease];
                    ok = parameters && [processor processWithParameters:parameters error:&failure];
                    // Scaled to the size asked for (2×: half of what came out) as it is written.
                    ok = ok && copy.write(result);
                }
                if (previousSource)
                    CVPixelBufferRelease(previousSource);
                if (previousOutput)
                    CVPixelBufferRelease(previousOutput);
                previousSource = source;
                previousOutput = result;
            }
            if (ok && !reportProgress(progress, frame->pts(), from, to)) {
                ok = false;
                failure = nil;
                if (error)
                    error->clear();
                [processor endSession];
                if (previousSource)
                    CVPixelBufferRelease(previousSource);
                if (previousOutput)
                    CVPixelBufferRelease(previousOutput);
                return false;
            }
        }
        [processor endSession];
        if (previousSource)
            CVPixelBufferRelease(previousSource);
        if (previousOutput)
            CVPixelBufferRelease(previousOutput);
        if (!ok) {
            fail(error, std::string("Upscaling failed") + (failure ? std::string(": ") + failure.localizedDescription.UTF8String : "."));
            return false;
        }
        return copy.finish(output, error);
    }
}

// --- smooth slow motion ---

bool smoothVideo(const std::string& path, Ticks from, Ticks to, int factor, const std::string& output,
                 const AnalysisProgress& progress, std::string* error)
{
    factor = std::clamp(factor, 2, 8);
    auto decoder = openAt(path, from, error);
    if (!decoder)
        return false;
    const VideoStreamInfo info = decoder->info();
    @autoreleasepool {
        if (!VTFrameRateConversionConfiguration.isSupported) {
            fail(error, "Smooth slow motion is not available on this Mac.");
            return false;
        }
        VTFrameRateConversionConfiguration* configuration = [[[VTFrameRateConversionConfiguration alloc]
               initWithFrameWidth:info.width
                      frameHeight:info.height
               usePrecomputedFlow:NO
            qualityPrioritization:VTFrameRateConversionConfigurationQualityPrioritizationQuality
                         revision:VTFrameRateConversionConfiguration.defaultRevision] autorelease];
        if (!configuration) {
            fail(error, "Smooth slow motion does not work on pictures of this size.");
            return false;
        }
        VTFrameProcessor* processor = [[[VTFrameProcessor alloc] init] autorelease];
        NSError* failure = nil;
        if (![processor startSessionWithConfiguration:configuration error:&failure]) {
            fail(error, std::string("Smooth slow motion could not start: ") + failure.localizedDescription.UTF8String);
            return false;
        }
        Copy copy;
        if (!copy.open(output, info.width, info.height, FrameRate{info.frameRate.num * factor, info.frameRate.den}, error)) {
            [processor endSession];
            return false;
        }
        const OSType format = OSType([configuration.frameSupportedPixelFormats.firstObject unsignedIntValue]);
        Transfer transfer;
        NSMutableArray<NSNumber*>* phases = [NSMutableArray array];
        for (int k = 1; k < factor; ++k)
            [phases addObject:@(float(k) / float(factor))];
        std::vector<CVPixelBufferRef> between(std::size_t(factor - 1), nullptr);
        for (auto& b : between)
            b = makeBuffer(info.width, info.height, format, configuration.destinationPixelBufferAttributes);
        CVPixelBufferRef previous = nullptr;
        Ticks previousTime = 0;
        bool ok = std::ranges::all_of(between, [](CVPixelBufferRef b) { return b != nullptr; });
        const auto finish = [&] {
            [processor endSession];
            if (previous)
                CVPixelBufferRelease(previous);
            for (auto& b : between)
                if (b)
                    CVPixelBufferRelease(b);
        };
        while (ok) {
            auto frame = decoder->nextFrame();
            if (!frame || frame->pts() >= to)
                break;
            @autoreleasepool {
                CVPixelBufferRef current = makeBuffer(info.width, info.height, format, configuration.sourcePixelBufferAttributes);
                ok = current && transfer(frame->pixelBuffer(), current);
                if (ok && previous) {
                    // The frames between the one before and this one, then this one.
                    VTFrameProcessorFrame* a = [[[VTFrameProcessorFrame alloc] initWithBuffer:previous
                                                                       presentationTimeStamp:timeOf(previousTime)] autorelease];
                    VTFrameProcessorFrame* b = [[[VTFrameProcessorFrame alloc] initWithBuffer:current
                                                                       presentationTimeStamp:timeOf(frame->pts())] autorelease];
                    NSMutableArray<VTFrameProcessorFrame*>* outs = [NSMutableArray array];
                    for (int k = 1; k < factor; ++k) {
                        const Ticks at = previousTime + (frame->pts() - previousTime) * k / factor;
                        [outs addObject:[[[VTFrameProcessorFrame alloc] initWithBuffer:between[std::size_t(k - 1)]
                                                                presentationTimeStamp:timeOf(at)] autorelease]];
                    }
                    VTFrameRateConversionParameters* parameters = [[[VTFrameRateConversionParameters alloc]
                        initWithSourceFrame:a
                                  nextFrame:b
                                opticalFlow:nil
                         interpolationPhase:phases
                             submissionMode:VTFrameRateConversionParametersSubmissionModeSequential
                          destinationFrames:outs] autorelease];
                    ok = parameters && [processor processWithParameters:parameters error:&failure];
                    for (std::size_t k = 0; ok && k < between.size(); ++k)
                        ok = copy.write(between[k]);
                }
                ok = ok && copy.write(current);
                if (previous)
                    CVPixelBufferRelease(previous);
                previous = current;
                previousTime = frame->pts();
            }
            if (ok && !reportProgress(progress, frame->pts(), from, to)) {
                finish();
                if (error)
                    error->clear();
                return false;
            }
        }
        // The last frame stands for the frames that would have followed it.
        for (int k = 1; ok && previous && k < factor; ++k)
            ok = copy.write(previous);
        finish();
        if (!ok) {
            fail(error, std::string("Smooth slow motion failed") + (failure ? std::string(": ") + failure.localizedDescription.UTF8String : "."));
            return false;
        }
        return copy.finish(output, error);
    }
}

// --- object removal ---

namespace {

// A decoded frame as 8-bit BGRA rows, with its luma 320 wide for the camera's motion.
struct Picture {
    Ticks time = 0;
    int width = 0, height = 0;
    std::vector<std::uint8_t> bgra;
    GrayImage small;
    // Where its pixels are relative to the first frame: p_this = s·R·p_first + t (pixels at
    // full size, about the centre).
    double a = 1, b = 0, tx = 0, ty = 0;
};

bool toPicture(Transfer& transfer, CVPixelBufferRef frame, Picture& out)
{
    const int w = int(CVPixelBufferGetWidth(frame)), h = int(CVPixelBufferGetHeight(frame));
    CVPixelBufferRef bgra = makeBuffer(w, h, kCVPixelFormatType_32BGRA);
    if (!bgra || !transfer(frame, bgra) || CVPixelBufferLockBaseAddress(bgra, kCVPixelBufferLock_ReadOnly) != kCVReturnSuccess) {
        if (bgra)
            CVPixelBufferRelease(bgra);
        return false;
    }
    out.width = w;
    out.height = h;
    out.bgra.resize(std::size_t(w) * std::size_t(h) * 4);
    const auto* base = static_cast<const std::uint8_t*>(CVPixelBufferGetBaseAddress(bgra));
    const std::size_t stride = CVPixelBufferGetBytesPerRow(bgra);
    for (int y = 0; y < h; ++y)
        std::copy_n(base + std::size_t(y) * stride, std::size_t(w) * 4, out.bgra.begin() + std::ptrdiff_t(std::size_t(y) * std::size_t(w) * 4));
    CVPixelBufferUnlockBaseAddress(bgra, kCVPixelBufferLock_ReadOnly);
    CVPixelBufferRelease(bgra);
    // Luma at 320 wide, each pixel the mean of its block.
    const int sw = 320, sh = std::max(36, int(std::lround(320.0 * h / w)));
    out.small = {sw, sh, std::vector<float>(std::size_t(sw) * std::size_t(sh))};
    for (int y = 0; y < sh; ++y)
        for (int x = 0; x < sw; ++x) {
            const int x0 = x * w / sw, x1 = std::max(x0 + 1, (x + 1) * w / sw);
            const int y0 = y * h / sh, y1 = std::max(y0 + 1, (y + 1) * h / sh);
            double sum = 0.0;
            for (int yy = y0; yy < y1; yy += 2)
                for (int xx = x0; xx < x1; xx += 2) {
                    const std::uint8_t* p = &out.bgra[(std::size_t(yy) * std::size_t(w) + std::size_t(xx)) * 4];
                    sum += 0.0722 * p[0] + 0.7152 * p[1] + 0.2126 * p[2];
                }
            const int n = ((x1 - x0 + 1) / 2) * ((y1 - y0 + 1) / 2);
            out.small.pixels[std::size_t(y) * std::size_t(sw) + std::size_t(x)] = float(sum / (255.0 * std::max(1, n)));
        }
    return true;
}

// The object's box in a frame (pixels), grown.
struct Box {
    double x0, y0, x1, y1;
    bool contains(double x, double y, double margin = 0) const
    {
        return x >= x0 - margin && x < x1 + margin && y >= y0 - margin && y < y1 + margin;
    }
};

Box boxAt(const std::vector<TrackSample>& object, Ticks time, double expand, int w, int h)
{
    const MotionTrack track{0, false, object};
    const TrackSample s = time <= object.front().time ? object.front()
                        : time >= object.back().time  ? object.back()
                                                      : *track.at(time);
    const double bw = s.width * (1.0 + expand) * w, bh = s.height * (1.0 + expand) * h;
    return {s.x * w - bw / 2, s.y * h - bh / 2, s.x * w + bw / 2, s.y * h + bh / 2};
}

std::array<float, 3> sample(const Picture& p, double x, double y)
{
    const int ix = std::clamp(int(x), 0, p.width - 2), iy = std::clamp(int(y), 0, p.height - 2);
    const double fx = std::clamp(x - ix, 0.0, 1.0), fy = std::clamp(y - iy, 0.0, 1.0);
    std::array<float, 3> c{};
    for (int k = 0; k < 3; ++k) {
        const auto at = [&](int xx, int yy) { return float(p.bgra[(std::size_t(yy) * std::size_t(p.width) + std::size_t(xx)) * 4 + std::size_t(k)]); };
        c[std::size_t(k)] = float((1 - fy) * ((1 - fx) * at(ix, iy) + fx * at(ix + 1, iy)) + fy * ((1 - fx) * at(ix, iy + 1) + fx * at(ix + 1, iy + 1)));
    }
    return c;
}

// Fills the pixels marked unknown from those around them: averaged down a pyramid (known
// pixels only), then brought back up into the holes.
void fillHoles(std::vector<std::array<float, 3>>& colour, std::vector<float>& known, int w, int h)
{
    if (w <= 1 || h <= 1)
        return;
    const int hw = (w + 1) / 2, hh = (h + 1) / 2;
    std::vector<std::array<float, 3>> small(std::size_t(hw) * std::size_t(hh));
    std::vector<float> weight(std::size_t(hw) * std::size_t(hh), 0.0f);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const float k = known[std::size_t(y) * std::size_t(w) + std::size_t(x)];
            if (k <= 0.0f)
                continue;
            auto& s = small[std::size_t(y / 2) * std::size_t(hw) + std::size_t(x / 2)];
            for (int c = 0; c < 3; ++c)
                s[std::size_t(c)] += colour[std::size_t(y) * std::size_t(w) + std::size_t(x)][std::size_t(c)] * k;
            weight[std::size_t(y / 2) * std::size_t(hw) + std::size_t(x / 2)] += k;
        }
    for (std::size_t i = 0; i < small.size(); ++i) {
        if (weight[i] > 0.0f)
            for (auto& v : small[i])
                v /= weight[i];
        weight[i] = std::min(1.0f, weight[i]);
    }
    if (std::ranges::any_of(weight, [](float v) { return v <= 0.0f; }))
        fillHoles(small, weight, hw, hh);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const std::size_t i = std::size_t(y) * std::size_t(w) + std::size_t(x);
            if (known[i] < 1.0f) {
                const auto& s = small[std::size_t(y / 2) * std::size_t(hw) + std::size_t(x / 2)];
                for (int c = 0; c < 3; ++c)
                    colour[i][std::size_t(c)] = colour[i][std::size_t(c)] * known[i] + s[std::size_t(c)] * (1.0f - known[i]);
            }
        }
}

} // namespace

bool removeObject(const std::string& path, Ticks from, Ticks to, const std::vector<TrackSample>& object, double expand,
                  const std::string& output, const AnalysisProgress& progress, std::string* error)
{
    if (object.empty()) {
        fail(error, "Draw a box around what to remove first.");
        return false;
    }
    auto decoder = openAt(path, from, error);
    if (!decoder)
        return false;
    const VideoStreamInfo info = decoder->info();
    Copy copy;
    if (!copy.open(output, info.width, info.height, info.frameRate, error))
        return false;
    Transfer transfer;
    constexpr std::size_t kReach = 15; // frames looked at either side
    std::deque<Picture> window;        // frames around the one being filled
    std::size_t next = 0;              // index in `window` of the next frame to fill
    bool ended = false;
    CVPixelBufferRef out = makeBuffer(info.width, info.height, kCVPixelFormatType_32BGRA);
    if (!out)
        return false;
    const auto release = [&] { CVPixelBufferRelease(out); };

    const auto fillAndWrite = [&](std::size_t index) -> bool {
        Picture& p = window[index];
        const Box hole = boxAt(object, p.time, expand, p.width, p.height);
        const int x0 = std::clamp(int(std::floor(hole.x0)) - 8, 0, p.width), x1 = std::clamp(int(std::ceil(hole.x1)) + 8, 0, p.width);
        const int y0 = std::clamp(int(std::floor(hole.y0)) - 8, 0, p.height), y1 = std::clamp(int(std::ceil(hole.y1)) + 8, 0, p.height);
        const int rw = x1 - x0, rh = y1 - y0;
        std::vector<Picture*> others; // nearest first
        for (std::size_t d = 1; d <= kReach; ++d) {
            if (index + d < window.size())
                others.push_back(&window[index + d]);
            if (index >= d)
                others.push_back(&window[index - d]);
        }
        std::vector<Box> otherHoles;
        for (Picture* o : others)
            otherHoles.push_back(boxAt(object, o->time, expand, o->width, o->height));
        std::vector<std::array<float, 3>> colour(std::size_t(std::max(0, rw)) * std::size_t(std::max(0, rh)));
        std::vector<float> known(colour.size(), 1.0f);
        const double cx = p.width / 2.0, cy = p.height / 2.0;
        // This frame → the first: the inverse of its similarity.
        const double n2 = p.a * p.a + p.b * p.b;
        for (int y = y0; y < y1; ++y)
            for (int x = x0; x < x1; ++x) {
                const std::size_t i = std::size_t(y - y0) * std::size_t(rw) + std::size_t(x - x0);
                const auto here = sample(p, x, y);
                colour[i] = here;
                if (!hole.contains(x + 0.5, y + 0.5))
                    continue;
                // Where this point of the scene is in the other frames.
                const double px = x + 0.5 - cx - p.tx, py = y + 0.5 - cy - p.ty;
                const double fx = (p.a * px + p.b * py) / n2, fy = (-p.b * px + p.a * py) / n2;
                std::array<std::vector<float>, 3> seen;
                for (std::size_t k = 0; k < others.size() && seen[0].size() < 7; ++k) {
                    const Picture& o = *others[k];
                    const double ox = o.a * fx - o.b * fy + o.tx + cx, oy = o.b * fx + o.a * fy + o.ty + cy;
                    if (ox < 1 || oy < 1 || ox >= o.width - 2 || oy >= o.height - 2 || otherHoles[k].contains(ox, oy, 4.0))
                        continue;
                    const auto c = sample(o, ox, oy);
                    for (int ch = 0; ch < 3; ++ch)
                        seen[std::size_t(ch)].push_back(c[std::size_t(ch)]);
                }
                if (seen[0].empty()) {
                    known[i] = 0.0f;
                    continue;
                }
                for (int ch = 0; ch < 3; ++ch) {
                    auto& v = seen[std::size_t(ch)];
                    std::nth_element(v.begin(), v.begin() + std::ptrdiff_t(v.size() / 2), v.end());
                    colour[i][std::size_t(ch)] = v[v.size() / 2];
                }
            }
        if (std::ranges::any_of(known, [](float k) { return k <= 0.0f; }))
            fillHoles(colour, known, rw, rh);
        // Into the frame, the box's edge feathered over 6 pixels.
        if (CVPixelBufferLockBaseAddress(out, 0) != kCVReturnSuccess)
            return false;
        auto* base = static_cast<std::uint8_t*>(CVPixelBufferGetBaseAddress(out));
        const std::size_t stride = CVPixelBufferGetBytesPerRow(out);
        for (int y = 0; y < p.height; ++y)
            std::copy_n(&p.bgra[std::size_t(y) * std::size_t(p.width) * 4], std::size_t(p.width) * 4, base + std::size_t(y) * stride);
        for (int y = y0; y < y1; ++y)
            for (int x = x0; x < x1; ++x) {
                const double inside = std::min({x + 0.5 - (hole.x0 - 6), (hole.x1 + 6) - (x + 0.5), y + 0.5 - (hole.y0 - 6),
                                                (hole.y1 + 6) - (y + 0.5)});
                const float t = float(std::clamp(inside / 12.0, 0.0, 1.0));
                if (t <= 0.0f)
                    continue;
                std::uint8_t* d = base + std::size_t(y) * stride + std::size_t(x) * 4;
                const auto& c = colour[std::size_t(y - y0) * std::size_t(rw) + std::size_t(x - x0)];
                for (int ch = 0; ch < 3; ++ch)
                    d[ch] = std::uint8_t(std::clamp(d[ch] * (1.0f - t) + c[std::size_t(ch)] * t + 0.5f, 0.0f, 255.0f));
            }
        CVPixelBufferUnlockBaseAddress(out, 0);
        return copy.write(out);
    };

    while (true) {
        // Keep kReach frames decoded past the one to fill.
        while (!ended && window.size() < next + kReach + 1) {
            auto frame = decoder->nextFrame();
            if (!frame || frame->pts() >= to) {
                ended = true;
                break;
            }
            Picture p;
            p.time = frame->pts();
            if (!toPicture(transfer, frame->pixelBuffer(), p)) {
                release();
                fail(error, "A frame could not be read.");
                return false;
            }
            if (!window.empty()) {
                // Its place relative to the first frame: its motion from the one before, after that one's.
                const Picture& q = window.back();
                const FrameMotion m = estimateMotion(q.small, p.small);
                const double k = double(p.width) / double(p.small.width);
                const double ma = m.confidence >= 0.3 ? m.scale * std::cos(m.angle) : 1.0;
                const double mb = m.confidence >= 0.3 ? m.scale * std::sin(m.angle) : 0.0;
                const double mx = m.confidence >= 0.3 ? m.dx * k : 0.0, my = m.confidence >= 0.3 ? m.dy * k : 0.0;
                p.a = ma * q.a - mb * q.b;
                p.b = mb * q.a + ma * q.b;
                p.tx = ma * q.tx - mb * q.ty + mx;
                p.ty = mb * q.tx + ma * q.ty + my;
            }
            window.push_back(std::move(p));
        }
        if (next >= window.size())
            break;
        if (!fillAndWrite(next)) {
            release();
            fail(error, "The processed copy could not be written.");
            return false;
        }
        if (!reportProgress(progress, window[next].time, from, to)) {
            release();
            if (error)
                error->clear();
            return false;
        }
        ++next;
        // Frames more than kReach behind are not needed again.
        while (next > kReach) {
            window.pop_front();
            --next;
        }
    }
    release();
    return copy.finish(output, error);
}

} // namespace tve
