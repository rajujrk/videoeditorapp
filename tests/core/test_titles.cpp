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

TEST_CASE("text animation presets and their checks", "[titles]")
{
    const auto& presets = textAnimationPresets();
    CHECK(presets.size() == 19);
    CHECK(std::ranges::count_if(presets, &TextAnimationPreset::loop) == 5);
    // 14 that arrive (and leave, backwards) and 5 loops: 33 animations.
    CHECK(validateTextAnimation({}, TextAnimationRole::In) == std::nullopt); // none
    CHECK_FALSE(validateTextAnimation({"pop", 0.5, TextUnit::Word, 0.5}, TextAnimationRole::In));
    CHECK_FALSE(validateTextAnimation({"wave", 1.5, TextUnit::Character, 0.0}, TextAnimationRole::Loop));
    CHECK(validateTextAnimation({"twirl", 1.0, TextUnit::Word, 0.5}, TextAnimationRole::In).value()
          == "unknown text animation \"twirl\"");
    CHECK(validateTextAnimation({"wave", 1.0, TextUnit::Word, 0.5}, TextAnimationRole::Out).value()
          == "\"wave\" is a loop, not an in or out animation");
    CHECK(validateTextAnimation({"fade", 1.0, TextUnit::Word, 0.5}, TextAnimationRole::Loop).value() == "\"fade\" is not a loop");
    CHECK(validateTextAnimation({"fade", 0.0, TextUnit::Word, 0.5}, TextAnimationRole::In).value()
          == "text animation duration is out of range");
    CHECK(validateTextAnimation({"fade", 1.0, TextUnit::Word, 1.5}, TextAnimationRole::In).value()
          == "text animation stagger must be between 0 and 1");
}

TEST_CASE("units arrive one after another and leave as the clip ends", "[titles]")
{
    const TextAnimation in{"fade", 1.0, TextUnit::Character, 1.0}; // fully sequential
    // Four units of a quarter second each.
    CHECK(animateTextUnit(in, TextAnimationRole::In, 0, 4, 0.0, 5.0).opacity == 0.0);
    CHECK(animateTextUnit(in, TextAnimationRole::In, 0, 4, 0.25, 5.0).identity());
    CHECK(animateTextUnit(in, TextAnimationRole::In, 1, 4, 0.25, 5.0).opacity == 0.0);
    CHECK(animateTextUnit(in, TextAnimationRole::In, 3, 4, 0.8, 5.0).opacity > 0.0);
    CHECK(animateTextUnit(in, TextAnimationRole::In, 3, 4, 0.8, 5.0).opacity < 1.0);
    CHECK(animateTextUnit(in, TextAnimationRole::In, 3, 4, 1.0, 5.0).identity());
    // No stagger: all together.
    const TextAnimation together{"rise", 1.0, TextUnit::Character, 0.0};
    CHECK(animateTextUnit(together, TextAnimationRole::In, 0, 4, 0.5, 5.0)
          == animateTextUnit(together, TextAnimationRole::In, 3, 4, 0.5, 5.0));
    CHECK(animateTextUnit(together, TextAnimationRole::In, 0, 4, 0.0, 5.0).dy == Catch::Approx(0.6));

    // Out: untouched until its window, gone at the end; the first unit leaves first.
    const TextAnimation out{"fade", 1.0, TextUnit::Word, 1.0};
    CHECK(animateTextUnit(out, TextAnimationRole::Out, 0, 2, 3.9, 5.0).identity());
    CHECK(animateTextUnit(out, TextAnimationRole::Out, 0, 2, 5.0, 5.0).opacity == 0.0);
    CHECK(animateTextUnit(out, TextAnimationRole::Out, 1, 2, 5.0, 5.0).opacity == 0.0);
    CHECK(animateTextUnit(out, TextAnimationRole::Out, 0, 2, 4.5, 5.0).opacity == 0.0);
    CHECK(animateTextUnit(out, TextAnimationRole::Out, 1, 2, 4.5, 5.0).opacity == 1.0);

    // Typewriter: each character appears at once, in turn.
    const TextAnimation typed{"typewriter", 1.0, TextUnit::Character, 0.6};
    CHECK(animateTextUnit(typed, TextAnimationRole::In, 0, 10, 0.0, 5.0).identity());
    CHECK(animateTextUnit(typed, TextAnimationRole::In, 5, 10, 0.45, 5.0).opacity == 0.0);
    CHECK(animateTextUnit(typed, TextAnimationRole::In, 5, 10, 0.55, 5.0).identity());

    // Shapes of motion: a pop overshoots, a wipe reveals, tracking spreads from the middle.
    const TextAnimation pop{"pop", 1.0, TextUnit::Whole, 0.0};
    double biggest = 0.0;
    for (double t = 0.0; t < 1.0; t += 0.05)
        biggest = std::max(biggest, animateTextUnit(pop, TextAnimationRole::In, 0, 1, t, 5.0).scale);
    CHECK(biggest > 1.05);
    CHECK(animateTextUnit({"wipe", 1.0, TextUnit::Whole, 0.0}, TextAnimationRole::In, 0, 1, 0.0, 5.0).reveal == 0.0);
    CHECK(animateTextUnit({"tracking", 1.0, TextUnit::Character, 0.0}, TextAnimationRole::In, 0, 5, 0.0, 5.0).dx < 0.0);
    CHECK(animateTextUnit({"tracking", 1.0, TextUnit::Character, 0.0}, TextAnimationRole::In, 4, 5, 0.0, 5.0).dx > 0.0);
    CHECK(animateTextUnit({"tracking", 1.0, TextUnit::Character, 0.0}, TextAnimationRole::In, 2, 5, 0.0, 5.0).dx == 0.0);

    // Loops repeat every cycle and differ between units.
    const TextAnimation wave{"wave", 2.0, TextUnit::Character, 0.0};
    CHECK(animateTextUnit(wave, TextAnimationRole::Loop, 1, 5, 0.3, 5.0).dy
          == Catch::Approx(animateTextUnit(wave, TextAnimationRole::Loop, 1, 5, 2.3, 5.0).dy).margin(1e-9));
    CHECK(animateTextUnit(wave, TextAnimationRole::Loop, 1, 5, 0.3, 5.0).dy
          != Catch::Approx(animateTextUnit(wave, TextAnimationRole::Loop, 2, 5, 0.3, 5.0).dy));
}

TEST_CASE("title shapes, animations and scrolling are checked and saved", "[titles]")
{
    ClipText text;
    CHECK_FALSE(text.animated());
    text.shape = {TitleShapeKind::Speech, {1, 1, 1, 0.9}, 0.6, 0.8};
    text.animationIn = {"bounce", 1.2, TextUnit::Word, 0.4};
    text.animationOut = {"fade", 0.5, TextUnit::Whole, 0.0};
    text.animationLoop = {"swing", 2.0, TextUnit::Whole, 0.0};
    text.scroll = TextScroll::CrawlLeft;
    CHECK(text.animated());
    CHECK_FALSE(validateText(text));

    TitleProject p;
    const Id title = p.push<AddTitleCommand>(kNoId, 0, 5 * kSec, text).clipId();
    nlohmann::json json = projectToJson(p.project);
    nlohmann::json& saved = json["tracks"].back()["clips"][0]["text"];
    CHECK(saved["shape"]["kind"] == "speech");
    CHECK(saved["animationIn"]["unit"] == "word");
    CHECK(saved["scroll"] == "crawlLeft");
    CHECK(*projectFromJson(json).locateClip(title)->clip->text == text);
    // Older titles have none of it.
    for (const char* field : {"shape", "animationIn", "animationOut", "animationLoop", "scroll"})
        saved.erase(field);
    Project old = projectFromJson(json);
    const ClipText plain = *old.locateClip(title)->clip->text;
    CHECK(plain.shape.kind == TitleShapeKind::None);
    CHECK_FALSE(plain.animated());

    ClipText bad = text;
    bad.shape.padding = 3.0;
    CHECK(validateText(bad).value() == "shape padding is out of range");
    bad = text;
    bad.shape.color.a = 1.5;
    CHECK(validateText(bad).value() == "colours must be between 0 and 1");
    bad = text;
    bad.animationLoop.preset = "pop";
    CHECK(validateText(bad).value() == "\"pop\" is not a loop");
}
