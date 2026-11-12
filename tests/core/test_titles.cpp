#include "commands/CommandStack.h"
#include "commands/EditCommands.h"
#include "io/ProjectSerializer.h"
#include "model/ClipAnimation.h"
#include "model/LogoReveal.h"
#include "model/TimelineQuery.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <limits>

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

TEST_CASE("title styles: fill, underline, vertical, curve and 3D are checked and saved", "[titles]")
{
    ClipText text;
    text.fill = TextFill{TextFillKind::Linear, {0.0, 0.0, 1.0, 1.0}, 45.0, {}};
    text.underline = true;
    text.vertical = true;
    text.curve = -0.4;
    text.depth = 0.2;
    text.depthColor = {0.5, 0.0, 0.0, 1.0};
    text.tiltX = 20;
    text.tiltY = -35;
    CHECK_FALSE(validateText(text));
    Project project;
    Clip clip{.id = 1, .duration = kTicksPerSecond};
    clip.text = text;
    project.tracks.push_back(Track{.id = 2, .kind = TrackKind::Title, .clips = {clip}});
    project.setNextId(3);
    nlohmann::json json = projectToJson(project);
    CHECK(projectFromJson(json).tracks == project.tracks);
    // An older title (none of them) loads as before.
    nlohmann::json& saved = json["tracks"][0]["clips"][0]["text"];
    for (const char* key : {"fill", "underline", "vertical", "curve", "depth", "depthColor", "tiltX", "tiltY"})
        saved.erase(key);
    const ClipText loaded = *projectFromJson(json).tracks.front().clips.front().text;
    CHECK(loaded.fill == TextFill{});
    CHECK_FALSE(loaded.underline);
    CHECK(loaded.depth == 0.0);

    ClipText bad = text;
    bad.curve = 1.5;
    CHECK(validateText(bad));
    bad = text;
    bad.depth = 0.8;
    CHECK(validateText(bad));
    bad = text;
    bad.tiltY = 70;
    CHECK(validateText(bad));
    bad = text;
    bad.fill.kind = TextFillKind::Image;
    CHECK(validateText(bad).value() == "choose a picture to fill the text with");
}

TEST_CASE("titles that move with the music: levels between measurements, checked and saved", "[titles]")
{
    ClipText text;
    CHECK_FALSE(text.animated());
    text.react.kind = TextReactKind::Pulse;
    CHECK_FALSE(text.react.active()); // not measured yet
    text.react.levels = {0.0f, 1.0f, 0.5f};
    CHECK(text.react.active());
    CHECK(text.animated());
    CHECK(text.react.levelAt(0.0) == Catch::Approx(0.0));
    CHECK(text.react.levelAt(0.5 / TextReact::kRate) == Catch::Approx(0.5));
    CHECK(text.react.levelAt(1.5 / TextReact::kRate) == Catch::Approx(0.75));
    CHECK(text.react.levelAt(2.0 / TextReact::kRate) == Catch::Approx(0.5));
    CHECK(text.react.levelAt(10.0) == 0.0); // after the measurements
    CHECK(text.react.levelAt(-1.0) == 0.0);
    CHECK_FALSE(validateText(text));

    ClipText bad = text;
    bad.react.amount = 1.5;
    CHECK(validateText(bad).value() == "how much it moves with the music must be between 0% and 100%");
    bad = text;
    bad.react.levels = {2.0f};
    CHECK(validateText(bad).value() == "music levels must be between 0 and 1");
}

TEST_CASE("chart titles: values checked, saved, and older titles have none", "[titles]")
{
    ClipText text;
    text.text = "Sales";
    text.chart = TextChart{.kind = ChartKind::Line, .points = {{"Jan", 3.0}, {"Feb", -1.5}, {"Mar", 7.0}},
                           .color = {0.1, 0.8, 0.3, 1.0}, .grow = 2.0, .values = false};
    CHECK(text.animated()); // it grows in
    CHECK_FALSE(validateText(text));
    Project project;
    Clip clip{.id = 1, .duration = kTicksPerSecond};
    clip.text = text;
    project.tracks.push_back(Track{.id = 2, .kind = TrackKind::Title, .clips = {clip}});
    project.setNextId(3);
    nlohmann::json json = projectToJson(project);
    CHECK(projectFromJson(json).tracks == project.tracks);
    nlohmann::json& saved = json["tracks"][0]["clips"][0]["text"];
    saved.erase("chart");
    saved.erase("react");
    const ClipText loaded = *projectFromJson(json).tracks.front().clips.front().text;
    CHECK_FALSE(loaded.chart);
    CHECK(loaded.react == TextReact{});

    ClipText still = text;
    still.chart->grow = 0.0;
    CHECK_FALSE(still.animated());
    ClipText bad = text;
    bad.chart->points.clear();
    CHECK(validateText(bad).value() == "a chart needs at least one value");
    bad = text;
    bad.chart->points.resize(TextChart::kMaxPoints + 1);
    CHECK(validateText(bad).value() == "a chart can show at most 50 values");
    bad = text;
    bad.chart->points[0].value = std::numeric_limits<double>::infinity();
    CHECK(validateText(bad).value() == "chart values must be numbers");
    bad = text;
    bad.chart->points[0].label = std::string(TextChart::kMaxLabel + 1, 'x');
    CHECK(validateText(bad).value() == "a chart label is too long");
    bad = text;
    bad.chart->grow = 31.0;
    CHECK(validateText(bad).value() == "a chart grows in over at most 30 seconds");
    bad = text;
    bad.chart->color.g = 1.2;
    CHECK(validateText(bad).value() == "colours must be between 0 and 1");
}

TEST_CASE("logo reveals are built from the app's own animations", "[titles]")
{
    const auto& presets = logoRevealPresets();
    REQUIRE(presets.size() >= 6);
    for (const LogoRevealPreset& preset : presets) {
        INFO(preset.id);
        CHECK(findLogoReveal(preset.id) == &preset);
        CHECK(std::ranges::count_if(presets, [&](const LogoRevealPreset& p) { return std::string_view(p.id) == preset.id; }) == 1);
        // The logo's animations apply to a picture clip.
        Clip logo{.id = 1, .mediaId = 2, .duration = fromSeconds(kLogoRevealLength)};
        CHECK_FALSE(applyClipAnimation(logo, preset.logoIn, fromSeconds(preset.inSeconds)));
        CHECK_FALSE(applyClipAnimation(logo, preset.logoOut, fromSeconds(preset.outSeconds)));
        CHECK_FALSE(logo.keyframes.empty());
        // The tagline's animation is a real one that arrives.
        ClipText tagline;
        tagline.animationIn = {.preset = preset.taglineIn, .duration = preset.taglineSeconds, .unit = preset.taglineUnit,
                               .stagger = preset.taglineStagger};
        CHECK_FALSE(validateText(tagline));
        CHECK(preset.taglineDelay < kLogoRevealLength / 2);
    }
    CHECK(findLogoReveal("nope") == nullptr);
}

TEST_CASE("audio visualisers: levels between frames, checked, and saved compactly", "[titles]")
{
    ClipText text;
    text.text = "On air";
    TextVisualiser v;
    v.style = VisualiserStyle::Ring;
    v.size = 0.6;
    CHECK_FALSE(validateText(ClipText{.visualiser = v}));
    text.visualiser = v;
    CHECK_FALSE(text.animated()); // not measured yet
    // Every byte value, over a few frames.
    constexpr auto bands = std::size_t(TextVisualiser::kBands);
    for (std::size_t i = 0; i < bands * 9; ++i)
        text.visualiser->frames.push_back(std::uint8_t((i * 7) % 256));
    text.visualiser->frames[0] = 0;
    text.visualiser->frames[bands] = 255; // band 0: 0 then full
    CHECK(text.animated());
    CHECK(text.visualiser->frameCount() == 9);
    CHECK(text.visualiser->levelAt(0.0, 0) == 0.0);
    CHECK(text.visualiser->levelAt(0.5 / TextVisualiser::kRate, 0) == Catch::Approx(0.5));
    CHECK(text.visualiser->levelAt(1.0 / TextVisualiser::kRate, 0) == Catch::Approx(1.0));
    CHECK(text.visualiser->levelAt(10.0, 0) == 0.0); // past the measurements
    CHECK(text.visualiser->levelAt(0.0, TextVisualiser::kBands) == 0.0);
    CHECK_FALSE(validateText(text));

    Project project;
    Clip clip{.id = 1, .duration = kTicksPerSecond};
    clip.text = text;
    project.tracks.push_back(Track{.id = 2, .kind = TrackKind::Title, .clips = {clip}});
    project.setNextId(3);
    nlohmann::json json = projectToJson(project);
    const nlohmann::json& saved = json["tracks"][0]["clips"][0]["text"]["visualiser"];
    CHECK(saved["frames"].is_string()); // base64, not an array of numbers
    CHECK(saved["style"] == "ring");
    CHECK(projectFromJson(json).tracks == project.tracks);
    json["tracks"][0]["clips"][0]["text"].erase("visualiser");
    CHECK_FALSE(projectFromJson(json).tracks.front().clips.front().text->visualiser);

    ClipText bad = text;
    bad.visualiser->size = 0.05;
    CHECK(validateText(bad).value() == "a visualiser's size must be between 10% and 100%");
    bad = text;
    bad.visualiser->frames.pop_back();
    CHECK(validateText(bad).value() == "a visualiser's levels are damaged");
    bad = text;
    bad.visualiser->peak.r = 2.0;
    CHECK(validateText(bad).value() == "colours must be between 0 and 1");
    bad = text;
    bad.chart = TextChart{.points = {{"A", 1.0}}};
    CHECK(validateText(bad).value() == "a title is a chart or a visualiser, not both");
}
