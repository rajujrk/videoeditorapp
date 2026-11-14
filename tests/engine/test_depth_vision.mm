#include "analysis/DepthVision.h"
#include "analysis/PeopleVision.h"
#include "decode/VideoDecoder.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>

using namespace tve;
namespace fs = std::filesystem;

namespace {

// samples/models/depth-anything-v2-small-f16 (tools/fetch_models.sh).
std::string depthModel()
{
    const fs::path path = fs::path(TVE_SAMPLES_DIR) / "models" / "depth-anything-v2-small-f16" / "DepthAnythingV2SmallF16.mlpackage";
    if (!fs::exists(path))
        SKIP("the depth model is missing (run tools/fetch_models.sh)");
    return path.string();
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

} // namespace

TEST_CASE("The depth model finds what is near and far", "[apple]")
{
    const std::string model = depthModel();
    std::string error;
    CHECK_FALSE(setDepthModel("/nonexistent/Depth.mlpackage", &error));
    CHECK(!error.empty());
    CHECK_FALSE(hasDepthModel());
    const DecodedFrame frame = frameAt((fs::path(TVE_SAMPLES_DIR) / "shaky.mp4").string(), kTicksPerSecond);
    CHECK_FALSE(estimateDepth(frame.pixelBuffer())); // no model: nothing

    REQUIRE(setDepthModel(model, &error));
    CHECK(hasDepthModel());
    CHECK(fs::is_directory(fs::path(model).replace_extension(".mlmodelc"))); // compiled once, beside it
    const DepthMap map = estimateDepth(frame.pixelBuffer());
    REQUIRE(map);
    CHECK(map.width == 518);
    CHECK(map.height == 392);
    // shaky.mp4 at 1 s: the astronaut's helmet (bottom left) nearest, then the astronaut, the
    // flag and the backdrop behind (top right) farthest.
    const int helmet = map.at(0.15, 0.9), body = map.at(0.6, 0.6), back = map.at(0.75, 0.05);
    INFO("helmet " << helmet << " body " << body << " back " << back);
    CHECK(helmet > body + 40);
    CHECK(body > back + 40);
    CHECK(map.percentile(0.0) == 0);
    CHECK(map.percentile(1.0) == 255);

    // The focus: the nearest subject, or what is at the point.
    CHECK(focusNearness(map, DepthOfField{.autoFocus = true}) == map.percentile(0.9));
    // With faces, the largest one's.
    const std::vector<FaceBox> faces{{0.6, 0.6, 0.05, 0.05}, {0.75, 0.05, 0.2, 0.2}};
    CHECK(focusNearness(map, DepthOfField{.autoFocus = true}, faces) == back);
    CHECK(focusNearness(map, DepthOfField{.autoFocus = false, .focusX = 0.6, .focusY = 0.6}) == body);

    // Unloaded.
    REQUIRE(setDepthModel(""));
    CHECK_FALSE(hasDepthModel());
    CHECK_FALSE(estimateDepth(frame.pixelBuffer()));
}
