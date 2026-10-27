#include "commands/CommandStack.h"
#include "commands/EditCommands.h"
#include "model/TimelineQuery.h"

#include <catch2/catch_test_macros.hpp>

using namespace tve;

namespace {

constexpr Ticks kSec = kTicksPerSecond;

struct Timeline {
    Project project;
    CommandStack stack{project};
    Id v1 = kNoId;
    Id v2 = kNoId;
    Id a1 = kNoId;
    Id media = kNoId;

    Timeline()
    {
        v1 = add<AddTrackCommand>(TrackKind::Video, "V1").trackId();
        v2 = add<AddTrackCommand>(TrackKind::Video, "V2").trackId();
        a1 = add<AddTrackCommand>(TrackKind::Audio, "A1").trackId();
        media = add<ImportMediaCommand>("/m.mp4", 100 * kSec).mediaId();
    }

    template <typename T, typename... Args>
    T& add(Args&&... args)
    {
        auto command = std::make_unique<T>(std::forward<Args>(args)...);
        T& ref = *command;
        REQUIRE_FALSE(stack.push(std::move(command)));
        return ref;
    }
};

} // namespace

TEST_CASE("visibleVideoAt finds the clip and source time")
{
    Timeline t;
    const Id clip = t.add<AddClipCommand>(t.v1, t.media, 10 * kSec, 5 * kSec, 30 * kSec).clipId();

    CHECK_FALSE(visibleVideoAt(t.project, 9 * kSec));
    auto hit = visibleVideoAt(t.project, 12 * kSec);
    REQUIRE(hit);
    CHECK(hit->clip->id == clip);
    CHECK(hit->sourceTime == 32 * kSec);
    CHECK(visibleVideoAt(t.project, 10 * kSec)); // start is inclusive
    CHECK_FALSE(visibleVideoAt(t.project, 15 * kSec)); // end is exclusive
}

TEST_CASE("upper tracks cover lower ones; hidden tracks and audio are ignored")
{
    Timeline t;
    const Id lower = t.add<AddClipCommand>(t.v1, t.media, 0, 20 * kSec).clipId();
    const Id upper = t.add<AddClipCommand>(t.v2, t.media, 5 * kSec, 5 * kSec).clipId();
    t.add<AddClipCommand>(t.a1, t.media, 0, 20 * kSec);

    CHECK(visibleVideoAt(t.project, 2 * kSec)->clip->id == lower);
    CHECK(visibleVideoAt(t.project, 7 * kSec)->clip->id == upper);
    t.project.findTrack(t.v2)->hidden = true;
    CHECK(visibleVideoAt(t.project, 7 * kSec)->clip->id == lower);
}

TEST_CASE("nextVideoClipStart skips to the next clip after a gap")
{
    Timeline t;
    t.add<AddClipCommand>(t.v1, t.media, 0, 2 * kSec);
    t.add<AddClipCommand>(t.v2, t.media, 6 * kSec, 2 * kSec);
    t.add<AddClipCommand>(t.v1, t.media, 4 * kSec, 1 * kSec);

    CHECK(nextVideoClipStart(t.project, 2 * kSec) == 4 * kSec);
    CHECK(nextVideoClipStart(t.project, 4 * kSec) == 6 * kSec);
    CHECK_FALSE(nextVideoClipStart(t.project, 6 * kSec));
}

TEST_CASE("editPoints lists every clip edge once")
{
    Timeline t;
    t.add<AddClipCommand>(t.v1, t.media, 0, 2 * kSec);
    t.add<AddClipCommand>(t.v1, t.media, 2 * kSec, 3 * kSec);
    t.add<AddClipCommand>(t.a1, t.media, 1 * kSec, 1 * kSec);
    CHECK(editPoints(t.project) == std::vector<Ticks>{0, 1 * kSec, 2 * kSec, 5 * kSec});
}
