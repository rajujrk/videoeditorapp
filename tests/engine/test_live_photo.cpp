#include "decode/VideoDecoder.h"
#include "encode/LivePhoto.h"
#include "encode/MediaWriter.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <filesystem>
#include <random>
#include <vector>

using namespace tve;
namespace fs = std::filesystem;

namespace {

struct Folder {
    fs::path path;
    Folder()
    {
        std::random_device random;
        path = fs::temp_directory_path() / ("tve-live-photo-" + std::to_string(random()));
        fs::create_directories(path);
    }
    ~Folder()
    {
        std::error_code ignored;
        fs::remove_all(path, ignored); // (only this test's own folder)
    }
    std::string file(const std::string& name) const { return (path / name).string(); }
};

// Three seconds of H.264 with sound at 30 fps, 320 × 180: dark, then light from 1.5 s.
bool writeMovie(const std::string& path)
{
    MediaSettings settings;
    settings.container = Container::Mov;
    settings.codec = VideoCodec::H264;
    settings.width = 320;
    settings.height = 180;
    settings.frameRate = {30, 1};
    auto writer = MediaWriter::open(path, settings);
    if (!writer)
        return false;
    std::vector<std::uint8_t> luma(320 * 180), chroma(320 * 90, 128);
    std::vector<float> sound(1600 * 2);
    for (int f = 0; f < 90; ++f) {
        std::fill(luma.begin(), luma.end(), std::uint8_t(f < 45 ? 40 : 200));
        if (!writer->writeVideo(luma.data(), 320, chroma.data(), 320))
            return false;
        for (int i = 0; i < 1600; ++i)
            sound[std::size_t(i) * 2] = sound[std::size_t(i) * 2 + 1] = 0.1f * float(std::sin((f * 1600 + i) * 0.05));
        writer->writeAudio(sound.data(), 1600);
    }
    return writer->finish();
}

std::vector<std::uint8_t> picture(int width, int height)
{
    std::vector<std::uint8_t> rgba(std::size_t(width) * std::size_t(height) * 4, 255);
    for (std::size_t i = 0; i < rgba.size(); i += 4)
        rgba[i] = 200, rgba[i + 1] = 60, rgba[i + 2] = 40;
    return rgba;
}

} // namespace

TEST_CASE("A Live Photo's movie and key photo are paired by their identifier", "[live-photo]")
{
    Folder folder;
    const std::string source = folder.file("source.mov");
    REQUIRE(writeMovie(source));

    const std::string identifier = livephoto::newIdentifier();
    CHECK(identifier.size() == 36);
    CHECK(livephoto::newIdentifier() != identifier);

    const std::string movie = folder.file("clip.mov");
    std::string error;
    REQUIRE(livephoto::writeMovie(source, movie, identifier, kTicksPerSecond * 3 / 2, &error));
    CHECK(error.empty());
    CHECK(livephoto::movieIdentifier(movie) == identifier);
    const auto still = livephoto::movieStillTime(movie);
    REQUIRE(still);
    CHECK(std::llabs(*still - kTicksPerSecond * 3 / 2) < kTicksPerSecond / 100);
    CHECK(livephoto::movieIdentifier(source).empty());
    CHECK_FALSE(livephoto::movieStillTime(source));

    // The pictures and sound copied as they were: all 90 frames, 3 seconds.
    auto decoder = VideoDecoder::open(movie);
    REQUIRE(decoder);
    int frames = 0;
    while (decoder->nextFrame())
        ++frames;
    CHECK(frames == 90);

    const auto rgba = picture(320, 180);
    const std::string heic = folder.file("clip.heic");
    const std::string jpeg = folder.file("clip.jpg");
    REQUIRE(livephoto::writeStill(heic, rgba.data(), 320, 180, 320 * 4, identifier, &error));
    REQUIRE(livephoto::writeStill(jpeg, rgba.data(), 320, 180, 320 * 4, identifier, &error));
    CHECK(livephoto::stillIdentifier(heic) == identifier);
    CHECK(livephoto::stillIdentifier(jpeg) == identifier);

    // Photos itself takes them for one Live Photo; not with another photo's identifier.
    CHECK(livephoto::photosAcceptsPair(heic, movie));
    const std::string stranger = folder.file("other.heic");
    REQUIRE(livephoto::writeStill(stranger, rgba.data(), 320, 180, 320 * 4, livephoto::newIdentifier(), &error));
    CHECK_FALSE(livephoto::photosAcceptsPair(stranger, movie));
}

TEST_CASE("A Live Photo's movie needs pictures", "[live-photo]")
{
    Folder folder;
    std::string error;
    CHECK_FALSE(livephoto::writeMovie(folder.file("missing.mov"), folder.file("out.mov"), "x", 0, &error));
    CHECK_FALSE(error.empty());
    CHECK_FALSE(fs::exists(folder.file("out.mov")));
}
