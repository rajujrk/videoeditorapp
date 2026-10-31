#include "capture/Capture.h"

#import <AVFoundation/AVFoundation.h>
#import <CoreMedia/CoreMedia.h>
#import <Foundation/Foundation.h>
#import <ScreenCaptureKit/ScreenCaptureKit.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <numbers>
#include <thread>

using namespace tve;

// --- Objective-C receivers: hand ScreenCaptureKit's and AVFoundation's buffers to C++ ---

@interface TveStreamOutput : NSObject <SCStreamOutput> {
@public
    CaptureCallbacks callbacks;
    std::vector<float> interleaved;
}
@end

@implementation TveStreamOutput
- (void)stream:(SCStream*)stream didOutputSampleBuffer:(CMSampleBufferRef)buffer ofType:(SCStreamOutputType)type
{
    (void)stream;
    if (!CMSampleBufferIsValid(buffer))
        return;
    const Ticks time = Ticks(CMTimeGetSeconds(CMSampleBufferGetPresentationTimeStamp(buffer)) * double(kTicksPerSecond));
    if (type == SCStreamOutputTypeScreen) {
        // Only complete frames (others repeat the last picture or carry none).
        CFArrayRef attachments = CMSampleBufferGetSampleAttachmentsArray(buffer, false);
        if (attachments && CFArrayGetCount(attachments) > 0) {
            NSDictionary* info = (__bridge NSDictionary*)CFArrayGetValueAtIndex(attachments, 0);
            NSNumber* status = info[SCStreamFrameInfoStatus];
            if (status && status.integerValue != SCFrameStatusComplete)
                return;
        }
        CVPixelBufferRef picture = CMSampleBufferGetImageBuffer(buffer);
        if (picture && callbacks.frame)
            callbacks.frame(picture, time);
    } else if (type == SCStreamOutputTypeAudio && callbacks.sound) {
        // Float, usually one buffer per channel: interleaved to stereo.
        CMBlockBufferRef block = nullptr;
        std::size_t needed = 0;
        CMSampleBufferGetAudioBufferListWithRetainedBlockBuffer(buffer, &needed, nullptr, 0, nullptr, nullptr, 0, nullptr);
        std::vector<std::uint8_t> storage(needed);
        auto* buffers = reinterpret_cast<AudioBufferList*>(storage.data());
        if (CMSampleBufferGetAudioBufferListWithRetainedBlockBuffer(buffer, nullptr, buffers, needed, nullptr, nullptr, 0,
                                                                    &block)
            != noErr)
            return;
        const int frames = int(CMSampleBufferGetNumSamples(buffer));
        interleaved.assign(std::size_t(frames) * 2, 0.0f);
        if (buffers->mNumberBuffers >= 2) {
            const auto* l = static_cast<const float*>(buffers->mBuffers[0].mData);
            const auto* r = static_cast<const float*>(buffers->mBuffers[1].mData);
            for (int i = 0; i < frames; ++i) {
                interleaved[std::size_t(i) * 2] = l[i];
                interleaved[std::size_t(i) * 2 + 1] = r[i];
            }
        } else if (buffers->mNumberBuffers == 1) {
            const auto* s = static_cast<const float*>(buffers->mBuffers[0].mData);
            const int channels = int(buffers->mBuffers[0].mNumberChannels);
            for (int i = 0; i < frames; ++i) {
                interleaved[std::size_t(i) * 2] = s[std::size_t(i) * std::size_t(channels)];
                interleaved[std::size_t(i) * 2 + 1] = s[std::size_t(i) * std::size_t(channels) + (channels > 1 ? 1 : 0)];
            }
        }
        if (block)
            CFRelease(block);
        callbacks.sound(interleaved.data(), frames);
    }
}
@end

@interface TveCameraOutput : NSObject <AVCaptureVideoDataOutputSampleBufferDelegate> {
@public
    CaptureCallbacks callbacks;
}
@end

@implementation TveCameraOutput
- (void)captureOutput:(AVCaptureOutput*)output
    didOutputSampleBuffer:(CMSampleBufferRef)buffer
           fromConnection:(AVCaptureConnection*)connection
{
    (void)output;
    (void)connection;
    CVPixelBufferRef picture = CMSampleBufferGetImageBuffer(buffer);
    const Ticks time = Ticks(CMTimeGetSeconds(CMSampleBufferGetPresentationTimeStamp(buffer)) * double(kTicksPerSecond));
    if (picture && callbacks.frame)
        callbacks.frame(picture, time);
}
@end

namespace tve {

namespace {

// ScreenCaptureKit answers in completion handlers: these wait for them (they always come),
// the results written through pointers to the caller's variables.
SCShareableContent* shareableContent(std::string* error) // retained, or nil
{
    SCShareableContent* content = nil;
    std::string why;
    SCShareableContent** contentOut = &content;
    std::string* whyOut = &why;
    dispatch_semaphore_t done = dispatch_semaphore_create(0);
    [SCShareableContent getShareableContentExcludingDesktopWindows:YES
                                               onScreenWindowsOnly:YES
                                                 completionHandler:^(SCShareableContent* found, NSError* failure) {
                                                     *contentOut = [found retain];
                                                     if (failure)
                                                         *whyOut = failure.localizedDescription.UTF8String;
                                                     dispatch_semaphore_signal(done);
                                                 }];
    dispatch_semaphore_wait(done, DISPATCH_TIME_FOREVER);
    dispatch_release(done);
    if (!content && error)
        *error = why.empty() ? std::string("The screen cannot be recorded (check Screen Recording in System Settings).") : why;
    return content;
}

// --- the screen ---

class ScreenSource final : public CaptureSource {
public:
    ScreenSource(ScreenTarget target, std::optional<std::array<double, 4>> region, bool sound, int rate)
        : m_target(std::move(target)), m_region(region), m_sound(sound), m_rate(rate)
    {
        const double w = m_region ? (*m_region)[2] : m_target.width;
        const double h = m_region ? (*m_region)[3] : m_target.height;
        m_scale = 2.0; // Retina; corrected from the filter when the stream starts
        m_width = int(std::lround(w * m_scale)) & ~1;
        m_height = int(std::lround(h * m_scale)) & ~1;
    }
    ~ScreenSource() override { stop(); }

    bool start(CaptureCallbacks callbacks, std::string* error) override
    {
        SCShareableContent* content = shareableContent(error);
        if (!content)
            return false;
        SCContentFilter* filter = nil;
        if (m_target.kind == ScreenTarget::Kind::Display) {
            for (SCDisplay* d in content.displays)
                if (d.displayID == m_target.id)
                    filter = [[SCContentFilter alloc] initWithDisplay:d excludingWindows:@[]];
        } else {
            for (SCWindow* w in content.windows)
                if (w.windowID == m_target.id)
                    filter = [[SCContentFilter alloc] initWithDesktopIndependentWindow:w];
        }
        [content release];
        if (!filter) {
            if (error)
                *error = "That screen or window is gone.";
            return false;
        }
        m_scale = filter.pointPixelScale > 0 ? filter.pointPixelScale : 2.0;
        const CGRect whole = filter.contentRect;
        const CGRect area = m_region ? CGRectMake((*m_region)[0], (*m_region)[1], (*m_region)[2], (*m_region)[3]) : whole;
        m_width = int(std::lround(area.size.width * m_scale)) & ~1;
        m_height = int(std::lround(area.size.height * m_scale)) & ~1;
        SCStreamConfiguration* configuration = [[[SCStreamConfiguration alloc] init] autorelease];
        configuration.width = std::size_t(m_width);
        configuration.height = std::size_t(m_height);
        if (m_region)
            configuration.sourceRect = area;
        configuration.minimumFrameInterval = CMTimeMake(1, m_rate);
        configuration.pixelFormat = kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange;
        configuration.showsCursor = YES;
        configuration.queueDepth = 6;
        configuration.capturesAudio = m_sound;
        configuration.sampleRate = 48000;
        configuration.channelCount = 2;
        configuration.excludesCurrentProcessAudio = YES;
        m_output = [[TveStreamOutput alloc] init];
        m_output->callbacks = std::move(callbacks);
        m_stream = [[SCStream alloc] initWithFilter:filter configuration:configuration delegate:nil];
        [filter release];
        NSError* failure = nil;
        dispatch_queue_t queue = dispatch_queue_create("tve.screen", DISPATCH_QUEUE_SERIAL);
        [m_stream addStreamOutput:m_output type:SCStreamOutputTypeScreen sampleHandlerQueue:queue error:&failure];
        if (m_sound)
            [m_stream addStreamOutput:m_output type:SCStreamOutputTypeAudio sampleHandlerQueue:queue error:&failure];
        dispatch_release(queue);
        std::string why;
        bool started = false;
        std::string* whyOut = &why;
        bool* startedOut = &started;
        dispatch_semaphore_t done = dispatch_semaphore_create(0);
        [m_stream startCaptureWithCompletionHandler:^(NSError* e) {
            *startedOut = e == nil;
            if (e)
                *whyOut = e.localizedDescription.UTF8String;
            dispatch_semaphore_signal(done);
        }];
        dispatch_semaphore_wait(done, DISPATCH_TIME_FOREVER);
        dispatch_release(done);
        if (!started) {
            if (error)
                *error = why.empty() ? std::string("The screen recording could not start.") : why;
            release();
            return false;
        }
        m_area = {m_target.x + (m_region ? (*m_region)[0] : 0.0), m_target.y + (m_region ? (*m_region)[1] : 0.0),
                  area.size.width, area.size.height};
        return true;
    }
    void stop() override
    {
        if (!m_stream)
            return;
        dispatch_semaphore_t done = dispatch_semaphore_create(0);
        [m_stream stopCaptureWithCompletionHandler:^(NSError*) { dispatch_semaphore_signal(done); }];
        dispatch_semaphore_wait(done, DISPATCH_TIME_FOREVER);
        dispatch_release(done);
        release();
    }
    int width() const override { return m_width; }
    int height() const override { return m_height; }
    bool hasSound() const override { return m_sound; }
    std::optional<std::array<double, 4>> screenArea() const override
    {
        if (m_target.kind == ScreenTarget::Kind::Window)
            return std::array<double, 4>{m_target.x, m_target.y, m_target.width, m_target.height};
        return m_area;
    }

private:
    void release()
    {
        [m_stream release];
        m_stream = nil;
        [m_output release];
        m_output = nil;
    }

    ScreenTarget m_target;
    std::optional<std::array<double, 4>> m_region;
    bool m_sound;
    int m_rate;
    double m_scale = 2.0;
    int m_width = 0, m_height = 0;
    std::array<double, 4> m_area{};
    SCStream* m_stream = nil;
    TveStreamOutput* m_output = nil;
};

// --- a camera ---

class CameraSource final : public CaptureSource {
public:
    CameraSource(std::string id, int rate) : m_id(std::move(id))
    {
        (void)rate; // the camera's own rate (frames are taken at the recorder's)
        if (AVCaptureDevice* device = [AVCaptureDevice deviceWithUniqueID:@(m_id.c_str())]) {
            const CMVideoDimensions size = CMVideoFormatDescriptionGetDimensions(device.activeFormat.formatDescription);
            m_width = size.width & ~1;
            m_height = size.height & ~1;
        }
    }
    ~CameraSource() override { stop(); }

    bool start(CaptureCallbacks callbacks, std::string* error) override
    {
        AVCaptureDevice* device = [AVCaptureDevice deviceWithUniqueID:@(m_id.c_str())];
        if (!device) {
            if (error)
                *error = "That camera is not connected.";
            return false;
        }
        NSError* failure = nil;
        AVCaptureDeviceInput* input = [AVCaptureDeviceInput deviceInputWithDevice:device error:&failure];
        if (!input) {
            if (error)
                *error = failure ? failure.localizedDescription.UTF8String : "The camera cannot be opened.";
            return false;
        }
        m_session = [[AVCaptureSession alloc] init];
        if ([m_session canAddInput:input])
            [m_session addInput:input];
        AVCaptureVideoDataOutput* output = [[[AVCaptureVideoDataOutput alloc] init] autorelease];
        output.videoSettings = @{(id)kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange)};
        output.alwaysDiscardsLateVideoFrames = YES;
        m_receiver = [[TveCameraOutput alloc] init];
        m_receiver->callbacks = std::move(callbacks);
        dispatch_queue_t queue = dispatch_queue_create("tve.camera", DISPATCH_QUEUE_SERIAL);
        [output setSampleBufferDelegate:m_receiver queue:queue];
        dispatch_release(queue);
        if ([m_session canAddOutput:output])
            [m_session addOutput:output];
        [m_session startRunning];
        if (!m_session.running) {
            if (error)
                *error = "The camera did not start.";
            stop();
            return false;
        }
        const CMVideoDimensions size = CMVideoFormatDescriptionGetDimensions(device.activeFormat.formatDescription);
        m_width = size.width & ~1;
        m_height = size.height & ~1;
        return true;
    }
    void stop() override
    {
        if (!m_session)
            return;
        [m_session stopRunning];
        [m_session release];
        m_session = nil;
        [m_receiver release];
        m_receiver = nil;
    }
    int width() const override { return m_width; }
    int height() const override { return m_height; }

private:
    std::string m_id;
    int m_width = 1280, m_height = 720;
    AVCaptureSession* m_session = nil;
    TveCameraOutput* m_receiver = nil;
};

// --- the test source ---

class TestSource final : public CaptureSource {
public:
    TestSource(int width, int height, int rate, bool sound) : m_width(width & ~1), m_height(height & ~1), m_rate(rate), m_sound(sound) {}
    ~TestSource() override { stop(); }

    bool start(CaptureCallbacks callbacks, std::string* error) override
    {
        (void)error;
        m_callbacks = std::move(callbacks);
        m_running = true;
        m_thread = std::thread([this] { run(); });
        return true;
    }
    void stop() override
    {
        m_running = false;
        if (m_thread.joinable())
            m_thread.join();
    }
    int width() const override { return m_width; }
    int height() const override { return m_height; }
    bool hasSound() const override { return m_sound; }
    std::optional<std::array<double, 4>> screenArea() const override
    {
        return std::array<double, 4>{0, 0, m_width / 2.0, m_height / 2.0};
    }

private:
    void run()
    {
        CVPixelBufferRef frame = nullptr;
        NSDictionary* attributes = @{(id)kCVPixelBufferIOSurfacePropertiesKey: @{}};
        CVPixelBufferCreate(kCFAllocatorDefault, std::size_t(m_width), std::size_t(m_height),
                            kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange, (CFDictionaryRef)attributes, &frame);
        const auto begin = std::chrono::steady_clock::now();
        std::vector<float> tone;
        std::int64_t samplesSent = 0;
        for (int n = 0; m_running; ++n) {
            const auto due = begin + std::chrono::microseconds(std::int64_t(1e6 * n / m_rate));
            std::this_thread::sleep_until(due);
            // Eight bars sliding right a pixel each frame (luma), grey chroma.
            if (frame && CVPixelBufferLockBaseAddress(frame, 0) == kCVReturnSuccess) {
                auto* y = static_cast<std::uint8_t*>(CVPixelBufferGetBaseAddressOfPlane(frame, 0));
                const std::size_t ys = CVPixelBufferGetBytesPerRowOfPlane(frame, 0);
                for (int r = 0; r < m_height; ++r)
                    for (int c = 0; c < m_width; ++c)
                        y[std::size_t(r) * ys + std::size_t(c)] = std::uint8_t(16 + 219 * (((c + n * 4) * 8 / m_width) % 8) / 7);
                auto* uv = static_cast<std::uint8_t*>(CVPixelBufferGetBaseAddressOfPlane(frame, 1));
                const std::size_t uvs = CVPixelBufferGetBytesPerRowOfPlane(frame, 1);
                for (int r = 0; r < m_height / 2; ++r)
                    std::fill_n(uv + std::size_t(r) * uvs, std::size_t(m_width), std::uint8_t(128));
                CVPixelBufferUnlockBaseAddress(frame, 0);
                if (m_callbacks.frame)
                    m_callbacks.frame(frame, Ticks(double(n) / m_rate * double(kTicksPerSecond)));
            }
            if (m_sound && m_callbacks.sound) {
                // Up to now in time, a 440 Hz tone at -12 dB.
                const std::int64_t soundDue = std::int64_t(48000.0 * (n + 1) / m_rate);
                tone.resize(std::size_t(soundDue - samplesSent) * 2);
                for (std::int64_t i = samplesSent; i < soundDue; ++i) {
                    const float v = 0.25f * float(std::sin(2.0 * std::numbers::pi * 440.0 * double(i) / 48000.0));
                    tone[std::size_t(i - samplesSent) * 2] = tone[std::size_t(i - samplesSent) * 2 + 1] = v;
                }
                m_callbacks.sound(tone.data(), int(soundDue - samplesSent));
                samplesSent = soundDue;
            }
        }
        if (frame)
            CVPixelBufferRelease(frame);
    }

    int m_width, m_height, m_rate;
    bool m_sound;
    CaptureCallbacks m_callbacks;
    std::atomic<bool> m_running{false};
    std::thread m_thread;
};

} // namespace

std::vector<ScreenTarget> screenTargets(std::string* error)
{
    std::vector<ScreenTarget> targets;
    @autoreleasepool {
        SCShareableContent* content = shareableContent(error);
        if (!content)
            return targets;
        int number = 1;
        for (SCDisplay* d in content.displays) {
            const CGRect frame = CGDisplayBounds(d.displayID);
            targets.push_back({ScreenTarget::Kind::Display, d.displayID,
                               "Display " + std::to_string(number++) + " (" + std::to_string(int(d.width)) + " × "
                                   + std::to_string(int(d.height)) + ")",
                               frame.origin.x, frame.origin.y, frame.size.width, frame.size.height});
        }
        for (SCWindow* w in content.windows) {
            if (!w.onScreen || w.windowLayer != 0 || w.frame.size.width < 100 || w.frame.size.height < 100 || !w.owningApplication)
                continue;
            if (w.owningApplication.processID == NSProcessInfo.processInfo.processIdentifier)
                continue; // not our own windows
            const char* app = w.owningApplication.applicationName.UTF8String;
            std::string title = app ? app : "";
            if (w.title.length > 0)
                title += std::string(" — ") + w.title.UTF8String;
            targets.push_back({ScreenTarget::Kind::Window, w.windowID, title, w.frame.origin.x, w.frame.origin.y,
                               w.frame.size.width, w.frame.size.height});
        }
        [content release];
    }
    return targets;
}

std::unique_ptr<CaptureSource> makeScreenSource(const ScreenTarget& target, std::optional<std::array<double, 4>> region,
                                                bool systemSound, int frameRate)
{
    return std::make_unique<ScreenSource>(target, region, systemSound, std::clamp(frameRate, 1, 60));
}

std::vector<CameraDevice> cameras()
{
    std::vector<CameraDevice> list;
    @autoreleasepool {
        AVCaptureDeviceDiscoverySession* discovery = [AVCaptureDeviceDiscoverySession
            discoverySessionWithDeviceTypes:@[AVCaptureDeviceTypeBuiltInWideAngleCamera, AVCaptureDeviceTypeExternal,
                                              AVCaptureDeviceTypeContinuityCamera]
                                  mediaType:AVMediaTypeVideo
                                   position:AVCaptureDevicePositionUnspecified];
        for (AVCaptureDevice* d in discovery.devices)
            list.push_back({d.uniqueID.UTF8String, d.localizedName.UTF8String});
    }
    return list;
}

std::unique_ptr<CaptureSource> makeCameraSource(const std::string& id, int frameRate)
{
    return std::make_unique<CameraSource>(id, std::clamp(frameRate, 1, 60));
}

std::unique_ptr<CaptureSource> makeTestSource(int width, int height, int frameRate, bool sound)
{
    return std::make_unique<TestSource>(width, height, std::clamp(frameRate, 1, 60), sound);
}

CaptureAccess screenAccess()
{
    return CGPreflightScreenCaptureAccess() ? CaptureAccess::Granted : CaptureAccess::Undetermined;
}

void requestScreenAccess()
{
    CGRequestScreenCaptureAccess();
}

CaptureAccess cameraAccess()
{
    switch ([AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeVideo]) {
    case AVAuthorizationStatusAuthorized:
        return CaptureAccess::Granted;
    case AVAuthorizationStatusNotDetermined:
        return CaptureAccess::Undetermined;
    default:
        return CaptureAccess::Denied;
    }
}

void requestCameraAccess(std::function<void(bool)> done)
{
    auto callback = std::make_shared<std::function<void(bool)>>(std::move(done));
    [AVCaptureDevice requestAccessForMediaType:AVMediaTypeVideo completionHandler:^(BOOL granted) { (*callback)(granted); }];
}

} // namespace tve
