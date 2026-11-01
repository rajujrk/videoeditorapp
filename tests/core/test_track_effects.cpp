#include "commands/CommandStack.h"
#include "commands/EditCommands.h"
#include "io/ProjectSerializer.h"

#include <nlohmann/json.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace tve;
using Catch::Approx;

TEST_CASE("plug-in ids")
{
    CHECK(isPluginEffect("au:aufx/dely/appl"));
    CHECK(isPluginEffect("vst3:5A3C1E427B9D4F608E21C3A4D05B6F17"));
    CHECK_FALSE(isPluginEffect("tve.audio.hall"));
}

TEST_CASE("a track's own audio effects are one undo step, and not animated")
{
    Project project;
    CommandStack stack(project);
    auto add = std::make_unique<AddTrackCommand>(TrackKind::Audio, "A1");
    auto* raw = add.get();
    REQUIRE_FALSE(stack.push(std::move(add)));
    const Id track = raw->trackId();
    std::vector<ClipEffect> effects{{.effectId = "au:aufx/dely/appl", .values = {{"0", {1.0f}}}, .state = "c3RhdGU="}};
    REQUIRE_FALSE(stack.push(std::make_unique<SetTrackAudioEffectsCommand>(track, effects, "Add Track Plug-in")));
    CHECK(project.findTrack(track)->audioEffects == effects);
    stack.undo();
    CHECK(project.findTrack(track)->audioEffects.empty());
    effects[0].keyframes["0"] = {{.time = 0, .value = 0.5}};
    CHECK(stack.push(std::make_unique<SetTrackAudioEffectsCommand>(track, effects, "x")));
    // Title tracks have no sound.
    auto titles = std::make_unique<AddTrackCommand>(TrackKind::Title, "T1");
    auto* titleRaw = titles.get();
    REQUIRE_FALSE(stack.push(std::move(titles)));
    CHECK(stack.push(std::make_unique<SetTrackAudioEffectsCommand>(titleRaw->trackId(), std::vector<ClipEffect>{}, "x")));
}

TEST_CASE("a clip's audio effects animate, and plug-in state and track effects are saved")
{
    Clip clip{.id = 1, .start = 10 * kTicksPerSecond, .duration = 4 * kTicksPerSecond};
    ClipEffect plugin{.effectId = "vst3:5A3C1E427B9D4F608E21C3A4D05B6F17", .state = "VFZFMw=="};
    plugin.keyframes["1"] = {{.time = 0, .value = 0.0}, {.time = 2 * kTicksPerSecond, .value = 1.0}};
    clip.audioEffects = {plugin};
    const auto halfway = clip.audioEffectsAt(11 * kTicksPerSecond);
    CHECK(halfway[0].values.at("1")[0] == Approx(0.5));
    CHECK(halfway[0].keyframes.empty());
    CHECK(halfway[0].state == "VFZFMw==");

    Project project;
    Track track{.id = project.allocateId(), .kind = TrackKind::Audio, .name = "A1"};
    track.clips = {clip};
    track.audioEffects = {{.effectId = "au:aufx/dely/appl", .enabled = false, .state = "c3RhdGU="}};
    project.tracks.push_back(track);
    const Project back = projectFromJson(projectToJson(project));
    CHECK(back.tracks[0].audioEffects == track.audioEffects);
    CHECK(back.tracks[0].clips[0].audioEffects[0].state == "VFZFMw==");
    CHECK(back.tracks[0].clips[0].audioEffects[0].keyframes.at("1").size() == 2);
}
