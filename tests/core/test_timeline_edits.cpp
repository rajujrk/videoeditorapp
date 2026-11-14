#include "commands/TimelineEdits.h"
#include "io/ProjectSerializer.h"

#include <nlohmann/json.hpp>
#include "model/Keyframes.h"
#include "model/TimelineQuery.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace tve;
using Catch::Approx;

namespace {

constexpr Ticks kSec = kTicksPerSecond;

// Two videos (10 s and 3 s), a song and a photo; a picture track with clip A (0–4 s of the
// first video from 2 s) then B (4–8 s from 0), a sound track with the song, a title track.
struct Fixture {
    Project project;
    Id video = 0, shortVideo = 0, song = 0, photo = 0;
    Id main = 0, sound = 0, titles = 0;
    Id a = 0, b = 0, music = 0;

    Fixture()
    {
        const auto media = [&](const char* path, Ticks duration, bool video, bool audio, bool still = false) {
            MediaItem m{.id = project.allocateId(), .path = path, .duration = duration, .hasVideo = video, .hasAudio = audio,
                        .width = 1920, .height = 1080, .still = still};
            project.media.push_back(m);
            return m.id;
        };
        video = media("a.mov", 10 * kSec, true, true);
        shortVideo = media("b.mov", 3 * kSec, true, true);
        song = media("song.wav", 60 * kSec, false, true);
        photo = media("photo.png", kStillDuration, true, false, true);
        const auto track = [&](TrackKind kind, const char* name) {
            Track t{.id = project.allocateId(), .kind = kind, .name = name};
            project.tracks.push_back(t);
            return t.id;
        };
        sound = track(TrackKind::Audio, "Audio 1");
        main = track(TrackKind::Video, "Video 1");
        titles = track(TrackKind::Title, "Titles");
        const auto clip = [&](Id trackId, Id mediaId, Ticks start, Ticks duration, Ticks in) {
            Clip c{.id = project.allocateId(), .mediaId = mediaId, .start = start, .duration = duration, .sourceIn = in};
            project.findTrack(trackId)->insertClip(c);
            return c.id;
        };
        a = clip(main, video, 0, 4 * kSec, 2 * kSec);
        b = clip(main, video, 4 * kSec, 4 * kSec, 0);
        music = clip(sound, song, 0, 20 * kSec, 0);
    }
    Clip& clip(Id id) { return *project.locateClip(id)->clip; }
};

} // namespace

TEST_CASE("Replacing a clip's media keeps its place, length and settings", "[timeline-edits]")
{
    Fixture f;
    f.clip(f.b).visual.opacity = 0.5;
    f.clip(f.b).effects.push_back({.effectId = "tve.blur"});
    f.clip(f.b).transitionIn = ClipTransition{"tve.transition.dissolve"};
    f.clip(f.b).sourceIn = 5 * kSec;
    bool shortened = true;
    // A photo: from its start, the same length.
    REQUIRE_FALSE(replaceClipMedia(f.project, f.b, f.photo, &shortened));
    CHECK_FALSE(shortened);
    const Clip& b = f.clip(f.b);
    CHECK(b.mediaId == f.photo);
    CHECK(b.start == 4 * kSec);
    CHECK(b.duration == 4 * kSec);
    CHECK(b.sourceIn == 0);
    CHECK(b.visual.opacity == 0.5);
    CHECK(b.effects.size() == 1);
    CHECK(b.transitionIn.has_value());

    // A video long enough from the same point keeps it; one too short there plays as late as fits.
    Fixture g;
    g.clip(g.a).sourceIn = 1 * kSec;
    REQUIRE_FALSE(replaceClipMedia(g.project, g.a, g.video, nullptr)); // the same media: nothing to do
    g.project.media.push_back({.id = 99, .path = "c.mov", .duration = 6 * kSec, .hasVideo = true});
    REQUIRE_FALSE(replaceClipMedia(g.project, g.a, 99, &shortened));
    CHECK(g.clip(g.a).sourceIn == 1 * kSec);
    g.clip(g.a).mediaId = g.video;
    g.clip(g.a).sourceIn = 5 * kSec;
    REQUIRE_FALSE(replaceClipMedia(g.project, g.a, 99, &shortened));
    CHECK(g.clip(g.a).sourceIn == 2 * kSec); // 6 s of media, 4 s needed
    CHECK(g.clip(g.a).duration == 4 * kSec);

    // Too short altogether: the clip is shortened.
    Fixture h;
    REQUIRE_FALSE(replaceClipMedia(h.project, h.a, h.shortVideo, &shortened));
    CHECK(shortened);
    CHECK(h.clip(h.a).duration == 3 * kSec);
    CHECK(h.clip(h.a).start == 0);

    // Pictures on picture tracks, sound on sound tracks.
    CHECK(replaceClipMedia(h.project, h.a, h.song, nullptr));
    CHECK(replaceClipMedia(h.project, h.music, h.photo, nullptr));
    REQUIRE_FALSE(replaceClipMedia(h.project, h.music, h.video, nullptr)); // a video's sound will do
    h.project.findTrack(h.main)->locked = true;
    CHECK(replaceClipMedia(h.project, h.b, h.photo, nullptr));
}

TEST_CASE("A clip turned off is neither seen nor heard, and is kept", "[timeline-edits]")
{
    Fixture f;
    f.clip(f.b).transitionIn = ClipTransition{"tve.transition.dissolve", kSec};
    REQUIRE(visibleVideoLayersAt(f.project, 5 * kSec).size() == 1);
    REQUIRE(transitionInto(*f.project.findTrack(f.main), f.clip(f.b)));
    REQUIRE_FALSE(setClipsEnabled(f.project, {f.b}, false));
    CHECK(visibleVideoLayersAt(f.project, 5 * kSec).empty());
    CHECK_FALSE(visibleVideoAt(f.project, 5 * kSec));
    CHECK_FALSE(transitionInto(*f.project.findTrack(f.main), f.clip(f.b))); // no transition into what is off
    CHECK(visibleVideoLayersAt(f.project, 3 * kSec).size() == 1);           // A still plays
    CHECK(f.project.findTrack(f.main)->clips.size() == 2);

    // Saved and opened again.
    Project copy = projectFromJson(projectToJson(f.project));
    CHECK_FALSE(copy.locateClip(f.b)->clip->enabled);
    CHECK(copy.locateClip(f.a)->clip->enabled);

    REQUIRE_FALSE(setClipsEnabled(f.project, {f.a, f.b}, true));
    CHECK(visibleVideoLayersAt(f.project, 5 * kSec).size() == 1);
    CHECK(setClipsEnabled(f.project, {}, true));
}

TEST_CASE("Extending an edit rolls a cut, or trims a free edge, to the playhead", "[timeline-edits]")
{
    Fixture f;
    // The cut between A and B (at 4 s) rolled later to 5 s: A runs on into its media, B starts later in its.
    REQUIRE_FALSE(extendEdit(f.project, f.a, 5 * kSec));
    CHECK(f.clip(f.a).duration == 5 * kSec);
    CHECK(f.clip(f.b).start == 5 * kSec);
    CHECK(f.clip(f.b).duration == 3 * kSec);
    CHECK(f.clip(f.b).sourceIn == 1 * kSec);
    // And back to 4 s, from B's side.
    REQUIRE_FALSE(extendEdit(f.project, f.b, 4 * kSec));
    CHECK(f.clip(f.a).duration == 4 * kSec);
    CHECK(f.clip(f.b).start == 4 * kSec);
    CHECK(f.clip(f.b).sourceIn == 0);
    // B can't start earlier than its media (the attempt is thrown away).
    Project tried = f.project;
    CHECK(extendEdit(tried, f.b, 3 * kSec));
    // B's free end trimmed out to 13 s (its media has 10 s from 0, from 4 s on the timeline).
    REQUIRE_FALSE(extendEdit(f.project, f.b, 13 * kSec));
    CHECK(f.clip(f.b).end() == 13 * kSec);
    tried = f.project;
    CHECK(extendEdit(tried, f.b, 15 * kSec)); // past its media (it ends at 14 s)
    // Not emptying the clip on the other side of a cut.
    tried = f.project;
    CHECK(extendEdit(tried, f.a, 13 * kSec));

    // The clip nearest the playhead.
    CHECK(nearestEdit(f.project, 13 * kSec + 10) == f.b);
    f.project.findTrack(f.main)->locked = true;
    CHECK(nearestEdit(f.project, 13 * kSec) == f.music); // only unlocked tracks
}

TEST_CASE("Clips from the playhead on, or before it, are found", "[timeline-edits]")
{
    Fixture f;
    auto forward = clipsFrom(f.project, 5 * kSec, 1);
    CHECK(forward.size() == 2); // B and the music
    auto backward = clipsFrom(f.project, 5 * kSec, -1);
    CHECK(backward.size() == 3); // A, B (began before) and the music
    CHECK(clipsFrom(f.project, 5 * kSec, 1, f.main) == std::vector<Id>{f.b});
    f.project.findTrack(f.sound)->locked = true;
    CHECK(clipsFrom(f.project, 5 * kSec, 1) == std::vector<Id>{f.b});
}

TEST_CASE("The next and previous edits are every clip edge and the end", "[timeline-edits]")
{
    Fixture f;
    CHECK(nextEdit(f.project, 0, 1) == 4 * kSec);
    CHECK(nextEdit(f.project, 4 * kSec, 1) == 8 * kSec);
    CHECK(nextEdit(f.project, 8 * kSec, 1) == 20 * kSec); // the music's end
    CHECK_FALSE(nextEdit(f.project, 20 * kSec, 1));
    CHECK(nextEdit(f.project, 5 * kSec, -1) == 4 * kSec);
    CHECK(nextEdit(f.project, 4 * kSec, -1) == 0);
    CHECK_FALSE(nextEdit(f.project, 0, -1));
}

TEST_CASE("Tracks move among their kind, and lock all at once", "[timeline-edits]")
{
    Fixture f;
    // Pictures: Video 1 under Titles; up swaps them, past the top is refused.
    REQUIRE_FALSE(moveTrack(f.project, f.main, 1));
    CHECK(f.project.tracks[1].id == f.titles);
    CHECK(f.project.tracks[2].id == f.main);
    CHECK(moveTrack(f.project, f.main, 1));
    // Down skips nothing of its kind; the sound track is a group of its own.
    REQUIRE_FALSE(moveTrack(f.project, f.main, -1));
    CHECK(f.project.tracks[1].id == f.main);
    CHECK(moveTrack(f.project, f.main, -1));
    CHECK(moveTrack(f.project, f.sound, 1));
    CHECK(moveTrack(f.project, 12345, 1));

    CHECK(setAllTracksLocked(f.project, true) == 3);
    CHECK(setAllTracksLocked(f.project, true) == 0);
    CHECK(std::ranges::all_of(f.project.tracks, &Track::locked));
    CHECK(setAllTracksLocked(f.project, false) == 3);
}

TEST_CASE("Every transition, or every marker, goes at once", "[timeline-edits]")
{
    Fixture f;
    f.clip(f.b).transitionIn = ClipTransition{"tve.transition.dissolve"};
    Sequence inside{.id = f.project.allocateId(), .name = "Inside"};
    Track t{.id = f.project.allocateId(), .kind = TrackKind::Video};
    t.clips.push_back({.id = f.project.allocateId(), .mediaId = f.video, .duration = kSec, .transitionIn = ClipTransition{"tve.transition.wipe"}});
    inside.tracks.push_back(t);
    f.project.sequences.push_back(inside);
    f.project.markers.push_back({.id = f.project.allocateId(), .time = kSec});
    f.project.markers.push_back({.id = f.project.allocateId(), .time = 2 * kSec});
    CHECK(removeAllTransitions(f.project) == 2);
    CHECK_FALSE(f.clip(f.b).transitionIn);
    CHECK(removeAllTransitions(f.project) == 0);
    CHECK(removeAllMarkers(f.project) == 2);
    CHECK(f.project.markers.empty());
}

TEST_CASE("Rotating by right angles and flipping", "[timeline-edits]")
{
    Fixture f;
    REQUIRE_FALSE(rotateClip(f.project, f.a, 90));
    CHECK(f.clip(f.a).visual.transform.rotation == Approx(90));
    REQUIRE_FALSE(rotateClip(f.project, f.a, 90));
    REQUIRE_FALSE(rotateClip(f.project, f.a, 90));
    CHECK(f.clip(f.a).visual.transform.rotation == Approx(-90)); // 270° is a quarter turn back
    REQUIRE_FALSE(rotateClip(f.project, f.a, 90));
    CHECK(f.clip(f.a).visual.transform.rotation == Approx(0));
    REQUIRE_FALSE(rotateClip(f.project, f.a, 180));
    CHECK(f.clip(f.a).visual.transform.rotation == Approx(180));
    // Its rotation keyframes turn with it.
    f.clip(f.b).keyframes[std::string(property::kRotation)] = {{0, 10.0}, {kSec, 20.0}};
    REQUIRE_FALSE(rotateClip(f.project, f.b, -90));
    CHECK(f.clip(f.b).keyframes[std::string(property::kRotation)][1].value == Approx(-70));
    CHECK(rotateClip(f.project, f.music, 90)); // sound has no picture

    REQUIRE_FALSE(flipClip(f.project, f.a, true));
    CHECK(f.clip(f.a).visual.transform.flipH);
    REQUIRE_FALSE(flipClip(f.project, f.a, false));
    CHECK(f.clip(f.a).visual.transform.flipV);
    REQUIRE_FALSE(flipClip(f.project, f.a, true));
    CHECK_FALSE(f.clip(f.a).visual.transform.flipH);
    const Project reopened = projectFromJson(projectToJson(f.project));
    CHECK(reopened.tracks[1].clips[0].visual.transform.flipV);
}
