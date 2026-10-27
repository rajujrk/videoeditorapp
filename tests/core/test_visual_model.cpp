#include "commands/CommandStack.h"
#include "commands/EditCommands.h"
#include "io/ProjectSerializer.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

using namespace tve;

namespace {

constexpr Ticks kSec = kTicksPerSecond;

struct VisualProject {
    Project project;
    CommandStack stack{project};
    Id videoTrack = kNoId;
    Id audioTrack = kNoId;
    Id clip = kNoId;
    Id audioClip = kNoId;

    VisualProject()
    {
        audioTrack = push<AddTrackCommand>(TrackKind::Audio, "A1").trackId();
        videoTrack = push<AddTrackCommand>(TrackKind::Video, "V1").trackId();
        const Id media =
            push<ImportMediaCommand>(MediaItem{.path = "/av.mp4", .duration = 20 * kSec, .hasAudio = true}).mediaId();
        clip = push<AddClipCommand>(videoTrack, media, 0, 10 * kSec).clipId();
        audioClip = push<AddClipCommand>(audioTrack, media, 0, 10 * kSec).clipId();
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
};

} // namespace

TEST_CASE("clip picture settings are validated and undoable")
{
    VisualProject p;
    ClipVisual visual;
    visual.transform = {0.25, -0.1, 0.5, 30.0};
    visual.crop = {0.1, 0.0, 0.2, 0.05};
    visual.opacity = 0.6;
    visual.blend = BlendMode::Screen;
    p.push<SetClipVisualCommand>(p.clip, visual);
    CHECK(p.get(p.clip).visual == visual);
    CHECK(p.stack.undoLabel() == "Change Picture");

    ClipVisual bad = visual;
    bad.transform.scale = 0;
    CHECK(p.tryPush<SetClipVisualCommand>(p.clip, bad));
    bad = visual;
    bad.opacity = 1.5;
    CHECK(p.tryPush<SetClipVisualCommand>(p.clip, bad));
    bad = visual;
    bad.crop.left = 0.6;
    bad.crop.right = 0.6;
    CHECK(p.tryPush<SetClipVisualCommand>(p.clip, bad));
    CHECK(p.tryPush<SetClipVisualCommand>(p.audioClip, visual) == "only video clips have a picture");

    p.stack.undo();
    CHECK(p.get(p.clip).visual == ClipVisual{});
}

TEST_CASE("effect stacks are replaced as one labelled step")
{
    VisualProject p;
    std::vector<ClipEffect> stack{{"tve.color.adjust", true, {{"brightness", {0.2f}}}}};
    p.push<SetClipEffectsCommand>(p.clip, stack, "Add Effect");
    CHECK(p.get(p.clip).effects == stack);
    CHECK(p.stack.undoLabel() == "Add Effect");

    stack.push_back({"tve.blur.gaussian", false, {}});
    p.push<SetClipEffectsCommand>(p.clip, stack, "Add Effect");
    CHECK(p.get(p.clip).effects.size() == 2);

    CHECK(p.tryPush<SetClipEffectsCommand>(p.audioClip, stack, "Add Effect") == "effects apply to video clips");
    p.stack.undo();
    CHECK(p.get(p.clip).effects.size() == 1);
}

TEST_CASE("split keeps picture settings and effects on both halves")
{
    VisualProject p;
    ClipVisual visual;
    visual.opacity = 0.5;
    p.push<SetClipVisualCommand>(p.clip, visual);
    p.push<SetClipEffectsCommand>(p.clip, std::vector<ClipEffect>{{"tve.stylize.vignette", true, {}}}, "Add Effect");
    const Id right = p.push<SplitClipCommand>(p.clip, 4 * kSec).rightClipId();
    CHECK(p.get(right).visual.opacity == 0.5);
    CHECK(p.get(right).effects.size() == 1);
}

TEST_CASE("canvas settings change and undo")
{
    VisualProject p;
    p.push<SetProjectSettingsCommand>(ProjectSettings{.width = 3840, .height = 2160, .frameRate = {60, 1}});
    CHECK(p.project.settings.width == 3840);
    CHECK(p.project.settings.frameRate == FrameRate{60, 1});
    CHECK(p.tryPush<SetProjectSettingsCommand>(ProjectSettings{.width = 4, .height = 4}));
    p.stack.undo();
    CHECK(p.project.settings.width == 1920);
}

TEST_CASE("picture settings and effects round-trip; old files get defaults")
{
    VisualProject p;
    ClipVisual visual;
    visual.transform.rotation = -12.5;
    visual.blend = BlendMode::Multiply;
    p.push<SetClipVisualCommand>(p.clip, visual);
    p.push<SetClipEffectsCommand>(p.clip, std::vector<ClipEffect>{{"tve.color.adjust", false, {{"contrast", {1.4f}}}}},
                                  "Add Effect");
    CHECK(projectFromJson(projectToJson(p.project)) == p.project);

    auto json = projectToJson(p.project);
    for (auto& track : json["tracks"])
        for (auto& c : track["clips"]) {
            c.erase("visual");
            c.erase("effects");
        }
    const Project old = projectFromJson(json);
    CHECK(old.tracks[1].clips.front().visual == ClipVisual{});
    CHECK(old.tracks[1].clips.front().effects.empty());
}
