#include "commands/CommandStack.h"
#include "commands/EditCommands.h"
#include "io/ProjectSerializer.h"
#include "model/TimelineQuery.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

using namespace tve;

namespace {

constexpr Ticks kSec = kTicksPerSecond;

// Two touching clips on one track: A plays source 10–20 s at 0–10 s, B plays source
// 30–35 s at 10–15 s. The media is 60 s long, so both have handles.
struct CutProject {
    Project project;
    CommandStack stack{project};
    Id track = kNoId;
    Id media = kNoId;
    Id a = kNoId;
    Id b = kNoId;

    CutProject()
    {
        track = push<AddTrackCommand>(TrackKind::Video, "V1").trackId();
        media = push<ImportMediaCommand>(MediaItem{.path = "/m.mp4", .duration = 60 * kSec, .hasAudio = true}).mediaId();
        a = push<AddClipCommand>(track, media, 0, 10 * kSec, 10 * kSec).clipId();
        b = push<AddClipCommand>(track, media, 10 * kSec, 5 * kSec, 30 * kSec).clipId();
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

    template <typename T, typename... Args>
    std::optional<std::string> tryPush(Args&&... args)
    {
        return stack.push(std::make_unique<T>(std::forward<Args>(args)...));
    }

    Clip& get(Id id) { return *project.locateClip(id)->clip; }
    const Track& trackRef() { return *project.findTrack(track); }

    void addDissolve(Ticks duration = 2 * kSec, TransitionAlignment alignment = TransitionAlignment::Center)
    {
        push<SetClipTransitionCommand>(b, ClipTransition{"tve.transition.dissolve", duration, alignment, {}});
    }
};

} // namespace

TEST_CASE("a transition window sits around the cut by its alignment")
{
    CutProject p;
    p.addDissolve();
    auto window = transitionInto(p.trackRef(), p.get(p.b));
    REQUIRE(window);
    CHECK(window->from->id == p.a);
    CHECK(window->to->id == p.b);
    CHECK(window->cut == 10 * kSec);
    CHECK(window->start == 9 * kSec);
    CHECK(window->end == 11 * kSec);
    CHECK(window->progressAt(10 * kSec) == Catch::Approx(0.5));

    p.addDissolve(2 * kSec, TransitionAlignment::EndsAtCut);
    window = transitionInto(p.trackRef(), p.get(p.b));
    CHECK(window->start == 8 * kSec);
    CHECK(window->end == 10 * kSec);

    p.addDissolve(2 * kSec, TransitionAlignment::StartsAtCut);
    window = transitionOutOf(p.trackRef(), p.get(p.a));
    REQUIRE(window);
    CHECK(window->start == 10 * kSec);
    CHECK(window->end == 12 * kSec);
}

TEST_CASE("each side of a transition is limited to half its clip")
{
    CutProject p;
    p.addDissolve(10 * kSec); // B is 5 s long: at most 2.5 s after the cut
    auto window = transitionInto(p.trackRef(), p.get(p.b));
    REQUIRE(window);
    CHECK(window->start == 5 * kSec);
    CHECK(window->end == 10 * kSec + 5 * kSec / 2);
}

TEST_CASE("a transition plays only while the clips touch")
{
    CutProject p;
    p.addDissolve();
    p.push<TrimClipCommand>(p.a, TrimClipCommand::Edge::End, 9 * kSec);
    CHECK_FALSE(transitionInto(p.trackRef(), p.get(p.b)));
    p.stack.undo();
    CHECK(transitionInto(p.trackRef(), p.get(p.b)));
}

TEST_CASE("during a transition the layer shows both clips, using their handles")
{
    CutProject p;
    p.addDissolve();
    // Before the cut: A in its own span, B early in its head handle.
    auto layers = visibleVideoLayersAt(p.project, 9 * kSec + kSec / 2);
    REQUIRE(layers.size() == 1);
    CHECK(layers[0].clip->id == p.a);
    CHECK(layers[0].sourceTime == 19 * kSec + kSec / 2);
    REQUIRE(layers[0].transition);
    CHECK(layers[0].transition->to->id == p.b);
    CHECK(layers[0].transition->toSourceTime == 29 * kSec + kSec / 2);
    CHECK(layers[0].transition->progress == Catch::Approx(0.25));

    // After the cut: A runs on into its tail handle.
    layers = visibleVideoLayersAt(p.project, 10 * kSec + kSec / 2);
    REQUIRE(layers.size() == 1);
    CHECK(layers[0].clip->id == p.a);
    CHECK(layers[0].sourceTime == 20 * kSec + kSec / 2);
    CHECK(layers[0].transition->toSourceTime == 30 * kSec + kSec / 2);
    CHECK(layers[0].transition->progress == Catch::Approx(0.75));

    // Outside the window: plain clips.
    layers = visibleVideoLayersAt(p.project, 11 * kSec);
    REQUIRE(layers.size() == 1);
    CHECK(layers[0].clip->id == p.b);
    CHECK_FALSE(layers[0].transition);
}

TEST_CASE("where the media runs out, the edge frame is held")
{
    CutProject p;
    p.get(p.b).sourceIn = 0; // no head handle
    p.addDissolve();
    const auto layers = visibleVideoLayersAt(p.project, 9 * kSec);
    REQUIRE(layers[0].transition);
    CHECK(layers[0].transition->toSourceTime == 0);
}

TEST_CASE("reversed clips continue backwards into their handles")
{
    CutProject p;
    p.push<SetClipSpeedCommand>(p.a, 1.0, true); // A plays source 20 → 10 s
    p.addDissolve();
    const auto layers = visibleVideoLayersAt(p.project, 10 * kSec + kSec / 2);
    REQUIRE(layers[0].transition);
    CHECK(layers[0].sourceTime == 10 * kSec - kSec / 2 - 1);
}

TEST_CASE("sound crossfades with constant power across a transition")
{
    CutProject p;
    p.addDissolve();
    const Track& track = p.trackRef();
    const auto spanA = audibleSpan(track, p.get(p.a));
    const auto spanB = audibleSpan(track, p.get(p.b));
    CHECK(spanA.start == 0);
    CHECK(spanA.end == 11 * kSec);
    CHECK(spanB.start == 9 * kSec);
    CHECK(spanB.end == 15 * kSec);

    CHECK(transitionGainAt(track, p.get(p.a), 5 * kSec) == Catch::Approx(1.0));
    CHECK(transitionGainAt(track, p.get(p.a), 9 * kSec) == Catch::Approx(1.0));
    CHECK(transitionGainAt(track, p.get(p.b), 9 * kSec) == Catch::Approx(0.0));
    const float a = transitionGainAt(track, p.get(p.a), 10 * kSec);
    const float b = transitionGainAt(track, p.get(p.b), 10 * kSec);
    CHECK(a == Catch::Approx(b));
    CHECK(a * a + b * b == Catch::Approx(1.0)); // equal power at the cut
}

TEST_CASE("setting a transition checks the clips and is undoable")
{
    CutProject p;
    CHECK(p.tryPush<SetClipTransitionCommand>(p.a, ClipTransition{"x"}) == "a transition needs a clip ending where this one starts");
    CHECK(p.tryPush<SetClipTransitionCommand>(p.b, ClipTransition{""}) == "no transition chosen");
    CHECK(p.tryPush<SetClipTransitionCommand>(p.b, ClipTransition{"x", kSec / 100}) == "transition length is out of range");

    p.addDissolve();
    CHECK(p.stack.undoLabel() == "Add Transition");
    p.push<SetClipTransitionCommand>(p.b, ClipTransition{"tve.transition.wipe", kSec});
    CHECK(p.stack.undoLabel() == "Change Transition");
    p.push<SetClipTransitionCommand>(p.b, std::nullopt);
    CHECK(p.stack.undoLabel() == "Remove Transition");
    CHECK_FALSE(p.get(p.b).transitionIn);
    p.stack.undo();
    REQUIRE(p.get(p.b).transitionIn);
    CHECK(p.get(p.b).transitionIn->transitionId == "tve.transition.wipe");
    p.stack.undo();
    CHECK(p.get(p.b).transitionIn->transitionId == "tve.transition.dissolve");
}

TEST_CASE("moving or deleting a clip removes the transitions it was part of")
{
    CutProject p;
    const Id c = p.push<AddClipCommand>(p.track, p.media, 15 * kSec, 5 * kSec, 0).clipId();
    p.addDissolve();
    p.push<SetClipTransitionCommand>(c, ClipTransition{"tve.transition.wipe", kSec});

    // Moving B away drops A→B (on B) and B→C (on C).
    p.push<MoveClipCommand>(p.b, p.track, 30 * kSec);
    CHECK_FALSE(p.get(p.b).transitionIn);
    CHECK_FALSE(p.get(c).transitionIn);
    p.stack.undo();
    CHECK(p.get(p.b).transitionIn);
    CHECK(p.get(c).transitionIn);

    // Ripple-deleting B drops B→C, so A and C meet with a plain cut.
    p.push<DeleteClipCommand>(p.b, true);
    CHECK(p.get(c).start == 10 * kSec);
    CHECK_FALSE(p.get(c).transitionIn);
    p.stack.undo();
    CHECK(p.get(c).transitionIn);
    CHECK(p.get(p.b).transitionIn);
}

TEST_CASE("splitting keeps the transition on the left part")
{
    CutProject p;
    p.addDissolve();
    const Id right = p.push<SplitClipCommand>(p.b, 12 * kSec).rightClipId();
    CHECK(p.get(p.b).transitionIn);
    CHECK_FALSE(p.get(right).transitionIn);
}

TEST_CASE("transitions survive save and load")
{
    CutProject p;
    ClipTransition transition{"tve.transition.push", kSec + kSec / 2, TransitionAlignment::StartsAtCut, {{"direction", {2.0f}}}};
    p.push<SetClipTransitionCommand>(p.b, transition);
    const Project loaded = projectFromJson(projectToJson(p.project));
    CHECK(loaded == p.project);
    CHECK(projectToJson(p.project)["tracks"][0]["clips"][1]["transitionIn"]["alignment"] == "startsAtCut");
    CHECK(projectToJson(p.project)["tracks"][0]["clips"][0]["transitionIn"].is_null());
}
