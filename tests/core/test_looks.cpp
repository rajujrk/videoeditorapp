#include "commands/CommandStack.h"
#include "commands/EditCommands.h"
#include "io/ProjectSerializer.h"
#include "model/TimelineQuery.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

using namespace tve;
using Catch::Approx;

namespace {

constexpr Ticks kSec = kTicksPerSecond;

struct LookProject {
    Project project;
    CommandStack stack{project};
    Id audio = kNoId;
    Id video = kNoId;
    Id media = kNoId;
    Id clip = kNoId;

    LookProject()
    {
        audio = push<AddTrackCommand>(TrackKind::Audio, "A1").trackId();
        video = push<AddTrackCommand>(TrackKind::Video, "V1").trackId();
        media = push<ImportMediaCommand>(MediaItem{.path = "/m.mp4", .duration = 60 * kSec, .hasAudio = true}).mediaId();
        clip = push<AddClipCommand>(video, media, 0, 10 * kSec, 0).clipId();
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
    std::optional<std::string> tryPush(std::unique_ptr<Command> command) { return stack.push(std::move(command)); }
    const Clip& get(Id id) { return *project.locateClip(id)->clip; }
};

ClipLook warmLook()
{
    ColorGrade g;
    g.temperature = 0.6;
    g.lift = {0.4, 0.0, 0.2};
    g.red = {{0, 0}, {0.5, 0.7}, {1, 1}};
    g.hueVsSat = {{0, 0.5}, {0.5, 0.9}, {1, 0.5}};
    g.hsl[2].saturation = 0.8;
    g.lutPath = ":/effects/looks/luts/warm_film.cube";
    return ClipLook{"warm_film", "Warm Film", 1.0, g};
}

} // namespace

TEST_CASE("a grade at part strength", "[looks]")
{
    const ColorGrade g = warmLook().grade;
    CHECK(scaledGrade(g, 1.0) == g);
    CHECK(scaledGrade(g, 0.0).isNeutral());
    const ColorGrade half = scaledGrade(g, 0.5);
    CHECK(half.temperature == Approx(0.3));
    CHECK(half.lift.x == Approx(0.2));
    CHECK(half.lift.master == Approx(0.1));
    CHECK(half.red[1].y == Approx(0.6));  // halfway back to the diagonal
    CHECK(half.hueVsSat[1].y == Approx(0.7)); // halfway back to the middle line
    CHECK(half.hsl[2].saturation == Approx(0.4));
    CHECK(half.lutIntensity == Approx(0.5));
    CHECK(half.lutPath == g.lutPath);
    ClipLook look = warmLook();
    look.intensity = 0.5;
    CHECK(look.effective() == half);
}

TEST_CASE("looks on clips", "[looks]")
{
    LookProject p;
    auto& apply = p.push<SetClipLookCommand>(p.clip, warmLook(), "Apply Look");
    CHECK(apply.label() == "Apply Look");
    REQUIRE(p.get(p.clip).look);
    CHECK(p.get(p.clip).look->title == "Warm Film");
    p.stack.undo();
    CHECK_FALSE(p.get(p.clip).look);
    p.stack.redo();

    ClipLook weaker = warmLook();
    weaker.intensity = 0.25;
    p.push<SetClipLookCommand>(p.clip, weaker, "Change Look");
    CHECK(p.get(p.clip).look->intensity == 0.25);

    // Splits keep it; it is saved.
    auto& split = p.push<SplitClipCommand>(p.clip, 4 * kSec);
    CHECK(p.get(split.rightClipId()).look == p.get(p.clip).look);
    const Project loaded = projectFromJson(projectToJson(p.project));
    CHECK(loaded.tracks[1].clips[0].look == p.get(p.clip).look);
    nlohmann::json old = projectToJson(p.project);
    old["tracks"][1]["clips"][0].erase("look");
    CHECK_FALSE(projectFromJson(old).tracks[1].clips[0].look);

    p.push<SetClipLookCommand>(p.clip, std::nullopt, "Remove Look");
    CHECK_FALSE(p.get(p.clip).look);

    ClipLook bad = warmLook();
    bad.intensity = 1.5;
    CHECK(p.tryPush(std::make_unique<SetClipLookCommand>(p.clip, bad, "x")).value()
          == "look intensity must be between 0 and 100%");
    bad = warmLook();
    bad.id.clear();
    CHECK(p.tryPush(std::make_unique<SetClipLookCommand>(p.clip, bad, "x")).has_value());
    const Id sound = p.push<AddClipCommand>(p.audio, p.media, 0, kSec, 0).clipId();
    CHECK(p.tryPush(std::make_unique<SetClipLookCommand>(sound, warmLook(), "x")).value() == "looks apply to video clips");
}

TEST_CASE("adjustment layers", "[looks]")
{
    LookProject p;
    // On a new track on top of everything.
    auto& added = p.push<AddAdjustmentCommand>(kNoId, 2 * kSec, 5 * kSec);
    CHECK(added.label() == "Add Adjustment Layer");
    REQUIRE(p.project.tracks.size() == 3);
    CHECK(p.project.tracks.back().id == added.trackId());
    const Clip& layer = p.get(added.clipId());
    CHECK(layer.adjustment);
    CHECK(layer.generated());
    CHECK(layer.mediaId == kNoId);
    CHECK_FALSE(layer.audioEnabled);

    // It is a layer like any other on screen: on top while it lasts.
    const auto layers = visibleVideoLayersAt(p.project, 3 * kSec);
    REQUIRE(layers.size() == 2);
    CHECK(layers.back().clip->id == added.clipId());

    // Any length; no speed, freeze frames or transitions.
    p.push<TrimClipCommand>(added.clipId(), TrimClipCommand::Edge::End, 60 * kSec);
    CHECK(p.get(added.clipId()).duration == 58 * kSec);
    CHECK(p.tryPush(std::make_unique<SetClipSpeedCommand>(added.clipId(), 2.0, false)).value()
          == "adjustment layers have no speed");
    CHECK(p.tryPush(std::make_unique<FreezeFrameCommand>(added.clipId(), 3 * kSec, kSec)).has_value());
    // A grade, a look and effects go on it.
    ColorGrade grey;
    grey.saturation = -1;
    p.push<SetClipColorCommand>(std::vector<Id>{added.clipId()}, grey);
    p.push<SetClipLookCommand>(added.clipId(), warmLook(), "Apply Look");
    p.push<SetClipEffectsCommand>(added.clipId(), std::vector<ClipEffect>{{"tve.overlay.film_grain", true, {}}}, "Add Effect");
    auto& split = p.push<SplitClipCommand>(added.clipId(), 10 * kSec);
    CHECK(p.get(split.rightClipId()).adjustment);
    CHECK(projectFromJson(projectToJson(p.project)).locateClip(added.clipId())->clip->adjustment);

    // On a given track; not over another clip, nor on an audio track.
    CHECK(p.tryPush(std::make_unique<AddAdjustmentCommand>(p.video, 0, kSec)).value() == "clip overlaps an existing clip");
    CHECK(p.tryPush(std::make_unique<AddAdjustmentCommand>(p.audio, 0, kSec)).value() == "adjustment layers go on video tracks");
    auto& onTrack = p.push<AddAdjustmentCommand>(p.video, 20 * kSec, kSec);
    CHECK(onTrack.trackId() == p.video);
    p.stack.undo();
    CHECK_FALSE(p.project.locateClip(onTrack.clipId()));
    // Undoing the first removes its track again.
    while (p.stack.canUndo() && p.project.tracks.size() == 3)
        p.stack.undo();
    CHECK(p.project.tracks.size() == 2);
}
