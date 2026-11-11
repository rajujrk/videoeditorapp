#include "commands/CommandStack.h"
#include "commands/EditCommands.h"
#include "commands/StructureCommands.h"
#include "commands/TrackCommands.h"

#include <catch2/catch_test_macros.hpp>

using namespace tve;

namespace {

constexpr Ticks kSec = kTicksPerSecond;

// Audio A1 (bottom), video V1 (main), video V2.
struct Timeline {
    Project project;
    CommandStack stack{project};
    Id audio = kNoId;
    Id main = kNoId;
    Id upper = kNoId;
    Id av = kNoId;

    Timeline()
    {
        audio = push<AddTrackCommand>(TrackKind::Audio, "Audio 1").trackId();
        main = push<AddTrackCommand>(TrackKind::Video, "Video 1").trackId();
        upper = push<AddTrackCommand>(TrackKind::Video, "Video 2").trackId();
        av = push<ImportMediaCommand>(MediaItem{.path = "/av.mp4", .duration = 60 * kSec, .hasAudio = true}).mediaId();
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
    Id clip(Id track, Ticks start, Ticks duration, Id media = kNoId)
    {
        return push<AddClipCommand>(track, media == kNoId ? av : media, start, duration, 0).clipId();
    }
    const Clip* get(Id id) { auto l = project.locateClip(id); return l ? l->clip : nullptr; }
    Id trackOf(Id id) { return project.locateClip(id)->track->id; }
};

} // namespace

TEST_CASE("tracks are added where asked, renamed and deleted, and the last video track stays", "[tracks]")
{
    Timeline t;
    // A video track between V1 and V2 (index 2), and an audio track at the very bottom.
    auto& added = t.push<InsertTrackCommand>(TrackKind::Video, "Video 3", 2);
    CHECK(added.label() == "Add Track");
    REQUIRE(t.project.tracks.size() == 4);
    CHECK(t.project.tracks[2].id == added.trackId());
    CHECK(t.project.tracks[3].id == t.upper);
    const Id bottom = t.push<InsertTrackCommand>(TrackKind::Audio, "Audio 2", 0).trackId();
    CHECK(t.project.tracks.front().id == bottom);
    // Past the end: on top.
    const Id top = t.push<InsertTrackCommand>(TrackKind::Title, "Title 1", 99).trackId();
    CHECK(t.project.tracks.back().id == top);

    t.push<RenameTrackCommand>(added.trackId(), "  B-roll ");
    CHECK(t.project.findTrack(added.trackId())->name == "B-roll");
    CHECK(t.tryPush(std::make_unique<RenameTrackCommand>(added.trackId(), "   ")).value() == "a track needs a name");
    t.stack.undo();
    CHECK(t.project.findTrack(added.trackId())->name == "Video 3");
    t.stack.redo();

    // Deleting a track takes its clips; undo puts both back where they were.
    const Id onIt = t.clip(t.upper, 0, kSec);
    t.push<RemoveTrackCommand>(t.upper);
    CHECK_FALSE(t.project.findTrack(t.upper));
    CHECK_FALSE(t.get(onIt));
    t.stack.undo();
    CHECK(t.get(onIt));
    CHECK(t.project.tracks[4].id == t.upper);
    // A locked track stays; so does the last video track.
    t.push<SetTrackFlagCommand>(t.upper, SetTrackFlagCommand::Flag::Locked, true);
    CHECK(t.tryPush(std::make_unique<RemoveTrackCommand>(t.upper)).value() == "track is locked");
    t.push<SetTrackFlagCommand>(t.upper, SetTrackFlagCommand::Flag::Locked, false);
    t.push<RemoveTrackCommand>(t.upper);
    t.push<RemoveTrackCommand>(added.trackId());
    CHECK(t.tryPush(std::make_unique<RemoveTrackCommand>(t.main)).value() == "the timeline keeps one video track");
    t.push<RemoveTrackCommand>(t.audio); // audio tracks can all go
}

TEST_CASE("copied clips paste at the playhead on their lanes, moving up past clips in the way", "[tracks]")
{
    Timeline t;
    const Id a = t.clip(t.main, 2 * kSec, 2 * kSec);
    const Id b = t.clip(t.upper, 3 * kSec, kSec);
    const Id sound = t.clip(t.audio, 2 * kSec, kSec);
    t.push<GroupClipsCommand>(std::vector<Id>{a, b});

    const std::vector<CopiedClip> copied = copyClips(t.project, {b, a, sound});
    REQUIRE(copied.size() == 3);
    CHECK(copied[0].clip.start == 0); // from the earliest copied start
    CHECK(copied.back().clip.start == kSec);
    const auto copyOf = [&](Id id) { return *std::ranges::find(copied, id, [](const CopiedClip& c) { return c.clip.id; }); };
    CHECK(copyOf(a).lane == 0);
    CHECK(copyOf(b).lane == 1); // V2 is one video track above V1
    CHECK(copyOf(b).kind == TrackKind::Video);
    CHECK(copyOf(sound).kind == TrackKind::Audio);

    // At 10 s, on the main track: the same layout there, new ids, a new group.
    auto& paste = t.push<PasteClipsCommand>(copied, 10 * kSec, t.main, "Paste");
    CHECK(paste.label() == "Paste");
    REQUIRE(paste.pastedIds().size() == 3);
    for (Id id : paste.pastedIds())
        CHECK((id != a && id != b && id != sound));
    const Clip* pastedA = t.get(paste.pastedIds()[0]);
    const Clip* pastedB = nullptr;
    for (Id id : paste.pastedIds())
        if (t.get(id)->start == 11 * kSec && t.trackOf(id) == t.upper)
            pastedB = t.get(id);
    REQUIRE(pastedA);
    REQUIRE(pastedB);
    CHECK(pastedA->start == 10 * kSec);
    CHECK(t.trackOf(pastedA->id) == t.main);
    CHECK(pastedA->groupId != kNoId);
    CHECK(pastedA->groupId != t.get(a)->groupId);
    CHECK(pastedA->groupId == pastedB->groupId);
    const auto sounds = std::ranges::count_if(t.project.findTrack(t.audio)->clips, [](const Clip& c) { return c.start == 10 * kSec; });
    CHECK(sounds == 1);

    // Pasted again over the first paste: every lane is taken there, so each copy moves up
    // onto a new track of its kind, on top (in time order: the V1 copy, the sound, the V2 copy).
    const std::size_t tracksBefore = t.project.tracks.size();
    auto& again = t.push<PasteClipsCommand>(copied, 10 * kSec, t.main, "Paste");
    REQUIRE(t.project.tracks.size() == tracksBefore + 3);
    CHECK(t.trackOf(again.pastedIds()[0]) == t.project.tracks[tracksBefore].id);
    CHECK(t.project.tracks[tracksBefore].name == "Video 3");
    CHECK(t.project.tracks[tracksBefore + 1].name == "Audio 2");
    CHECK(t.project.tracks[tracksBefore + 2].name == "Video 4");
    // Undo takes the new tracks away too.
    t.stack.undo();
    CHECK(t.project.tracks.size() == tracksBefore);
    // Duplicating is pasting right after: from the selection's end.
    auto& duplicate = t.push<PasteClipsCommand>(copyClips(t.project, {a}), t.get(a)->end(), t.main, "Duplicate");
    CHECK(t.get(duplicate.pastedIds()[0])->start == 4 * kSec);
    CHECK(t.trackOf(duplicate.pastedIds()[0]) == t.main);

    // Media from another project, or nothing at all, cannot be pasted.
    Clip foreign = t.get(a) ? *t.get(a) : Clip{};
    foreign.mediaId = 999999;
    CHECK(t.tryPush(std::make_unique<PasteClipsCommand>(std::vector<CopiedClip>{{foreign, TrackKind::Video, 0}}, 0, t.main, "Paste")).value()
          == "a copied clip's media is not in this project");
    CHECK(t.tryPush(std::make_unique<PasteClipsCommand>(std::vector<CopiedClip>{}, 0, t.main, "Paste")).value()
          == "there is nothing to paste");
}

TEST_CASE("removing media takes its clips, inside compound clips too, and undoes", "[tracks]")
{
    Timeline t;
    const Id other = t.push<ImportMediaCommand>(MediaItem{.path = "/b.mp4", .duration = 10 * kSec}).mediaId();
    const Id a = t.clip(t.main, 0, kSec);
    const Id b = t.clip(t.main, kSec, kSec, other);
    const Id c = t.clip(t.upper, 0, kSec);
    t.push<CreateCompoundCommand>(std::vector<Id>{c}, "Inside");
    CHECK(clipsUsingMedia(t.project, t.av) == 2);
    const Project before = t.project;

    auto& removed = t.push<RemoveMediaCommand>(t.av);
    CHECK(removed.label() == "Remove Media");
    CHECK(removed.clipsRemoved() == 2);
    CHECK_FALSE(t.project.findMedia(t.av));
    CHECK_FALSE(t.get(a));
    CHECK(t.get(b));
    CHECK(clipsUsingMedia(t.project, t.av) == 0); // (the one in the compound clip's sequence too)
    CHECK(t.project.sequences.size() == 1);
    t.stack.undo();
    CHECK(t.project.media == before.media);
    CHECK(t.project.tracks == before.tracks);
    CHECK(t.project.sequences == before.sequences);
    CHECK(t.tryPush(std::make_unique<RemoveMediaCommand>(Id{424242})).value() == "media not found");
}

#include "model/TimelineQuery.h"

TEST_CASE("sound clips crossfade with a transition, and speeds run from 0.01× to 100×", "[tracks]")
{
    Timeline t;
    const Id music = t.push<ImportMediaCommand>(MediaItem{.path = "/m.wav", .duration = 60 * kSec, .hasVideo = false, .hasAudio = true})
                         .mediaId();
    const Id a = t.clip(t.audio, 0, 4 * kSec, music);
    const Id b = t.clip(t.audio, 4 * kSec, 4 * kSec, music);
    // On a sound track a transition is a crossfade: one rises as the other falls.
    t.push<SetClipTransitionCommand>(b, ClipTransition{"tve.transition.dissolve", 2 * kSec});
    const Track& audio = *t.project.findTrack(t.audio);
    CHECK(transitionGainAt(audio, *t.get(a), 4 * kSec + kSec / 2) < 0.75f);
    CHECK(transitionGainAt(audio, *t.get(b), 3 * kSec + kSec / 2) < 0.75f);
    CHECK(transitionGainAt(audio, *t.get(a), kSec) == 1.0f);
    CHECK(audibleSpan(audio, *t.get(b)).start == 3 * kSec);

    const Id v = t.clip(t.main, 0, 10 * kSec);
    t.push<SetClipSpeedCommand>(v, 100.0, false);
    CHECK(t.get(v)->duration == kSec / 10);
    t.push<SetClipSpeedCommand>(v, 0.01, false);
    CHECK(t.get(v)->duration == 1000 * kSec); // 10 s of source at 0.01×
    CHECK(t.tryPush(std::make_unique<SetClipSpeedCommand>(v, 101.0, false)).value() == "speed must be between 0.01× and 100×");
    CHECK(t.tryPush(std::make_unique<SetClipSpeedCommand>(v, 0.005, false)).value() == "speed must be between 0.01× and 100×");
}
