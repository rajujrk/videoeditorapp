// The built-in Lottie stickers, drawn by ThorVG.
#include "decode/LottieAnimation.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <set>

using namespace tve;

namespace {

const std::filesystem::path kStickers = std::filesystem::path(TVE_EFFECTS_DIR) / "stickers";

// Share of pixels with any opacity, and a checksum of the image.
std::pair<double, std::uint64_t> coverage(const std::vector<std::uint8_t>& rgba)
{
    std::size_t opaque = 0;
    std::uint64_t sum = 0;
    for (std::size_t i = 0; i < rgba.size(); i += 4) {
        opaque += rgba[i + 3] > 0;
        sum = sum * 31 + rgba[i] + rgba[i + 1] * 7 + rgba[i + 2] * 13 + rgba[i + 3] * 17;
    }
    return {double(opaque) / double(rgba.size() / 4), sum};
}

} // namespace

TEST_CASE("every built-in sticker plays: a two-second loop that draws and moves", "[stickers]")
{
    std::ifstream file(kStickers / "stickers.json");
    REQUIRE(file);
    const nlohmann::json catalog = nlohmann::json::parse(file)["stickers"];
    CHECK(catalog.size() == 100);
    std::set<std::string> categories;
    for (const auto& entry : catalog) {
        const std::string id = entry["id"];
        INFO(id);
        categories.insert(entry["category"].get<std::string>());
        auto animation = LottieAnimation::open((kStickers / entry["file"].get<std::string>()).string());
        REQUIRE(animation);
        CHECK(animation->width() == 512);
        CHECK(animation->height() == 512);
        CHECK(animation->duration() == Catch::Approx(2.0).margin(0.05));
        // Not moments a quarter-cycle either side of a swing, where a wobble looks the same.
        const auto a = coverage(animation->render(0.2, 128, 128));
        const auto b = coverage(animation->render(0.6, 128, 128));
        CHECK(a.first > 0.01); // something is drawn
        CHECK(a.first < 0.95); // with transparency around it
        CHECK(a.second != b.second); // it moves
    }
    CHECK(categories == std::set<std::string>{"shapes", "marks", "nature", "emoji", "party"});
}

TEST_CASE("a Lottie animation loops and renders at any size", "[stickers]")
{
    auto animation = LottieAnimation::open((kStickers / "star_spin.json").string());
    REQUIRE(animation);
    // A full turn later it looks the same.
    CHECK(animation->render(0.5, 64, 64) == animation->render(2.5, 64, 64));
    // The star is yellow, opaque in the middle, clear in the corners; straight alpha.
    const auto big = animation->render(0.0, 256, 256);
    const std::size_t middle = (128 * 256 + 128) * 4;
    CHECK(big[middle + 3] == 255);
    CHECK(big[middle] > 200);
    CHECK(big[middle + 1] > 150);
    CHECK(big[middle + 2] < 120);
    CHECK(big[3] == 0);
    CHECK_FALSE(LottieAnimation::fromData("{\"not\": \"lottie\"}"));
    CHECK_FALSE(LottieAnimation::fromData("not json at all"));
    CHECK(isLottieFile("/a/b/sticker.JSON"));
    CHECK_FALSE(isLottieFile("/a/b/clip.mp4"));
}
