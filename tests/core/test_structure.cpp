#include "commands/CommandStack.h"
#include "commands/EditCommands.h"
#include "commands/StructureCommands.h"
#include "io/ProjectSerializer.h"
#include "model/TimelineQuery.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

using namespace tve;

namespace {

constexpr Ticks kSec = kTicksPerSecond;

// Audio A1 (bottom), video V1 (main), video V2 above it.
struct StructureProject {
    Project project;
    CommandStack stack{project};
    Id audio = kNoId;
    Id main = kNoId;
    Id upper = kNoId;
    Id av = kNoId;
    Id wav = kNoId;

    StructureProject()
    {
        audio = push<AddTrackCommand>(TrackKind::Audio, "A1").trackId();
        main = push<AddTrackCommand>(TrackKind::Video, "V1").trackId();
        upper = push<AddTrackCommand>(TrackKind::Video, "V2").trackId();
        av = push<ImportMediaCommand>(MediaItem{.path = "/av.mp4", .duration = 60 * kSec, .hasAudio = true}).mediaId();
        wav = push<ImportMediaCommand>(MediaItem{.path = "/m.wav", .duration = 60 * kSec, .hasVideo = false, .hasAudio = true})
                  .mediaId();
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
    Id clip(Id track, Ticks start, Ticks duration, Ticks sourceIn = 0, Id media = kNoId)
    {
        return push<AddClipCommand>(track, media == kNoId ? av : media, start, duration, sourceIn).clipId();
    }
    const Clip* get(Id id) { auto l = project.locateClip(id); return l ? l->clip : nullptr; }
};

} // namespace

TEST_CASE("markers: added in time order, changed, removed, saved", "[structure]")
{
    StructureProject p;
    const Id b = p.push<AddMarkerCommand>(Marker{.time = 5 * kSec, .note = "Second"}).markerId();
    auto& added = p.push<AddMarkerCommand>(Marker{.time = 2 * kSec, .color = MarkerColor::Red, .note = "Intro", .chapter = true});
    CHECK(added.label() == "Add Marker");
    REQUIRE(p.project.markers.size() == 2);
    CHECK(p.project.markers[0].note == "Intro"); // sorted by time
    CHECK(p.project.markers[1].id == b);

    Marker moved = *p.project.findMarker(b);
    moved.time = kSec;
    moved.duration = 3 * kSec;
    moved.color = MarkerColor::Green;
    p.push<SetMarkerCommand>(moved);
    CHECK(p.project.markers[0].id == b);
    CHECK(p.project.markers[0].duration == 3 * kSec);
    p.stack.undo();
    CHECK(p.project.markers[1].id == b);
    p.stack.redo();

    CHECK(p.tryPush(std::make_unique<AddMarkerCommand>(Marker{.time = -kSec})).value()
          == "a marker's time and length cannot be negative");
    CHECK(p.tryPush(std::make_unique<AddMarkerCommand>(Marker{.note = std::string(600, 'x')})).value()
          == "the marker's note is too long");

    const Project loaded = projectFromJson(projectToJson(p.project));
    CHECK(loaded.markers == p.project.markers);
    nlohmann::json old = projectToJson(p.project);
    old.erase("markers");
    old.erase("sequences");
    CHECK(projectFromJson(old).markers.empty());

    p.push<RemoveMarkerCommand>(added.markerId());
    CHECK(p.project.markers.size() == 1);
    p.stack.undo();
    CHECK(p.project.markers.size() == 2);
    CHECK(p.project.markers[1].chapter);
}

TEST_CASE("groups: clips join one, splits stay in it, ungrouping frees them", "[structure]")
{
    StructureProject p;
    const Id a = p.clip(p.main, 0, 4 * kSec);
    const Id b = p.clip(p.upper, kSec, 2 * kSec);
    const Id c = p.clip(p.audio, 0, 3 * kSec, 0, p.wav);
    auto& group = p.push<GroupClipsCommand>(std::vector<Id>{a, b, c});
    CHECK(group.label() == "Group");
    const Id g = group.groupId();
    CHECK(groupMembers(p.project, g) == std::vector<Id>{c, a, b}); // in track order
    auto& split = p.push<SplitClipCommand>(a, 2 * kSec);
    CHECK(p.get(split.rightClipId())->groupId == g);
    CHECK(p.tryPush(std::make_unique<GroupClipsCommand>(std::vector<Id>{a})).value() == "select two or more clips to group");
    CHECK(projectFromJson(projectToJson(p.project)).locateClip(b)->clip->groupId == g);

    p.push<UngroupCommand>(g);
    CHECK(p.get(a)->groupId == kNoId);
    CHECK(groupMembers(p.project, g).empty());
    p.stack.undo();
    CHECK(groupMembers(p.project, g).size() == 4);
    CHECK(p.tryPush(std::make_unique<UngroupCommand>(Id{9999})).value() == "group not found");
}

TEST_CASE("compound clips: made from clips, played as one, broken apart again", "[structure]")
{
    StructureProject p;
    const Id a = p.clip(p.main, 0, 2 * kSec, 10 * kSec);
    const Id b = p.clip(p.main, 2 * kSec, 2 * kSec, 20 * kSec);
    const Id over = p.clip(p.upper, kSec, 2 * kSec);
    const Id sound = p.clip(p.audio, kSec, kSec, 0, p.wav);
    const Id later = p.clip(p.main, 4 * kSec, kSec);
    const Project before = p.project;

    auto& make = p.push<CreateCompoundCommand>(std::vector<Id>{a, b, over, sound}, "Compound 1");
    CHECK(make.label() == "Create Compound Clip");
    const Clip* compound = p.get(make.clipId());
    REQUIRE(compound);
    CHECK(compound->compound());
    CHECK(compound->start == 0);
    CHECK(compound->duration == 4 * kSec);
    CHECK(p.project.locateClip(make.clipId())->track->id == p.main); // the lowest video track
    CHECK_FALSE(p.get(a));
    CHECK(p.get(later)); // left alone
    const Sequence* sequence = p.project.findSequence(make.sequenceId());
    REQUIRE(sequence);
    CHECK(sequence->name == "Compound 1");
    REQUIRE(sequence->tracks.size() == 3); // A1, V1, V2 kept in order
    CHECK(sequence->tracks[0].kind == TrackKind::Audio);
    CHECK(sequence->tracks[1].clips.size() == 2);
    CHECK(sequence->tracks[1].clips[1].start == 2 * kSec);
    CHECK(sequence->tracks[2].clips[0].start == kSec);
    CHECK(sequence->duration() == 4 * kSec);
    CHECK(p.project.sourceLimit(*compound) == 4 * kSec);
    // It is a video layer over the timeline at 1.5 s; its insides are not.
    const auto layers = visibleVideoLayersAt(p.project, kSec * 3 / 2);
    REQUIRE(layers.size() == 1);
    CHECK(layers[0].clip->id == make.clipId());

    // Trimmed within its sequence; no speed; no transitions; saved.
    p.push<TrimClipCommand>(make.clipId(), TrimClipCommand::Edge::Start, kSec / 2);
    CHECK(p.get(make.clipId())->sourceIn == kSec / 2);
    CHECK(p.tryPush(std::make_unique<TrimClipCommand>(make.clipId(), TrimClipCommand::Edge::End, 5 * kSec)).has_value());
    CHECK(p.tryPush(std::make_unique<SetClipSpeedCommand>(make.clipId(), 2.0, false)).value() == "compound clips have no speed");
    CHECK(p.tryPush(std::make_unique<SetClipTransitionCommand>(later, ClipTransition{"tve.transition.dissolve"})).value()
          == "transitions do not go on compound clips yet");
    const Project loaded = projectFromJson(projectToJson(p.project));
    CHECK(loaded.sequences == p.project.sequences);

    // Broken apart: the insides come back where it played them, cut to the part it showed.
    auto& broken = p.push<BreakCompoundCommand>(make.clipId());
    CHECK(broken.label() == "Break Apart");
    CHECK_FALSE(p.get(make.clipId()));
    CHECK(p.project.sequences.empty());
    const Clip* first = p.get(a);
    REQUIRE(first);
    CHECK(first->start == kSec / 2);
    CHECK(first->duration == kSec * 3 / 2);
    CHECK(first->sourceIn == 10 * kSec + kSec / 2);
    CHECK(p.project.locateClip(over)->track->id == p.upper);
    CHECK(p.project.locateClip(sound)->track->id == p.audio);
    // Undo all of it: back to the start, exactly.
    p.stack.undo();
    p.stack.undo();
    p.stack.undo();
    CHECK(p.project.tracks == before.tracks);
    CHECK(p.project.sequences.empty());
    // Ids handed out stay used: a new clip does not reuse the compound's.
    const Id fresh = p.clip(p.upper, 10 * kSec, kSec);
    CHECK(fresh > make.clipId());
}

TEST_CASE("a compound clip needs room and real clips", "[structure]")
{
    StructureProject p;
    const Id a = p.clip(p.upper, 0, 2 * kSec);
    const Id b = p.clip(p.upper, 4 * kSec, 2 * kSec);
    p.clip(p.upper, 2 * kSec, kSec); // in between, not chosen
    CHECK(p.tryPush(std::make_unique<CreateCompoundCommand>(std::vector<Id>{a, b}, "C")).value()
          == "the compound clip needs the space its clips covered on V2");
    CHECK(p.get(a)); // nothing changed
    CHECK(p.tryPush(std::make_unique<CreateCompoundCommand>(std::vector<Id>{}, "C")).value()
          == "select clips to make a compound clip");
    CHECK(p.tryPush(std::make_unique<CreateCompoundCommand>(std::vector<Id>{Id{9999}}, "C")).value() == "clip not found");
    // Sound alone makes a compound clip on its sound track.
    const Id s1 = p.clip(p.audio, 0, kSec, 0, p.wav);
    const Id s2 = p.clip(p.audio, kSec, kSec, 0, p.wav);
    auto& sounds = p.push<CreateCompoundCommand>(std::vector<Id>{s1, s2}, "Sounds");
    CHECK(p.project.locateClip(sounds.clipId())->track->id == p.audio);
}

TEST_CASE("chapters run from one chapter marker to the next", "[structure]")
{
    StructureProject p;
    p.push<AddClipCommand>(p.main, p.av, 0, 10 * kSec);
    CHECK(p.project.chapters().empty());

    const auto chapter = [&](Ticks time, std::string note) {
        Marker marker;
        marker.time = time;
        marker.note = std::move(note);
        marker.chapter = true;
        p.push<AddMarkerCommand>(marker);
    };
    chapter(2 * kSec, "Setup");
    chapter(6 * kSec, "");
    chapter(12 * kSec, "After the end"); // past the last clip: left out
    Marker plain;
    plain.time = 4 * kSec;
    p.push<AddMarkerCommand>(plain); // not a chapter

    const std::vector<Chapter> expected{
        {0, 2 * kSec, "Opening"}, // the time before the first chapter
        {2 * kSec, 6 * kSec, "Setup"},
        {6 * kSec, 10 * kSec, "Chapter 2"},
    };
    CHECK(p.project.chapters() == expected);

    chapter(0, "Intro");
    CHECK(p.project.chapters().front() == Chapter{0, 2 * kSec, "Intro"});
    CHECK(p.project.chapters().size() == 3);
}
