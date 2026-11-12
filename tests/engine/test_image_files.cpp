#include "TestDng.h"
#include "decode/ImageFile.h"
#include "decode/VideoDecoder.h"
#include "model/ImageSequence.h"

#include <catch2/catch_test_macros.hpp>

#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>

#include <filesystem>
#include <fstream>
#include <functional>
#include <random>
#include <string>
#include <vector>

using namespace tve;
namespace fs = std::filesystem;

namespace {

struct Rgba {
    int r, g, b, a;
};

// A picture file drawn by `at`, written by ImageIO as `type` ("public.png",
// "com.ilm.openexr-image"…).
bool writePicture(const std::string& path, int width, int height, CFStringRef type, const std::function<Rgba(int, int)>& at)
{
    std::vector<std::uint8_t> pixels(std::size_t(width) * std::size_t(height) * 4);
    bool opaque = true; // (then written with no alpha channel)
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            const Rgba c = at(x, y);
            std::uint8_t* p = &pixels[(std::size_t(y) * std::size_t(width) + std::size_t(x)) * 4];
            // premultiplied, as Core Graphics wants it
            p[0] = std::uint8_t(c.r * c.a / 255), p[1] = std::uint8_t(c.g * c.a / 255), p[2] = std::uint8_t(c.b * c.a / 255);
            p[3] = std::uint8_t(c.a);
            opaque = opaque && c.a == 255;
        }
    CGColorSpaceRef srgb = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CGContextRef context = CGBitmapContextCreate(pixels.data(), std::size_t(width), std::size_t(height), 8,
                                                 std::size_t(width) * 4, srgb,
                                                 CGBitmapInfo(opaque ? kCGImageAlphaNoneSkipLast : kCGImageAlphaPremultipliedLast)
                                                     | kCGBitmapByteOrder32Big);
    CGImageRef image = CGBitmapContextCreateImage(context);
    CFURLRef url = CFURLCreateFromFileSystemRepresentation(nullptr, reinterpret_cast<const UInt8*>(path.data()),
                                                           CFIndex(path.size()), false);
    CGImageDestinationRef destination = CGImageDestinationCreateWithURL(url, type, 1, nullptr);
    bool ok = destination != nullptr;
    if (destination) {
        CGImageDestinationAddImage(destination, image, nullptr);
        ok = CGImageDestinationFinalize(destination);
        CFRelease(destination);
    }
    CFRelease(url);
    CGImageRelease(image);
    CGContextRelease(context);
    CGColorSpaceRelease(srgb);
    return ok;
}

struct Folder {
    fs::path path;
    Folder()
    {
        std::random_device random;
        path = fs::temp_directory_path() / ("tve-image-files-" + std::to_string(random()));
        fs::create_directories(path);
    }
    ~Folder()
    {
        std::error_code ignored;
        fs::remove_all(path, ignored); // (only this test's own folder)
    }
    std::string file(const std::string& name) const { return (path / name).string(); }
};

// One pixel of a decoded NV12 frame's luma, or a BGRA frame's channels.
int lumaAt(const DecodedFrame& frame, int x, int y)
{
    CVPixelBufferRef buffer = frame.pixelBuffer();
    CVPixelBufferLockBaseAddress(buffer, kCVPixelBufferLock_ReadOnly);
    const auto* luma = static_cast<const std::uint8_t*>(CVPixelBufferGetBaseAddressOfPlane(buffer, 0));
    const int value = luma[std::size_t(y) * CVPixelBufferGetBytesPerRowOfPlane(buffer, 0) + std::size_t(x)];
    CVPixelBufferUnlockBaseAddress(buffer, kCVPixelBufferLock_ReadOnly);
    return value;
}

Rgba bgraAt(const DecodedFrame& frame, int x, int y)
{
    CVPixelBufferRef buffer = frame.pixelBuffer();
    CVPixelBufferLockBaseAddress(buffer, kCVPixelBufferLock_ReadOnly);
    const auto* p = static_cast<const std::uint8_t*>(CVPixelBufferGetBaseAddress(buffer))
                    + std::size_t(y) * CVPixelBufferGetBytesPerRow(buffer) + std::size_t(x) * 4;
    const Rgba c{p[2], p[1], p[0], p[3]};
    CVPixelBufferUnlockBaseAddress(buffer, kCVPixelBufferLock_ReadOnly);
    return c;
}

} // namespace

TEST_CASE("An image sequence plays as video, frame by frame", "[image-files]")
{
    Folder folder;
    // shot_0001 … shot_0010 at 64 × 36: dark until 5, light from 6; 0007 missing.
    for (int n = 1; n <= 10; ++n) {
        if (n == 7)
            continue;
        char name[32];
        std::snprintf(name, sizeof name, "shot_%04d.png", n);
        const int grey = n <= 5 ? 40 : 220;
        REQUIRE(writePicture(folder.file(name), 64, 36, CFSTR("public.png"), [&](int, int) { return Rgba{grey, grey, grey, 255}; }));
    }
    const auto sequence = findImageSequence(folder.file("shot_0004.png"));
    REQUIRE(sequence);
    CHECK(sequence->pattern == "shot_%04d.png");
    CHECK(sequence->first == 1);
    CHECK(sequence->last == 10);
    CHECK(sequence->length() == 10);

    const std::string first = folder.file("shot_0001.png");
    // Not registered: the first frame is only a picture (one frame long).
    if (auto single = VideoDecoder::open(first)) {
        CHECK(single->info().codecName != "image sequence");
    }

    registerImageSequence(first, *sequence, FrameRate{24, 1});
    std::string error;
    auto decoder = VideoDecoder::open(first, &error);
    REQUIRE(decoder);
    const VideoStreamInfo& info = decoder->info();
    CHECK(info.codecName == "image sequence");
    CHECK(info.width == 64);
    CHECK(info.height == 36);
    CHECK(info.frameRate == FrameRate{24, 1});
    CHECK(info.duration == 10 * frameDuration(FrameRate{24, 1}));
    CHECK_FALSE(info.hasAlpha);

    // Every frame in order, NV12, a frame (1/24 s) apart.
    std::vector<int> lumas;
    Ticks expected = 0;
    while (auto frame = decoder->nextFrame()) {
        CHECK(frame->pts() == expected);
        CHECK(CVPixelBufferGetPixelFormatType(frame->pixelBuffer()) == kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange);
        lumas.push_back(lumaAt(*frame, 32, 18));
        expected += frameDuration(FrameRate{24, 1});
    }
    REQUIRE(lumas.size() == 10);
    for (int i = 0; i < 5; ++i)
        CHECK(lumas[std::size_t(i)] < 80);
    CHECK(lumas[5] > 180);
    CHECK(lumas[6] > 180); // 0007 is missing: 0006 shows
    CHECK(lumas[9] > 180);

    // Seeking lands on the frame on screen at the time.
    REQUIRE(decoder->seek(frameDuration(FrameRate{24, 1}) * 3 + 10));
    auto fourth = decoder->nextFrame();
    REQUIRE(fourth);
    CHECK(fourth->pts() == 3 * frameDuration(FrameRate{24, 1}));
    CHECK(lumaAt(*fourth, 32, 18) < 80);
    REQUIRE(decoder->seek(frameDuration(FrameRate{24, 1}) * 8));
    auto ninth = decoder->nextFrame();
    REQUIRE(ninth);
    CHECK(lumaAt(*ninth, 32, 18) > 180);
    CHECK(decoder->keyframeBefore(frameDuration(FrameRate{24, 1}) * 8 + 5) == frameDuration(FrameRate{24, 1}) * 8);
    REQUIRE(decoder->seek(info.duration + 1));
    CHECK_FALSE(decoder->nextFrame());
}

TEST_CASE("An image sequence with transparency decodes to straight BGRA", "[image-files]")
{
    Folder folder;
    // glow1 … glow3 (not padded), a half-clear green; and glow10 to show unpadded numbers
    // run on past nine.
    for (int n : {1, 2, 3, 10})
        REQUIRE(writePicture(folder.file("glow" + std::to_string(n) + ".png"), 40, 20, CFSTR("public.png"),
                             [&](int x, int) { return x < 20 ? Rgba{0, 200, 0, 128} : Rgba{0, 0, 0, 0}; }));
    REQUIRE(writePicture(folder.file("glow05.png"), 40, 20, CFSTR("public.png"), [](int, int) { return Rgba{255, 0, 0, 255}; }));
    const auto sequence = findImageSequence(folder.file("glow2.png"));
    REQUIRE(sequence);
    CHECK(sequence->pattern == "glow%d.png");
    CHECK(sequence->first == 1);
    CHECK(sequence->last == 10); // (glow05, padded, is not one of them)
    CHECK(sequenceDisplayName(*sequence) == "glow#.png");

    registerImageSequence(folder.file("glow1.png"), *sequence, FrameRate{30, 1});
    auto decoder = VideoDecoder::open(folder.file("glow1.png"));
    REQUIRE(decoder);
    CHECK(decoder->info().hasAlpha);
    CHECK(decoder->info().duration == 10 * frameDuration(FrameRate{30, 1}));
    auto frame = decoder->nextFrame();
    REQUIRE(frame);
    CHECK(CVPixelBufferGetPixelFormatType(frame->pixelBuffer()) == kCVPixelFormatType_32BGRA);
    const Rgba half = bgraAt(*frame, 5, 10);
    CHECK(half.a >= 126);
    CHECK(half.a <= 130);
    CHECK(half.g >= 190); // straight, not 100 premultiplied
    CHECK(half.r < 10);
    CHECK(bgraAt(*frame, 30, 10).a == 0);
}

TEST_CASE("Finding a sequence from one of its pictures", "[image-files]")
{
    Folder folder;
    const auto touch = [&](const std::string& name) { std::ofstream(folder.file(name)) << "x"; };
    for (const char* name : {"take 1 v2_0998.jpg", "take 1 v2_0999.jpg", "take 1 v2_1000.jpg", "take 1 v2_1001.jpg",
                             "take 1 v2_1001.png", "other_0998.jpg", "100% done 7.tif", "100% done 8.tif", "alone.png",
                             "lonely_0001.png"})
        touch(name);
    // The last number in the name counts ("v2" is part of the name); a run padded to four
    // digits found from 1000, which has no zero to show it.
    const auto takes = findImageSequence(folder.file("take 1 v2_1000.jpg"));
    REQUIRE(takes);
    CHECK(takes->pattern == "take 1 v2_%04d.jpg");
    CHECK(takes->first == 998);
    CHECK(takes->last == 1001);
    CHECK(sequenceFrameFile(folder.path.string(), *takes, 999) == folder.file("take 1 v2_0999.jpg"));
    CHECK(sequenceDisplayName(*takes) == "take 1 v2_####.jpg");
    // A % in the name is kept as itself.
    const auto percent = findImageSequence(folder.file("100% done 8.tif"));
    REQUIRE(percent);
    CHECK(percent->pattern == "100%% done %d.tif");
    CHECK(sequenceFrameFile(folder.path.string(), *percent, 7) == folder.file("100% done 7.tif"));
    CHECK(sequenceDisplayName(*percent) == "100% done #.tif");
    // No number, or no other frame: no sequence.
    CHECK_FALSE(findImageSequence(folder.file("alone.png")));
    CHECK_FALSE(findImageSequence(folder.file("lonely_0001.png")));
    // Gaps show the frame before; the list is as long as the numbers.
    ImageSequence gappy{"take 1 v2_%04d.jpg", 997, 1003};
    const auto files = sequenceFrameFiles(folder.file("take 1 v2_0997.jpg"), gappy);
    REQUIRE(files.size() == 7);
    CHECK(files[0] == folder.file("take 1 v2_0998.jpg")); // (997 missing: the first one there)
    CHECK(files[1] == folder.file("take 1 v2_0998.jpg"));
    CHECK(files[4] == folder.file("take 1 v2_1001.jpg"));
    CHECK(files[6] == folder.file("take 1 v2_1001.jpg"));
}

TEST_CASE("ImageIO reads camera RAW and OpenEXR pictures", "[image-files]")
{
    Folder folder;
    const std::string dng = folder.file("photo.dng");
    REQUIRE(testing::writeTestDng(dng));
    const auto raw = imagefile::probe(dng);
    REQUIRE(raw);
    CHECK(raw->raw);
    CHECK(raw->type == "com.adobe.raw-image");
    CHECK(raw->width == 256);
    CHECK(raw->height == 192);
    CHECK(raw->count == 1);
    // Developed from the mosaic (not the preview): red-ish on the left, blue-ish on the right.
    std::vector<std::uint8_t> rgba(256 * 192 * 4);
    REQUIRE(imagefile::decodeInto(dng, 256, 192, imagefile::Layout::Rgba, rgba.data(), 256 * 4));
    const auto at = [&](int x, int y) { return &rgba[std::size_t((y * 256 + x) * 4)]; };
    CHECK(at(40, 96)[0] > at(40, 96)[2] + 60);
    CHECK(at(216, 96)[2] > at(216, 96)[0] + 60);
    CHECK(at(40, 96)[3] == 255);

    // An EXR (Qt reads none): half-clear red on the left.
    const std::string exr = folder.file("halves.exr");
    REQUIRE(writePicture(exr, 80, 40, CFSTR("com.ilm.openexr-image"),
                         [](int x, int) { return x < 40 ? Rgba{230, 20, 20, 255} : Rgba{20, 40, 230, 255}; }));
    const auto info = imagefile::probe(exr);
    REQUIRE(info);
    CHECK_FALSE(info->raw);
    CHECK(info->width == 80);
    CHECK(info->height == 40);
    // Scaled on the way: drawn at 40 × 20.
    std::vector<std::uint8_t> small(40 * 20 * 4);
    REQUIRE(imagefile::decodeInto(exr, 40, 20, imagefile::Layout::Bgra, small.data(), 40 * 4));
    const std::uint8_t* left = &small[std::size_t((10 * 40 + 5) * 4)];
    const std::uint8_t* right = &small[std::size_t((10 * 40 + 35) * 4)];
    CHECK(left[2] > 180);  // (BGRA: red third)
    CHECK(left[0] < 80);
    CHECK(right[0] > 180);
    CHECK(right[2] < 80);

    // Not a picture: nothing.
    std::ofstream(folder.file("notes.txt")) << "not a picture";
    CHECK_FALSE(imagefile::probe(folder.file("notes.txt")));
    CHECK_FALSE(imagefile::probe(folder.file("missing.png")));
}
