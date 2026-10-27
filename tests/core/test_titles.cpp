#include "commands/CommandStack.h"
#include "commands/EditCommands.h"
#include "io/ProjectSerializer.h"
#include "model/TimelineQuery.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

using namespace tve;

namespace {

constexpr Ticks kSec = kTicksPerSecond;

// A video track with a clip at 0–10 s, and an audio track.
struct TitleProject {
    Project project;
    CommandStack stack{project};
    Id video = kNoId;
    Id audio = kNoId;
    Id media = kNoId;
    Id clip = kNoId;

    TitleProject()
    {
        audio = push<AddTrackCommand>(TrackKind::Audio, "A1").trackId();
        video = push<AddTrackCommand>(TrackKind::Video, "V1").trackId();
        media = push<ImportMediaCommand>(MediaItem{.path = "/m.mp4", .duration = 60 * kSec}).mediaId();
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

    template <typename T, typename... Args>
    std::optional<std::string> tryPush(Args&&... args)
    {
        return stack.push(std::make_unique<T>(std::forward<Args>(args)...));
    }

    Clip& get(Id id) { return *project.locateClip(id)->clip; }
};

ClipText hello()
{
    ClipText text;
    text.text = "Hello";
    return text;
}

} // namespace

TEST_CASE("a title is a clip with text and no media, drawn over the video below")
{
    TitleProject p;
    const auto& add = p.push<AddTitleCommand>(kNoId, 2 * kSec, 3 * kSec, hello());
    CHECK(p.stack.undoLabel() == "Add Title");
    const Clip& title = p.get(add.clipId());
    CHECK(title.mediaId == kNoId);
    REQUIRE(title.text);
    CHECK(title.text->text == "Hello");
    CHECK_FALSE(title.audioEnabled);

    // No free track was named: a new video track on top holds it.
    REQUIRE(p.project.tracks.size() == 3);
    CHECK(p.project.tracks.back().id == add.trackId());
    CHECK(p.project.tracks.back().name == "Video 2");

    const auto layers = visibleVideoLayersAt(p.project, 3 * kSec);
    REQUIRE(layers.size() == 2);
    CHECK(layers[0].clip->id == p.clip);
    CHECK(layers[1].clip->id == add.clipId());

    // Undo removes the clip and the track it made.
    p.stack.undo();
    CHECK(p.project.tracks.size() == 2);
    p.stack.redo();
    CHECK(p.project.tracks.size() == 3);
    CHECK(p.get(add.clipId()).text->text == "Hello");
}

TEST_CASE("titles go on free space on video or title tracks")
{
    TitleProject p;
    CHECK(p.tryPush<AddTitleCommand>(p.audio, 0, kSec, hello()) == "titles go on video or title tracks");
    CHECK(p.tryPush<AddTitleCommand>(p.video, kSec, kSec, hello()) == "clip overlaps an existing clip");
    CHECK(p.tryPush<AddTitleCommand>(p.video, 10 * kSec, 0, hello()) == "clip times are out of range");
    p.push<AddTitleCommand>(p.video, 10 * kSec, 2 * kSec, hello());

    const Id titleTrack = p.push<AddTrackCommand>(TrackKind::Title, "T1").trackId();
    const Id onTitleTrack = p.push<AddTitleCommand>(titleTrack, 0, kSec, hello()).clipId();
    const auto layers = visibleVideoLayersAt(p.project, kSec / 2);
    REQUIRE(layers.size() == 2);
    CHECK(layers.back().clip->id == onTitleTrack);
}

TEST_CASE("title text and sizes are checked")
{
    TitleProject p;
    ClipText text = hello();
    text.size = 0.9;
    CHECK(p.tryPush<AddTitleCommand>(kNoId, 0, kSec, text) == "text size is out of range");
    text = hello();
    text.color.r = 2.0;
    CHECK(p.tryPush<AddTitleCommand>(kNoId, 0, kSec, text) == "colours must be between 0 and 1");
    text = hello();
    text.text = std::string(kMaxTextLength + 1, 'x');
    CHECK(p.tryPush<AddTitleCommand>(kNoId, 0, kSec, text) == "the text is too long");
    CHECK(p.project.tracks.size() == 2); // a failed add leaves no track behind
}

TEST_CASE("editing a title's text is one undo step")
{
    TitleProject p;
    const Id title = p.push<AddTitleCommand>(kNoId, 0, kSec, hello()).clipId();
    ClipText edited = hello();
    edited.text = "Goodbye";
    edited.bold = true;
    edited.alignment = TextAlignment::Left;
    p.push<SetClipTextCommand>(title, edited);
    CHECK(p.stack.undoLabel() == "Edit Title");
    CHECK(p.get(title).text == edited);
    p.stack.undo();
    CHECK(p.get(title).text->text == "Hello");
    CHECK(p.tryPush<SetClipTextCommand>(p.clip, edited) == "only titles have text");
}

TEST_CASE("titles trim to any length, split with their text, and take no speed or freeze")
{
    TitleProject p;
    const Id title = p.push<AddTitleCommand>(kNoId, 0, 2 * kSec, hello()).clipId();
    p.push<TrimClipCommand>(title, TrimClipCommand::Edge::End, 100 * kSec); // past any media length
    CHECK(p.get(title).duration == 100 * kSec);

    const Id right = p.push<SplitClipCommand>(title, 50 * kSec).rightClipId();
    REQUIRE(p.get(right).text);
    CHECK(p.get(right).text->text == "Hello");

    CHECK(p.tryPush<SetClipSpeedCommand>(title, 2.0, false) == "titles have no speed");
    CHECK(p.tryPush<FreezeFrameCommand>(title, kSec, kSec) == "freeze frames are made from video clips");
    CHECK(p.tryPush<SetClipTransitionCommand>(right, ClipTransition{"tve.transition.dissolve"})
          == "transitions go between video clips");
}

TEST_CASE("titles survive save and load")
{
    TitleProject p;
    ClipText text = hello();
    text.fontFamily = "Avenir Next";
    text.italic = true;
    text.color = {1.0, 0.8, 0.2, 1.0};
    text.alignment = TextAlignment::Right;
    text.outlineWidth = 0.05;
    text.shadow = true;
    ClipVisual visual;
    visual.transform.y = 0.35;
    p.push<AddTitleCommand>(kNoId, 0, kSec, text, visual);
    const nlohmann::json json = projectToJson(p.project);
    CHECK(projectFromJson(json) == p.project);
    const auto& saved = json["tracks"][2]["clips"][0]["text"];
    CHECK(saved["alignment"] == "right");
    CHECK(saved["fontFamily"] == "Avenir Next");
    CHECK(json["tracks"][1]["clips"][0]["text"].is_null());
}
