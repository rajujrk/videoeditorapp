#include "decode/ImageFile.h"

#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>

#include <algorithm>
#include <cstring>

namespace tve::imagefile {

namespace {

// Releases a Core Foundation object at the end of its scope.
template <typename T> struct Owned {
    T ref = nullptr;
    explicit Owned(T r) : ref(r) {}
    ~Owned()
    {
        if (ref)
            CFRelease(ref);
    }
    Owned(const Owned&) = delete;
    Owned& operator=(const Owned&) = delete;
    explicit operator bool() const { return ref != nullptr; }
};

CGImageSourceRef openSource(const std::string& path)
{
    Owned<CFURLRef> url(CFURLCreateFromFileSystemRepresentation(
        nullptr, reinterpret_cast<const UInt8*>(path.data()), CFIndex(path.size()), false));
    if (!url)
        return nullptr;
    // Only a file's first bytes are needed to know it; nothing decoded until asked.
    const void* keys[] = {kCGImageSourceShouldCache};
    const void* values[] = {kCFBooleanFalse};
    Owned<CFDictionaryRef> options(CFDictionaryCreate(nullptr, keys, values, 1, &kCFTypeDictionaryKeyCallBacks,
                                                      &kCFTypeDictionaryValueCallBacks));
    return CGImageSourceCreateWithURL(url.ref, options.ref);
}

int intProperty(CFDictionaryRef properties, CFStringRef key, int fallback = 0)
{
    const auto number = static_cast<CFNumberRef>(CFDictionaryGetValue(properties, key));
    int value = fallback;
    if (number && CFGetTypeID(number) == CFNumberGetTypeID())
        CFNumberGetValue(number, kCFNumberIntType, &value);
    return value;
}

std::string text(CFStringRef string)
{
    if (!string)
        return {};
    char buffer[256] = {};
    CFStringGetCString(string, buffer, sizeof buffer, kCFStringEncodingUTF8);
    return buffer;
}

} // namespace

std::optional<Info> probe(const std::string& path)
{
    Owned<CGImageSourceRef> source(openSource(path));
    if (!source || CGImageSourceGetStatus(source.ref) != kCGImageStatusComplete)
        return std::nullopt;
    const CFStringRef type = CGImageSourceGetType(source.ref);
    const std::size_t count = CGImageSourceGetCount(source.ref);
    if (!type || count == 0)
        return std::nullopt;
    Owned<CFDictionaryRef> properties(CGImageSourceCopyPropertiesAtIndex(source.ref, 0, nullptr));
    if (!properties)
        return std::nullopt;
    Info info;
    info.width = intProperty(properties.ref, kCGImagePropertyPixelWidth);
    info.height = intProperty(properties.ref, kCGImagePropertyPixelHeight);
    if (info.width <= 0 || info.height <= 0)
        return std::nullopt;
    // EXIF orientations 5 to 8 turn the picture a quarter: width and height trade places.
    if (intProperty(properties.ref, kCGImagePropertyOrientation, 1) >= 5)
        std::swap(info.width, info.height);
    info.alpha = CFDictionaryGetValue(properties.ref, kCGImagePropertyHasAlpha) == kCFBooleanTrue;
    info.count = int(count);
    info.type = text(type);
    // Every camera RAW type the system reads is named "….raw-image" or "….<format>-raw-image".
    info.raw = info.type.ends_with("raw-image");
    return info;
}

bool decodeInto(const std::string& path, int width, int height, Layout layout, std::uint8_t* dest,
                std::size_t bytesPerRow)
{
    if (width <= 0 || height <= 0 || !dest)
        return false;
    Owned<CGImageSourceRef> source(openSource(path));
    if (!source)
        return false;
    // Decoded at (about) the size wanted and turned the right way up; a RAW photo developed
    // by the system's camera RAW support.
    const int maxSide = std::max(width, height);
    Owned<CFNumberRef> size(CFNumberCreate(nullptr, kCFNumberIntType, &maxSide));
    const void* keys[] = {kCGImageSourceCreateThumbnailFromImageAlways, kCGImageSourceCreateThumbnailWithTransform,
                          kCGImageSourceThumbnailMaxPixelSize, kCGImageSourceShouldCacheImmediately};
    const void* values[] = {kCFBooleanTrue, kCFBooleanTrue, size.ref, kCFBooleanTrue};
    Owned<CFDictionaryRef> options(CFDictionaryCreate(nullptr, keys, values, 4, &kCFTypeDictionaryKeyCallBacks,
                                                      &kCFTypeDictionaryValueCallBacks));
    Owned<CGImageRef> image(CGImageSourceCreateThumbnailAtIndex(source.ref, 0, options.ref));
    if (!image)
        return false;

    Owned<CGColorSpaceRef> srgb(CGColorSpaceCreateWithName(kCGColorSpaceSRGB));
    const CGBitmapInfo bitmap = layout == Layout::Bgra
                                    ? CGBitmapInfo(kCGImageAlphaPremultipliedFirst) | kCGBitmapByteOrder32Little
                                    : CGBitmapInfo(kCGImageAlphaPremultipliedLast) | kCGBitmapByteOrder32Big;
    Owned<CGContextRef> context(CGBitmapContextCreate(dest, std::size_t(width), std::size_t(height), 8, bytesPerRow,
                                                      srgb.ref, bitmap));
    if (!context)
        return false;
    for (int y = 0; y < height; ++y)
        std::memset(dest + std::size_t(y) * bytesPerRow, 0, std::size_t(width) * 4);
    CGContextSetInterpolationQuality(context.ref, kCGInterpolationHigh);
    CGContextDrawImage(context.ref, CGRectMake(0, 0, width, height), image.ref);

    // Core Graphics draws premultiplied; the frames and stills here are straight.
    const CGImageAlphaInfo alpha = CGImageGetAlphaInfo(image.ref);
    if (alpha == kCGImageAlphaNone || alpha == kCGImageAlphaNoneSkipFirst || alpha == kCGImageAlphaNoneSkipLast)
        return true;
    const int alphaByte = 3; // BGRA little-endian: B G R A; RGBA big-endian: R G B A
    for (int y = 0; y < height; ++y) {
        std::uint8_t* p = dest + std::size_t(y) * bytesPerRow;
        for (int x = 0; x < width; ++x, p += 4) {
            const int a = p[alphaByte];
            if (a == 0 || a == 255)
                continue;
            for (int c = 0; c < 3; ++c)
                p[c] = std::uint8_t(std::min(255, (p[c] * 255 + a / 2) / a));
        }
    }
    return true;
}

} // namespace tve::imagefile
