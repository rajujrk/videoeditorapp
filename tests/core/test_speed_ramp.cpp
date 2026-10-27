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

// Two video clips, 10 s each, back to back at 0 s and 10 s, from 60 s media.
struct RampProject {
    Project project;
    CommandStack stack{project};
    Id video = kNoId;
    Id media = kNoId;
    Id clip = kNoId;
    Id next = kNoId;

    RampProject()
    {
        video = push<AddTrackCommand>(TrackKind::Video, "V1").trackId();
        media = push<ImportMediaCommand>(MediaItem{.path = "/m.mp4", .duration = 60 * kSec, .hasAudio = true}).mediaId();
        clip = push<AddClipCommand>(video, media, 0, 10 * kSec, 5 * kSec).clipId();
        next = push<AddClipCommand>(video, media, 10 * kSec, 10 * kSec, 0).clipId();
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
    const Clip& get(Id id) { return *project.locateClip(id)->clip; }
};

const SpeedRamp kUp{{0.0, 1.0}, {1.0, 3.0}}; // average 2×

} // namespace

TEST_CASE("a speed ramp's speed, average and progress", "[speedramp]")
{
    CHECK(rampSpeedAt(kUp, 0.0) == 1.0);
    CHECK(rampSpeedAt(kUp, 0.5) == Approx(2.0));
    CHECK(rampSpeedAt(kUp, 1.0) == 3.0);
    CHECK(rampAverage(kUp) == Approx(2.0));
    // ∫ from 0 to 0.5 of (1 + 2u) = 0.75, of the total 2.
    CHECK(rampProgress(kUp, 0.5) == Approx(0.375));
    CHECK(rampProgress(kUp, 0.0) == 0.0);
    CHECK(rampProgress(kUp, 1.0) == Approx(1.0));
    CHECK(rampFractionAt(kUp, 0.375) == Approx(0.5).margin(1e-9));

    const SpeedRamp bumpy = speedRampPresets().at(4).ramp; // bullet time
    for (double u : {0.1, 0.35, 0.5, 0.77, 0.95})
        CHECK(rampFractionAt(bumpy, rampProgress(bumpy, u)) == Approx(u).margin(1e-9));

    // No ramp: constant.
    CHECK(rampAverage({}) == 1.0);
    CHECK(rampProgress({}, 0.3) == Approx(0.3));
}

TEST_CASE("cutting a ramp keeps each part's speeds", "[speedramp]")
{
    const SpeedRamp left = subRamp(kUp, 0.0, 0.5);
    REQUIRE(left.size() == 2);
    CHECK(left.front().speed == 1.0);
    CHECK(left.back().speed == Approx(2.0));
    CHECK(left.back().at == 1.0);
    const SpeedRamp right = subRamp(speedRampPresets().at(4).ramp, 0.35, 0.9);
    CHECK_FALSE(validateSpeedRamp(right));
    for (double u : {0.0, 0.2, 0.6, 1.0})
        CHECK(rampSpeedAt(right, u) == Approx(rampSpeedAt(speedRampPresets().at(4).ramp, 0.35 + u * 0.55)));
}

TEST_CASE("speed ramps are checked", "[speedramp]")
{
    CHECK_FALSE(validateSpeedRamp(kUp));
    CHECK(validateSpeedRamp({{0.0, 1.0}}).value() == "a speed ramp needs at least two points");
    CHECK(validateSpeedRamp({{0.1, 1.0}, {1.0, 1.0}}).value() == "a speed ramp runs from the clip's start to its end");
    CHECK(validateSpeedRamp({{0.0, 1.0}, {1.0, 20.0}}).value() == "speed must be between 0.1× and 10×");
    CHECK(validateSpeedRamp({{0.0, 1.0}, {0.5, 1.0}, {0.5, 2.0}, {1.0, 1.0}}).value() == "speed points must be in order");
    SpeedRamp many;
    for (int i = 0; i <= 20; ++i)
        many.push_back({i / 20.0, 1.0});
    CHECK(validateSpeedRamp(many).has_value());
    for (const SpeedRampPreset& preset : speedRampPresets()) {
        INFO(preset.id);
        CHECK_FALSE(validateSpeedRamp(preset.ramp));
        CHECK_FALSE(preset.title.empty());
    }
}

TEST_CASE("a ramped clip plays the same footage in a new length", "[speedramp]")
{
    RampProject p;
    auto& command = p.push<SetClipSpeedRampCommand>(p.clip, kUp);
    CHECK(command.label() == "Speed Ramp");
    const Clip& clip = p.get(p.clip);
    CHECK(clip.duration == 5 * kSec); // 10 s of footage at 2× on average
    CHECK(clip.speed == Approx(2.0));
    CHECK(clip.sourceSpan() == 10 * kSec);
    CHECK(p.get(p.next).start == 5 * kSec); // the next clip follows the end
    CHECK(clip.sourceTimeAt(0) == 5 * kSec);
    CHECK(clip.sourceTimeAt(kSec * 5 / 2) == 5 * kSec + kSec * 375 / 100); // halfway: 3.75 s in
    CHECK(clip.sourceTimeAt(5 * kSec - 1) == Approx(double(15 * kSec)).margin(double(kSec) / 1000));
    // Beyond the ends (transition handles) the end speeds carry on.
    CHECK(clip.sourceTimeBeyond(-kSec) == 4 * kSec);
    CHECK(clip.sourceTimeBeyond(6 * kSec) == 18 * kSec);

    p.stack.undo();
    CHECK(p.get(p.clip).duration == 10 * kSec);
    CHECK(p.get(p.clip).speedRamp.empty());
    CHECK(p.get(p.next).start == 10 * kSec);
    p.stack.redo();

    SECTION("removing the ramp keeps its average and length")
    {
        auto& remove = p.push<SetClipSpeedRampCommand>(p.clip, SpeedRamp{});
        CHECK(remove.label() == "Remove Speed Ramp");
        CHECK(p.get(p.clip).speedRamp.empty());
        CHECK(p.get(p.clip).duration == 5 * kSec);
        CHECK(p.get(p.clip).sourceTimeAt(kSec * 5 / 2) == 10 * kSec);
        CHECK(p.stack.push(std::make_unique<SetClipSpeedRampCommand>(p.clip, SpeedRamp{})).value()
              == "this clip has no speed ramp");
    }
    SECTION("a constant speed replaces it")
    {
        p.push<SetClipSpeedCommand>(p.clip, 1.0, false);
        CHECK(p.get(p.clip).speedRamp.empty());
        CHECK(p.get(p.clip).duration == 10 * kSec);
    }
    SECTION("turning it around keeps the ramp")
    {
        auto& reverse = p.push<SetClipSpeedRampCommand>(p.clip, kUp, true);
        CHECK(reverse.label() == "Change Speed");
        const Clip& reversed = p.get(p.clip);
        CHECK(reversed.reversed);
        CHECK(reversed.speedRamp == kUp);
        CHECK(reversed.sourceTimeAt(0) == 15 * kSec - 1);
        CHECK(reversed.sourceTimeAt(kSec * 5 / 2) == 15 * kSec - kSec * 375 / 100 - 1);
    }
    SECTION("splitting keeps every frame where it was")
    {
        std::vector<Ticks> before;
        for (Ticks t = 0; t < 5 * kSec; t += kSec / 7)
            before.push_back(p.get(p.clip).sourceTimeAt(t));
        auto& split = p.push<SplitClipCommand>(p.clip, 2 * kSec);
        std::size_t i = 0;
        for (Ticks t = 0; t < 5 * kSec; t += kSec / 7, ++i) {
            const Clip& part = t < 2 * kSec ? p.get(p.clip) : p.get(split.rightClipId());
            CHECK(std::llabs(part.sourceTimeAt(t) - before[i]) <= 2);
        }
        CHECK(p.get(p.clip).speedRamp.back().speed == Approx(rampSpeedAt(kUp, 0.4)));
        CHECK(p.get(split.rightClipId()).speedRamp.front().speed == Approx(rampSpeedAt(kUp, 0.4)));
    }
    SECTION("a freeze frame holds without a ramp")
    {
        auto& freeze = p.push<FreezeFrameCommand>(p.clip, 2 * kSec, kSec);
        CHECK(p.get(freeze.freezeClipId()).speedRamp.empty());
        // The frame shown at 2 s: 5 s in, plus 10 s × progress(0.4) = 2.8 s.
        CHECK(p.get(freeze.freezeClipId()).sourceIn == 5 * kSec + kSec * 28 / 10);
    }
    SECTION("it is saved and loaded")
    {
        Project loaded = projectFromJson(projectToJson(p.project));
        CHECK(*loaded.locateClip(p.clip)->clip == p.get(p.clip));
    }
}

TEST_CASE("speed ramps are refused where they cannot apply", "[speedramp]")
{
    RampProject p;
    const auto refuse = [&](Id id, SpeedRamp ramp) {
        auto error = p.stack.push(std::make_unique<SetClipSpeedRampCommand>(id, std::move(ramp)));
        REQUIRE(error);
        return *error;
    };
    CHECK(refuse(p.clip, {{0.0, 1.0}, {1.0, 50.0}}) == "speed must be between 0.1× and 10×");
    CHECK(refuse(999, kUp) == "clip not found");
    const Id title = p.push<AddTitleCommand>(kNoId, 0, kSec, ClipText{}).clipId();
    CHECK(refuse(title, kUp) == "titles have no speed");
    auto& freeze = p.push<FreezeFrameCommand>(p.clip, 2 * kSec, kSec);
    CHECK(refuse(freeze.freezeClipId(), kUp) == "a freeze frame has no speed");
}
