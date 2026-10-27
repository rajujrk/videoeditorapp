#include "commands/CommandStack.h"
#include "commands/EditCommands.h"
#include "io/ProjectSerializer.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

using namespace tve;

namespace {

constexpr Ticks kSec = kTicksPerSecond;

struct Fixture {
    Project project;
    CommandStack stack{project};
    Id videoTrack = kNoId;
    Id videoTrack2 = kNoId;
    Id audioTrack = kNoId;
    Id media = kNoId;

    Fixture()
    {
        videoTrack = push<AddTrackCommand>(TrackKind::Video, "V1").trackId();
        videoTrack2 = push<AddTrackCommand>(TrackKind::Video, "V2").trackId();
        audioTrack = push<AddTrackCommand>(TrackKind::Audio, "A1").trackId();
        media = push<ImportMediaCommand>("/media/clip.mp4", 60 * kSec).mediaId();
    }

    // Pushes a command that must succeed and returns it for reading assigned IDs.
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

    const Track& track(Id id) const { return *project.findTrack(id); }
};

// The ID counter deliberately does not rewind on undo (IDs are never reused), so state
// comparisons ignore it.
nlohmann::json snapshot(const Project& project)
{
    auto json = projectToJson(project);
    json.erase("nextId");
    return json;
}

} // namespace

TEST_CASE("adding clips keeps tracks sorted and rejects overlaps")
{
    Fixture f;
    f.push<AddClipCommand>(f.videoTrack, f.media, 10 * kSec, 5 * kSec);
    f.push<AddClipCommand>(f.videoTrack, f.media, 0, 5 * kSec);

    const auto& clips = f.track(f.videoTrack).clips;
    REQUIRE(clips.size() == 2);
    CHECK(clips[0].start == 0);
    CHECK(clips[1].start == 10 * kSec);

    CHECK(f.tryPush<AddClipCommand>(f.videoTrack, f.media, 4 * kSec, 2 * kSec));  // overlap
    CHECK(f.tryPush<AddClipCommand>(f.videoTrack, f.media, 20 * kSec, 61 * kSec)); // past media end
    CHECK(f.tryPush<AddClipCommand>(f.videoTrack, 9999, 20 * kSec, kSec));         // unknown media
    CHECK(f.track(f.videoTrack).clips.size() == 2);
}

TEST_CASE("clips touching end-to-start do not overlap")
{
    Fixture f;
    f.push<AddClipCommand>(f.videoTrack, f.media, 0, 5 * kSec);
    CHECK_FALSE(f.tryPush<AddClipCommand>(f.videoTrack, f.media, 5 * kSec, 5 * kSec));
}

TEST_CASE("locked tracks reject edits")
{
    Fixture f;
    f.project.findTrack(f.videoTrack)->locked = true;
    CHECK(f.tryPush<AddClipCommand>(f.videoTrack, f.media, 0, kSec) == "track is locked");
}

TEST_CASE("split creates two clips that cover the original")
{
    Fixture f;
    const Id clip = f.push<AddClipCommand>(f.videoTrack, f.media, 2 * kSec, 10 * kSec, 3 * kSec).clipId();
    const Id right = f.push<SplitClipCommand>(clip, 6 * kSec).rightClipId();

    const auto& clips = f.track(f.videoTrack).clips;
    REQUIRE(clips.size() == 2);
    CHECK(clips[0] == Clip{clip, f.media, 2 * kSec, 4 * kSec, 3 * kSec});
    CHECK(clips[1] == Clip{right, f.media, 6 * kSec, 6 * kSec, 7 * kSec});

    CHECK(f.tryPush<SplitClipCommand>(clip, 2 * kSec)); // at the clip's own start
    CHECK(f.tryPush<SplitClipCommand>(clip, 6 * kSec)); // at the clip's own end
}

TEST_CASE("move respects track kind and overlaps")
{
    Fixture f;
    const Id a = f.push<AddClipCommand>(f.videoTrack, f.media, 0, 5 * kSec).clipId();
    f.push<AddClipCommand>(f.videoTrack2, f.media, 10 * kSec, 5 * kSec);

    CHECK(f.tryPush<MoveClipCommand>(a, f.audioTrack, 0));         // video → audio
    CHECK(f.tryPush<MoveClipCommand>(a, f.videoTrack2, 12 * kSec)); // overlap on V2

    f.push<MoveClipCommand>(a, f.videoTrack2, 20 * kSec);
    CHECK(f.track(f.videoTrack).clips.empty());
    CHECK(f.track(f.videoTrack2).clips.back().id == a);
    CHECK(f.track(f.videoTrack2).clips.back().start == 20 * kSec);
}

TEST_CASE("move within the same track may overlap its own old position")
{
    Fixture f;
    const Id a = f.push<AddClipCommand>(f.videoTrack, f.media, 0, 5 * kSec).clipId();
    f.push<MoveClipCommand>(a, f.videoTrack, 2 * kSec);
    CHECK(f.track(f.videoTrack).clips.front().start == 2 * kSec);
}

TEST_CASE("ripple delete closes the gap; plain delete leaves it")
{
    Fixture f;
    const Id a = f.push<AddClipCommand>(f.videoTrack, f.media, 0, 4 * kSec).clipId();
    const Id b = f.push<AddClipCommand>(f.videoTrack, f.media, 4 * kSec, 3 * kSec).clipId();
    const Id c = f.push<AddClipCommand>(f.videoTrack, f.media, 10 * kSec, 2 * kSec).clipId();

    f.push<DeleteClipCommand>(b, /*ripple=*/true);
    const auto& clips = f.track(f.videoTrack).clips;
    REQUIRE(clips.size() == 2);
    CHECK(clips[0].id == a);
    CHECK(clips[1].id == c);
    CHECK(clips[1].start == 7 * kSec);

    f.push<DeleteClipCommand>(a, /*ripple=*/false);
    CHECK(f.track(f.videoTrack).clips.front().start == 7 * kSec);
}

TEST_CASE("every edit undoes to the exact prior state and redoes to the exact later state")
{
    Fixture f;
    std::vector<nlohmann::json> states{snapshot(f.project)};
    auto record = [&] { states.push_back(snapshot(f.project)); };

    const Id a = f.push<AddClipCommand>(f.videoTrack, f.media, 0, 8 * kSec).clipId();
    record();
    const Id b = f.push<AddClipCommand>(f.videoTrack, f.media, 8 * kSec, 4 * kSec, 10 * kSec).clipId();
    record();
    const Id right = f.push<SplitClipCommand>(a, 3 * kSec).rightClipId();
    record();
    f.push<MoveClipCommand>(right, f.videoTrack2, 30 * kSec);
    record();
    f.push<DeleteClipCommand>(b, true);
    record();
    f.push<DeleteClipCommand>(a, true);
    record();

    const Id idsAfterEdits = f.project.nextId();
    for (std::size_t i = states.size() - 1; i > 0; --i) {
        f.stack.undo();
        INFO("after undo to state " << i - 1);
        CHECK(snapshot(f.project) == states[i - 1]);
    }
    for (std::size_t i = 1; i < states.size(); ++i) {
        f.stack.redo();
        INFO("after redo to state " << i);
        CHECK(snapshot(f.project) == states[i]);
    }
    CHECK(f.project.nextId() == idsAfterEdits); // redo reuses the IDs it assigned first time
}

TEST_CASE("pushing after undo discards redo history and clean state")
{
    Fixture f;
    f.push<AddClipCommand>(f.videoTrack, f.media, 0, kSec);
    f.stack.setClean();
    f.stack.undo();
    CHECK_FALSE(f.stack.isClean());
    CHECK(f.stack.canRedo());

    f.push<AddClipCommand>(f.videoTrack, f.media, 5 * kSec, kSec);
    CHECK_FALSE(f.stack.canRedo());
    CHECK_FALSE(f.stack.isClean());
    CHECK(f.stack.undoLabel() == "Add Clip");
}

TEST_CASE("failed commands do not enter the history")
{
    Fixture f;
    const auto count = f.stack.count();
    CHECK(f.tryPush<SplitClipCommand>(12345, kSec) == "clip not found");
    CHECK(f.stack.count() == count);
}
