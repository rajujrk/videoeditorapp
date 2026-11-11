#include "commands/CommandStack.h"
#include "commands/EditCommands.h"
#include "commands/SequenceEdits.h"
#include "commands/StructureCommands.h"
#include "commands/TrackCommands.h"
#include "io/ProjectSerializer.h"

#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>
#include <set>

using namespace tve;

namespace {

constexpr Ticks kSec = kTicksPerSecond;

// Audio A1 (bottom), video V1 (main), video V2; two cameras and a recorder.
struct Timeline {
    Project project;
    CommandStack stack{project};
    Id audio = kNoId, main = kNoId, upper = kNoId;
    Id camA = kNoId, camB = kNoId, recorder = kNoId;

    Timeline()
    {
        audio = push<AddTrackCommand>(TrackKind::Audio, "Audio 1").trackId();
        main = push<AddTrackCommand>(TrackKind::Video, "Video 1").trackId();
        upper = push<AddTrackCommand>(TrackKind::Video, "Video 2").trackId();
        camA = push<ImportMediaCommand>(MediaItem{.path = "/shoot/Camera A.mp4", .duration = 60 * kSec, .hasVideo = true, .hasAudio = true}).mediaId();
        camB = push<ImportMediaCommand>(MediaItem{.path = "/shoot/Camera B.mp4", .duration = 60 * kSec, .hasVideo = true, .hasAudio = true}).mediaId();
        recorder = push<ImportMediaCommand>(MediaItem{.path = "/shoot/Recorder.wav", .duration = 60 * kSec, .hasVideo = false, .hasAudio = true}).mediaId();
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
    Id clip(Id track, Ticks start, Ticks duration, Id media)
    {
        return push<AddClipCommand>(track, media, start, duration, 0).clipId();
    }
    const Clip* get(Id id) { auto l = project.locateClip(id); return l ? l->clip : nullptr; }
};

} // namespace

TEST_CASE("a compound clip opens on the timeline and closes back, fitted to its new length", "[sequence]")
{
    Timeline t;
    const Id a = t.clip(t.main, 0, 4 * kSec, t.camA);
    const Id b = t.clip(t.main, 4 * kSec, 4 * kSec, t.camB);
    const Id after = t.clip(t.main, 8 * kSec, 2 * kSec, t.camA);
    auto& made = t.push<CreateCompoundCommand>(std::vector<Id>{a, b}, "Opening");
    const Id compound = made.clipId();
    const Project before = t.project;

    OpenedSequence open;
    REQUIRE_FALSE(openSequence(t.project, compound, &open));
    // The timeline is the compound clip's insides; what it came from waits in a holder.
    CHECK(t.project.duration() == 8 * kSec);
    CHECK(t.project.findSequence(open.holderId)->name == kHolderSequenceName);
    CHECK(t.project.sequenceInUse(open.sequenceId));
    // Saved as it is: the timeline as it was, the insides in their sequence.
    CHECK(withSequencesClosed(t.project, {open}).tracks == before.tracks);

    // Shorter inside (the second clip goes): the compound clip shortens with it.
    Track& inside = t.project.tracks.front();
    REQUIRE(inside.kind == TrackKind::Video);
    REQUIRE(inside.clips.size() == 2);
    inside.clips.pop_back();
    closeSequence(t.project, open);
    CHECK(t.project.findSequence(open.holderId) == nullptr);
    CHECK(t.get(compound)->duration == 4 * kSec);
    CHECK(t.get(after)->start == 8 * kSec); // the rest of the timeline as it was
    CHECK(t.project.findSequence(made.sequenceId())->duration() == 4 * kSec);

    // Longer inside: it grows, up to the next clip on its track.
    REQUIRE_FALSE(openSequence(t.project, compound, &open));
    t.project.tracks.front().insertClip(Clip{.id = t.project.allocateId(), .mediaId = t.camB, .start = 4 * kSec, .duration = 10 * kSec});
    closeSequence(t.project, open);
    CHECK(t.get(compound)->duration == 8 * kSec); // the clip at 8 s stops it

    // Only compound clips open.
    CHECK(openSequence(t.project, after, &open).value() == "only a compound clip opens on the timeline");
}

TEST_CASE("open compound clips nest, and the project saves as if they were closed", "[sequence]")
{
    Timeline t;
    const Id a = t.clip(t.main, 0, 4 * kSec, t.camA);
    const Id inner = t.push<CreateCompoundCommand>(std::vector<Id>{a}, "Inner").clipId();
    const Id outer = t.push<CreateCompoundCommand>(std::vector<Id>{inner}, "Outer").clipId();
    const Project before = t.project;
    std::vector<OpenedSequence> open(2);
    REQUIRE_FALSE(openSequence(t.project, outer, &open[0]));
    REQUIRE(t.project.tracks.front().clips.size() == 1);
    REQUIRE_FALSE(openSequence(t.project, t.project.tracks.front().clips.front().id, &open[1]));
    CHECK(t.project.tracks.front().clips.front().mediaId == t.camA);
    const Project saved = withSequencesClosed(t.project, open);
    CHECK(saved.tracks == before.tracks);
    CHECK(saved.sequences == before.sequences);
    // It loads as saved.
    CHECK(projectFromJson(projectToJson(saved)).tracks == before.tracks);
}

TEST_CASE("cameras lined up become a multicam clip whose angle is switched and cut to", "[multicam]")
{
    Timeline t;
    // Camera A from 1 s, camera B from 0 s (lined up), the recorder under them.
    const Id a = t.clip(t.main, 1 * kSec, 6 * kSec, t.camA);
    const Id b = t.clip(t.upper, 0, 5 * kSec, t.camB);
    const Id sound = t.clip(t.audio, 0, 8 * kSec, t.recorder);
    Project p = t.project;
    Id made = kNoId;
    CHECK(makeMulticam(p, {a}, &made).value() == "a multicam clip needs two or more picture clips (one per camera)");
    REQUIRE_FALSE(makeMulticam(p, {a, b, sound}, &made));
    const Clip multicam = *p.locateClip(made)->clip; // (a copy: p is replaced by each edit)
    CHECK(p.locateClip(made)->track->id == t.main); // the lowest picture track
    CHECK(multicam.start == 0);
    CHECK(multicam.duration == 8 * kSec);
    REQUIRE(multicam.angles.size() == 2);
    CHECK(multicam.angle() == 0);
    CHECK(p.findSequence(multicam.angles[0])->name == "Camera A");
    CHECK(p.findSequence(multicam.angles[1])->name == "Camera B");
    // Each angle: its camera where it was (relative to the start), the recorder under it.
    const Sequence& angleA = *p.findSequence(multicam.angles[0]);
    REQUIRE(angleA.tracks.size() == 2);
    CHECK(angleA.tracks[0].clips.front().start == 1 * kSec);
    CHECK(angleA.tracks[1].kind == TrackKind::Audio);
    CHECK(angleA.tracks[1].clips.front().mediaId == t.recorder);
    // The clips themselves are inside it now.
    CHECK_FALSE(p.locateClip(a));
    CHECK_FALSE(p.locateClip(sound));

    REQUIRE_FALSE(setAngle(p, made, 1));
    CHECK(p.locateClip(made)->clip->sequenceId == multicam.angles[1]);
    CHECK(setAngle(p, made, 2).value() == "the multicam clip has no angle 3");
    // Cut to camera A at 3 s: two parts, B then A.
    Id right = kNoId;
    REQUIRE_FALSE(cutToAngle(p, made, 3 * kSec, 0, &right));
    CHECK(p.locateClip(made)->clip->angle() == 1);
    CHECK(p.locateClip(right)->clip->angle() == 0);
    CHECK(p.locateClip(right)->clip->start == 3 * kSec);
    CHECK(p.locateClip(right)->clip->sourceIn == 3 * kSec);
    // At a part's start: the whole part switches.
    REQUIRE_FALSE(cutToAngle(p, right, 3 * kSec, 1, nullptr));
    CHECK(p.locateClip(right)->clip->angle() == 1);
    // Angles survive saving.
    const Project loaded = projectFromJson(projectToJson(p));
    CHECK(loaded.tracks == p.tracks);

    // Breaking one part apart keeps the angles the other part still shows.
    BreakCompoundCommand broken(made);
    REQUIRE_FALSE(broken.redo(p));
    CHECK(p.findSequence(multicam.angles[0]) != nullptr);
    CHECK(p.findSequence(multicam.angles[1]) != nullptr);
}

TEST_CASE("another project comes in as a compound clip, its ids renumbered", "[sequence]")
{
    Timeline t;
    t.clip(t.main, 0, 3 * kSec, t.camA);
    // The other project: camera A (the same file) and a file of its own, and a compound clip.
    Timeline other;
    const Id x = other.clip(other.main, 0, 2 * kSec, other.camB);
    const Id y = other.clip(other.main, 2 * kSec, 2 * kSec, other.camA);
    other.push<CreateCompoundCommand>(std::vector<Id>{y}, "Inner");
    other.clip(other.upper, 0, 1 * kSec, other.camA);
    Project p = t.project;
    const std::size_t mediaBefore = p.media.size();
    Id made = kNoId;
    REQUIRE_FALSE(importProjectAsCompound(p, other.project, "Trip", 1 * kSec, &made));
    // The same files are not imported twice.
    CHECK(p.media.size() == mediaBefore);
    const auto location = p.locateClip(made);
    REQUIRE(location);
    CHECK(location->track->id != t.main); // no room on the main track at 1 s: the one above
    CHECK(location->clip->start == 1 * kSec);
    CHECK(location->clip->duration == 4 * kSec);
    const Sequence* trip = p.findSequence(location->clip->sequenceId);
    REQUIRE(trip);
    CHECK(trip->name == "Trip");
    // Every id is unique in the project.
    std::set<Id> ids;
    std::size_t count = 0;
    const auto collect = [&](const std::vector<Track>& tracks) {
        for (const Track& track : tracks) {
            ids.insert(track.id);
            ++count;
            for (const Clip& clip : track.clips) {
                ids.insert(clip.id);
                ++count;
                if (clip.compound())
                    CHECK(p.findSequence(clip.sequenceId) != nullptr);
                if (clip.mediaId != kNoId)
                    CHECK(p.findMedia(clip.mediaId) != nullptr);
            }
        }
    };
    collect(p.tracks);
    for (const Sequence& s : p.sequences) {
        ids.insert(s.id);
        ++count;
        collect(s.tracks);
    }
    CHECK(ids.size() == count);
    CHECK(*ids.rbegin() < p.nextId());
    // Its first clip plays camera B (by this project's id for it).
    CHECK(trip->tracks[std::size_t(*other.project.trackIndex(other.main))].clips.front().mediaId == t.camB);
    (void)x;

    Project empty;
    CHECK(importProjectAsCompound(p, empty, "Nothing", 0, &made).value() == "that project's timeline is empty");
}

TEST_CASE("the source monitor inserts, overwrites and appends a part of its media", "[sequence]")
{
    Timeline t;
    const Id first = t.clip(t.main, 0, 4 * kSec, t.camA);
    const Id second = t.clip(t.main, 4 * kSec, 4 * kSec, t.camB);
    Project p = t.project;
    Id made = kNoId;
    // Insert 2 s (from 10 s into camera B) at 2 s: the first clip split, everything later.
    REQUIRE_FALSE(placeMedia(p, t.main, t.camB, 10 * kSec, 2 * kSec, 2 * kSec, PlaceMode::Insert, &made));
    const Track& main = *p.findTrack(t.main);
    REQUIRE(main.clips.size() == 4);
    CHECK(main.clips[0].duration == 2 * kSec);
    CHECK(main.clips[1].id == made);
    CHECK(main.clips[1].sourceIn == 10 * kSec);
    CHECK(main.clips[2].start == 4 * kSec);
    CHECK(p.locateClip(second)->clip->start == 6 * kSec);
    CHECK(p.duration() == 10 * kSec);

    // Overwrite 3 s at 1 s: what was there is cut out, nothing moves.
    p = t.project;
    REQUIRE_FALSE(placeMedia(p, t.main, t.camB, 0, 3 * kSec, 1 * kSec, PlaceMode::Overwrite, &made));
    CHECK(p.duration() == 8 * kSec);
    CHECK(p.locateClip(first)->clip->duration == 1 * kSec);
    CHECK(p.locateClip(made)->clip->start == 1 * kSec);
    CHECK(p.findTrack(t.main)->clips.size() == 3);
    CHECK(p.findTrack(t.main)->clips[2].start == 4 * kSec);

    // Append: after the last clip, wherever the playhead is.
    p = t.project;
    REQUIRE_FALSE(placeMedia(p, t.main, t.camA, 5 * kSec, 1 * kSec, 0, PlaceMode::Append, &made));
    CHECK(p.locateClip(made)->clip->start == 8 * kSec);
    // Out before In.
    CHECK(placeMedia(p, t.main, t.camA, 5 * kSec, 0, 0, PlaceMode::Append, &made).value()
          == "choose a part of the media to place (In before Out)");
}
