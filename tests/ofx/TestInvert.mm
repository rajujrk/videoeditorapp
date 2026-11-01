// An OpenFX image effect for the tests, against the bare C API: inverts the picture, tinted,
// by an amount, or keeps it; an offset adds to red and green. It renders on the CPU (float
// or 8-bit) and with Metal (float, its kernel compiled from source the first time).

#include "ofxCore.h"
#include "ofxGPURender.h"
#include "ofxImageEffect.h"
#include "ofxParam.h"
#include "ofxProperty.h"

#import <Metal/Metal.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

OfxHost* gHost = nullptr;
OfxPropertySuiteV1* gProp = nullptr;
OfxImageEffectSuiteV1* gEffect = nullptr;
OfxParameterSuiteV1* gParam = nullptr;

struct Settings {
    double amount = 1.0;
    double tint[3] = {1.0, 1.0, 1.0};
    int keep = 0;
    double offset[2] = {0.0, 0.0};
};

OfxStatus load()
{
    gProp = (OfxPropertySuiteV1*)gHost->fetchSuite(gHost->host, kOfxPropertySuite, 1);
    gEffect = (OfxImageEffectSuiteV1*)gHost->fetchSuite(gHost->host, kOfxImageEffectSuite, 1);
    gParam = (OfxParameterSuiteV1*)gHost->fetchSuite(gHost->host, kOfxParameterSuite, 1);
    return gProp && gEffect && gParam ? kOfxStatOK : kOfxStatErrMissingHostFeature;
}

OfxStatus describe(OfxImageEffectHandle effect)
{
    OfxPropertySetHandle props;
    gEffect->getPropertySet(effect, &props);
    gProp->propSetString(props, kOfxPropLabel, 0, "TVE Test Invert");
    gProp->propSetString(props, kOfxImageEffectPluginPropGrouping, 0, "TVE/Tests");
    gProp->propSetString(props, kOfxImageEffectPropSupportedContexts, 0, kOfxImageEffectContextFilter);
    gProp->propSetString(props, kOfxImageEffectPropSupportedPixelDepths, 0, kOfxBitDepthFloat);
    gProp->propSetString(props, kOfxImageEffectPropSupportedPixelDepths, 1, kOfxBitDepthByte);
    gProp->propSetString(props, kOfxImageEffectPropMetalRenderSupported, 0, "true");
    return kOfxStatOK;
}

OfxStatus describeInContext(OfxImageEffectHandle effect)
{
    OfxPropertySetHandle props;
    gEffect->clipDefine(effect, kOfxImageEffectSimpleSourceClipName, &props);
    gProp->propSetString(props, kOfxImageEffectPropSupportedComponents, 0, kOfxImageComponentRGBA);
    gEffect->clipDefine(effect, kOfxImageEffectOutputClipName, &props);
    gProp->propSetString(props, kOfxImageEffectPropSupportedComponents, 0, kOfxImageComponentRGBA);

    OfxParamSetHandle params;
    gEffect->getParamSet(effect, &params);
    gParam->paramDefine(params, kOfxParamTypeDouble, "amount", &props);
    gProp->propSetString(props, kOfxPropLabel, 0, "Amount");
    gProp->propSetDouble(props, kOfxParamPropDefault, 0, 1.0);
    gProp->propSetDouble(props, kOfxParamPropMin, 0, 0.0);
    gProp->propSetDouble(props, kOfxParamPropMax, 0, 1.0);
    gProp->propSetDouble(props, kOfxParamPropDisplayMin, 0, 0.0);
    gProp->propSetDouble(props, kOfxParamPropDisplayMax, 0, 1.0);
    gParam->paramDefine(params, kOfxParamTypeRGB, "tint", &props);
    gProp->propSetString(props, kOfxPropLabel, 0, "Tint");
    for (int i = 0; i < 3; ++i)
        gProp->propSetDouble(props, kOfxParamPropDefault, i, 1.0);
    gParam->paramDefine(params, kOfxParamTypeChoice, "mode", &props);
    gProp->propSetString(props, kOfxPropLabel, 0, "Mode");
    gProp->propSetString(props, kOfxParamPropChoiceOption, 0, "Invert");
    gProp->propSetString(props, kOfxParamPropChoiceOption, 1, "Keep");
    gProp->propSetInt(props, kOfxParamPropDefault, 0, 0);
    gParam->paramDefine(params, kOfxParamTypeDouble2D, "offset", &props);
    gProp->propSetString(props, kOfxPropLabel, 0, "Offset");
    gProp->propSetDouble(props, kOfxParamPropDisplayMin, 0, -1.0);
    gProp->propSetDouble(props, kOfxParamPropDisplayMax, 0, 1.0);
    gParam->paramDefine(params, kOfxParamTypeString, "note", &props); // not offered by hosts like ours
    return kOfxStatOK;
}

Settings settingsOf(OfxImageEffectHandle effect, double time)
{
    Settings s;
    OfxParamSetHandle params;
    gEffect->getParamSet(effect, &params);
    OfxParamHandle p;
    gParam->paramGetHandle(params, "amount", &p, nullptr);
    gParam->paramGetValueAtTime(p, time, &s.amount);
    gParam->paramGetHandle(params, "tint", &p, nullptr);
    gParam->paramGetValueAtTime(p, time, &s.tint[0], &s.tint[1], &s.tint[2]);
    gParam->paramGetHandle(params, "mode", &p, nullptr);
    gParam->paramGetValueAtTime(p, time, &s.keep);
    gParam->paramGetHandle(params, "offset", &p, nullptr);
    gParam->paramGetValueAtTime(p, time, &s.offset[0], &s.offset[1]);
    return s;
}

const char* kKernel = R"(
#include <metal_stdlib>
using namespace metal;
struct Settings { float amount; float tint[3]; int keep; float offset[2]; };
kernel void invert(device const float4* in [[buffer(0)]], device float4* out [[buffer(1)]],
                   constant Settings& s [[buffer(2)]], uint i [[thread_position_in_grid]])
{
    float4 c = in[i];
    if (s.keep == 0) {
        float3 inverted = (float3(c.a) - c.rgb) * float3(s.tint[0], s.tint[1], s.tint[2]);
        c.rgb = mix(c.rgb, inverted, s.amount);
    }
    c.r += s.offset[0] * c.a;
    c.g += s.offset[1] * c.a;
    out[i] = c;
}
)";

struct GpuSettings {
    float amount;
    float tint[3];
    int keep;
    float offset[2];
};

OfxStatus renderMetal(id<MTLCommandQueue> queue, id<MTLBuffer> source, id<MTLBuffer> output, int pixels, const Settings& s)
{
    static id<MTLComputePipelineState> pipeline = nil;
    if (!pipeline) {
        NSError* error = nil;
        id<MTLLibrary> library = [queue.device newLibraryWithSource:@(kKernel) options:nil error:&error];
        id<MTLFunction> function = [library newFunctionWithName:@"invert"];
        pipeline = function ? [queue.device newComputePipelineStateWithFunction:function error:&error] : nil;
        if (!pipeline)
            return kOfxStatFailed;
    }
    GpuSettings g{float(s.amount), {float(s.tint[0]), float(s.tint[1]), float(s.tint[2])}, s.keep,
                  {float(s.offset[0]), float(s.offset[1])}};
    id<MTLCommandBuffer> commands = [queue commandBuffer];
    id<MTLComputeCommandEncoder> encoder = [commands computeCommandEncoder];
    [encoder setComputePipelineState:pipeline];
    [encoder setBuffer:source offset:0 atIndex:0];
    [encoder setBuffer:output offset:0 atIndex:1];
    [encoder setBytes:&g length:sizeof g atIndex:2];
    [encoder dispatchThreads:MTLSizeMake(NSUInteger(pixels), 1, 1) threadsPerThreadgroup:MTLSizeMake(64, 1, 1)];
    [encoder endEncoding];
    [commands commit]; // the host waits for it
    return kOfxStatOK;
}

OfxStatus render(OfxImageEffectHandle effect, OfxPropertySetHandle in)
{
    OfxTime time = 0;
    gProp->propGetDouble(in, kOfxPropTime, 0, &time);
    int metal = 0;
    gProp->propGetInt(in, kOfxImageEffectPropMetalEnabled, 0, &metal);
    const Settings s = settingsOf(effect, time);

    OfxImageClipHandle sourceClip, outputClip;
    gEffect->clipGetHandle(effect, kOfxImageEffectSimpleSourceClipName, &sourceClip, nullptr);
    gEffect->clipGetHandle(effect, kOfxImageEffectOutputClipName, &outputClip, nullptr);
    OfxPropertySetHandle sourceImage = nullptr, outputImage = nullptr;
    if (gEffect->clipGetImage(sourceClip, time, nullptr, &sourceImage) != kOfxStatOK
        || gEffect->clipGetImage(outputClip, time, nullptr, &outputImage) != kOfxStatOK)
        return kOfxStatFailed;
    void* sourceData = nullptr;
    void* outputData = nullptr;
    int bounds[4] = {};
    char* depth = nullptr;
    gProp->propGetPointer(sourceImage, kOfxImagePropData, 0, &sourceData);
    gProp->propGetPointer(outputImage, kOfxImagePropData, 0, &outputData);
    gProp->propGetIntN(outputImage, kOfxImagePropBounds, 4, bounds);
    gProp->propGetString(outputImage, kOfxImageEffectPropPixelDepth, 0, &depth);
    const int pixels = (bounds[2] - bounds[0]) * (bounds[3] - bounds[1]);
    OfxStatus status = kOfxStatOK;
    if (metal) {
        void* queue = nullptr;
        gProp->propGetPointer(in, kOfxImageEffectPropMetalCommandQueue, 0, &queue);
        status = renderMetal((__bridge id<MTLCommandQueue>)queue, (__bridge id<MTLBuffer>)sourceData,
                             (__bridge id<MTLBuffer>)outputData, pixels, s);
    } else {
        const bool bytes = depth && std::strcmp(depth, kOfxBitDepthByte) == 0;
        for (int i = 0; i < pixels; ++i) {
            float c[4];
            for (int k = 0; k < 4; ++k)
                c[k] = bytes ? float(static_cast<unsigned char*>(sourceData)[i * 4 + k]) / 255.0f
                             : static_cast<float*>(sourceData)[i * 4 + k];
            if (!s.keep)
                for (int k = 0; k < 3; ++k)
                    c[k] = c[k] + (float((c[3] - c[k]) * s.tint[k]) - c[k]) * float(s.amount);
            c[0] += float(s.offset[0]) * c[3];
            c[1] += float(s.offset[1]) * c[3];
            for (int k = 0; k < 4; ++k) {
                if (bytes)
                    static_cast<unsigned char*>(outputData)[i * 4 + k] =
                        static_cast<unsigned char>(std::lround(std::clamp(c[k], 0.0f, 1.0f) * 255.0f));
                else
                    static_cast<float*>(outputData)[i * 4 + k] = c[k];
            }
        }
    }
    gEffect->clipReleaseImage(sourceImage);
    gEffect->clipReleaseImage(outputImage);
    return status;
}

OfxStatus mainEntry(const char* action, const void* handle, OfxPropertySetHandle in, OfxPropertySetHandle)
{
    auto effect = static_cast<OfxImageEffectHandle>(const_cast<void*>(handle));
    if (std::strcmp(action, kOfxActionLoad) == 0)
        return load();
    if (std::strcmp(action, kOfxActionDescribe) == 0)
        return describe(effect);
    if (std::strcmp(action, kOfxImageEffectActionDescribeInContext) == 0)
        return describeInContext(effect);
    if (std::strcmp(action, kOfxImageEffectActionRender) == 0)
        return render(effect, in);
    return kOfxStatReplyDefault;
}

void setHost(OfxHost* host)
{
    gHost = host;
}

OfxPlugin plugin = {kOfxImageEffectPluginApi, 1, "com.techievideoeditor.tests.Invert", 1, 0, setHost, mainEntry};

} // namespace

extern "C" __attribute__((visibility("default"))) int OfxGetNumberOfPlugins()
{
    return 1;
}

extern "C" __attribute__((visibility("default"))) OfxPlugin* OfxGetPlugin(int index)
{
    return index == 0 ? &plugin : nullptr;
}
