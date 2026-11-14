#include "analysis/ContentVision.h"

#include <catch2/catch_test_macros.hpp>

#import <Foundation/Foundation.h>
#include <CoreGraphics/CoreGraphics.h>
#include <CoreText/CoreText.h>
#include <ImageIO/ImageIO.h>

#include <algorithm>
#include <filesystem>

#include <unistd.h>

using namespace tve;
namespace fs = std::filesystem;

namespace {

// A sign: dark words on a light board, drawn with Core Text into a BGRA buffer.
CVPixelBufferRef sign(int width, int height)
{
    CVPixelBufferRef buffer = nullptr;
    NSDictionary* attributes = @{(id)kCVPixelBufferIOSurfacePropertiesKey: @{}};
    REQUIRE(CVPixelBufferCreate(kCFAllocatorDefault, width, height, kCVPixelFormatType_32BGRA,
                                (__bridge CFDictionaryRef)attributes, &buffer) == kCVReturnSuccess);
    CVPixelBufferLockBaseAddress(buffer, 0);
    CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
    CGContextRef context = CGBitmapContextCreate(CVPixelBufferGetBaseAddress(buffer), width, height, 8,
                                                 CVPixelBufferGetBytesPerRow(buffer), space,
                                                 kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little);
    CGContextSetRGBFillColor(context, 0.93, 0.91, 0.86, 1);
    CGContextFillRect(context, CGRectMake(0, 0, width, height));
    const auto line = [&](NSString* text, double size, double y) {
        CTFontRef font = CTFontCreateWithName(CFSTR("Helvetica-Bold"), size, nullptr);
        NSDictionary* style = @{(id)kCTFontAttributeName: (__bridge id)font, (id)kCTForegroundColorFromContextAttributeName: @YES};
        NSAttributedString* string = [[[NSAttributedString alloc] initWithString:text attributes:style] autorelease];
        CTLineRef ctLine = CTLineCreateWithAttributedString((__bridge CFAttributedStringRef)string);
        CGContextSetRGBFillColor(context, 0.08, 0.1, 0.14, 1);
        CGContextSetTextPosition(context, width * 0.12, y); // (from the bottom)
        CTLineDraw(ctLine, context);
        CFRelease(ctLine);
        CFRelease(font);
    };
    line(@"HARBOUR CAFE", height * 0.16, height * 0.62);
    line(@"Open every day", height * 0.08, height * 0.32);
    CGContextRelease(context);
    CGColorSpaceRelease(space);
    CVPixelBufferUnlockBaseAddress(buffer, 0);
    return buffer;
}

std::string portrait()
{
    const std::string path = (fs::path(TVE_SAMPLES_DIR) / "portrait.jpg").string();
    if (!fs::exists(path))
        SKIP("portrait.jpg missing (run tools/make_test_media.sh)");
    return path;
}

std::string allText(const ContentFrame& frame)
{
    std::string text;
    for (const ScreenText& t : frame.text)
        text += t.text + "\n";
    return text;
}

} // namespace

TEST_CASE("Vision reads the text on screen, top to bottom", "[apple]")
{
    CVPixelBufferRef buffer = sign(1280, 720);
    const ContentFrame frame = describeFrame(buffer, 5 * kTicksPerSecond);
    CVPixelBufferRelease(buffer);
    CHECK(frame.time == 5 * kTicksPerSecond);
    INFO(allText(frame));
    REQUIRE(frame.text.size() >= 2);
    CHECK(frame.text[0].text == "HARBOUR CAFE");
    CHECK(frame.text[1].text == "Open every day");
    // Where it is: the big line above the small one, in the left half and more.
    CHECK(frame.text[0].y < frame.text[1].y);
    CHECK(frame.text[0].y > 0.2);
    CHECK(frame.text[0].y < 0.5);
    CHECK(frame.text[0].x > 0.3);
    CHECK(frame.text[0].x < 0.6);
    CHECK(frame.text[0].confidence >= 0.4f);

    // Found by a search, as said on screen.
    ContentIndex index;
    index.frames.push_back(frame);
    const auto hits = searchMedia("cafe", {{1, 10 * kTicksPerSecond, false, &index, nullptr}});
    REQUIRE(hits.size() == 1);
    CHECK(hits[0].found == kFoundShown);
    CHECK(hits[0].start == 5 * kTicksPerSecond);
}

TEST_CASE("Vision labels what is in a picture", "[apple]")
{
    const auto frame = describeImage(portrait());
    REQUIRE(frame);
    std::string names;
    for (const ContentLabel& l : frame->labels)
        names += l.name + " ";
    INFO(names);
    REQUIRE(!frame->labels.empty());
    CHECK(frame->labels.size() <= 12);
    for (std::size_t i = 0; i < frame->labels.size(); ++i) {
        CHECK(frame->labels[i].confidence >= kLabelThreshold);
        if (i > 0)
            CHECK(frame->labels[i].confidence <= frame->labels[i - 1].confidence);
    }
    // A person.
    CHECK(std::ranges::any_of(frame->labels, [](const ContentLabel& l) {
        return l.name == "people" || l.name == "adult" || l.name == "portrait" || l.name == "person";
    }));

    std::string error;
    CHECK_FALSE(describeImage("/nonexistent/picture.png", &error));
    CHECK(!error.empty());
}

TEST_CASE("A video's content is looked at once a second and kept", "[apple]")
{
    const std::string path = (fs::path(TVE_SAMPLES_DIR) / "people.mp4").string();
    if (!fs::exists(path))
        SKIP("people.mp4 missing (run tools/make_test_media.sh)");
    const auto index = analyzeContent(path, false);
    REQUIRE(index);
    REQUIRE(index->frames.size() == 3); // 3 s
    for (std::size_t i = 0; i < 3; ++i) {
        CHECK(std::llabs(index->frames[i].time - Ticks(i) * kTicksPerSecond) < kTicksPerSecond / 10);
        CHECK(!index->frames[i].labels.empty());
    }

    // Cancelled: nothing.
    CHECK_FALSE(analyzeContent(path, false, [](double) { return false; }));

    // Kept and read back.
    const fs::path folder = fs::temp_directory_path() / ("tve-content-" + std::to_string(getpid()));
    const AnalysisCache cache(folder.string());
    REQUIRE(saveContent(cache, "sha:abc", *index));
    const auto back = loadContent(cache, "sha:abc");
    REQUIRE(back);
    CHECK(back->frames.size() == index->frames.size());
    CHECK(back->frames[1].labels.front().name == index->frames[1].labels.front().name);
    CHECK_FALSE(loadContent(cache, "sha:other"));
    fs::remove_all(folder);

    // A still: one frame.
    const auto still = analyzeContent(portrait(), true);
    REQUIRE(still);
    CHECK(still->frames.size() == 1);
}
