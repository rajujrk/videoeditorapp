#include "io/Interchange.h"

#include <nlohmann/json.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/catch_test_macros.hpp>

#include <set>

using namespace tve;
using Catch::Approx;

namespace {

constexpr Ticks kSec = kTicksPerSecond;

// Main track: A (0–4 s, from 1 s in), B at double speed (4–7 s) dissolving in over 1 s
// centred, a gap, A reversed (8–9 s). Above it a title; below, a sound-only clip. Markers.
Project sample()
{
    Project p;
    p.settings.width = 1280;
    p.settings.height = 720;
    p.media.push_back({.id = 1, .path = "/media/a b.mov", .duration = 10 * kSec, .hasVideo = true, .hasAudio = true, .width = 1280, .height = 720});
    p.media.push_back({.id = 2, .path = "/media/b.mp4", .duration = 20 * kSec, .hasVideo = true, .hasAudio = true});
    p.media.push_back({.id = 3, .path = "/media/music.wav", .duration = 60 * kSec, .hasVideo = false, .hasAudio = true});
    Track audio{.id = 10, .kind = TrackKind::Audio, .name = "Music"};
    audio.clips.push_back(Clip{.id = 20, .mediaId = 3, .start = 1 * kSec, .duration = 3 * kSec, .sourceIn = 5 * kSec});
    Track main{.id = 11, .kind = TrackKind::Video, .name = "Video 1"};
    main.clips.push_back(Clip{.id = 21, .mediaId = 1, .start = 0, .duration = 4 * kSec, .sourceIn = 1 * kSec});
    Clip fast{.id = 22, .mediaId = 2, .start = 4 * kSec, .duration = 3 * kSec, .sourceIn = 2 * kSec};
    fast.speed = 2.0;
    fast.transitionIn = ClipTransition{.transitionId = "tve.transition.dissolve", .duration = 1 * kSec};
    fast.color.exposure = 0.5; // (not carried)
    main.clips.push_back(fast);
    Clip back{.id = 23, .mediaId = 1, .start = 8 * kSec, .duration = 1 * kSec, .sourceIn = 3 * kSec};
    back.reversed = true;
    main.clips.push_back(back);
    Track upper{.id = 12, .kind = TrackKind::Video, .name = "Video 2"};
    Clip title{.id = 24, .start = 2 * kSec, .duration = 3 * kSec};
    title.text = ClipText{};
    title.text->text = "Hello & <welcome>";
    upper.clips.push_back(title);
    p.tracks = {audio, main, upper};
    p.markers.push_back({.id = 30, .time = 3 * kSec, .color = MarkerColor::Red, .note = "Look here"});
    p.markers.push_back({.id = 31, .time = 6 * kSec, .duration = kSec, .color = MarkerColor::Green, .note = "Part 2", .chapter = true});
    p.setNextId(100);
    return p;
}

const Track& trackNamed(const Project& p, const std::string& name)
{
    const auto it = std::ranges::find(p.tracks, name, &Track::name);
    REQUIRE(it != p.tracks.end());
    return *it;
}

const MediaItem& mediaOf(const Project& p, const Clip& clip)
{
    const MediaItem* media = p.findMedia(clip.mediaId);
    REQUIRE(media);
    return *media;
}

bool mentions(const std::vector<std::string>& notes, const std::string& text)
{
    return std::ranges::any_of(notes, [&](const std::string& n) { return n.find(text) != std::string::npos; });
}

// What both formats carry, checked on a timeline read back.
void carriesTheSample(const ImportedTimeline& back, bool markerColours)
{
    const Project& p = back.project;
    CHECK(p.settings.width == 1280);
    CHECK(p.settings.height == 720);
    CHECK(p.settings.frameRate == FrameRate{30, 1});
    const Track& main = trackNamed(p, "Video 1");
    REQUIRE(main.clips.size() == 3);
    const Clip& a = main.clips[0];
    CHECK(mediaOf(p, a).path == "/media/a b.mov");
    CHECK(a.start == 0);
    CHECK(a.duration == 4 * kSec);
    CHECK(a.sourceIn == 1 * kSec);
    CHECK(a.audioEnabled); // its sound plays with it (no doubled sound)
    const Clip& b = main.clips[1];
    CHECK(mediaOf(p, b).path == "/media/b.mp4");
    CHECK(b.start == 4 * kSec);
    CHECK(b.duration == 3 * kSec);
    CHECK(b.speed == Approx(2.0));
    CHECK(b.sourceIn == 2 * kSec);
    REQUIRE(b.transitionIn);
    CHECK(b.transitionIn->transitionId == "tve.transition.dissolve");
    CHECK(b.transitionIn->duration == 1 * kSec);
    CHECK(b.transitionIn->alignment == TransitionAlignment::Center);
    const Clip& r = main.clips[2];
    CHECK(r.start == 8 * kSec);
    CHECK(r.reversed);
    CHECK(r.sourceIn == 3 * kSec);
    // The title, above.
    const auto upper = std::ranges::find_if(p.tracks, [&](const Track& t) {
        return t.kind == TrackKind::Video && t.id != main.id && !t.clips.empty();
    });
    REQUIRE(upper != p.tracks.end());
    REQUIRE(upper->clips.size() == 1);
    REQUIRE(upper->clips[0].text);
    CHECK(upper->clips[0].text->text == "Hello & <welcome>");
    CHECK(upper->clips[0].start == 2 * kSec);
    CHECK(upper->clips[0].duration == 3 * kSec);
    // The sound, below.
    const auto audio = std::ranges::find_if(p.tracks, [](const Track& t) { return t.kind == TrackKind::Audio && !t.clips.empty(); });
    REQUIRE(audio != p.tracks.end());
    REQUIRE(audio->clips.size() == 1);
    CHECK(mediaOf(p, audio->clips[0]).path == "/media/music.wav");
    CHECK(audio->clips[0].start == 1 * kSec);
    CHECK(audio->clips[0].sourceIn == 5 * kSec);
    // Media: each file once, long enough for its clips.
    CHECK(p.media.size() == 3);
    CHECK(mediaOf(p, b).duration >= 2 * kSec + 6 * kSec);
    // Markers.
    REQUIRE(p.markers.size() == 2);
    CHECK(p.markers[0].time == 3 * kSec);
    CHECK(p.markers[0].note == "Look here");
    CHECK(p.markers[1].chapter);
    CHECK(p.markers[1].note == "Part 2");
    if (markerColours) {
        CHECK(p.markers[0].color == MarkerColor::Red);
        CHECK(p.markers[1].color == MarkerColor::Green);
        CHECK(p.markers[1].duration == kSec);
    }
    // Every id unique, and the project's next id beyond them.
    std::set<Id> ids;
    for (const Track& t : p.tracks) {
        ids.insert(t.id);
        for (const Clip& c : t.clips)
            ids.insert(c.id);
    }
    for (const MediaItem& m : p.media)
        ids.insert(m.id);
    CHECK(*ids.rbegin() < p.nextId());
}

} // namespace

TEST_CASE("an OpenTimelineIO file carries clips, speed, dissolves, gaps, titles and markers both ways")
{
    const ExportedTimeline out = exportTimeline(sample(), InterchangeFormat::Otio, "Sample");
    CHECK(mentions(out.notes, "Colour grades and looks — 1 clip"));
    CHECK(mentions(out.notes, "Titles' look and animation"));

    // As OpenTimelineIO writes it.
    const auto json = nlohmann::json::parse(out.text);
    CHECK(json["OTIO_SCHEMA"] == "Timeline.1");
    CHECK(json["name"] == "Sample");
    const auto& tracks = json["tracks"]["children"];
    REQUIRE(tracks.size() == 4); // two video, the music, and the main track's own sound
    CHECK(tracks[0]["kind"] == "Video");
    CHECK(tracks[2]["kind"] == "Audio");
    const auto& main = tracks[0]["children"];
    CHECK(main[0]["OTIO_SCHEMA"] == "Clip.2");
    CHECK(main[0]["media_references"]["DEFAULT_MEDIA"]["target_url"] == "file:///media/a%20b.mov");
    CHECK(main[0]["source_range"]["start_time"]["value"] == 30.0);
    CHECK(main[1]["OTIO_SCHEMA"] == "Transition.1");
    CHECK(main[1]["transition_type"] == "SMPTE_Dissolve");
    CHECK(main[1]["in_offset"]["value"] == 15.0);
    CHECK(main[1]["out_offset"]["value"] == 15.0);
    CHECK(main[2]["effects"][0]["OTIO_SCHEMA"] == "LinearTimeWarp.1");
    CHECK(main[2]["effects"][0]["time_scalar"] == 2.0);
    CHECK(main[3]["OTIO_SCHEMA"] == "Gap.1");
    CHECK(main[3]["source_range"]["duration"]["value"] == 30.0);
    CHECK(main[4]["effects"][0]["time_scalar"] == -1.0);
    CHECK(tracks[1]["children"][1]["media_references"]["DEFAULT_MEDIA"]["OTIO_SCHEMA"] == "GeneratorReference.1");
    CHECK(json["tracks"]["markers"][0]["color"] == "RED");

    const ImportedTimeline back = importTimeline(out.text, InterchangeFormat::Otio, "/");
    CHECK(back.canvasStated);
    carriesTheSample(back, true);
}

TEST_CASE("another editor's OpenTimelineIO: linked sound, older clips, nested stacks and effects")
{
    const std::string text = R"({
      "OTIO_SCHEMA": "Timeline.1", "name": "From elsewhere",
      "global_start_time": {"OTIO_SCHEMA": "RationalTime.1", "rate": 23.976023976023978, "value": 86400.0},
      "tracks": {"OTIO_SCHEMA": "Stack.1", "children": [
        {"OTIO_SCHEMA": "Track.1", "kind": "Video", "name": "V1", "children": [
          {"OTIO_SCHEMA": "Clip.1", "name": "shot",
           "media_reference": {"OTIO_SCHEMA": "ExternalReference.1", "target_url": "footage/shot%201.mov"},
           "source_range": {"OTIO_SCHEMA": "TimeRange.1",
             "start_time": {"OTIO_SCHEMA": "RationalTime.1", "rate": 23.976023976023978, "value": 48},
             "duration": {"OTIO_SCHEMA": "RationalTime.1", "rate": 23.976023976023978, "value": 96}},
           "effects": [{"OTIO_SCHEMA": "Effect.1", "effect_name": "Blur"}],
           "markers": [{"OTIO_SCHEMA": "Marker.2", "name": "beat", "color": "CYAN",
             "marked_range": {"OTIO_SCHEMA": "TimeRange.1",
               "start_time": {"OTIO_SCHEMA": "RationalTime.1", "rate": 23.976023976023978, "value": 72},
               "duration": {"OTIO_SCHEMA": "RationalTime.1", "rate": 23.976023976023978, "value": 0}}}]},
          {"OTIO_SCHEMA": "Stack.1", "name": "nest", "children": [],
           "source_range": {"OTIO_SCHEMA": "TimeRange.1",
             "start_time": {"OTIO_SCHEMA": "RationalTime.1", "rate": 23.976023976023978, "value": 0},
             "duration": {"OTIO_SCHEMA": "RationalTime.1", "rate": 23.976023976023978, "value": 24}}},
          {"OTIO_SCHEMA": "Clip.1", "name": "after",
           "media_reference": {"OTIO_SCHEMA": "ExternalReference.1", "target_url": "file:///abs/after.mov"},
           "source_range": {"OTIO_SCHEMA": "TimeRange.1",
             "start_time": {"OTIO_SCHEMA": "RationalTime.1", "rate": 23.976023976023978, "value": 0},
             "duration": {"OTIO_SCHEMA": "RationalTime.1", "rate": 23.976023976023978, "value": 24}}}]},
        {"OTIO_SCHEMA": "Track.1", "kind": "Audio", "name": "A1", "children": [
          {"OTIO_SCHEMA": "Clip.1", "name": "shot",
           "media_reference": {"OTIO_SCHEMA": "ExternalReference.1", "target_url": "footage/shot%201.mov"},
           "source_range": {"OTIO_SCHEMA": "TimeRange.1",
             "start_time": {"OTIO_SCHEMA": "RationalTime.1", "rate": 23.976023976023978, "value": 48},
             "duration": {"OTIO_SCHEMA": "RationalTime.1", "rate": 23.976023976023978, "value": 96}}}]}]}
    })";
    const ImportedTimeline back = importTimeline(text, InterchangeFormat::Otio, "/projects/x");
    const Project& p = back.project;
    CHECK_FALSE(back.canvasStated);
    CHECK(p.settings.frameRate == FrameRate{24000, 1001});
    const Track& v1 = trackNamed(p, "V1");
    REQUIRE(v1.clips.size() == 2);
    CHECK(mediaOf(p, v1.clips[0]).path == "/projects/x/footage/shot 1.mov");
    CHECK(v1.clips[0].sourceIn == fromSeconds(48 / (24000.0 / 1001)));
    CHECK_FALSE(v1.clips[0].audioEnabled); // its sound is on A1
    // The nested stack is space: the next clip starts after it.
    CHECK(v1.clips[1].start == fromSeconds((96 + 24) / (24000.0 / 1001)));
    CHECK(mediaOf(p, v1.clips[1]).path == "/abs/after.mov");
    CHECK(trackNamed(p, "A1").clips.size() == 1);
    // The clip's marker, at its timeline time.
    REQUIRE(p.markers.size() == 1);
    CHECK(p.markers[0].time == fromSeconds((72 - 48) / (24000.0 / 1001)));
    CHECK(p.markers[0].note == "beat");
    CHECK(mentions(back.notes, "Nested sequences (left as space) — 1 clip"));
    CHECK(mentions(back.notes, "Effects (left out) — 1 clip"));

    CHECK_THROWS_WITH(importTimeline("{\"OTIO_SCHEMA\": \"Clip.2\"}", InterchangeFormat::Otio, "/"),
                      "it isn't an OpenTimelineIO timeline");
    CHECK_THROWS_WITH(importTimeline("not json", InterchangeFormat::Otio, "/"), "it isn't an OpenTimelineIO file (not JSON)");
}

TEST_CASE("Final Cut Pro XML carries clips, speed, dissolves, gaps, titles and markers both ways")
{
    const ExportedTimeline out = exportTimeline(sample(), InterchangeFormat::Fcpxml, "Sample");
    const std::string& x = out.text;
    CHECK(x.find("<fcpxml version=\"1.10\">") != std::string::npos);
    CHECK(x.find("frameDuration=\"1/30s\"") != std::string::npos);
    CHECK(x.find("src=\"file:///media/a%20b.mov\"") != std::string::npos);
    CHECK(x.find("<transition name=\"Cross Dissolve\" offset=\"7/2s\" duration=\"1s\">") != std::string::npos);
    CHECK(x.find("<gap name=\"Gap\" offset=\"7s\"") != std::string::npos);
    CHECK(x.find("lane=\"1\"") != std::string::npos);
    CHECK(x.find("lane=\"-1\"") != std::string::npos);
    CHECK(x.find("Hello &amp; &lt;welcome&gt;") != std::string::npos);
    CHECK(x.find("<chapter-marker") != std::string::npos);
    CHECK(x.find("<timeMap>") != std::string::npos);
    CHECK(mentions(out.notes, "Marker colours"));

    const ImportedTimeline back = importTimeline(out.text, InterchangeFormat::Fcpxml, "/");
    CHECK(back.canvasStated);
    carriesTheSample(back, false);
}

TEST_CASE("another editor's Final Cut Pro XML: timecode starts, compound clips, effects, nothing fetched")
{
    const std::string text = R"(<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE fcpxml [ <!ENTITY secret SYSTEM "file:///etc/passwd"> ]>
<fcpxml version="1.11">
  <resources>
    <format id="r1" frameDuration="1001/24000s" width="3840" height="2160"/>
    <asset id="r2" name="shot" start="3600s" duration="20s" hasVideo="1" hasAudio="1" format="r1">
      <media-rep kind="original-media" src="file:///footage/shot.mov"/>
    </asset>
    <media id="r3" name="Compound"><sequence format="r1"><spine/></sequence></media>
    <effect id="r4" name="Basic Title" uid=".../Basic Title.moti"/>
  </resources>
  <library><event name="E"><project name="Cut">
    <sequence format="r1" duration="30s" tcStart="3600s">
      <spine>
        <asset-clip ref="r2" offset="3600s" name="shot" start="3605s" duration="4s">
          <adjust-volume amount="-6dB"/>
          <title ref="r4" lane="1" offset="3606s" start="0s" duration="2s"><text><text-style>Hi &secret;</text-style></text></title>
          <marker start="3606s" duration="1001/24000s" value="cue"/>
        </asset-clip>
        <ref-clip ref="r3" offset="3604s" duration="2s"/>
        <asset-clip ref="r2" offset="3606s" start="3610s" duration="1s"/>
      </spine>
    </sequence>
  </project></event></library>
</fcpxml>)";
    const ImportedTimeline back = importTimeline(text, InterchangeFormat::Fcpxml, "/");
    const Project& p = back.project;
    CHECK(p.settings.width == 3840);
    CHECK(p.settings.frameRate == FrameRate{24000, 1001});
    const Track& main = trackNamed(p, "Video 1");
    REQUIRE(main.clips.size() == 2);
    CHECK(main.clips[0].start == 0);
    CHECK(main.clips[0].sourceIn == 5 * kSec); // the asset's own timecode start taken off
    CHECK(main.clips[1].start == 6 * kSec);    // after the compound clip's space
    CHECK(main.clips[1].sourceIn == 10 * kSec);
    // The connected title, in the clip's own time.
    const auto upper = std::ranges::find_if(p.tracks, [&](const Track& t) {
        return t.kind == TrackKind::Video && t.id != main.id && !t.clips.empty();
    });
    REQUIRE(upper != p.tracks.end());
    REQUIRE(upper->clips[0].text);
    CHECK(upper->clips[0].start == 1 * kSec);
    // The external entity wasn't fetched.
    CHECK(upper->clips[0].text->text.find("root") == std::string::npos);
    REQUIRE(p.markers.size() == 1);
    CHECK(p.markers[0].time == 1 * kSec);
    CHECK(p.markers[0].duration == 0); // a one-frame marker is a point
    CHECK(mentions(back.notes, "Compound clips (left as space)"));
    CHECK(mentions(back.notes, "Effects, transforms and volume (left out)"));

    CHECK_THROWS_WITH(importTimeline("<xmeml/>", InterchangeFormat::Fcpxml, "/"), "it isn't Final Cut Pro XML");
    CHECK_THROWS_WITH(importTimeline("<fcpxml version=\"1.10\"/>", InterchangeFormat::Fcpxml, "/"),
                      "it has no project with a timeline");
}

TEST_CASE("an EDL has the main track's events, dissolves, speed and markers")
{
    const ExportedTimeline out = exportTimeline(sample(), InterchangeFormat::Edl, "Sample");
    const std::string& e = out.text;
    CHECK(e.rfind("TITLE: Sample\nFCM: NON-DROP FRAME\n", 0) == 0);
    CHECK(e.find("001  AX       AA/V  C        00:00:01:00 00:00:04:15 01:00:00:00 01:00:03:15") != std::string::npos);
    CHECK(e.find("002  AX       AA/V  D    030 ") != std::string::npos);
    CHECK(e.find("M2   AX       060.0                00:00:01:00") != std::string::npos); // the dissolve event's source in
    CHECK(e.find("M2   AX       -30.0                00:00:03:00") != std::string::npos);
    CHECK(e.find("* FROM CLIP NAME: a b.mov\n* SOURCE FILE: /media/a b.mov") != std::string::npos);
    CHECK(e.find("* LOC: 01:00:03:00 RED Look here") != std::string::npos);
    CHECK(mentions(out.notes, "Tracks other than the main video track"));
    CHECK_THROWS(importTimeline(e, InterchangeFormat::Edl, "/"));
}

TEST_CASE("interchange formats by extension")
{
    CHECK(interchangeFormatOf("/a/b.OTIO") == InterchangeFormat::Otio);
    CHECK(interchangeFormatOf("cut.fcpxml") == InterchangeFormat::Fcpxml);
    CHECK(interchangeFormatOf("cut.edl") == InterchangeFormat::Edl);
    CHECK_FALSE(interchangeFormatOf("cut.xml"));
    CHECK(interchange::pathFromUrl("file://localhost/a%20b/c.mov", "") == "/a b/c.mov");
    CHECK(interchange::fileUrl("/a b/ü.mov") == "file:///a%20b/%C3%BC.mov");
}
