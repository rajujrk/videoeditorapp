#include "commands/CommandStack.h"
#include "commands/EditCommands.h"
#include "io/ProjectSerializer.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

using namespace tve;
using Catch::Approx;

namespace {

constexpr Ticks kSec = kTicksPerSecond;

// A video clip at 10–20 s with sound, an audio track, and a silent picture-only clip.
struct AnimProject {
    Project project;
    CommandStack stack{project};
    Id audio = kNoId;
    Id video = kNoId;
    Id media = kNoId;
    Id clip = kNoId;

    AnimProject()
    {
        audio = push<AddTrackCommand>(TrackKind::Audio, "A1").trackId();
        video = push<AddTrackCommand>(TrackKind::Video, "V1").trackId();
        media = push<ImportMediaCommand>(MediaItem{.path = "/m.mp4", .duration = 60 * kSec, .hasAudio = true}).mediaId();
        clip = push<AddClipCommand>(video, media, 10 * kSec, 10 * kSec, 0).clipId();
    }

    template <typename T, typename... Args>
    T& push(Args&&... args)
    {
        auto command = std::make_unique<T>(std::forward<Args>(args)...);
        T& ref = *command;
        auto error = stack.push(std::move(command));
        INFO(error.value_or(""));
        REQUIRE_FALSE(error);
        return ref;
    }
    std::optional<std::string> tryKeys(Id id, std::map<std::string, Keyframes> changes)
    {
        return stack.push(std::make_unique<SetClipKeyframesCommand>(id, std::move(changes), "Add Keyframe"));
    }
    const Clip& get(Id id) { return *project.locateClip(id)->clip; }
};

} // namespace

TEST_CASE("easing curves run from 0 to 1", "[keyframes]")
{
    for (Easing e : {Easing::Linear, Easing::EaseIn, Easing::EaseOut, Easing::EaseInOut}) {
        CHECK(ease(e, 0.0) == Approx(0.0));
        CHECK(ease(e, 1.0) == Approx(1.0));
        double last = 0.0;
        for (int i = 1; i <= 20; ++i) { // never step back
            const double v = ease(e, i / 20.0);
            CHECK(v >= last);
            last = v;
        }
    }
    CHECK(ease(Easing::EaseIn, 0.5) < 0.5);
    CHECK(ease(Easing::EaseOut, 0.5) > 0.5);
    CHECK(ease(Easing::EaseInOut, 0.5) == Approx(0.5));
    CHECK(ease(Easing::Hold, 0.99) == 0.0);
    CHECK(ease(Easing::Linear, 2.0) == 1.0); // clamped
}

TEST_CASE("keyframes evaluate between, before and after", "[keyframes]")
{
    CHECK(evaluate({}, kSec, 7.0) == 7.0);
    const Keyframes keys{{kSec, 0.0, Easing::Linear}, {3 * kSec, 1.0, Easing::Hold}, {4 * kSec, 5.0, Easing::Linear}};
    CHECK(evaluate(keys, 0, 9.0) == 0.0);
    CHECK(evaluate(keys, kSec, 9.0) == 0.0);
    CHECK(evaluate(keys, 2 * kSec, 9.0) == Approx(0.5));
    CHECK(evaluate(keys, 3 * kSec, 9.0) == 1.0);
    CHECK(evaluate(keys, 3 * kSec + kSec / 2, 9.0) == 1.0); // held
    CHECK(evaluate(keys, 4 * kSec, 9.0) == 5.0);
    CHECK(evaluate(keys, 9 * kSec, 9.0) == 5.0);
    CHECK(keyframeAt(keys, 3 * kSec));
    CHECK_FALSE(keyframeAt(keys, 2 * kSec));
}

TEST_CASE("keyframes are checked against their property", "[keyframes]")
{
    CHECK_FALSE(validateKeyframes("opacity", {{0, 0.0}, {kSec, 1.0}}));
    CHECK(validateKeyframes("colour", {{0, 0.0}}).value() == "\"colour\" cannot be animated");
    CHECK(validateKeyframes("opacity", {{0, 1.5}}).value() == "a keyframe of opacity is out of range");
    CHECK(validateKeyframes("scale", {{0, 0.0}}).has_value());
    CHECK(validateKeyframes("volume", {{0, -61.0}}).has_value());
    CHECK(validateKeyframes("x", {{kSec, 0.0}, {kSec, 1.0}}).value() == "keyframes must be in order, one per time");
    CHECK(validateKeyframes("x", {{kSec, 0.0}, {0, 1.0}}).has_value());
    CHECK(findAnimatable("cropBottom"));
    CHECK_FALSE(findAnimatable("blend"));
}

TEST_CASE("an animated clip moves, fades and changes volume over time", "[keyframes]")
{
    AnimProject p;
    p.push<SetClipKeyframesCommand>(p.clip,
                                    std::map<std::string, Keyframes>{
                                        {"x", {{0, -0.5}, {2 * kSec, 0.5}}},
                                        {"opacity", {{kSec, 1.0, Easing::EaseIn}, {3 * kSec, 0.0}}},
                                        {"volume", {{0, -60.0}, {kSec, 0.0}}},
                                    },
                                    "Add Keyframe");
    const Clip& clip = p.get(p.clip);
    CHECK(clip.visualAt(10 * kSec).transform.x == Approx(-0.5));
    CHECK(clip.visualAt(11 * kSec).transform.x == Approx(0.0));
    CHECK(clip.visualAt(15 * kSec).transform.x == Approx(0.5));
    CHECK(clip.visualAt(12 * kSec).opacity == Approx(1.0 - 0.125)); // ease in: slow start
    CHECK(clip.visualAt(13 * kSec).opacity == Approx(0.0));
    CHECK(clip.visualAt(11 * kSec).transform.scale == 1.0); // not animated: static
    CHECK(clip.gainAt(10 * kSec) == 0.0f);
    CHECK(clip.gainDbAt(10 * kSec + kSec / 2) == Approx(-30.0));
    CHECK(clip.gainAt(12 * kSec) == Approx(1.0f));

    p.stack.undo();
    CHECK(p.get(p.clip).keyframes.empty());
    CHECK(p.get(p.clip).visualAt(11 * kSec) == ClipVisual{});
    p.stack.redo();
    CHECK(p.get(p.clip).keyframes.size() == 3);

    // An empty list stops animating; the static value is set alongside.
    ClipVisual still;
    still.transform.x = 0.25;
    p.push<SetClipKeyframesCommand>(p.clip, std::map<std::string, Keyframes>{{"x", {}}}, "Clear Keyframes", still);
    CHECK_FALSE(p.get(p.clip).keyframesOf("x"));
    CHECK(p.get(p.clip).visualAt(11 * kSec).transform.x == 0.25);
    CHECK(p.get(p.clip).keyframesOf("opacity"));
}

TEST_CASE("crop edges keyed apart still leave some picture", "[keyframes]")
{
    AnimProject p;
    p.push<SetClipKeyframesCommand>(p.clip,
                                    std::map<std::string, Keyframes>{{"cropLeft", {{0, 0.9}}}, {"cropRight", {{0, 0.9}}}},
                                    "Add Keyframe");
    const ClipVisual v = p.get(p.clip).visualAt(11 * kSec);
    CHECK(v.crop.left + v.crop.right == Approx(0.95));
    CHECK(v.crop.left == Approx(v.crop.right));
}

TEST_CASE("keyframes are refused where they cannot apply", "[keyframes]")
{
    AnimProject p;
    CHECK(p.tryKeys(p.clip, {{"x", {{0, 99.0}}}}).value() == "a keyframe of x is out of range");
    CHECK(p.tryKeys(p.clip, {{"speed", {{0, 1.0}}}}).has_value());
    CHECK(p.tryKeys(999, {{"x", {{0, 0.0}}}}).value() == "clip not found");

    const Id tone = p.push<ImportMediaCommand>(
        MediaItem{.path = "/t.wav", .duration = 5 * kSec, .hasVideo = false, .hasAudio = true}).mediaId();
    const Id sound = p.push<AddClipCommand>(p.audio, tone, 0, 5 * kSec, 0).clipId();
    CHECK(p.tryKeys(sound, {{"opacity", {{0, 0.5}}}}).value() == "only video clips have a picture");
    CHECK_FALSE(p.tryKeys(sound, {{"volume", {{0, -6.0}}}}));

    const Id silent = p.push<ImportMediaCommand>(MediaItem{.path = "/s.mp4", .duration = 5 * kSec}).mediaId();
    const Id picture = p.push<AddClipCommand>(p.video, silent, 0, 5 * kSec, 0).clipId();
    CHECK(p.tryKeys(picture, {{"volume", {{0, -6.0}}}}).value() == "this clip has no sound");

    p.push<SetTrackFlagCommand>(p.video, SetTrackFlagCommand::Flag::Locked, true);
    CHECK(p.tryKeys(p.clip, {{"x", {{0, 0.0}}}}).value() == "track is locked");
}

TEST_CASE("keyframes stay on the same moments through trims, splits and speed changes", "[keyframes]")
{
    AnimProject p;
    p.push<SetClipKeyframesCommand>(p.clip, std::map<std::string, Keyframes>{{"x", {{0, 0.0}, {10 * kSec, 1.0}}}},
                                    "Add Keyframe");
    const auto xAt = [&](Id id, Ticks t) { return p.get(id).visualAt(t).transform.x; };
    const double atFourteen = xAt(p.clip, 14 * kSec);
    REQUIRE(atFourteen == Approx(0.4));

    SECTION("trimming the start")
    {
        p.push<TrimClipCommand>(p.clip, TrimClipCommand::Edge::Start, 12 * kSec);
        CHECK(xAt(p.clip, 14 * kSec) == Approx(atFourteen));
        CHECK(p.get(p.clip).keyframes.at("x").front().time == -2 * kSec);
        p.stack.undo();
        CHECK(p.get(p.clip).keyframes.at("x").front().time == 0);
    }
    SECTION("splitting")
    {
        auto& split = p.push<SplitClipCommand>(p.clip, 13 * kSec);
        CHECK(xAt(p.clip, 12 * kSec) == Approx(0.2));
        CHECK(xAt(split.rightClipId(), 14 * kSec) == Approx(atFourteen));
        CHECK(xAt(split.rightClipId(), 19 * kSec) == Approx(0.9));
    }
    SECTION("doubling the speed")
    {
        p.push<SetClipSpeedCommand>(p.clip, 2.0, false);
        REQUIRE(p.get(p.clip).duration == 5 * kSec);
        CHECK(p.get(p.clip).keyframes.at("x").back().time == 5 * kSec);
        CHECK(xAt(p.clip, 12 * kSec) == Approx(atFourteen)); // the same source moment
    }
    SECTION("a freeze frame holds the placement of its moment")
    {
        auto& freeze = p.push<FreezeFrameCommand>(p.clip, 14 * kSec, 2 * kSec);
        const Clip& still = p.get(freeze.freezeClipId());
        CHECK(still.keyframes.empty());
        CHECK(still.visual.transform.x == Approx(atFourteen));
        CHECK(xAt(freeze.rightClipId(), 16 * kSec) == Approx(atFourteen)); // continues after the hold
    }
}

TEST_CASE("keyframes are saved and loaded", "[keyframes]")
{
    AnimProject p;
    p.push<SetClipKeyframesCommand>(p.clip,
                                    std::map<std::string, Keyframes>{{"scale", {{0, 1.0, Easing::EaseInOut}, {kSec, 2.0, Easing::Hold}}},
                                                                     {"volume", {{0, -12.0}}}},
                                    "Add Keyframe");
    const nlohmann::json json = projectToJson(p.project);
    CHECK(json.dump().find("\"easeInOut\"") != std::string::npos);
    Project loaded = projectFromJson(json);
    const Clip& clip = *loaded.locateClip(p.clip)->clip;
    CHECK(clip.keyframes == p.get(p.clip).keyframes);

    // Projects from before keyframes load without them.
    nlohmann::json old = json;
    for (auto& track : old["tracks"])
        for (auto& c : track["clips"])
            c.erase("keyframes");
    CHECK(projectFromJson(old).locateClip(p.clip)->clip->keyframes.empty());
}
