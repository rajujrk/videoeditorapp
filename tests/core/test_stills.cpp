#include "commands/CommandStack.h"
#include "commands/EditCommands.h"
#include "io/ProjectSerializer.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

using namespace tve;

namespace {

constexpr Ticks kSec = kTicksPerSecond;

struct StillProject {
    Project project;
    CommandStack stack{project};
    Id video = kNoId;
    Id audio = kNoId;
    Id photo = kNoId;
    Id clip = kNoId;

    StillProject()
    {
        audio = push<AddTrackCommand>(TrackKind::Audio, "A1").trackId();
        video = push<AddTrackCommand>(TrackKind::Video, "V1").trackId();
        photo = push<ImportMediaCommand>(MediaItem{.path = "/p.jpg", .duration = kStillDuration, .width = 4032,
                                                   .height = 3024, .still = true})
                    .mediaId();
        clip = push<AddClipCommand>(video, photo, 0, kStillDuration, 0).clipId();
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

} // namespace

TEST_CASE("a still image lasts as long as its clip", "[stills]")
{
    StillProject p;
    CHECK(p.get(p.clip).duration == 5 * kSec);
    // Longer than the "media": fine for a still.
    p.push<AddClipCommand>(p.video, p.photo, 10 * kSec, 60 * kSec, 0);
    p.push<TrimClipCommand>(p.clip, TrimClipCommand::Edge::End, 9 * kSec);
    CHECK(p.get(p.clip).duration == 9 * kSec);
    // Trimming the start moves only the start: the picture is the same throughout.
    p.push<TrimClipCommand>(p.clip, TrimClipCommand::Edge::Start, 2 * kSec);
    CHECK(p.get(p.clip).start == 2 * kSec);
    CHECK(p.get(p.clip).sourceIn == 0);
    p.push<TrimClipCommand>(p.clip, TrimClipCommand::Edge::Start, 0);
    CHECK(p.get(p.clip).sourceIn == 0);

    CHECK(p.tryPush(std::make_unique<SetClipSpeedCommand>(p.clip, 2.0, false)).value() == "a still image has no speed");
    CHECK(p.tryPush(std::make_unique<SetClipSpeedRampCommand>(p.clip, SpeedRamp{{0, 1}, {1, 2}})).value()
          == "a still image has no speed");
    CHECK(p.tryPush(std::make_unique<FreezeFrameCommand>(p.clip, kSec, kSec)).value() == "a still image is already still");
    CHECK(p.tryPush(std::make_unique<AddClipCommand>(p.audio, p.photo, 0, kSec, 0)).value()
          == "this media has no sound for an audio track");

    // Splits, colour and looks work as on any picture.
    auto& split = p.push<SplitClipCommand>(p.clip, 4 * kSec);
    CHECK(p.get(split.rightClipId()).mediaId == p.photo);
    ColorGrade warm;
    warm.temperature = 0.5;
    p.push<SetClipColorCommand>(std::vector<Id>{p.clip}, warm);
}

TEST_CASE("the still flag is saved", "[stills]")
{
    StillProject p;
    const Project loaded = projectFromJson(projectToJson(p.project));
    REQUIRE(loaded.media.size() == 1);
    CHECK(loaded.media[0].still);
    CHECK(loaded.media[0].width == 4032);
    nlohmann::json old = projectToJson(p.project);
    old["media"][0].erase("still");
    CHECK_FALSE(projectFromJson(old).media[0].still);
}

TEST_CASE("stickers and transparent video are media like any other, flagged and saved", "[stills]")
{
    StillProject p;
    // A sticker: a Lottie animation that lasts as long as its clip, like a still.
    const Id sticker = p.push<ImportMediaCommand>(MediaItem{.path = ":/effects/stickers/star_spin.json",
                                                            .duration = kStillDuration, .width = 512, .height = 512,
                                                            .still = true, .alpha = true, .lottie = true})
                           .mediaId();
    ClipVisual small;
    small.transform.scale = 0.4;
    auto& added = p.push<AddClipCommand>(p.video, sticker, 20 * kSec, kStillDuration, 0, small, "Add Sticker");
    CHECK(added.label() == "Add Sticker");
    CHECK(p.project.locateClip(added.clipId())->clip->visual.transform.scale == 0.4);
    p.push<TrimClipCommand>(added.clipId(), TrimClipCommand::Edge::End, 60 * kSec); // any length
    CHECK(p.tryPush(std::make_unique<SetClipSpeedCommand>(added.clipId(), 2.0, false)).has_value());
    const Id clear = p.push<ImportMediaCommand>(MediaItem{.path = "/alpha.mov", .duration = 2 * kSec, .width = 320,
                                                          .height = 180, .alpha = true})
                         .mediaId();
    const Project loaded = projectFromJson(projectToJson(p.project));
    CHECK(loaded.findMedia(sticker)->lottie);
    CHECK(loaded.findMedia(sticker)->alpha);
    CHECK(loaded.findMedia(clear)->alpha);
    CHECK_FALSE(loaded.findMedia(clear)->lottie);
    CHECK_FALSE(loaded.findMedia(p.photo)->alpha);
}
