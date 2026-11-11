#include "io/ProjectSerializer.h"
#include "model/ClipAnimation.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

using namespace tve;

namespace {
constexpr Ticks kSec = kTicksPerSecond;

Clip picture()
{
    Clip clip;
    clip.id = 1;
    clip.duration = 4 * kSec;
    clip.visual.transform.scale = 0.8;
    clip.visual.transform.x = 0.1;
    return clip;
}
double at(const Clip& clip, Ticks local, double ClipTransform::*field)
{
    return clip.visualAt(clip.start + local).transform.*field;
}
} // namespace

TEST_CASE("an in animation arrives at the clip as it is", "[animation]")
{
    Clip clip = picture();
    REQUIRE_FALSE(applyClipAnimation(clip, "slide-in-left", kSec / 2));
    CHECK(at(clip, 0, &ClipTransform::x) == Catch::Approx(0.1 - 1.1));
    CHECK(at(clip, kSec / 2, &ClipTransform::x) == Catch::Approx(0.1));
    CHECK(at(clip, 3 * kSec, &ClipTransform::x) == Catch::Approx(0.1));
    CHECK(at(clip, kSec / 4, &ClipTransform::x) > -1.0);

    // A pop overshoots into place, from half size and transparent.
    Clip pop = picture();
    REQUIRE_FALSE(applyClipAnimation(pop, "pop-in", kSec));
    CHECK(at(pop, 0, &ClipTransform::scale) == Catch::Approx(0.4));
    CHECK(pop.visualAt(0).opacity == Catch::Approx(0.0));
    bool over = false;
    for (Ticks t = 0; t < kSec; t += kSec / 30)
        over = over || at(pop, t, &ClipTransform::scale) > 0.8 + 1e-6;
    CHECK(over);
}

TEST_CASE("an out animation leaves from the clip as it is; combos and movements", "[animation]")
{
    Clip clip = picture();
    REQUIRE_FALSE(applyClipAnimation(clip, "fade-in", kSec));
    REQUIRE_FALSE(applyClipAnimation(clip, "spin-out", kSec));
    CHECK(clip.visualAt(0).opacity == Catch::Approx(0.0));
    CHECK(clip.visualAt(2 * kSec).opacity == Catch::Approx(1.0));
    CHECK(at(clip, 3 * kSec, &ClipTransform::rotation) == Catch::Approx(0.0));
    CHECK(at(clip, 4 * kSec, &ClipTransform::rotation) == Catch::Approx(180.0));
    CHECK(clip.visualAt(4 * kSec).opacity == Catch::Approx(0.0));
    // The in is kept (the out wrote only the last second).
    CHECK(clip.keyframes.at("opacity").front().time == 0);

    Clip combo = picture();
    REQUIRE_FALSE(applyClipAnimation(combo, "slide-through", 10 * kSec)); // each half the clip at most
    CHECK(at(combo, 2 * kSec, &ClipTransform::x) == Catch::Approx(0.1));
    CHECK(at(combo, 4 * kSec, &ClipTransform::x) == Catch::Approx(0.1 + 1.1));

    Clip pulse = picture();
    REQUIRE_FALSE(applyClipAnimation(pulse, "pulse", kSec));
    CHECK(at(pulse, kSec / 2, &ClipTransform::scale) == Catch::Approx(0.8 * 1.08));
    CHECK(at(pulse, kSec, &ClipTransform::scale) == Catch::Approx(0.8));

    CHECK(applyClipAnimation(pulse, "moonwalk", kSec).value() == "there is no such animation");
    clearClipAnimation(clip);
    CHECK(clip.keyframes.empty());
}

TEST_CASE("pan and zoom frames the picture from one view to another", "[animation]")
{
    Clip clip = picture();
    const auto& presets = panZoomPresets();
    const auto left = std::ranges::find(presets, std::string("pan-left"), [](const PanZoomPreset& p) { return std::string(p.id); });
    REQUIRE(left != presets.end());
    REQUIRE_FALSE(applyPanZoom(clip, left->framing));
    CHECK(at(clip, 0, &ClipTransform::x) == Catch::Approx(0.08));
    CHECK(at(clip, 2 * kSec, &ClipTransform::x) == Catch::Approx(0.0).margin(1e-9));
    CHECK(at(clip, 4 * kSec, &ClipTransform::x) == Catch::Approx(-0.08));
    CHECK(at(clip, 2 * kSec, &ClipTransform::scale) == Catch::Approx(1.2));
    CHECK(applyPanZoom(clip, {0.01, 0, 0, 1, 0, 0}).value() == "the pan and zoom framing is out of range");
}

TEST_CASE("the anchor point, corner pin and colour fills are saved", "[animation]")
{
    Project project;
    project.media.push_back(MediaItem{.id = 1, .path = "/c.png", .still = true,
                                      .colourFill = ColourFill{ColourFillKind::Linear, {1, 0, 0, 1}, {0, 0, 1, 1}, 45}});
    Clip clip = picture();
    clip.mediaId = 1;
    clip.visual.transform.anchorX = 0.25;
    clip.visual.pin.bottomLeftX = 0.1;
    clip.keyframes["pinTopLeftY"] = {{0, 0.2}};
    project.tracks.push_back(Track{.id = 2, .clips = {clip}});
    project.setNextId(3);
    const Project loaded = projectFromJson(projectToJson(project));
    CHECK(loaded.tracks == project.tracks);
    CHECK(loaded.media == project.media);
    CHECK(loaded.tracks.front().clips.front().visualAt(0).pin.topLeftY == Catch::Approx(0.2));
}

TEST_CASE("motion blur samples a moving clip across its shutter", "[animation]")
{
    Clip clip = picture();
    const Ticks frame = kSec / 30;
    clip.motionBlur = 0.5;
    CHECK(motionBlurVisuals(clip, kSec, frame).empty()); // not moving: none
    clip.keyframes["x"] = {{0, 0.0}, {2 * kSec, 1.0}};
    const auto samples = motionBlurVisuals(clip, kSec, frame);
    REQUIRE(samples.size() == std::size_t(kMotionBlurSamples));
    // Half a frame wide, centred on the frame's time: x 0.5 ± a quarter frame's move.
    const double move = 0.5 / 30.0; // canvas widths a frame
    CHECK(samples.front().transform.x == Catch::Approx(0.5 - move / 4).margin(1e-4));
    CHECK(samples.back().transform.x == Catch::Approx(0.5 + move / 4).margin(1e-4));
    clip.motionBlur = 0;
    CHECK(motionBlurVisuals(clip, kSec, frame).empty());
}

TEST_CASE("motion blur and a mask's target are saved", "[animation]")
{
    Project project;
    Clip clip = picture();
    clip.motionBlur = 0.75;
    clip.masks.push_back(ClipMask{.target = MaskTarget::Colour});
    clip.masks.push_back(ClipMask{.invert = true, .target = MaskTarget::Effects});
    project.tracks.push_back(Track{.id = 2, .clips = {clip}});
    project.setNextId(3);
    const Project loaded = projectFromJson(projectToJson(project));
    CHECK(loaded.tracks == project.tracks);
}
