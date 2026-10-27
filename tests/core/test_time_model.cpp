#include "commands/CommandStack.h"
#include "commands/EditCommands.h"
#include "io/ProjectSerializer.h"
#include "model/TimelineQuery.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

using namespace tve;

namespace {

constexpr Ticks kSec = kTicksPerSecond;

struct SpeedProject {
    Project project;
    CommandStack stack{project};
    Id track = kNoId;
    Id media = kNoId;
    Id clip = kNoId;
    Id next = kNoId;

    SpeedProject()
    {
        track = push<AddTrackCommand>(TrackKind::Video, "V1").trackId();
        media = push<ImportMediaCommand>(MediaItem{.path = "/m.mp4", .duration = 60 * kSec, .hasAudio = true}).mediaId();
        // Clip: source 10–20 s at timeline 0–10 s; another clip right after it.
        clip = push<AddClipCommand>(track, media, 0, 10 * kSec, 10 * kSec).clipId();
        next = push<AddClipCommand>(track, media, 10 * kSec, 5 * kSec, 0).clipId();
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

} // namespace

TEST_CASE("source time follows speed, direction and freeze")
{
    Clip c{.start = 5 * kSec, .duration = 4 * kSec, .sourceIn = 10 * kSec};
    CHECK(c.sourceTimeAt(6 * kSec) == 11 * kSec);

    c.speed = 2.0;
    CHECK(c.sourceSpan() == 8 * kSec);
    CHECK(c.sourceTimeAt(6 * kSec) == 12 * kSec);

    c.reversed = true; // plays source 18 s → 10 s
    CHECK(c.sourceTimeAt(5 * kSec) == 18 * kSec - 1);
    CHECK(c.sourceTimeAt(7 * kSec) == 14 * kSec - 1);
    CHECK(c.sourceTimeAt(9 * kSec - 1) >= 10 * kSec);

    c.freeze = true;
    CHECK(c.sourceSpan() == 0);
    CHECK(c.sourceTimeAt(8 * kSec) == 10 * kSec);
}

TEST_CASE("changing speed keeps the source, changes length and moves later clips")
{
    SpeedProject p;
    p.push<SetClipSpeedCommand>(p.clip, 2.0, false);
    CHECK(p.get(p.clip).duration == 5 * kSec);
    CHECK(p.get(p.clip).sourceSpan() == 10 * kSec);
    CHECK(p.get(p.next).start == 5 * kSec); // followed the end in

    p.push<SetClipSpeedCommand>(p.clip, 0.5, true);
    CHECK(p.get(p.clip).duration == 20 * kSec);
    CHECK(p.get(p.clip).reversed);
    CHECK(p.get(p.next).start == 20 * kSec);

    CHECK(p.tryPush<SetClipSpeedCommand>(p.clip, 50.0, false));
    p.stack.undo();
    p.stack.undo();
    CHECK(p.get(p.clip).duration == 10 * kSec);
    CHECK(p.get(p.next).start == 10 * kSec);
    CHECK(p.get(p.clip).speed == 1.0);
}

TEST_CASE("trim and split respect speed and direction")
{
    SpeedProject p;
    p.push<SetClipSpeedCommand>(p.clip, 2.0, false); // still source 10–20 s, now 5 s long
    const Id right = p.push<SplitClipCommand>(p.clip, 2 * kSec).rightClipId();
    CHECK(p.get(p.clip).sourceSpan() == 4 * kSec);
    CHECK(p.get(right).sourceIn == 14 * kSec);
    CHECK(p.get(right).speed == 2.0);

    p.push<TrimClipCommand>(right, TrimClipCommand::Edge::Start, 3 * kSec); // 1 s timeline = 2 s source
    CHECK(p.get(right).sourceIn == 16 * kSec);
}

TEST_CASE("reversed clips split and trim from the right end of the source")
{
    SpeedProject p;
    p.push<SetClipSpeedCommand>(p.clip, 1.0, true); // plays 20 s → 10 s
    const Id right = p.push<SplitClipCommand>(p.clip, 4 * kSec).rightClipId();
    // Left part shows 20 → 16, right part 16 → 10.
    CHECK(p.get(p.clip).sourceIn == 16 * kSec);
    CHECK(p.get(right).sourceIn == 10 * kSec);
    CHECK(p.get(p.clip).sourceTimeAt(0) == 20 * kSec - 1);
    CHECK(p.get(right).sourceTimeAt(4 * kSec) == 16 * kSec - 1);

    p.push<TrimClipCommand>(right, TrimClipCommand::Edge::End, 8 * kSec); // drop the last 2 s (source 10–12)
    CHECK(p.get(right).sourceIn == 12 * kSec);
    CHECK(p.get(right).sourceTimeAt(4 * kSec) == 16 * kSec - 1); // the start is unchanged
}

TEST_CASE("freeze frame inserts a still and pushes the rest right")
{
    SpeedProject p;
    const Id still = p.push<FreezeFrameCommand>(p.clip, 4 * kSec, 2 * kSec).freezeClipId();
    const Clip& frozen = p.get(still);
    CHECK(frozen.freeze);
    CHECK(frozen.start == 4 * kSec);
    CHECK(frozen.duration == 2 * kSec);
    CHECK(frozen.sourceIn == 14 * kSec); // the frame shown at 4 s
    CHECK_FALSE(frozen.audioEnabled);
    CHECK(p.get(p.clip).duration == 4 * kSec);
    CHECK(p.get(p.next).start == 12 * kSec);
    CHECK(visibleVideoAt(p.project, 5 * kSec)->sourceTime == 14 * kSec);
    CHECK(visibleVideoAt(p.project, 6 * kSec)->sourceTime == 14 * kSec); // the rest resumes after

    CHECK(p.tryPush<SetClipSpeedCommand>(still, 2.0, false));     // stills have no speed
    CHECK(p.tryPush<DetachAudioCommand>(still, p.track));          // nor sound

    p.stack.undo();
    CHECK(p.project.tracks[0].clips.size() == 2);
    CHECK(p.get(p.clip).duration == 10 * kSec);
    CHECK(p.get(p.next).start == 10 * kSec);
}

TEST_CASE("time settings survive save and load")
{
    SpeedProject p;
    p.push<SetClipSpeedCommand>(p.clip, 0.25, true);
    p.push<FreezeFrameCommand>(p.next, p.get(p.next).start + kSec, kSec);
    CHECK(projectFromJson(projectToJson(p.project)) == p.project);
}
