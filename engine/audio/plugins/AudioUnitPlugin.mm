// Audio Units through AVAudioUnit / AUAudioUnit: AUv3 out of process, AUv2 in process.
// Manual retain/release (the engine is built without ARC).

#include "audio/plugins/PluginFormats.h"

#include "decode/AudioDecoder.h"

#import <AVFAudio/AVFAudio.h>
#import <AudioToolbox/AudioToolbox.h>
#import <AudioToolbox/AUCocoaUIView.h>
#import <CoreAudioKit/CoreAudioKit.h>
#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>

namespace tve::plugins {

namespace {

std::string fourCC(OSType code)
{
    char text[5] = {char(code >> 24), char(code >> 16), char(code >> 8), char(code), 0};
    const bool printable = std::all_of(text, text + 4, [](char c) { return c >= 32 && c < 127 && c != '/'; });
    if (printable)
        return text;
    char hex[11];
    std::snprintf(hex, sizeof hex, "0x%08x", unsigned(code));
    return hex;
}

bool parseFourCC(const std::string& text, OSType& code)
{
    if (text.size() == 10 && text.starts_with("0x")) {
        code = OSType(std::strtoul(text.c_str() + 2, nullptr, 16));
        return true;
    }
    if (text.size() != 4)
        return false;
    code = (OSType(std::uint8_t(text[0])) << 24) | (OSType(std::uint8_t(text[1])) << 16) | (OSType(std::uint8_t(text[2])) << 8)
           | OSType(std::uint8_t(text[3]));
    return true;
}

std::string idOf(const AudioComponentDescription& d)
{
    return "au:" + fourCC(d.componentType) + "/" + fourCC(d.componentSubType) + "/" + fourCC(d.componentManufacturer);
}

bool descriptionOf(const std::string& id, AudioComponentDescription& d)
{
    if (!id.starts_with("au:"))
        return false;
    const std::string rest = id.substr(3);
    const auto a = rest.find('/');
    const auto b = a == std::string::npos ? a : rest.find('/', a + 1);
    if (b == std::string::npos)
        return false;
    d = {};
    return parseFourCC(rest.substr(0, a), d.componentType) && parseFourCC(rest.substr(a + 1, b - a - 1), d.componentSubType)
           && parseFourCC(rest.substr(b + 1), d.componentManufacturer);
}

std::string utf8(NSString* text)
{
    return text ? std::string([text UTF8String]) : std::string();
}

PluginDescription describe(AVAudioUnitComponent* c)
{
    PluginDescription d;
    d.id = idOf(c.audioComponentDescription);
    d.name = utf8(c.name);
    d.vendor = utf8(c.manufacturerName);
    d.format = "AU";
    d.version = utf8(c.versionString);
    d.hasEditor = true; // a generic one at least
    if (!c.passesAUVal && !(c.audioComponentDescription.componentFlags & kAudioComponentFlag_IsV3AudioUnit))
        d.problem = "didn't pass the system's Audio Unit validation";
    return d;
}

class AudioUnitPlugin final : public AudioPlugin {
public:
    AudioUnitPlugin(AVAudioUnit* unit, PluginDescription description)
        : m_unit([unit retain]), m_description(std::move(description))
    {
        AUAudioUnit* au = m_unit.AUAudioUnit;
        m_render = [au.renderBlock copy];
        for (AUParameter* p in au.parameterTree.allParameters) {
            if (!(p.flags & kAudioUnitParameterFlag_IsWritable))
                continue;
            m_params[std::to_string(p.address)] = [p retain];
            m_order.push_back(std::to_string(p.address));
        }
        m_left.resize(kBlock);
        m_right.resize(kBlock);
    }

    ~AudioUnitPlugin() override
    {
        if (m_observer)
            [m_unit.AUAudioUnit.parameterTree removeParameterObserver:m_observer];
        for (auto& [id, p] : m_params)
            [p release];
        [m_render release];
        [m_unit.AUAudioUnit deallocateRenderResources];
        [m_unit release];
        *m_changed = nullptr; // callbacks still queued do nothing
    }

    bool prepare(std::string* error)
    {
        AUAudioUnit* au = m_unit.AUAudioUnit;
        AVAudioFormat* format = [[AVAudioFormat alloc] initStandardFormatWithSampleRate:audio::kSampleRate channels:2];
        NSError* problem = nil;
        const bool formats = au.inputBusses.count > 0 && au.outputBusses.count > 0
                             && [au.inputBusses[0] setFormat:format error:&problem] && [au.outputBusses[0] setFormat:format error:&problem];
        [format release];
        if (!formats) {
            if (error)
                *error = "it doesn't take stereo sound at 48 kHz";
            return false;
        }
        au.maximumFramesToRender = kBlock;
        if (![au allocateRenderResourcesAndReturnError:&problem]) {
            if (error)
                *error = "it couldn't get ready: " + utf8(problem.localizedDescription);
            return false;
        }
        // An AUv2 pulls its input through a render callback.
        if (AudioUnit v2 = legacyUnit()) {
            AURenderCallbackStruct callback{&AudioUnitPlugin::supplyInput, this};
            if (AudioUnitSetProperty(v2, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &callback,
                                     sizeof callback) != noErr) {
                if (error)
                    *error = "it won't take sound in";
                return false;
            }
        }
        return true;
    }

    const PluginDescription& description() const override { return m_description; }

    std::vector<PluginParameter> parameters() const override
    {
        std::vector<PluginParameter> list;
        for (const std::string& id : m_order) {
            AUParameter* p = m_params.at(id);
            PluginParameter param{.id = id, .name = utf8(p.displayName), .unit = utf8(p.unitName)};
            param.defaultValue = toNormalized(currentValue(p), p.minValue, p.maxValue, logarithmic(p));
            if (p.unit == kAudioUnitParameterUnit_Boolean || p.unit == kAudioUnitParameterUnit_Indexed || p.valueStrings)
                param.steps = int(std::lround(p.maxValue - p.minValue)) + 1;
            list.push_back(std::move(param));
        }
        return list;
    }

    void setParameter(const std::string& id, double normalized) override
    {
        const auto it = m_params.find(id);
        if (it == m_params.end())
            return;
        AUParameter* p = it->second;
        const double value = fromNormalized(std::clamp(normalized, 0.0, 1.0), p.minValue, p.maxValue, logarithmic(p));
        if (std::abs(currentValue(p) - value) <= 1e-7)
            return;
        // An AUv2 takes it directly (its bridged parameter tree doesn't always pass it on).
        if (AudioUnit v2 = legacyUnit())
            AudioUnitSetParameter(v2, AudioUnitParameterID(p.address), kAudioUnitScope_Global, 0, AudioUnitParameterValue(value), 0);
        else
            [p setValue:AUValue(value) originator:m_observer];
    }

    double parameter(const std::string& id) const override
    {
        const auto it = m_params.find(id);
        if (it == m_params.end())
            return 0.0;
        AUParameter* p = it->second;
        return toNormalized(currentValue(p), p.minValue, p.maxValue, logarithmic(p));
    }

    std::string displayValue(const std::string& id, double normalized) const override
    {
        const auto it = m_params.find(id);
        if (it == m_params.end())
            return {};
        AUParameter* p = it->second;
        AUValue value = AUValue(fromNormalized(std::clamp(normalized, 0.0, 1.0), p.minValue, p.maxValue, logarithmic(p)));
        std::string text = utf8([p stringFromValue:&value]);
        if (text.empty() || text.find_first_not_of(' ') == std::string::npos) {
            // Not shown by the plug-in: the number (whole from 100 up, else three figures).
            char number[32];
            std::snprintf(number, sizeof number, std::abs(value) >= 100 ? "%.0f" : "%.3g", double(value));
            text = number;
        }
        const std::string unit = utf8(p.unitName);
        return unit.empty() ? text : text + " " + unit;
    }

    std::string saveState() const override
    {
        NSDictionary* state = m_unit.AUAudioUnit.fullStateForDocument;
        if (!state)
            return {};
        NSData* data = [NSPropertyListSerialization dataWithPropertyList:state format:NSPropertyListBinaryFormat_v1_0
                                                                 options:0 error:nil];
        return data ? std::string(static_cast<const char*>(data.bytes), data.length) : std::string();
    }

    bool loadState(const std::string& state) override
    {
        NSData* data = [NSData dataWithBytes:state.data() length:state.size()];
        id plist = [NSPropertyListSerialization propertyListWithData:data options:NSPropertyListImmutable format:nil error:nil];
        if (![plist isKindOfClass:[NSDictionary class]])
            return false;
        m_unit.AUAudioUnit.fullStateForDocument = plist;
        return true;
    }

    void process(float* stereo, int frames) override
    {
        for (int done = 0; done < frames;) {
            const int count = std::min(kBlock, frames - done);
            float* block = stereo + std::size_t(done) * 2;
            for (int i = 0; i < count; ++i) {
                m_left[std::size_t(i)] = block[std::size_t(i) * 2];
                m_right[std::size_t(i)] = block[std::size_t(i) * 2 + 1];
            }
            renderBlock(count);
            for (int i = 0; i < count; ++i) {
                block[std::size_t(i) * 2] = m_outLeft[std::size_t(i)];
                block[std::size_t(i) * 2 + 1] = m_outRight[std::size_t(i)];
            }
            done += count;
        }
    }

    void reset() override
    {
        [m_unit.AUAudioUnit reset];
        m_sampleTime = 0;
    }

    int latency() const override { return int(std::lround(m_unit.AUAudioUnit.latency * audio::kSampleRate)); }
    double tail() const override { return std::min(m_unit.AUAudioUnit.tailTime, 30.0); }

    void requestEditor(std::function<void(void*)> done) override
    {
        AUAudioUnit* au = m_unit.AUAudioUnit;
        AudioUnit v2 = m_unit.audioUnit;
        auto* callback = new std::function<void(void*)>(std::move(done));
        const auto generic = [v2, callback] {
            // No view of its own: Core Audio Kit's generic one (AUv2 only).
            NSView* view = v2 ? [[AUGenericView alloc] initWithAudioUnit:v2] : nil;
            (*callback)(view);
            delete callback;
        };
        if (au.providesUserInterface || (m_unit.audioComponentDescription.componentFlags & kAudioComponentFlag_IsV3AudioUnit)) {
            [au requestViewControllerWithCompletionHandler:^(AUViewControllerBase* controller) {
                dispatch_async(dispatch_get_main_queue(), ^{
                    if (controller) {
                        NSView* view = [controller.view retain];
                        (*callback)(view);
                        delete callback;
                    } else {
                        generic();
                    }
                });
            }];
            return;
        }
        // An AUv2's own Cocoa view, if it has one.
        if (v2) {
            UInt32 size = 0;
            Boolean writable = false;
            if (AudioUnitGetPropertyInfo(v2, kAudioUnitProperty_CocoaUI, kAudioUnitScope_Global, 0, &size, &writable) == noErr
                && size >= sizeof(AudioUnitCocoaViewInfo)) {
                std::vector<char> buffer(size);
                auto* info = reinterpret_cast<AudioUnitCocoaViewInfo*>(buffer.data());
                if (AudioUnitGetProperty(v2, kAudioUnitProperty_CocoaUI, kAudioUnitScope_Global, 0, info, &size) == noErr) {
                    NSURL* location = (NSURL*)info->mCocoaAUViewBundleLocation;
                    NSString* className = (NSString*)info->mCocoaAUViewClass[0];
                    NSBundle* bundle = location ? [NSBundle bundleWithURL:location] : nil;
                    Class factoryClass = bundle ? [bundle classNamed:className] : nil;
                    NSView* view = nil;
                    if (factoryClass && [factoryClass conformsToProtocol:@protocol(AUCocoaUIBase)]) {
                        NSObject<AUCocoaUIBase>* factory = [[factoryClass alloc] init];
                        view = [[factory uiViewForAudioUnit:v2 withSize:NSMakeSize(400, 300)] retain];
                        [factory release];
                    }
                    if (location)
                        CFRelease(info->mCocoaAUViewBundleLocation);
                    if (className)
                        CFRelease(info->mCocoaAUViewClass[0]);
                    if (view) {
                        (*callback)(view);
                        delete callback;
                        return;
                    }
                }
            }
        }
        generic();
    }

    void setChangeHandler(std::function<void()> changed) override
    {
        *m_changed = std::move(changed);
        if (m_observer)
            return;
        std::shared_ptr<std::function<void()>> handler = m_changed;
        m_observer = [m_unit.AUAudioUnit.parameterTree tokenByAddingParameterObserver:^(AUParameterAddress, AUValue) {
            dispatch_async(dispatch_get_main_queue(), ^{
                if (*handler)
                    (*handler)();
            });
        }];
    }

private:
    static constexpr int kBlock = 1024;

    // An AUv2's input: the block being processed.
    static OSStatus supplyInput(void* self, AudioUnitRenderActionFlags*, const AudioTimeStamp*, UInt32, UInt32 frames,
                                AudioBufferList* input)
    {
        auto* plugin = static_cast<AudioUnitPlugin*>(self);
        float* sources[2] = {plugin->m_left.data(), plugin->m_right.data()};
        for (UInt32 b = 0; b < input->mNumberBuffers && b < 2; ++b) {
            const UInt32 bytes = std::min<UInt32>(frames, UInt32(plugin->m_left.size())) * sizeof(float);
            if (input->mBuffers[b].mData == nullptr)
                input->mBuffers[b].mData = sources[b];
            else
                std::memcpy(input->mBuffers[b].mData, sources[b], bytes);
            input->mBuffers[b].mDataByteSize = bytes;
        }
        return noErr;
    }

    // The AUv2 underneath, if this is one.
    AudioUnit legacyUnit() const
    {
        return (m_unit.audioComponentDescription.componentFlags & kAudioComponentFlag_IsV3AudioUnit) ? nullptr : m_unit.audioUnit;
    }
    double currentValue(AUParameter* p) const
    {
        AudioUnitParameterValue value = 0;
        if (AudioUnit v2 = legacyUnit();
            v2 && AudioUnitGetParameter(v2, AudioUnitParameterID(p.address), kAudioUnitScope_Global, 0, &value) == noErr)
            return value;
        return p.value;
    }

    static bool logarithmic(AUParameter* p) { return (p.flags & kAudioUnitParameterFlag_DisplayLogarithmic) && p.minValue > 0; }

    void renderBlock(int count)
    {
        m_outLeft.assign(std::size_t(count), 0.0f);
        m_outRight.assign(std::size_t(count), 0.0f);
        struct {
            AudioBufferList list;
            AudioBuffer second;
        } output{};
        output.list.mNumberBuffers = 2;
        output.list.mBuffers[0] = {1, UInt32(count * sizeof(float)), m_outLeft.data()};
        output.second = {1, UInt32(count * sizeof(float)), m_outRight.data()};
        float* left = m_left.data();
        float* right = m_right.data();
        AURenderPullInputBlock pull = ^AUAudioUnitStatus(AudioUnitRenderActionFlags*, const AudioTimeStamp*, AUAudioFrameCount frameCount,
                                                         NSInteger, AudioBufferList* input) {
            float* sources[2] = {left, right};
            for (UInt32 b = 0; b < input->mNumberBuffers && b < 2; ++b) {
                const UInt32 bytes = frameCount * sizeof(float);
                if (input->mBuffers[b].mData == nullptr)
                    input->mBuffers[b].mData = sources[b];
                else
                    std::memcpy(input->mBuffers[b].mData, sources[b], bytes);
                input->mBuffers[b].mDataByteSize = bytes;
            }
            return noErr;
        };
        AudioTimeStamp time{};
        time.mSampleTime = m_sampleTime;
        time.mFlags = kAudioTimeStampSampleTimeValid;
        AudioUnitRenderActionFlags flags = 0;
        const AUAudioUnitStatus status = legacyUnit()
                                             ? AudioUnitRender(legacyUnit(), &flags, &time, 0, UInt32(count), &output.list)
                                             : m_render(&flags, &time, AUAudioFrameCount(count), 0, &output.list, pull);
        m_sampleTime += count;
        if (status != noErr)
            return; // silence rather than noise
        // The plug-in may have pointed the output at its own buffers.
        if (output.list.mBuffers[0].mData != m_outLeft.data())
            std::memcpy(m_outLeft.data(), output.list.mBuffers[0].mData, std::size_t(count) * sizeof(float));
        if (output.second.mData != m_outRight.data())
            std::memcpy(m_outRight.data(), output.second.mData, std::size_t(count) * sizeof(float));
    }

    AVAudioUnit* m_unit;
    PluginDescription m_description;
    AURenderBlock m_render = nil;
    std::map<std::string, AUParameter*> m_params;
    std::vector<std::string> m_order;
    std::vector<float> m_left, m_right, m_outLeft, m_outRight;
    double m_sampleTime = 0;
    AUParameterObserverToken m_observer = nullptr;
    std::shared_ptr<std::function<void()>> m_changed = std::make_shared<std::function<void()>>();
};

} // namespace

double fromNormalized(double normalized, double min, double max, bool logarithmic)
{
    if (logarithmic && min > 0 && max > min)
        return min * std::pow(max / min, normalized);
    return min + (max - min) * normalized;
}

double toNormalized(double value, double min, double max, bool logarithmic)
{
    if (max <= min)
        return 0.0;
    if (logarithmic && min > 0 && value > 0)
        return std::clamp(std::log(value / min) / std::log(max / min), 0.0, 1.0);
    return std::clamp((value - min) / (max - min), 0.0, 1.0);
}

std::unique_ptr<AudioPlugin> openAudioUnit(const std::string& id, std::string* error)
{
    const auto fail = [&](const std::string& why) -> std::unique_ptr<AudioPlugin> {
        if (error)
            *error = why;
        return nullptr;
    };
    AudioComponentDescription wanted{};
    if (!descriptionOf(id, wanted))
        return fail("not an Audio Unit id");
    NSArray<AVAudioUnitComponent*>* found =
        [[AVAudioUnitComponentManager sharedAudioUnitComponentManager] componentsMatchingDescription:wanted];
    if (found.count == 0)
        return fail("the Audio Unit isn't installed on this Mac");
    AVAudioUnitComponent* component = found.firstObject;
    const bool v3 = component.audioComponentDescription.componentFlags & kAudioComponentFlag_IsV3AudioUnit;

    // Made asynchronously; waited for here (the main thread keeps its run loop turning).
    __block AVAudioUnit* made = nil;
    __block NSError* problem = nil;
    __block bool finished = false;
    dispatch_semaphore_t ready = dispatch_semaphore_create(0);
    [AVAudioUnit instantiateWithComponentDescription:component.audioComponentDescription
                                             options:v3 ? kAudioComponentInstantiation_LoadOutOfProcess : 0
                                   completionHandler:^(AVAudioUnit* unit, NSError* failure) {
                                       made = [unit retain];
                                       problem = [failure retain];
                                       finished = true;
                                       dispatch_semaphore_signal(ready);
                                   }];
    const auto deadline = dispatch_time(DISPATCH_TIME_NOW, 20 * NSEC_PER_SEC);
    if ([NSThread isMainThread]) {
        while (!finished && dispatch_time(DISPATCH_TIME_NOW, 0) < deadline)
            CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.01, true);
    } else {
        dispatch_semaphore_wait(ready, deadline);
    }
    dispatch_release(ready);
    if (!made) {
        const std::string why = problem ? utf8(problem.localizedDescription) : std::string("it took too long to load");
        [problem release];
        return fail("the Audio Unit couldn't be loaded: " + why);
    }
    [problem release];
    auto plugin = std::make_unique<AudioUnitPlugin>(made, describe(component));
    [made release];
    std::string why;
    if (!plugin->prepare(&why))
        return fail("the Audio Unit can't be used: " + why);
    return plugin;
}

} // namespace tve::plugins

namespace tve {

std::vector<PluginDescription> listAudioUnits()
{
    std::vector<PluginDescription> list;
    AVAudioUnitComponentManager* manager = [AVAudioUnitComponentManager sharedAudioUnitComponentManager];
    for (OSType type : {kAudioUnitType_Effect, kAudioUnitType_MusicEffect}) {
        AudioComponentDescription wanted{};
        wanted.componentType = type;
        for (AVAudioUnitComponent* c in [manager componentsMatchingDescription:wanted])
            list.push_back(plugins::describe(c));
    }
    std::ranges::sort(list, [](const PluginDescription& a, const PluginDescription& b) {
        return a.vendor != b.vendor ? a.vendor < b.vendor : a.name < b.name;
    });
    return list;
}

} // namespace tve
