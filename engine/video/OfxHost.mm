// An OpenFX 1.5 host for image effects: property sets, parameters, clips and images, the
// property, image effect, parameter, memory, multithread and message suites, loading
// plug-in binaries, describing them, and rendering pictures through instances on the CPU or
// with Metal (images as shared MTLBuffers). Manual retain/release for the Metal objects.

#include "video/OfxHost.h"

#include "audio/plugins/PluginFormats.h"

#include "ofxCore.h"
#include "ofxGPURender.h"
#include "ofxImageEffect.h"
#include "ofxMemory.h"
#include "ofxMessage.h"
#include "ofxMultiThread.h"
#include "ofxParam.h"
#include "ofxProperty.h"

#include <nlohmann/json.hpp>

#import <Metal/Metal.h>
#include <dispatch/dispatch.h>
#include <dlfcn.h>
#include <sys/stat.h>

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <thread>

namespace fs = std::filesystem;

// --- The host's objects (behind the API's opaque handles) ---

struct OfxPropertySetStruct {
    struct Prop {
        char type = 'i'; // 'i'nt, 'd'ouble, 's'tring, 'p'ointer
        std::vector<int> ints;
        std::vector<double> doubles;
        std::vector<std::string> strings;
        std::vector<void*> pointers;
        std::size_t size() const
        {
            return type == 'i' ? ints.size() : type == 'd' ? doubles.size() : type == 's' ? strings.size() : pointers.size();
        }
    };
    std::map<std::string, Prop> props;

    Prop& at(const char* name, char type)
    {
        Prop& p = props[name];
        if (p.size() == 0)
            p.type = type;
        return p;
    }
    void setInt(const char* name, std::vector<int> v) { at(name, 'i') = {.type = 'i', .ints = std::move(v)}; }
    void setDouble(const char* name, std::vector<double> v) { at(name, 'd') = {.type = 'd', .doubles = std::move(v)}; }
    void setString(const char* name, std::vector<std::string> v) { at(name, 's') = {.type = 's', .strings = std::move(v)}; }
    void setPointer(const char* name, std::vector<void*> v) { at(name, 'p') = {.type = 'p', .pointers = std::move(v)}; }
    const Prop* find(const char* name) const
    {
        const auto it = props.find(name);
        return it == props.end() ? nullptr : &it->second;
    }
    std::string string(const char* name, std::string fallback = {}) const
    {
        const Prop* p = find(name);
        return p && p->type == 's' && !p->strings.empty() ? p->strings[0] : fallback;
    }
    int integer(const char* name, int fallback = 0) const
    {
        const Prop* p = find(name);
        return p && p->type == 'i' && !p->ints.empty() ? p->ints[0] : fallback;
    }
    std::vector<double> doubles(const char* name) const
    {
        const Prop* p = find(name);
        if (!p)
            return {};
        if (p->type == 'd')
            return p->doubles;
        if (p->type == 'i')
            return std::vector<double>(p->ints.begin(), p->ints.end());
        return {};
    }
    std::vector<std::string> strings(const char* name) const
    {
        const Prop* p = find(name);
        return p && p->type == 's' ? p->strings : std::vector<std::string>{};
    }
};

struct OfxParamStruct {
    OfxPropertySetStruct props;
    std::string name;
    std::string type;
    std::vector<double> value; // numbers (each component)
    std::string text;          // a string parameter's
};

struct OfxParamSetStruct {
    OfxPropertySetStruct props;
    std::map<std::string, std::unique_ptr<OfxParamStruct>> params;
    std::vector<std::string> order;
};

struct OfxImageEffectStruct;

struct OfxImageClipStruct {
    OfxPropertySetStruct props;
    std::string name;
    OfxImageEffectStruct* effect = nullptr;
};

struct OfxImageEffectStruct {
    OfxPropertySetStruct props;
    OfxParamSetStruct params;
    std::map<std::string, std::unique_ptr<OfxImageClipStruct>> clips;
    std::vector<std::string> clipOrder;
    // While rendering: the images clips give.
    struct Frame {
        void* source = nullptr; // CPU pointer or id<MTLBuffer>
        void* output = nullptr;
        int width = 0;
        int height = 0;
        int bytesPerComponent = 4;
        double time = 0;
    } frame;
};

struct OfxImageMemoryStruct {
    std::vector<unsigned char> bytes;
};

struct OfxMutex {
    std::recursive_timed_mutex mutex;
};

namespace tve {

namespace {

// --- Copying property sets, params and clips from a descriptor to an instance ---

void copyEffect(const OfxImageEffectStruct& from, OfxImageEffectStruct& to)
{
    to.props = from.props;
    to.params.props = from.params.props;
    to.params.order = from.params.order;
    for (const auto& [name, param] : from.params.params)
        to.params.params[name] = std::make_unique<OfxParamStruct>(*param);
    to.clipOrder = from.clipOrder;
    for (const auto& [name, clip] : from.clips) {
        auto copy = std::make_unique<OfxImageClipStruct>(*clip);
        copy->effect = &to;
        to.clips[name] = std::move(copy);
    }
}

int componentsOf(const std::string& type)
{
    if (type == kOfxParamTypeRGBA)
        return 4;
    if (type == kOfxParamTypeRGB || type == kOfxParamTypeDouble3D || type == kOfxParamTypeInteger3D)
        return 3;
    if (type == kOfxParamTypeDouble2D || type == kOfxParamTypeInteger2D)
        return 2;
    return 1;
}

bool integral(const std::string& type)
{
    return type == kOfxParamTypeInteger || type == kOfxParamTypeInteger2D || type == kOfxParamTypeInteger3D
           || type == kOfxParamTypeBoolean || type == kOfxParamTypeChoice;
}

// --- The property suite ---

template <char Type, typename T>
OfxStatus setOne(OfxPropertySetHandle set, const char* name, int index, T value)
{
    if (!set || !name || index < 0)
        return kOfxStatErrBadHandle;
    auto& p = set->at(name, Type);
    p.type = Type;
    if constexpr (Type == 'i') {
        if (int(p.ints.size()) <= index)
            p.ints.resize(std::size_t(index) + 1);
        p.ints[std::size_t(index)] = value;
    } else if constexpr (Type == 'd') {
        if (int(p.doubles.size()) <= index)
            p.doubles.resize(std::size_t(index) + 1);
        p.doubles[std::size_t(index)] = value;
    } else if constexpr (Type == 's') {
        if (int(p.strings.size()) <= index)
            p.strings.resize(std::size_t(index) + 1);
        p.strings[std::size_t(index)] = value ? value : "";
    } else {
        if (int(p.pointers.size()) <= index)
            p.pointers.resize(std::size_t(index) + 1);
        p.pointers[std::size_t(index)] = value;
    }
    return kOfxStatOK;
}

template <char Type, typename T>
OfxStatus getOne(OfxPropertySetHandle set, const char* name, int index, T* value)
{
    if (!set || !name || !value)
        return kOfxStatErrBadHandle;
    const auto* p = set->find(name);
    if (!p)
        return kOfxStatErrUnknown;
    if (index < 0 || index >= int(p->size()))
        return kOfxStatErrBadIndex;
    if constexpr (Type == 'i') {
        if (p->type == 'i')
            *value = p->ints[std::size_t(index)];
        else if (p->type == 'd')
            *value = int(p->doubles[std::size_t(index)]);
        else
            return kOfxStatErrValue;
    } else if constexpr (Type == 'd') {
        if (p->type == 'd')
            *value = p->doubles[std::size_t(index)];
        else if (p->type == 'i')
            *value = double(p->ints[std::size_t(index)]);
        else
            return kOfxStatErrValue;
    } else if constexpr (Type == 's') {
        if (p->type != 's')
            return kOfxStatErrValue;
        *value = const_cast<char*>(p->strings[std::size_t(index)].c_str());
    } else {
        if (p->type != 'p')
            return kOfxStatErrValue;
        *value = p->pointers[std::size_t(index)];
    }
    return kOfxStatOK;
}

OfxStatus propSetPointer(OfxPropertySetHandle s, const char* n, int i, void* v) { return setOne<'p'>(s, n, i, v); }
OfxStatus propSetString(OfxPropertySetHandle s, const char* n, int i, const char* v) { return setOne<'s'>(s, n, i, v); }
OfxStatus propSetDouble(OfxPropertySetHandle s, const char* n, int i, double v) { return setOne<'d'>(s, n, i, v); }
OfxStatus propSetInt(OfxPropertySetHandle s, const char* n, int i, int v) { return setOne<'i'>(s, n, i, v); }
OfxStatus propSetPointerN(OfxPropertySetHandle s, const char* n, int count, void* const* v)
{
    for (int i = 0; i < count; ++i)
        if (OfxStatus st = setOne<'p'>(s, n, i, v[i]); st != kOfxStatOK)
            return st;
    return kOfxStatOK;
}
OfxStatus propSetStringN(OfxPropertySetHandle s, const char* n, int count, const char* const* v)
{
    for (int i = 0; i < count; ++i)
        if (OfxStatus st = setOne<'s'>(s, n, i, v[i]); st != kOfxStatOK)
            return st;
    return kOfxStatOK;
}
OfxStatus propSetDoubleN(OfxPropertySetHandle s, const char* n, int count, const double* v)
{
    for (int i = 0; i < count; ++i)
        if (OfxStatus st = setOne<'d'>(s, n, i, v[i]); st != kOfxStatOK)
            return st;
    return kOfxStatOK;
}
OfxStatus propSetIntN(OfxPropertySetHandle s, const char* n, int count, const int* v)
{
    for (int i = 0; i < count; ++i)
        if (OfxStatus st = setOne<'i'>(s, n, i, v[i]); st != kOfxStatOK)
            return st;
    return kOfxStatOK;
}
OfxStatus propGetPointer(OfxPropertySetHandle s, const char* n, int i, void** v) { return getOne<'p'>(s, n, i, v); }
OfxStatus propGetString(OfxPropertySetHandle s, const char* n, int i, char** v) { return getOne<'s'>(s, n, i, v); }
OfxStatus propGetDouble(OfxPropertySetHandle s, const char* n, int i, double* v) { return getOne<'d'>(s, n, i, v); }
OfxStatus propGetInt(OfxPropertySetHandle s, const char* n, int i, int* v) { return getOne<'i'>(s, n, i, v); }
OfxStatus propGetPointerN(OfxPropertySetHandle s, const char* n, int count, void** v)
{
    for (int i = 0; i < count; ++i)
        if (OfxStatus st = getOne<'p'>(s, n, i, v + i); st != kOfxStatOK)
            return st;
    return kOfxStatOK;
}
OfxStatus propGetStringN(OfxPropertySetHandle s, const char* n, int count, char** v)
{
    for (int i = 0; i < count; ++i)
        if (OfxStatus st = getOne<'s'>(s, n, i, v + i); st != kOfxStatOK)
            return st;
    return kOfxStatOK;
}
OfxStatus propGetDoubleN(OfxPropertySetHandle s, const char* n, int count, double* v)
{
    for (int i = 0; i < count; ++i)
        if (OfxStatus st = getOne<'d'>(s, n, i, v + i); st != kOfxStatOK)
            return st;
    return kOfxStatOK;
}
OfxStatus propGetIntN(OfxPropertySetHandle s, const char* n, int count, int* v)
{
    for (int i = 0; i < count; ++i)
        if (OfxStatus st = getOne<'i'>(s, n, i, v + i); st != kOfxStatOK)
            return st;
    return kOfxStatOK;
}
OfxStatus propReset(OfxPropertySetHandle s, const char* n)
{
    if (!s)
        return kOfxStatErrBadHandle;
    s->props.erase(n);
    return kOfxStatOK;
}
OfxStatus propGetDimension(OfxPropertySetHandle s, const char* n, int* count)
{
    if (!s || !count)
        return kOfxStatErrBadHandle;
    const auto* p = s->find(n);
    if (!p)
        return kOfxStatErrUnknown;
    *count = int(p->size());
    return kOfxStatOK;
}

OfxPropertySuiteV1 propertySuite = {propSetPointer,  propSetString,  propSetDouble,  propSetInt,      propSetPointerN,
                                    propSetStringN,  propSetDoubleN, propSetIntN,    propGetPointer,  propGetString,
                                    propGetDouble,   propGetInt,     propGetPointerN, propGetStringN, propGetDoubleN,
                                    propGetIntN,     propReset,      propGetDimension};

// --- The image effect suite ---

OfxStatus getPropertySet(OfxImageEffectHandle effect, OfxPropertySetHandle* props)
{
    if (!effect || !props)
        return kOfxStatErrBadHandle;
    *props = &effect->props;
    return kOfxStatOK;
}
OfxStatus getParamSet(OfxImageEffectHandle effect, OfxParamSetHandle* params)
{
    if (!effect || !params)
        return kOfxStatErrBadHandle;
    *params = &effect->params;
    return kOfxStatOK;
}
OfxStatus clipDefine(OfxImageEffectHandle effect, const char* name, OfxPropertySetHandle* props)
{
    if (!effect || !name)
        return kOfxStatErrBadHandle;
    auto& clip = effect->clips[name];
    if (!clip) {
        clip = std::make_unique<OfxImageClipStruct>();
        clip->name = name;
        clip->effect = effect;
        clip->props.setString(kOfxPropType, {kOfxTypeClip});
        clip->props.setString(kOfxPropName, {name});
        clip->props.setString(kOfxImageEffectPropSupportedComponents, {kOfxImageComponentRGBA});
        clip->props.setInt(kOfxImageClipPropOptional, {0});
        effect->clipOrder.push_back(name);
    }
    if (props)
        *props = &clip->props;
    return kOfxStatOK;
}
OfxStatus clipGetHandle(OfxImageEffectHandle effect, const char* name, OfxImageClipHandle* clip, OfxPropertySetHandle* props)
{
    if (!effect || !name || !clip)
        return kOfxStatErrBadHandle;
    const auto it = effect->clips.find(name);
    if (it == effect->clips.end())
        return kOfxStatErrUnknown;
    *clip = it->second.get();
    if (props)
        *props = &it->second->props;
    return kOfxStatOK;
}
OfxStatus clipGetPropertySet(OfxImageClipHandle clip, OfxPropertySetHandle* props)
{
    if (!clip || !props)
        return kOfxStatErrBadHandle;
    *props = &clip->props;
    return kOfxStatOK;
}
OfxStatus clipGetImage(OfxImageClipHandle clip, OfxTime time, const OfxRectD*, OfxPropertySetHandle* image)
{
    if (!clip || !image || !clip->effect)
        return kOfxStatErrBadHandle;
    const auto& f = clip->effect->frame;
    const bool output = clip->name == kOfxImageEffectOutputClipName;
    void* data = output ? f.output : f.source;
    if (!data)
        return kOfxStatFailed;
    auto* props = new OfxPropertySetStruct;
    props->setString(kOfxPropType, {kOfxTypeImage});
    props->setPointer(kOfxImagePropData, {data});
    props->setInt(kOfxImagePropBounds, {0, 0, f.width, f.height});
    props->setInt(kOfxImagePropRegionOfDefinition, {0, 0, f.width, f.height});
    props->setInt(kOfxImagePropRowBytes, {f.width * 4 * f.bytesPerComponent});
    props->setString(kOfxImageEffectPropComponents, {kOfxImageComponentRGBA});
    props->setString(kOfxImageEffectPropPixelDepth, {f.bytesPerComponent == 4 ? kOfxBitDepthFloat : kOfxBitDepthByte});
    props->setString(kOfxImageEffectPropPreMultiplication, {kOfxImagePreMultiplied});
    props->setDouble(kOfxImagePropPixelAspectRatio, {1.0});
    props->setDouble(kOfxImageEffectPropRenderScale, {1.0, 1.0});
    props->setString(kOfxImagePropField, {kOfxImageFieldNone});
    props->setString(kOfxImagePropUniqueIdentifier, {clip->name + "@" + std::to_string(time)});
    *image = props;
    return kOfxStatOK;
}
OfxStatus clipReleaseImage(OfxPropertySetHandle image)
{
    delete image;
    return kOfxStatOK;
}
OfxStatus clipGetRegionOfDefinition(OfxImageClipHandle clip, OfxTime, OfxRectD* bounds)
{
    if (!clip || !bounds || !clip->effect)
        return kOfxStatErrBadHandle;
    *bounds = {0, 0, double(clip->effect->frame.width), double(clip->effect->frame.height)};
    return kOfxStatOK;
}
int abortRender(OfxImageEffectHandle)
{
    return 0;
}
OfxStatus imageMemoryAlloc(OfxImageEffectHandle, size_t bytes, OfxImageMemoryHandle* memory)
{
    if (!memory)
        return kOfxStatErrBadHandle;
    *memory = new OfxImageMemoryStruct{std::vector<unsigned char>(bytes)};
    return kOfxStatOK;
}
OfxStatus imageMemoryFree(OfxImageMemoryHandle memory)
{
    delete memory;
    return kOfxStatOK;
}
OfxStatus imageMemoryLock(OfxImageMemoryHandle memory, void** data)
{
    if (!memory || !data)
        return kOfxStatErrBadHandle;
    *data = memory->bytes.data();
    return kOfxStatOK;
}
OfxStatus imageMemoryUnlock(OfxImageMemoryHandle)
{
    return kOfxStatOK;
}

OfxImageEffectSuiteV1 imageEffectSuite = {getPropertySet,  getParamSet,      clipDefine,       clipGetHandle,
                                          clipGetPropertySet, clipGetImage,  clipReleaseImage, clipGetRegionOfDefinition,
                                          abortRender,     imageMemoryAlloc, imageMemoryFree,  imageMemoryLock,
                                          imageMemoryUnlock};

// --- The parameter suite ---

OfxStatus paramDefine(OfxParamSetHandle set, const char* type, const char* name, OfxPropertySetHandle* props)
{
    if (!set || !type || !name)
        return kOfxStatErrBadHandle;
    auto& param = set->params[name];
    if (!param) {
        param = std::make_unique<OfxParamStruct>();
        param->name = name;
        param->type = type;
        param->value.assign(std::size_t(componentsOf(type)), 0.0);
        param->props.setString(kOfxPropType, {kOfxTypeParameter});
        param->props.setString(kOfxParamPropType, {type});
        param->props.setString(kOfxPropName, {name});
        param->props.setString(kOfxPropLabel, {name});
        param->props.setInt(kOfxParamPropAnimates, {1});
        set->order.push_back(name);
    }
    if (props)
        *props = &param->props;
    return kOfxStatOK;
}
OfxStatus paramGetHandle(OfxParamSetHandle set, const char* name, OfxParamHandle* param, OfxPropertySetHandle* props)
{
    if (!set || !name || !param)
        return kOfxStatErrBadHandle;
    const auto it = set->params.find(name);
    if (it == set->params.end())
        return kOfxStatErrUnknown;
    *param = it->second.get();
    if (props)
        *props = &it->second->props;
    return kOfxStatOK;
}
OfxStatus paramSetGetPropertySet(OfxParamSetHandle set, OfxPropertySetHandle* props)
{
    if (!set || !props)
        return kOfxStatErrBadHandle;
    *props = &set->props;
    return kOfxStatOK;
}
OfxStatus paramGetPropertySet(OfxParamHandle param, OfxPropertySetHandle* props)
{
    if (!param || !props)
        return kOfxStatErrBadHandle;
    *props = &param->props;
    return kOfxStatOK;
}

// Values out through the varargs, by type.
OfxStatus readValue(OfxParamHandle param, va_list args)
{
    if (!param)
        return kOfxStatErrBadHandle;
    const std::string& type = param->type;
    if (type == kOfxParamTypeString || type == kOfxParamTypeCustom || type == kOfxParamTypeStrChoice) {
        *va_arg(args, const char**) = param->text.c_str();
        return kOfxStatOK;
    }
    const int n = componentsOf(type);
    for (int i = 0; i < n; ++i) {
        const double v = i < int(param->value.size()) ? param->value[std::size_t(i)] : 0.0;
        if (integral(type))
            *va_arg(args, int*) = int(std::lround(v));
        else
            *va_arg(args, double*) = v;
    }
    return kOfxStatOK;
}
OfxStatus writeValue(OfxParamHandle param, va_list args)
{
    if (!param)
        return kOfxStatErrBadHandle;
    const std::string& type = param->type;
    if (type == kOfxParamTypeString || type == kOfxParamTypeCustom || type == kOfxParamTypeStrChoice) {
        const char* text = va_arg(args, const char*);
        param->text = text ? text : "";
        return kOfxStatOK;
    }
    const int n = componentsOf(type);
    param->value.resize(std::size_t(n));
    for (int i = 0; i < n; ++i)
        param->value[std::size_t(i)] = integral(type) ? double(va_arg(args, int)) : va_arg(args, double);
    return kOfxStatOK;
}
OfxStatus paramGetValue(OfxParamHandle param, ...)
{
    va_list args;
    va_start(args, param);
    const OfxStatus status = readValue(param, args);
    va_end(args);
    return status;
}
OfxStatus paramGetValueAtTime(OfxParamHandle param, OfxTime time, ...)
{
    (void)time; // set for the frame being rendered
    va_list args;
    va_start(args, time);
    const OfxStatus status = readValue(param, args);
    va_end(args);
    return status;
}
OfxStatus paramGetDerivative(OfxParamHandle param, OfxTime time, ...)
{
    if (!param)
        return kOfxStatErrBadHandle;
    va_list args;
    va_start(args, time);
    for (int i = 0; i < componentsOf(param->type); ++i)
        *va_arg(args, double*) = 0.0;
    va_end(args);
    return kOfxStatOK;
}
OfxStatus paramGetIntegral(OfxParamHandle param, OfxTime time1, OfxTime time2, ...)
{
    if (!param)
        return kOfxStatErrBadHandle;
    va_list args;
    va_start(args, time2);
    for (int i = 0; i < componentsOf(param->type); ++i)
        *va_arg(args, double*) = param->value[std::size_t(i)] * (time2 - time1);
    va_end(args);
    return kOfxStatOK;
}
OfxStatus paramSetValue(OfxParamHandle param, ...)
{
    va_list args;
    va_start(args, param);
    const OfxStatus status = writeValue(param, args);
    va_end(args);
    return status;
}
OfxStatus paramSetValueAtTime(OfxParamHandle param, OfxTime time, ...)
{
    va_list args;
    va_start(args, time);
    const OfxStatus status = writeValue(param, args);
    va_end(args);
    return status;
}
OfxStatus paramGetNumKeys(OfxParamHandle param, unsigned int* count)
{
    if (!param || !count)
        return kOfxStatErrBadHandle;
    *count = 0; // animation is the host's: the plug-in sees each frame's values
    return kOfxStatOK;
}
OfxStatus paramGetKeyTime(OfxParamHandle, unsigned int, OfxTime*)
{
    return kOfxStatErrBadIndex;
}
OfxStatus paramGetKeyIndex(OfxParamHandle, OfxTime, int, int*)
{
    return kOfxStatFailed;
}
OfxStatus paramDeleteKey(OfxParamHandle, OfxTime)
{
    return kOfxStatErrBadIndex;
}
OfxStatus paramDeleteAllKeys(OfxParamHandle)
{
    return kOfxStatOK;
}
OfxStatus paramCopy(OfxParamHandle to, OfxParamHandle from, OfxTime, const OfxRangeD*)
{
    if (!to || !from)
        return kOfxStatErrBadHandle;
    to->value = from->value;
    to->text = from->text;
    return kOfxStatOK;
}
OfxStatus paramEditBegin(OfxParamSetHandle, const char*)
{
    return kOfxStatOK;
}
OfxStatus paramEditEnd(OfxParamSetHandle)
{
    return kOfxStatOK;
}

OfxParameterSuiteV1 parameterSuite = {paramDefine,      paramGetHandle,     paramSetGetPropertySet, paramGetPropertySet,
                                      paramGetValue,    paramGetValueAtTime, paramGetDerivative,    paramGetIntegral,
                                      paramSetValue,    paramSetValueAtTime, paramGetNumKeys,       paramGetKeyTime,
                                      paramGetKeyIndex, paramDeleteKey,     paramDeleteAllKeys,     paramCopy,
                                      paramEditBegin,   paramEditEnd};

// --- Memory, threads and messages ---

OfxStatus memoryAlloc(void*, size_t bytes, void** data)
{
    if (!data)
        return kOfxStatErrBadHandle;
    *data = std::malloc(std::max<size_t>(bytes, 1));
    return *data ? kOfxStatOK : kOfxStatErrMemory;
}
OfxStatus memoryFree(void* data)
{
    std::free(data);
    return kOfxStatOK;
}
OfxMemorySuiteV1 memorySuite = {memoryAlloc, memoryFree};

thread_local unsigned int t_threadIndex = 0;
thread_local bool t_spawned = false;

OfxStatus multiThread(OfxThreadFunctionV1 func, unsigned int count, void* arg)
{
    if (!func)
        return kOfxStatErrBadHandle;
    if (count <= 1) {
        func(0, 1, arg);
        return kOfxStatOK;
    }
    dispatch_apply(count, dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^(size_t i) {
        t_threadIndex = unsigned(i);
        t_spawned = true;
        func(unsigned(i), count, arg);
        t_spawned = false;
        t_threadIndex = 0;
    });
    return kOfxStatOK;
}
OfxStatus multiThreadNumCPUs(unsigned int* count)
{
    if (!count)
        return kOfxStatErrBadHandle;
    *count = std::max(1u, std::thread::hardware_concurrency());
    return kOfxStatOK;
}
OfxStatus multiThreadIndex(unsigned int* index)
{
    if (!index)
        return kOfxStatErrBadHandle;
    *index = t_threadIndex;
    return kOfxStatOK;
}
int multiThreadIsSpawnedThread()
{
    return t_spawned ? 1 : 0;
}
OfxStatus mutexCreate(OfxMutexHandle* mutex, int lockCount)
{
    if (!mutex)
        return kOfxStatErrBadHandle;
    *mutex = new OfxMutex;
    for (int i = 0; i < lockCount; ++i)
        (*mutex)->mutex.lock();
    return kOfxStatOK;
}
OfxStatus mutexDestroy(const OfxMutexHandle mutex)
{
    delete mutex;
    return kOfxStatOK;
}
OfxStatus mutexLock(const OfxMutexHandle mutex)
{
    if (!mutex)
        return kOfxStatErrBadHandle;
    mutex->mutex.lock();
    return kOfxStatOK;
}
OfxStatus mutexUnLock(const OfxMutexHandle mutex)
{
    if (!mutex)
        return kOfxStatErrBadHandle;
    mutex->mutex.unlock();
    return kOfxStatOK;
}
OfxStatus mutexTryLock(const OfxMutexHandle mutex)
{
    if (!mutex)
        return kOfxStatErrBadHandle;
    return mutex->mutex.try_lock() ? kOfxStatOK : kOfxStatFailed;
}
OfxMultiThreadSuiteV1 multiThreadSuite = {multiThread, multiThreadNumCPUs, multiThreadIndex, multiThreadIsSpawnedThread,
                                          mutexCreate, mutexDestroy,       mutexLock,        mutexUnLock,
                                          mutexTryLock};

OfxStatus message(void*, const char* type, const char*, const char* format, ...)
{
    if (format && std::getenv("TVE_OFX_DEBUG")) {
        va_list args;
        va_start(args, format);
        std::fprintf(stderr, "OpenFX %s: ", type ? type : "message");
        std::vfprintf(stderr, format, args);
        std::fprintf(stderr, "\n");
        va_end(args);
    }
    // Questions are answered with their default.
    return type && std::strcmp(type, kOfxMessageQuestion) == 0 ? kOfxStatReplyDefault : kOfxStatOK;
}
OfxStatus setPersistentMessage(void*, const char*, const char*, const char*, ...)
{
    return kOfxStatOK;
}
OfxStatus clearPersistentMessage(void*)
{
    return kOfxStatOK;
}
OfxMessageSuiteV1 messageSuiteV1 = {message};
OfxMessageSuiteV2 messageSuiteV2 = {message, setPersistentMessage, clearPersistentMessage};

// --- The host ---

const void* fetchSuite(OfxPropertySetHandle, const char* name, int version)
{
    if (!name)
        return nullptr;
    const std::string suite = name;
    if (suite == kOfxPropertySuite && version == 1)
        return &propertySuite;
    if (suite == kOfxImageEffectSuite && version == 1)
        return &imageEffectSuite;
    if (suite == kOfxParameterSuite && version == 1)
        return &parameterSuite;
    if (suite == kOfxMemorySuite && version == 1)
        return &memorySuite;
    if (suite == kOfxMultiThreadSuite && version == 1)
        return &multiThreadSuite;
    if (suite == kOfxMessageSuite && version == 1)
        return &messageSuiteV1;
    if (suite == kOfxMessageSuite && version == 2)
        return &messageSuiteV2;
    return nullptr;
}

OfxHost& host()
{
    static OfxPropertySetStruct properties = [] {
        OfxPropertySetStruct p;
        p.setString(kOfxPropName, {"com.techievideoeditor"});
        p.setString(kOfxPropLabel, {"TechieVideoEditor"});
        p.setInt(kOfxPropAPIVersion, {1, 5});
        p.setInt(kOfxImageEffectHostPropIsBackground, {0});
        p.setInt(kOfxImageEffectPropSupportsOverlays, {0});
        p.setInt(kOfxImageEffectPropSupportsMultiResolution, {0});
        p.setInt(kOfxImageEffectPropSupportsTiles, {0});
        p.setInt(kOfxImageEffectPropTemporalClipAccess, {0});
        p.setString(kOfxImageEffectPropSupportedComponents, {kOfxImageComponentRGBA});
        p.setString(kOfxImageEffectPropSupportedContexts, {kOfxImageEffectContextFilter, kOfxImageEffectContextGeneral});
        p.setString(kOfxImageEffectPropSupportedPixelDepths, {kOfxBitDepthFloat, kOfxBitDepthByte});
        p.setInt(kOfxImageEffectPropSupportsMultipleClipDepths, {0});
        p.setInt(kOfxImageEffectPropSupportsMultipleClipPARs, {0});
        p.setInt(kOfxImageEffectPropSetableFrameRate, {0});
        p.setInt(kOfxImageEffectPropSetableFielding, {0});
        p.setInt(kOfxParamHostPropSupportsCustomInteract, {0});
        p.setInt(kOfxParamHostPropSupportsStringAnimation, {0});
        p.setInt(kOfxParamHostPropSupportsChoiceAnimation, {0});
        p.setInt(kOfxParamHostPropSupportsBooleanAnimation, {0});
        p.setInt(kOfxParamHostPropSupportsCustomAnimation, {0});
        p.setInt(kOfxParamHostPropMaxParameters, {-1});
        p.setInt(kOfxParamHostPropMaxPages, {0});
        p.setInt(kOfxParamHostPropPageRowColumnCount, {0, 0});
        p.setString(kOfxImageEffectPropMetalRenderSupported, {"true"});
        return p;
    }();
    static OfxHost made{&properties, fetchSuite};
    return made;
}

// --- Plug-in binaries and their image effects ---

struct Plugin {
    OfxPlugin* entry = nullptr;
    std::string path;
    bool loaded = false;
    std::unique_ptr<OfxImageEffectStruct> descriptor; // described in `context`
    std::string context;
    std::string problem;
};

std::recursive_mutex& pluginMutex()
{
    static std::recursive_mutex mutex;
    return mutex;
}

// The binary inside a bundle: <bundle>/Contents/MacOS/<name>.ofx (or its one file there).
std::string binaryOf(const std::string& bundle)
{
    std::error_code ec;
    std::vector<std::string> files;
    for (const auto& entry : fs::directory_iterator(fs::path(bundle) / "Contents" / "MacOS", ec)) {
        if (entry.path().extension() == ".ofx")
            return entry.path().string();
        if (entry.is_regular_file())
            files.push_back(entry.path().string());
    }
    return files.size() == 1 ? files[0] : std::string();
}

// Loads a bundle's binary (kept for the life of the app): its image effect plug-ins.
std::vector<OfxPlugin*> loadBinary(const std::string& bundle, std::string* error)
{
    static std::map<std::string, std::vector<OfxPlugin*>> loaded;
    if (const auto it = loaded.find(bundle); it != loaded.end())
        return it->second;
    const std::string binary = binaryOf(bundle);
    void* library = binary.empty() ? nullptr : dlopen(binary.c_str(), RTLD_LOCAL | RTLD_NOW);
    if (!library) {
        *error = binary.empty() ? "it has no OpenFX binary" : std::string("it couldn't be loaded: ") + dlerror();
        return {};
    }
    auto count = reinterpret_cast<int (*)()>(dlsym(library, "OfxGetNumberOfPlugins"));
    auto get = reinterpret_cast<OfxPlugin* (*)(int)>(dlsym(library, "OfxGetPlugin"));
    if (!count || !get) {
        *error = "it isn't an OpenFX plug-in";
        return {};
    }
    std::vector<OfxPlugin*> plugins;
    for (int i = 0; i < count(); ++i)
        if (OfxPlugin* p = get(i); p && p->pluginApi && std::strcmp(p->pluginApi, kOfxImageEffectPluginApi) == 0 && p->apiVersion == 1)
            plugins.push_back(p);
    loaded[bundle] = plugins;
    return plugins;
}

OfxStatus call(Plugin& plugin, const char* action, const void* handle, OfxPropertySetHandle in = nullptr,
               OfxPropertySetHandle out = nullptr)
{
    return plugin.entry->mainEntry(action, handle, in, out);
}

bool succeeded(OfxStatus status)
{
    return status == kOfxStatOK || status == kOfxStatReplyDefault;
}

// Loaded and described (in the filter context, or the general one).
bool describe(Plugin& plugin)
{
    if (plugin.descriptor || !plugin.problem.empty())
        return plugin.descriptor != nullptr;
    plugin.entry->setHost(&host());
    if (!plugin.loaded) {
        if (!succeeded(call(plugin, kOfxActionLoad, nullptr))) {
            plugin.problem = "it failed to load";
            return false;
        }
        plugin.loaded = true;
    }
    auto general = std::make_unique<OfxImageEffectStruct>();
    general->props.setString(kOfxPropType, {kOfxTypeImageEffect});
    general->props.setString(kOfxImageEffectPropSupportedPixelDepths, {kOfxBitDepthFloat});
    general->props.setString(kOfxImageEffectPluginRenderThreadSafety, {kOfxImageEffectRenderInstanceSafe});
    if (!succeeded(call(plugin, kOfxActionDescribe, general.get()))) {
        plugin.problem = "it couldn't describe itself";
        return false;
    }
    const auto contexts = general->props.strings(kOfxImageEffectPropSupportedContexts);
    const auto has = [&](const char* c) { return std::ranges::find(contexts, std::string(c)) != contexts.end(); };
    plugin.context = has(kOfxImageEffectContextFilter) ? kOfxImageEffectContextFilter
                   : has(kOfxImageEffectContextGeneral) ? kOfxImageEffectContextGeneral
                                                         : "";
    if (plugin.context.empty()) {
        plugin.problem = "it isn't an effect on a picture (a generator or transition)";
        return false;
    }
    auto inContext = std::make_unique<OfxImageEffectStruct>();
    inContext->props = general->props;
    OfxPropertySetStruct in;
    in.setString(kOfxImageEffectPropContext, {plugin.context});
    if (!succeeded(call(plugin, kOfxImageEffectActionDescribeInContext, inContext.get(), &in))) {
        plugin.problem = "it couldn't describe itself for this host";
        return false;
    }
    if (!inContext->clips.contains(kOfxImageEffectOutputClipName) || !inContext->clips.contains(kOfxImageEffectSimpleSourceClipName)) {
        plugin.problem = "it doesn't take one picture in and give one out";
        return false;
    }
    // Parameter values start at their defaults.
    for (auto& [name, param] : inContext->params.params) {
        const auto defaults = param->props.doubles(kOfxParamPropDefault);
        for (std::size_t i = 0; i < param->value.size() && i < defaults.size(); ++i)
            param->value[i] = defaults[i];
        param->text = param->props.string(kOfxParamPropDefault);
    }
    plugin.descriptor = std::move(inContext);
    return true;
}

// The plug-ins we know of, by identifier (each with the newest version).
std::map<std::string, Plugin>& plugins()
{
    static std::map<std::string, Plugin> all;
    return all;
}

Plugin* pluginFor(const std::string& identifier, const std::string& bundle, std::string* error)
{
    std::lock_guard lock(pluginMutex());
    if (const auto it = plugins().find(identifier); it != plugins().end())
        return &it->second;
    for (OfxPlugin* p : loadBinary(bundle, error)) {
        if (identifier != p->pluginIdentifier)
            continue;
        Plugin& made = plugins()[identifier];
        if (!made.entry || p->pluginVersionMajor > made.entry->pluginVersionMajor)
            made = Plugin{.entry = p, .path = bundle};
    }
    const auto it = plugins().find(identifier);
    if (it == plugins().end()) {
        if (error && error->empty())
            *error = "the plug-in isn't in its bundle";
        return nullptr;
    }
    return &it->second;
}

OfxPluginInfo infoOf(Plugin& plugin)
{
    OfxPluginInfo info;
    info.identifier = plugin.entry->pluginIdentifier;
    info.id = "ofx:" + info.identifier;
    info.path = plugin.path;
    info.versionMajor = int(plugin.entry->pluginVersionMajor);
    info.versionMinor = int(plugin.entry->pluginVersionMinor);
    if (!describe(plugin)) {
        info.name = info.identifier;
        info.problem = plugin.problem;
        return info;
    }
    const OfxPropertySetStruct& props = plugin.descriptor->props;
    info.name = props.string(kOfxPropLabel, info.identifier);
    info.group = props.string(kOfxImageEffectPluginPropGrouping);
    info.metal = props.string(kOfxImageEffectPropMetalRenderSupported) == "true";
    for (const std::string& name : plugin.descriptor->params.order) {
        const OfxParamStruct& param = *plugin.descriptor->params.params.at(name);
        static const std::map<std::string, std::string> kinds{
            {kOfxParamTypeDouble, "double"},     {kOfxParamTypeInteger, "int"},       {kOfxParamTypeBoolean, "bool"},
            {kOfxParamTypeChoice, "choice"},     {kOfxParamTypeRGB, "rgb"},           {kOfxParamTypeRGBA, "rgba"},
            {kOfxParamTypeDouble2D, "double2d"}, {kOfxParamTypeInteger2D, "int2d"},   {kOfxParamTypeDouble3D, "double3d"},
            {kOfxParamTypeInteger3D, "int3d"}};
        const auto kind = kinds.find(param.type);
        if (kind == kinds.end() || param.props.integer(kOfxParamPropSecret) != 0)
            continue;
        OfxParameterInfo p{.name = name, .label = param.props.string(kOfxPropLabel, name), .type = kind->second,
                           .components = componentsOf(param.type)};
        p.defaults = param.props.doubles(kOfxParamPropDefault);
        p.defaults.resize(std::size_t(p.components), 0.0);
        auto low = param.props.doubles(kOfxParamPropDisplayMin);
        auto high = param.props.doubles(kOfxParamPropDisplayMax);
        if (low.empty())
            low = param.props.doubles(kOfxParamPropMin);
        if (high.empty())
            high = param.props.doubles(kOfxParamPropMax);
        const bool colour = p.type == "rgb" || p.type == "rgba";
        p.min = colour ? 0.0 : !low.empty() && std::abs(low[0]) < 1e9 ? low[0] : (p.type == "int" ? 0.0 : 0.0);
        p.max = colour ? 1.0 : !high.empty() && std::abs(high[0]) < 1e9 ? high[0] : (p.type == "int" ? 100.0 : 1.0);
        if (p.max <= p.min)
            p.max = p.min + 1.0;
        p.options = param.props.strings(kOfxParamPropChoiceOption);
        p.animates = param.props.integer(kOfxParamPropAnimates, 1) != 0;
        p.hint = param.props.string(kOfxParamPropHint);
        info.parameters.push_back(std::move(p));
    }
    return info;
}

nlohmann::json toJson(const OfxPluginInfo& info)
{
    nlohmann::json params = nlohmann::json::array();
    for (const OfxParameterInfo& p : info.parameters)
        params.push_back({{"name", p.name}, {"label", p.label}, {"type", p.type}, {"components", p.components},
                          {"defaults", p.defaults}, {"min", p.min}, {"max", p.max}, {"options", p.options},
                          {"animates", p.animates}, {"hint", p.hint}});
    return {{"id", info.id}, {"identifier", info.identifier}, {"name", info.name}, {"group", info.group},
            {"major", info.versionMajor}, {"minor", info.versionMinor}, {"metal", info.metal}, {"params", params},
            {"problem", info.problem}};
}

OfxPluginInfo fromJson(const nlohmann::json& j, const std::string& bundle)
{
    OfxPluginInfo info{.id = j.value("id", ""), .identifier = j.value("identifier", ""), .name = j.value("name", ""),
                       .group = j.value("group", ""), .path = bundle, .versionMajor = j.value("major", 1),
                       .versionMinor = j.value("minor", 0), .metal = j.value("metal", false), .problem = j.value("problem", "")};
    for (const auto& p : j.value("params", nlohmann::json::array()))
        info.parameters.push_back({.name = p.value("name", ""), .label = p.value("label", ""), .type = p.value("type", ""),
                                   .components = p.value("components", 1),
                                   .defaults = p.value("defaults", std::vector<double>{}), .min = p.value("min", 0.0),
                                   .max = p.value("max", 1.0), .options = p.value("options", std::vector<std::string>{}),
                                   .animates = p.value("animates", true), .hint = p.value("hint", "")});
    return info;
}

// --- Finding bundles (described out of process, cached) ---

std::vector<std::string> ofxFolders()
{
    std::vector<std::string> folders;
    const auto add = [&](const char* list) {
        std::string all = list;
        for (std::size_t at = 0; at <= all.size();) {
            const std::size_t colon = std::min(all.find(':', at), all.size());
            if (colon > at)
                folders.push_back(all.substr(at, colon - at));
            at = colon + 1;
        }
    };
    if (const char* ours = std::getenv("TVE_OFX_PATH")) {
        add(ours);
        return folders;
    }
    if (const char* standard = std::getenv("OFX_PLUGIN_PATH"))
        add(standard);
    folders.push_back("/Library/OFX/Plugins");
    return folders;
}

struct Scanned {
    std::int64_t modified = 0;
    std::vector<OfxPluginInfo> plugins;
};

std::mutex& listMutex()
{
    static std::mutex mutex;
    return mutex;
}

std::map<std::string, Scanned>& scanned()
{
    static std::map<std::string, Scanned> cache;
    return cache;
}

std::string cacheFile()
{
    const char* path = std::getenv("TVE_PLUGIN_CACHE_FILE");
    return path ? std::string(path) + ".ofx" : std::string();
}

std::int64_t modified(const std::string& path)
{
    struct stat info{};
    return stat(path.c_str(), &info) == 0 ? std::int64_t(info.st_mtimespec.tv_sec) : 0;
}

// --- Metal ---

struct Gpu {
    id<MTLDevice> device = nil;
    id<MTLCommandQueue> queue = nil;
};

Gpu& gpu()
{
    static Gpu made = [] {
        Gpu g;
        if (const char* off = std::getenv("TVE_OFX_METAL"); off && std::strcmp(off, "0") == 0)
            return g;
        g.device = MTLCreateSystemDefaultDevice();
        g.queue = g.device ? [g.device newCommandQueue] : nil;
        return g;
    }();
    return made;
}

// --- An instance ---

class Instance final : public OfxEffect {
public:
    Instance(Plugin& plugin, OfxPluginInfo info) : m_plugin(plugin), m_info(std::move(info))
    {
        copyEffect(*plugin.descriptor, m_effect);
        m_effect.props.setString(kOfxImageEffectPropContext, {plugin.context});
        m_effect.props.setInt(kOfxImageEffectInstancePropSequentialRender, {0});
        m_effect.props.setDouble(kOfxImageEffectPropProjectSize, {1920, 1080});
        m_effect.props.setDouble(kOfxImageEffectPropProjectExtent, {1920, 1080});
        m_effect.props.setDouble(kOfxImageEffectPropProjectOffset, {0, 0});
        m_effect.props.setDouble(kOfxImageEffectPropProjectPixelAspectRatio, {1.0});
        m_effect.props.setDouble(kOfxImageEffectInstancePropEffectDuration, {1e6});
        m_effect.props.setDouble(kOfxImageEffectPropFrameRate, {30.0});
        m_effect.props.setPointer(kOfxPropInstanceData, {nullptr});
        const auto depths = plugin.descriptor->props.strings(kOfxImageEffectPropSupportedPixelDepths);
        m_bytes = depths.empty() || std::ranges::find(depths, std::string(kOfxBitDepthFloat)) != depths.end() ? 4 : 1;
        for (auto& [name, clip] : m_effect.clips) {
            clip->props.setString(kOfxImageEffectPropComponents, {kOfxImageComponentRGBA});
            clip->props.setString(kOfxImageClipPropUnmappedComponents, {kOfxImageComponentRGBA});
            clip->props.setString(kOfxImageEffectPropPixelDepth, {m_bytes == 4 ? kOfxBitDepthFloat : kOfxBitDepthByte});
            clip->props.setString(kOfxImageClipPropUnmappedPixelDepth, {m_bytes == 4 ? kOfxBitDepthFloat : kOfxBitDepthByte});
            clip->props.setString(kOfxImageEffectPropPreMultiplication, {kOfxImagePreMultiplied});
            clip->props.setDouble(kOfxImagePropPixelAspectRatio, {1.0});
            clip->props.setDouble(kOfxImageEffectPropFrameRate, {30.0});
            clip->props.setDouble(kOfxImageEffectPropFrameRange, {0, 1e6});
            clip->props.setInt(kOfxImageClipPropConnected, {1});
            clip->props.setString(kOfxImageClipPropFieldOrder, {kOfxImageFieldNone});
            clip->props.setInt(kOfxImageClipPropContinuousSamples, {0});
        }
    }

    bool create()
    {
        std::lock_guard lock(pluginMutex());
        m_created = succeeded(call(m_plugin, kOfxActionCreateInstance, &m_effect));
        return m_created;
    }

    ~Instance() override
    {
        if (m_created) {
            std::lock_guard lock(pluginMutex());
            call(m_plugin, kOfxActionDestroyInstance, &m_effect);
        }
        [m_sourceBuffer release];
        [m_outputBuffer release];
    }

    const OfxPluginInfo& info() const override { return m_info; }
    bool usedMetal() const override { return m_usedMetal; }

    bool render(std::vector<float>& rgba, int width, int height, double seconds,
                const std::map<std::string, std::vector<float>>& values, bool metal) override
    {
        if (width <= 0 || height <= 0 || rgba.size() < std::size_t(width) * std::size_t(height) * 4)
            return false;
        applyValues(values, seconds);
        const std::size_t count = std::size_t(width) * std::size_t(height) * 4;
        const std::size_t bytes = count * std::size_t(m_bytes);
        const bool useMetal = metal && m_info.metal && gpu().queue;

        // The picture in, bottom row first, premultiplied.
        void* source = nullptr;
        void* output = nullptr;
        if (useMetal) {
            if (!m_sourceBuffer || m_sourceBuffer.length < bytes) {
                [m_sourceBuffer release];
                [m_outputBuffer release];
                m_sourceBuffer = [gpu().device newBufferWithLength:bytes options:MTLResourceStorageModeShared];
                m_outputBuffer = [gpu().device newBufferWithLength:bytes options:MTLResourceStorageModeShared];
            }
            fill(static_cast<unsigned char*>(m_sourceBuffer.contents), rgba, width, height);
            std::memset(m_outputBuffer.contents, 0, bytes);
            source = m_sourceBuffer;
            output = m_outputBuffer;
        } else {
            m_source.resize(bytes);
            m_output.assign(bytes, 0);
            fill(m_source.data(), rgba, width, height);
            source = m_source.data();
            output = m_output.data();
        }
        m_effect.frame = {source, output, width, height, m_bytes, seconds * 30.0};

        OfxPropertySetStruct in;
        in.setDouble(kOfxPropTime, {seconds * 30.0});
        in.setDouble(kOfxImageEffectPropFrameRange, {seconds * 30.0, seconds * 30.0});
        in.setDouble(kOfxImageEffectPropFrameStep, {1.0});
        in.setString(kOfxImageEffectPropFieldToRender, {kOfxImageFieldNone});
        in.setInt(kOfxImageEffectPropRenderWindow, {0, 0, width, height});
        in.setDouble(kOfxImageEffectPropRenderScale, {1.0, 1.0});
        in.setInt(kOfxImageEffectPropSequentialRenderStatus, {0});
        in.setInt(kOfxImageEffectPropInteractiveRenderStatus, {0});
        in.setInt(kOfxImageEffectPropRenderQualityDraft, {0});
        in.setInt(kOfxPropIsInteractive, {0});
        in.setInt(kOfxImageEffectPropMetalEnabled, {useMetal ? 1 : 0});
        if (useMetal)
            in.setPointer(kOfxImageEffectPropMetalCommandQueue, {gpu().queue});
        bool ok = succeeded(call(m_plugin, kOfxImageEffectActionBeginSequenceRender, &m_effect, &in));
        ok = ok && succeeded(call(m_plugin, kOfxImageEffectActionRender, &m_effect, &in));
        call(m_plugin, kOfxImageEffectActionEndSequenceRender, &m_effect, &in);
        if (useMetal) {
            // What it queued has run once this, after it, has.
            id<MTLCommandBuffer> after = [gpu().queue commandBuffer];
            [after commit];
            [after waitUntilCompleted];
        }
        m_effect.frame = {};
        m_usedMetal = useMetal;
        if (!ok)
            return false;
        read(useMetal ? static_cast<const unsigned char*>(m_outputBuffer.contents) : m_output.data(), rgba, width, height);
        return true;
    }

private:
    // Each parameter: its value from `values` (by name, or "<name>#<i>" a number each), else
    // its default; those that changed are told to the plug-in.
    void applyValues(const std::map<std::string, std::vector<float>>& values, double seconds)
    {
        std::vector<std::string> changed;
        for (const OfxParameterInfo& info : m_info.parameters) {
            OfxParamStruct& param = *m_effect.params.params.at(info.name);
            std::vector<double> next = info.defaults;
            if (const auto it = values.find(info.name); it != values.end())
                for (std::size_t i = 0; i < next.size() && i < it->second.size(); ++i)
                    next[i] = it->second[i];
            for (std::size_t i = 0; i < next.size(); ++i)
                if (const auto it = values.find(info.name + "#" + std::to_string(i)); it != values.end() && !it->second.empty())
                    next[i] = it->second[0];
            if (next != param.value) {
                param.value = next;
                changed.push_back(info.name);
            }
        }
        if (changed.empty() || !m_rendered) {
            m_rendered = true;
            return;
        }
        OfxPropertySetStruct begin;
        begin.setString(kOfxPropChangeReason, {kOfxChangeUserEdited});
        call(m_plugin, kOfxActionBeginInstanceChanged, &m_effect, &begin);
        for (const std::string& name : changed) {
            OfxPropertySetStruct in;
            in.setString(kOfxPropType, {kOfxTypeParameter});
            in.setString(kOfxPropName, {name});
            in.setString(kOfxPropChangeReason, {kOfxChangeUserEdited});
            in.setDouble(kOfxPropTime, {seconds * 30.0});
            in.setDouble(kOfxImageEffectPropRenderScale, {1.0, 1.0});
            call(m_plugin, kOfxActionInstanceChanged, &m_effect, &in);
        }
        call(m_plugin, kOfxActionEndInstanceChanged, &m_effect, &begin);
    }

    // Top row first, straight → bottom row first, premultiplied, in the plug-in's depth.
    void fill(unsigned char* out, const std::vector<float>& rgba, int width, int height) const
    {
        for (int y = 0; y < height; ++y) {
            const float* row = rgba.data() + std::size_t(height - 1 - y) * std::size_t(width) * 4;
            for (int x = 0; x < width; ++x) {
                const float a = row[x * 4 + 3];
                const float px[4] = {row[x * 4] * a, row[x * 4 + 1] * a, row[x * 4 + 2] * a, a};
                const std::size_t at = (std::size_t(y) * std::size_t(width) + std::size_t(x)) * 4;
                if (m_bytes == 4)
                    std::memcpy(reinterpret_cast<float*>(out) + at, px, sizeof px);
                else
                    for (int c = 0; c < 4; ++c)
                        out[at + std::size_t(c)] = static_cast<unsigned char>(std::lround(std::clamp(px[c], 0.0f, 1.0f) * 255.0f));
            }
        }
    }
    void read(const unsigned char* in, std::vector<float>& rgba, int width, int height) const
    {
        for (int y = 0; y < height; ++y) {
            float* row = rgba.data() + std::size_t(height - 1 - y) * std::size_t(width) * 4;
            for (int x = 0; x < width; ++x) {
                const std::size_t at = (std::size_t(y) * std::size_t(width) + std::size_t(x)) * 4;
                float px[4];
                if (m_bytes == 4)
                    std::memcpy(px, reinterpret_cast<const float*>(in) + at, sizeof px);
                else
                    for (int c = 0; c < 4; ++c)
                        px[c] = float(in[at + std::size_t(c)]) / 255.0f;
                const float a = std::clamp(px[3], 0.0f, 1.0f);
                for (int c = 0; c < 3; ++c)
                    row[x * 4 + c] = a > 1e-6f ? std::clamp(px[c] / a, 0.0f, 1.0f) : 0.0f;
                row[x * 4 + 3] = a;
            }
        }
    }

    Plugin& m_plugin;
    OfxPluginInfo m_info;
    OfxImageEffectStruct m_effect;
    int m_bytes = 4;
    bool m_created = false;
    bool m_rendered = false;
    bool m_usedMetal = false;
    std::vector<unsigned char> m_source;
    std::vector<unsigned char> m_output;
    id<MTLBuffer> m_sourceBuffer = nil;
    id<MTLBuffer> m_outputBuffer = nil;
};

} // namespace

std::string describeOfxBundle(const std::string& bundle)
{
    std::string error;
    const auto entries = loadBinary(bundle, &error);
    if (!error.empty())
        return nlohmann::json{{"error", error}}.dump();
    nlohmann::json list = nlohmann::json::array();
    for (OfxPlugin* p : entries) {
        Plugin* plugin = pluginFor(p->pluginIdentifier, bundle, &error);
        if (plugin)
            list.push_back(toJson(infoOf(*plugin)));
    }
    return nlohmann::json{{"plugins", list}}.dump();
}

std::vector<OfxPluginInfo> listOfxPlugins()
{
    std::lock_guard lock(listMutex());
    static bool loadedCache = false;
    if (!loadedCache && !cacheFile().empty()) {
        loadedCache = true;
        std::ifstream in(cacheFile());
        const auto j = nlohmann::json::parse(in, nullptr, false);
        if (j.is_object())
            for (const auto& [path, entry] : j.items()) {
                Scanned s{.modified = entry.value("modified", std::int64_t(0))};
                for (const auto& p : entry.value("plugins", nlohmann::json::array()))
                    s.plugins.push_back(fromJson(p, path));
                scanned()[path] = std::move(s);
            }
    }
    std::vector<std::string> bundles;
    for (const std::string& folder : ofxFolders()) {
        std::error_code ec;
        for (auto it = fs::recursive_directory_iterator(folder, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec))
            if (it->is_directory() && it->path().filename().string().ends_with(".ofx.bundle")) {
                bundles.push_back(it->path().string());
                it.disable_recursion_pending();
            }
    }
    std::ranges::sort(bundles);
    bool changed = false;
    std::vector<OfxPluginInfo> list;
    for (const std::string& bundle : bundles) {
        Scanned& entry = scanned()[bundle];
        if (entry.modified != modified(bundle) || entry.plugins.empty()) {
            entry = {.modified = modified(bundle)};
            std::string problem;
            const auto j = nlohmann::json::parse(plugins::runPluginScanner({"--ofx", bundle}, &problem), nullptr, false);
            if (problem.empty() && j.is_object() && j.contains("error"))
                problem = j.value("error", "it couldn't be loaded");
            if (problem.empty() && j.is_object())
                for (const auto& p : j.value("plugins", nlohmann::json::array()))
                    entry.plugins.push_back(fromJson(p, bundle));
            if (entry.plugins.empty())
                entry.plugins.push_back({.id = "ofx:?" + bundle, .name = fs::path(bundle).stem().stem().string(), .path = bundle,
                                         .problem = problem.empty() ? std::string("it holds no picture effects") : problem});
            changed = true;
        }
        list.insert(list.end(), entry.plugins.begin(), entry.plugins.end());
    }
    std::erase_if(scanned(), [&](const auto& item) { return !std::ranges::binary_search(bundles, item.first); });
    if (changed && !cacheFile().empty()) {
        nlohmann::json j = nlohmann::json::object();
        for (const auto& [path, entry] : scanned()) {
            nlohmann::json ps = nlohmann::json::array();
            for (const OfxPluginInfo& p : entry.plugins)
                ps.push_back(toJson(p));
            j[path] = {{"modified", entry.modified}, {"plugins", ps}};
        }
        std::error_code ec;
        fs::create_directories(fs::path(cacheFile()).parent_path(), ec);
        std::ofstream(cacheFile()) << j.dump(1);
    }
    std::ranges::stable_sort(list, {}, &OfxPluginInfo::name);
    return list;
}

std::optional<OfxPluginInfo> findOfxPlugin(const std::string& id)
{
    for (OfxPluginInfo& info : listOfxPlugins())
        if (info.id == id)
            return std::move(info);
    return std::nullopt;
}

std::unique_ptr<OfxEffect> OfxEffect::open(const std::string& id, std::string* error)
{
    const auto fail = [&](const std::string& why) -> std::unique_ptr<OfxEffect> {
        if (error)
            *error = why;
        return nullptr;
    };
    const auto info = findOfxPlugin(id);
    if (!info)
        return fail("the OpenFX plug-in isn't installed on this Mac");
    if (!info->problem.empty())
        return fail("the OpenFX plug-in can't be used: " + info->problem);
    std::string why;
    Plugin* plugin = pluginFor(info->identifier, info->path, &why);
    if (!plugin)
        return fail("the OpenFX plug-in couldn't be loaded: " + why);
    {
        std::lock_guard lock(pluginMutex());
        if (!describe(*plugin))
            return fail("the OpenFX plug-in can't be used: " + plugin->problem);
    }
    auto instance = std::make_unique<Instance>(*plugin, *info);
    if (!instance->create())
        return fail("the OpenFX plug-in couldn't be started");
    return instance;
}

} // namespace tve
