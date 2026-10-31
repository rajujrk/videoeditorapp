#include "commands/CommandStack.h"
#include "commands/EditCommands.h"
#include "commands/StructureCommands.h"

#include <catch2/catch_test_macros.hpp>

using namespace tve;

namespace {

constexpr Ticks kSec = kTicksPerSecond;

struct Fixture {
    Project project;
    CommandStack stack{project};
    Id track = kNoId;
    Id other = kNoId;
    Id media = kNoId;

    Fixture()
    {
        auto add = std::make_unique<AddTrackCommand>(TrackKind::Video, "V1");
        auto* addCmd = add.get();
        REQUIRE_FALSE(stack.push(std::move(add)));
        track = addCmd->trackId();
        auto add2 = std::make_unique<AddTrackCommand>(TrackKind::Video, "V2");
        auto* add2Cmd = add2.get();
        REQUIRE_FALSE(stack.push(std::move(add2)));
        other = add2Cmd->trackId();
        auto import = std::make_unique<ImportMediaCommand>(MediaItem{.path = "/a.mov", .duration = 60 * kSec});
        auto* importCmd = import.get();
        REQUIRE_FALSE(stack.push(std::move(import)));
        media = importCmd->mediaId();
    }

    void clip(Id onTrack, Ticks start, Ticks duration, Ticks sourceIn = 0)
    {
        REQUIRE_FALSE(stack.push(std::make_unique<AddClipCommand>(onTrack, media, start, duration, sourceIn)));
    }

    // (start, duration, sourceIn) of each clip on the track, in seconds.
    std::vector<std::tuple<double, double, double>> layout(Id onTrack)
    {
        std::vector<std::tuple<double, double, double>> out;
        for (const Clip& c : project.findTrack(onTrack)->clips)
            out.emplace_back(toSeconds(c.start), toSeconds(c.duration), toSeconds(c.sourceIn));
        return out;
    }
};

} // namespace

TEST_CASE("removing ranges splits, deletes and closes the gaps", "[ranges]")
{
    Fixture f;
    f.clip(f.track, 0, 10 * kSec);           // one 10 s clip
    f.clip(f.track, 12 * kSec, 3 * kSec, 20 * kSec); // a gap, then another
    f.clip(f.other, 5 * kSec, 2 * kSec);     // another track: untouched

    // 2–3 s and 6–7 s inside the first clip, 9–13 s across its end, the gap and into the second.
    auto command = std::make_unique<RemoveRangesCommand>(
        f.track, std::vector<std::pair<Ticks, Ticks>>{{6 * kSec, 7 * kSec}, {2 * kSec, 3 * kSec}, {9 * kSec, 13 * kSec}},
        "Remove Silences");
    auto* raw = command.get();
    REQUIRE_FALSE(f.stack.push(std::move(command)));
    CHECK(raw->removed() == 6 * kSec);
    CHECK(f.stack.undoLabel() == "Remove Silences");
    using Layout = std::vector<std::tuple<double, double, double>>;
    CHECK(f.layout(f.track) == Layout{{0, 2, 0}, {2, 3, 3}, {5, 2, 7}, {7, 2, 21}});
    CHECK(f.layout(f.other) == Layout{{5, 2, 0}});

    const auto after = f.project;
    f.stack.undo();
    CHECK(f.layout(f.track) == Layout{{0, 10, 0}, {12, 3, 20}});
    f.stack.redo();
    CHECK(f.project == after); // the same pieces, the same ids
}

TEST_CASE("overlapping and touching ranges merge; nothing to remove is refused", "[ranges]")
{
    Fixture f;
    f.clip(f.track, 0, 10 * kSec);
    REQUIRE_FALSE(f.stack.push(std::make_unique<RemoveRangesCommand>(
        f.track, std::vector<std::pair<Ticks, Ticks>>{{1 * kSec, 3 * kSec}, {2 * kSec, 4 * kSec}, {4 * kSec, 5 * kSec}})));
    using Layout = std::vector<std::tuple<double, double, double>>;
    CHECK(f.layout(f.track) == Layout{{0, 1, 0}, {1, 5, 5}});

    CHECK(RemoveRangesCommand(f.track, {{20 * kSec, 25 * kSec}}).redo(f.project) == "no time to remove");
    CHECK(RemoveRangesCommand(f.track, {{3 * kSec, 3 * kSec}}).redo(f.project) == "no time to remove");
    CHECK(RemoveRangesCommand(999, {{0, kSec}}).redo(f.project) == "track not found");
    REQUIRE_FALSE(f.stack.push(std::make_unique<SetTrackFlagCommand>(f.track, SetTrackFlagCommand::Flag::Locked, true)));
    CHECK(RemoveRangesCommand(f.track, {{0, kSec}}).redo(f.project) == "track is locked");
}
