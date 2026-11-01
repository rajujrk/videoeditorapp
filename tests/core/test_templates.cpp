#include "commands/CommandStack.h"
#include "commands/EditCommands.h"
#include "commands/TemplateCommands.h"
#include "io/ProjectSerializer.h"
#include "model/Template.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <filesystem>

using namespace tve;
using Catch::Approx;

TEST_CASE("six template shapes, and the nearest to a canvas")
{
    REQUIRE(templateShapes().size() == 6);
    CHECK(templateShape("9:16")->width == 1080);
    CHECK(templateShape("9:16")->height == 1920);
    CHECK(templateShape("21:9")->width == 2560);
    CHECK(!templateShape("5:4"));
    CHECK(nearestShape(3840, 2160).id == "16:9");
    CHECK(nearestShape(720, 1280).id == "9:16");
    CHECK(nearestShape(1000, 1010).id == "1:1");
    CHECK(nearestShape(1200, 900).id == "4:3");
}

TEST_CASE("a picture is cropped to fill a canvas of another shape")
{
    // Wide into tall: the sides go.
    const ClipCrop tall = fillCrop(1920, 1080, 1080, 1920);
    CHECK(tall.top == 0);
    CHECK(tall.left == Approx((1.0 - (9.0 / 16) / (16.0 / 9)) / 2));
    CHECK(tall.left == tall.right);
    // Tall into wide: top and bottom go.
    const ClipCrop wide = fillCrop(1080, 1920, 1920, 1080);
    CHECK(wide.left == 0);
    CHECK(wide.top == Approx(tall.left));
    // The same shape: nothing.
    CHECK(fillCrop(1280, 720, 1920, 1080) == ClipCrop{});
    CHECK(fillCrop(0, 0, 1920, 1080) == ClipCrop{});
}

TEST_CASE("a short video is slowed to fill its slot, but not below a quarter speed")
{
    CHECK(fillSpeed(10 * kTicksPerSecond, 4 * kTicksPerSecond) == 1.0);
    CHECK(fillSpeed(3 * kTicksPerSecond, 4 * kTicksPerSecond) == Approx(0.75));
    CHECK(fillSpeed(kTicksPerSecond / 2, 4 * kTicksPerSecond) == 0.25);
}

TEST_CASE("filling a template slot replaces the clip's media in one undoable step")
{
    Project project;
    CommandStack stack(project);
    auto track = std::make_unique<AddTrackCommand>(TrackKind::Video, "Video 1");
    auto* trackCmd = track.get();
    REQUIRE_FALSE(stack.push(std::move(track)));
    MediaItem placeholder{.id = project.allocateId(), .path = "/t/slot-1.png", .duration = kStillDuration, .width = 480,
                          .height = 270, .still = true};
    MediaItem video{.id = project.allocateId(), .path = "/m/clip.mov", .duration = 10 * kTicksPerSecond, .width = 1080,
                    .height = 1920};
    project.media = {placeholder, video};
    Clip clip{.id = project.allocateId(), .mediaId = placeholder.id, .start = 0, .duration = 4 * kTicksPerSecond};
    clip.transitionIn = ClipTransition{.transitionId = "tve.transition.dissolve"};
    clip.visual.transform.scale = 1.1;
    project.findTrack(trackCmd->trackId())->insertClip(clip);
    project.templateInfo = TemplateInfo{.id = "tve.template.test", .mediaSlots = {{.clipId = clip.id, .label = "One"}}};
    const Project before = project;

    REQUIRE_FALSE(stack.push(std::make_unique<ReplaceClipSourceCommand>(clip.id, video.id, 2 * kTicksPerSecond, 1.0,
                                                                        fillCrop(1080, 1920, 1920, 1080), 0, "Fill Slot")));
    const Clip& filled = *project.locateClip(clip.id)->clip;
    CHECK(filled.mediaId == video.id);
    CHECK(filled.sourceIn == 2 * kTicksPerSecond);
    CHECK(filled.duration == 4 * kTicksPerSecond);
    CHECK(filled.visual.crop.top > 0);
    CHECK(filled.visual.transform.scale == 1.1); // its placement stays
    CHECK(filled.transitionIn);                  // and its transition
    CHECK(project.templateInfo->mediaSlots[0].filled);
    stack.undo();
    CHECK(project == before);
    stack.redo();
    CHECK(project.templateInfo->mediaSlots[0].filled);

    // Too little source for the clip at that speed, a slot that is not this clip's.
    CHECK(stack.push(std::make_unique<ReplaceClipSourceCommand>(clip.id, video.id, 8 * kTicksPerSecond, 1.0, std::nullopt, 0,
                                                                "Fill Slot")));
    CHECK(stack.push(std::make_unique<ReplaceClipSourceCommand>(clip.id, video.id, 0, 1.0, std::nullopt, 3, "Fill Slot")));
    // Slowed to fill, it fits.
    CHECK_FALSE(stack.push(std::make_unique<ReplaceClipSourceCommand>(clip.id, video.id, 8 * kTicksPerSecond, 0.5,
                                                                      std::nullopt, 0, "Fill Slot")));

    // The template parts survive a save and load.
    const auto path = std::filesystem::temp_directory_path() / "tve_template_test.tve";
    saveProject(project, path);
    const Project loaded = loadProject(path);
    std::filesystem::remove(path);
    CHECK(loaded == project);
    REQUIRE(loaded.templateInfo);
    CHECK(loaded.templateInfo->mediaSlots[0].label == "One");
}
