#include "analysis/PeopleVision.h"
#include "decode/VideoDecoder.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <numbers>

#include <unistd.h>

using namespace tve;
using Catch::Approx;
namespace fs = std::filesystem;

namespace {

// samples/people.mp4 (tools/make_test_media.sh): 3 s at 30 fps of a portrait photo sliding
// left to right across grey; the face's centre goes from x ≈ 0.30 to ≈ 0.75, at y ≈ 0.24.
std::string people()
{
    const std::string path = (fs::path(TVE_SAMPLES_DIR) / "people.mp4").string();
    if (!fs::exists(path))
        SKIP("people.mp4 missing (run tools/make_test_media.sh)");
    return path;
}

DecodedFrame frameAt(const std::string& path, Ticks time)
{
    auto decoder = VideoDecoder::open(path);
    REQUIRE(decoder);
    REQUIRE(decoder->seek(time));
    auto frame = decoder->nextFrame();
    REQUIRE(frame);
    return std::move(*frame);
}

double faceXAt(double seconds)
{
    return (100.0 + (1920.0 - 864.0 - 200.0) * seconds / 3.0 + 0.56 * 864.0) / 1920.0;
}

} // namespace

TEST_CASE("Vision finds the person and the face in a frame", "[apple]")
{
    const std::string path = people();
    const DecodedFrame frame = frameAt(path, kTicksPerSecond * 3 / 2);

    const PersonMatte matte = segmentPeople(frame.pixelBuffer());
    REQUIRE(matte);
    const auto at = [&](double u, double v) {
        return matte.pixels[std::size_t(int(v * (matte.height - 1)) * matte.width + int(u * (matte.width - 1)))];
    };
    CHECK(at(faceXAt(1.5), 0.3) > 200);   // the face
    CHECK(at(faceXAt(1.5), 0.6) > 200);   // the body
    CHECK(at(0.02, 0.5) < 30);            // the grey either side
    CHECK(at(0.98, 0.5) < 30);
    CHECK(segmentPeople(frame.pixelBuffer(), SegmentationQuality::Fast));

    const auto faces = detectFaces(frame.pixelBuffer());
    REQUIRE(faces.size() == 1);
    CHECK(faces[0].x == Approx(faceXAt(1.5)).margin(0.04));
    CHECK(faces[0].y == Approx(0.25).margin(0.07));
    CHECK(faces[0].width > 0.05);
    CHECK(faces[0].width < 0.2);

    const auto subject = findSubjectIn(frame.pixelBuffer());
    REQUIRE(subject);
    CHECK(subject->x == Approx(faces[0].x).margin(1e-9)); // a face: the face

    // Smart cutout: a box drawn around the astronaut keeps what stands out under it, not the grey.
    const PersonMatte lifted = segmentSubject(frame.pixelBuffer(), {faceXAt(1.5), 0.55, 0.42, 0.9});
    REQUIRE(lifted);
    const auto in = [&](double u, double v) {
        return lifted.pixels[std::size_t(int(v * (lifted.height - 1)) * lifted.width + int(u * (lifted.width - 1)))];
    };
    CHECK(in(faceXAt(1.5), 0.3) == 255);
    CHECK(in(0.02, 0.5) == 0);
    CHECK(in(0.98, 0.5) == 0);
    CHECK_FALSE(segmentSubject(nullptr, {0.5, 0.5, 0.2, 0.2}));

    CHECK_FALSE(segmentPeople(nullptr));
    CHECK(detectFaces(nullptr).empty());
}

TEST_CASE("a file's faces become one track that follows the face, kept on disk", "[apple]")
{
    const std::string path = people();
    std::vector<double> reported;
    const auto frames = findFaces(path, [&](double p) { reported.push_back(p); return true; });
    REQUIRE(frames);
    CHECK(frames->size() == 90);
    CHECK(std::ranges::count_if(*frames, [](const FaceFrame& f) { return f.faces.size() == 1; }) >= 85);
    CHECK(std::ranges::is_sorted(reported));

    const auto tracks = linkFaces(*frames);
    REQUIRE(tracks.size() == 1);
    CHECK(tracks[0].start() < kTicksPerSecond / 10);
    CHECK(tracks[0].end() > kTicksPerSecond * 28 / 10);
    CHECK(tracks[0].boxAt(0)->x == Approx(faceXAt(0.0)).margin(0.04));
    CHECK(tracks[0].boxAt(kTicksPerSecond * 2)->x == Approx(faceXAt(2.0)).margin(0.04));
    CHECK(tracks[0].samples.size() < 40); // a steady slide needs few samples

    const AnalysisCache cache((fs::temp_directory_path() / ("tve_people_" + std::to_string(::getpid()))).string());
    CHECK_FALSE(loadFaces(cache, "v1:1:abc"));
    REQUIRE(saveFaces(cache, "v1:1:abc", *frames));
    const auto loaded = loadFaces(cache, "v1:1:abc");
    REQUIRE(loaded);
    REQUIRE(loaded->size() == frames->size());
    CHECK((*loaded)[45].time == (*frames)[45].time);
    CHECK((*loaded)[45].faces == (*frames)[45].faces);

    // Cancelling stops it.
    CHECK_FALSE(findFaces(path, [](double) { return false; }));
    // A file without pictures says so.
    std::string error;
    CHECK_FALSE(findFaces((fs::path(TVE_SAMPLES_DIR) / "test_tone_440.wav").string(), {}, &error));
    CHECK_FALSE(error.empty());
    fs::remove_all(cache.directory());
}

TEST_CASE("the subject is followed into a portrait frame", "[apple]")
{
    const std::string path = people();
    const auto subject = findSubject(path);
    REQUIRE(subject);
    CHECK(subject->size() >= 28);
    CHECK(subject->size() <= 31); // 10 a second
    CHECK(subject->front().box.x == Approx(faceXAt(0.0)).margin(0.04));

    const AnalysisCache cache((fs::temp_directory_path() / ("tve_subject_" + std::to_string(::getpid()))).string());
    REQUIRE(saveSubject(cache, "v1:2:def", *subject));
    const auto loaded = loadSubject(cache, "v1:2:def");
    REQUIRE(loaded);
    CHECK(loaded->size() == subject->size());
    fs::remove_all(cache.directory());

    // Into 9:16 the picture moves right while the face is left of centre, then left.
    const ReframePath path916 = reframe(*subject, 1920, 1080, 1080, 1920, {.smoothing = 0.2});
    REQUIRE(path916.positions.size() >= 2);
    CHECK(path916.positions.front().second[0] > 0.3);
    CHECK(path916.positions.back().second[0] < -0.3);
}

TEST_CASE("a face's landmarks become a beauty mask: skin with the features cut out, and the teeth", "[apple]")
{
    // Drawn: a face whose jaw runs round the bottom of a box, an "eye", and a mouth.
    FaceParts face;
    face.box = {0.5, 0.5, 0.4, 0.5};
    for (int i = 0; i <= 12; ++i) {
        const double a = std::numbers::pi * double(i) / 12.0; // ear to ear, round the chin
        face.contour.push_back({float(0.5 - 0.2 * std::cos(a)), float(0.5 + 0.25 * std::sin(a))});
    }
    face.features.push_back({{0.38f, 0.40f}, {0.46f, 0.40f}, {0.46f, 0.44f}, {0.38f, 0.44f}});
    face.innerLips = {{0.45f, 0.62f}, {0.55f, 0.62f}, {0.55f, 0.66f}, {0.45f, 0.66f}};
    const BeautyMask mask = beautyMask({face}, 200, 200);
    REQUIRE(mask);
    REQUIRE(mask.pixels.size() == 200u * 200u * 2u);
    const auto skin = [&](double u, double v) { return mask.skinAt(int(u * 199), int(v * 199)); };
    const auto teeth = [&](double u, double v) { return mask.teethAt(int(u * 199), int(v * 199)); };
    CHECK(skin(0.6, 0.55) > 200);  // a cheek
    CHECK(skin(0.5, 0.33) > 200);  // the forehead, above the jaw line
    CHECK(skin(0.42, 0.42) < 60);  // the eye
    CHECK(skin(0.05, 0.5) == 0);   // beside the face
    CHECK(skin(0.5, 0.95) == 0);   // below the chin
    CHECK(teeth(0.5, 0.64) > 150); // the mouth
    CHECK(teeth(0.6, 0.55) == 0);
    CHECK_FALSE(beautyMask({}, 0, 10));
    const BeautyMask none = beautyMask({}, 32, 18);
    CHECK(std::ranges::all_of(none.pixels, [](std::uint8_t b) { return b == 0; }));
}

TEST_CASE("Vision finds a face's landmarks for the beauty filter", "[apple]")
{
    const DecodedFrame frame = frameAt(people(), kTicksPerSecond * 3 / 2);
    const auto faces = detectFaceParts(frame.pixelBuffer());
    REQUIRE(faces.size() == 1);
    const FaceParts& face = faces[0];
    CHECK(face.box.x == Approx(faceXAt(1.5)).margin(0.04));
    CHECK(face.contour.size() >= 5);
    CHECK(face.features.size() == 5); // two eyes, two brows, the lips
    CHECK(face.innerLips.size() >= 4);
    for (const FacePoint& p : face.contour) { // the jaw is round the face, below the eyes
        CHECK(std::abs(p.x - face.box.x) < face.box.width);
        CHECK(p.y > face.box.y - face.box.height * 0.3);
    }
    // The mask's skin covers a cheek, not the eyes.
    const BeautyMask mask = beautyMask(faces, 480, 270);
    const auto& eye = face.features[0];
    double ex = 0, ey = 0;
    for (const FacePoint& p : eye)
        ex += p.x, ey += p.y;
    ex /= double(eye.size());
    ey /= double(eye.size());
    CHECK(mask.skinAt(int(ex * 479), int(ey * 269)) < 80);
    CHECK(mask.skinAt(int((face.box.x + face.box.width * 0.22) * 479), int((face.box.y + face.box.height * 0.12) * 269)) > 120);
    CHECK(detectFaceParts(nullptr).empty());
}
