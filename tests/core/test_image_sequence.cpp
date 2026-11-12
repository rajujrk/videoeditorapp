#include "commands/CommandStack.h"
#include "commands/EditCommands.h"
#include "io/ProjectSerializer.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

using namespace tve;

TEST_CASE("An image sequence's run and rate are saved with its media")
{
    Project project;
    CommandStack stack(project);
    MediaItem item{.path = "/shots/take_0001.png", .duration = 120 * frameDuration(FrameRate{24, 1}), .width = 1920,
                   .height = 1080, .frameRate = FrameRate{24, 1}, .alpha = true};
    item.sequence = ImageSequence{"take_%04d.png", 1, 120};
    auto import = std::make_unique<ImportMediaCommand>(item);
    auto* command = import.get();
    REQUIRE_FALSE(stack.push(std::move(import)));
    const Id id = command->mediaId();

    const nlohmann::json json = projectToJson(project);
    const Project loaded = projectFromJson(json);
    CHECK(loaded == project);
    const MediaItem* media = loaded.findMedia(id);
    REQUIRE(media);
    REQUIRE(media->sequence);
    CHECK(media->sequence->pattern == "take_%04d.png");
    CHECK(media->sequence->first == 1);
    CHECK(media->sequence->last == 120);
    CHECK(media->sequence->length() == 120);
    CHECK(media->frameRate == FrameRate{24, 1});
    CHECK_FALSE(media->still);

    // Media saved before sequences existed has none.
    nlohmann::json older = json;
    for (auto& m : older["media"])
        m.erase("sequence");
    const Project old = projectFromJson(older);
    REQUIRE(old.findMedia(id));
    CHECK_FALSE(old.findMedia(id)->sequence);
}

TEST_CASE("The frames of an image sequence by number")
{
    const ImageSequence padded{"shot_%04d.exr", 98, 102};
    CHECK(padded.length() == 5);
    CHECK(sequenceFrameFile("/a/b", padded, 99) == "/a/b/shot_0099.exr");
    CHECK(sequenceFrameFile("/a/b", padded, 12345) == "/a/b/shot_12345.exr"); // wider than its padding
    CHECK(sequenceDisplayName(padded) == "shot_####.exr");
    const ImageSequence plain{"f%d.png", 1, 3};
    CHECK(sequenceFrameFile("/x", plain, 3) == "/x/f3.png");
    CHECK(sequenceDisplayName(plain) == "f#.png");
    // Nothing on disk: no frames.
    CHECK(sequenceFrameFiles("/no/such/folder/f1.png", plain).empty());
    // Registered for decoders, by the first frame's path.
    registerImageSequence("/x/f1.png", plain, FrameRate{25, 1});
    const auto registered = registeredImageSequence("/x/f1.png");
    REQUIRE(registered);
    CHECK(registered->sequence == plain);
    CHECK(registered->rate == FrameRate{25, 1});
    CHECK_FALSE(registeredImageSequence("/x/f2.png"));
}
