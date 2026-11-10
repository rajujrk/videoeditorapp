#include "commands/CommandStack.h"
#include "commands/EditCommands.h"
#include "commands/StructureCommands.h"
#include "io/ProjectSerializer.h"
#include "model/RenderCost.h"
#include "model/TimelineQuery.h"

#include <algorithm>

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

    // Trimmed within its sequence; saved. (Speed and transitions: the next test.)
    p.push<TrimClipCommand>(make.clipId(), TrimClipCommand::Edge::Start, kSec / 2);
    CHECK(p.get(make.clipId())->sourceIn == kSec / 2);
    CHECK(p.tryPush(std::make_unique<TrimClipCommand>(make.clipId(), TrimClipCommand::Edge::End, 5 * kSec)).has_value());
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

TEST_CASE("compound clips: faster, slower, backwards, ramped, frozen, and with transitions", "[structure]")
{
    StructureProject p;
    const Id first = p.clip(p.main, 0, kSec, 30 * kSec);
    const Id a = p.clip(p.main, kSec, 2 * kSec, 10 * kSec);
    const Id b = p.clip(p.main, 3 * kSec, 2 * kSec, 20 * kSec);
    const Id later = p.clip(p.main, 5 * kSec, kSec);
    const Id compound = p.push<CreateCompoundCommand>(std::vector<Id>{a, b}, "Both").clipId();
    REQUIRE(p.get(compound)->start == kSec);
    REQUIRE(p.get(compound)->duration == 4 * kSec);

    // Twice as fast: half as long, the clip after it moved up, its sequence shown twice as fast.
    p.push<SetClipSpeedCommand>(compound, 2.0, false);
    CHECK(p.get(compound)->duration == 2 * kSec);
    CHECK(p.get(compound)->sourceSpan() == 4 * kSec);
    CHECK(p.get(later)->start == 3 * kSec);
    auto layers = visibleVideoLayersAt(p.project, 2 * kSec);
    REQUIRE(layers.size() == 1);
    CHECK(layers[0].clip->id == compound);
    CHECK(layers[0].sourceTime == 2 * kSec);
    CHECK(p.get(compound)->timelineTimeOf(2 * kSec) == 2 * kSec);
    // Where its sequence cuts from one clip to the next (at 2 s inside), the timeline's load
    // is measured apart: at 2 s on the timeline, not at 1 s + 2 s.
    const auto stretches = playbackLoad(p.project);
    CHECK(std::ranges::any_of(stretches, [](const PlaybackLoad& s) { return s.from == 2 * kSec; }));

    // Backwards: the sequence's end first.
    p.push<SetClipSpeedCommand>(compound, 2.0, true);
    CHECK(visibleVideoLayersAt(p.project, kSec)[0].sourceTime == 4 * kSec - 1);
    CHECK(p.get(compound)->timelineTimeOf(kSec) == 2 * kSec + kSec / 2);
    CHECK(p.get(compound)->timelineTimeOf(-kSec) == p.get(compound)->end()); // never shown

    // A ramp from 1× to 3×: the same span, at an average of 2×.
    p.push<SetClipSpeedRampCommand>(compound, SpeedRamp{{0.0, 1.0}, {1.0, 3.0}}, false);
    CHECK(p.get(compound)->duration == 2 * kSec);
    CHECK_FALSE(p.get(compound)->speedRamp.empty());
    CHECK(visibleVideoLayersAt(p.project, kSec + kSec / 2)[0].sourceTime < kSec * 3 / 2); // slow at first

    // Broken apart only at normal speed (its clips would no longer match what it showed).
    CHECK(p.tryPush(std::make_unique<BreakCompoundCommand>(compound)).value()
          == "set the compound clip back to normal speed to break it apart");
    p.push<SetClipSpeedCommand>(compound, 1.0, false);
    CHECK(p.get(compound)->duration == 4 * kSec);
    CHECK(p.get(later)->start == 5 * kSec);
    CHECK(p.get(compound)->speedRamp.empty());

    // Its pitch kept or not, as a whole.
    p.push<SetClipKeepPitchCommand>(compound, false);
    CHECK_FALSE(p.get(compound)->keepPitch);

    // A frozen moment of it: a compound clip too, holding its sequence at 1 s.
    auto& freeze = p.push<FreezeFrameCommand>(compound, 2 * kSec, kSec);
    const Clip* still = p.get(freeze.freezeClipId());
    REQUIRE(still);
    CHECK(still->compound());
    CHECK(still->freeze);
    CHECK(still->sourceIn == kSec);
    CHECK(visibleVideoLayersAt(p.project, 2 * kSec + kSec / 2)[0].sourceTime == kSec);
    CHECK(p.tryPush(std::make_unique<BreakCompoundCommand>(freeze.freezeClipId())).value()
          == "a frozen compound clip cannot be broken apart");
    p.stack.undo();
    CHECK_FALSE(p.get(freeze.freezeClipId()));

    // Transitions into it and out of it, playing both sides' pictures.
    p.push<SetClipTransitionCommand>(compound, ClipTransition{"tve.transition.dissolve"});
    p.push<SetClipTransitionCommand>(later, ClipTransition{"tve.transition.dissolve"});
    const Track& main = *p.project.findTrack(p.main);
    const auto into = transitionInto(main, *p.get(compound));
    REQUIRE(into);
    CHECK(into->from->id == first);
    layers = visibleVideoLayersAt(p.project, kSec - kSec / 10);
    REQUIRE(layers.size() == 1);
    CHECK(layers[0].clip->id == first);
    REQUIRE(layers[0].transition);
    CHECK(layers[0].transition->to->id == compound);
    CHECK(layers[0].transition->toSourceTime == 0); // its head handle: none, held at its first moment
    layers = visibleVideoLayersAt(p.project, 5 * kSec + kSec / 10);
    REQUIRE(layers[0].transition);
    CHECK(layers[0].clip->id == compound);
    CHECK(layers[0].sourceTime == 4 * kSec - 1); // its tail: held at its last moment
    CHECK(layers[0].transition->to->id == later);
    // Its sound crossfades over the same span.
    CHECK(audibleSpan(main, *p.get(compound)).start < kSec);
    const Project loaded = projectFromJson(projectToJson(p.project));
    CHECK(loaded.tracks == p.project.tracks);
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

TEST_CASE("an export of part of the timeline gets the chapters in that part", "[structure]")
{
    StructureProject p;
    p.push<AddClipCommand>(p.main, p.av, 0, 10 * kSec);
    for (auto [time, note] : {std::pair{2 * kSec, "Setup"}, std::pair{6 * kSec, "Payoff"}}) {
        Marker marker;
        marker.time = time;
        marker.note = note;
        marker.chapter = true;
        p.push<AddMarkerCommand>(marker);
    }
    // 4–8 s: the end of Setup (now from 0) and the start of Payoff.
    const std::vector<Chapter> expected{{0, 2 * kSec, "Setup"}, {2 * kSec, 4 * kSec, "Payoff"}};
    CHECK(p.project.chapters(4 * kSec, 8 * kSec) == expected);
    CHECK(p.project.chapters(7 * kSec, 9 * kSec) == std::vector<Chapter>{{0, 2 * kSec, "Payoff"}});
    CHECK(p.project.chapters(0, 10 * kSec) == p.project.chapters());
}
