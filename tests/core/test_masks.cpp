#include "commands/CommandStack.h"
#include "commands/EditCommands.h"
#include "io/ProjectSerializer.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

using namespace tve;

namespace {

constexpr Ticks kSec = kTicksPerSecond;

struct KeyProject {
    Project project;
    CommandStack stack{project};
    Id audio = kNoId;
    Id video = kNoId;
    Id media = kNoId;
    Id clip = kNoId;

    KeyProject()
    {
        audio = push<AddTrackCommand>(TrackKind::Audio, "A1").trackId();
        video = push<AddTrackCommand>(TrackKind::Video, "V1").trackId();
        media = push<ImportMediaCommand>(MediaItem{.path = "/m.mp4", .duration = 60 * kSec, .hasAudio = true}).mediaId();
        clip = push<AddClipCommand>(video, media, 0, 10 * kSec, 0).clipId();
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
    const Clip& get(Id id) { return *project.locateClip(id)->clip; }
};

ClipMask pen(std::vector<std::array<double, 2>> points)
{
    ClipMask m;
    m.shape = MaskShape::Pen;
    m.points = std::move(points);
    return m;
}

} // namespace

TEST_CASE("mask and key validation", "[masks]")
{
    CHECK_FALSE(validateMasks({}));
    CHECK_FALSE(validateMasks({ClipMask{}, pen({{0, 0}, {1, 0}, {1, 1}})}));
    CHECK(validateMasks({pen({{0, 0}, {1, 0}})}).value() == "a pen mask needs at least three points");
    ClipMask withPoints;
    withPoints.points = {{0, 0}, {1, 0}, {1, 1}};
    CHECK(validateMasks({withPoints}).value() == "only pen masks have points");
    ClipMask wide;
    wide.feather = 0.6;
    CHECK(validateMasks({wide}).value() == "a mask setting is out of range");
    wide.feather = 0.1;
    wide.x = std::nan("");
    CHECK(validateMasks({wide}).value() == "a mask setting is out of range");
    // Partly outside the picture is fine.
    wide.x = -0.5;
    wide.width = 3;
    CHECK_FALSE(validateMasks({wide}));
    CHECK(validateMasks(std::vector<ClipMask>(kMaxMasks + 1)).has_value());
    // The shader holds 32 pen points in all.
    std::vector<std::array<double, 2>> many(20, {0.5, 0.5});
    CHECK(validateMasks({pen(many), pen(many)}).has_value());
    CHECK(validateMasks({pen({{0, 0}, {3, 0}, {1, 1}})}).value() == "a pen mask point is out of range");

    CHECK_FALSE(validateChromaKey({}));
    ChromaKey key;
    key.color = {0, 1.2, 0};
    CHECK(validateChromaKey(key).value() == "the key colour is out of range");
    key = {};
    key.spill = -0.1;
    CHECK(validateChromaKey(key).value() == "chroma key settings must be between 0 and 100%");
}

TEST_CASE("masks, keys and mattes on clips", "[masks]")
{
    KeyProject p;
    ClipMask ellipse;
    ellipse.shape = MaskShape::Ellipse;
    ellipse.invert = true;
    ellipse.rotation = 30;
    ChromaKey key;
    key.enabled = true;
    key.color = {0.05, 0.28, 0.85};
    auto& set = p.push<SetClipKeyingCommand>(p.clip, ClipKeying{{ellipse, pen({{0.1, 0.1}, {0.9, 0.2}, {0.5, 0.9}})}, key,
                                                                MatteMode::Luma},
                                             "Add Mask");
    CHECK(set.label() == "Add Mask");
    const Clip& clip = p.get(p.clip);
    REQUIRE(clip.masks.size() == 2);
    CHECK(clip.masks[0].invert);
    CHECK(clip.masks[1].points.size() == 3);
    CHECK(clip.chromaKey.enabled);
    CHECK(clip.matte == MatteMode::Luma);
    p.stack.undo();
    CHECK(p.get(p.clip).masks.empty());
    CHECK_FALSE(p.get(p.clip).chromaKey.enabled);
    CHECK(p.get(p.clip).matte == MatteMode::None);
    p.stack.redo();

    // Splits keep them, and they are saved.
    auto& split = p.push<SplitClipCommand>(p.clip, 4 * kSec);
    const Clip& right = p.get(split.rightClipId());
    CHECK(right.masks == p.get(p.clip).masks);
    CHECK(right.chromaKey == p.get(p.clip).chromaKey);
    CHECK(right.matte == MatteMode::Luma);
    const nlohmann::json json = projectToJson(p.project);
    CHECK(json["tracks"][1]["clips"][0]["masks"][0]["shape"] == "ellipse");
    CHECK(json["tracks"][1]["clips"][0]["matte"] == "luma");
    const Project loaded = projectFromJson(json);
    const Clip& again = loaded.tracks[1].clips[0];
    CHECK(again.masks == p.get(p.clip).masks);
    CHECK(again.chromaKey == p.get(p.clip).chromaKey);
    CHECK(again.matte == MatteMode::Luma);
    // Older projects have none.
    nlohmann::json old = json;
    for (const char* field : {"masks", "chromaKey", "matte"})
        old["tracks"][1]["clips"][0].erase(field);
    const Clip& plain = projectFromJson(old).tracks[1].clips[0];
    CHECK(plain.masks.empty());
    CHECK_FALSE(plain.chromaKey.enabled);
    CHECK(plain.matte == MatteMode::None);

    // Only on pictures, and only valid ones.
    const Id sound = p.push<AddClipCommand>(p.audio, p.media, 0, kSec, 0).clipId();
    CHECK(p.tryPush(std::make_unique<SetClipKeyingCommand>(sound, ClipKeying{}, "x")).value()
          == "masks and keys apply to pictures");
    const Id layer = p.push<AddAdjustmentCommand>(kNoId, 0, kSec).clipId();
    CHECK(p.tryPush(std::make_unique<SetClipKeyingCommand>(layer, ClipKeying{}, "x")).value()
          == "masks and keys apply to pictures");
    CHECK(p.tryPush(std::make_unique<SetClipKeyingCommand>(p.clip, ClipKeying{{pen({{0, 0}})}, {}, MatteMode::None}, "x"))
              .value()
          == "a pen mask needs at least three points");
}

TEST_CASE("several commands as one undo step", "[masks]")
{
    KeyProject p;
    const Id second = p.push<AddClipCommand>(p.video, p.media, 20 * kSec, 5 * kSec, 0).clipId();
    ClipVisual left;
    left.transform.x = -0.25;
    ClipVisual right;
    right.transform.x = 0.25;
    std::vector<std::unique_ptr<Command>> commands;
    commands.push_back(std::make_unique<SetClipVisualCommand>(p.clip, left));
    commands.push_back(std::make_unique<SetClipVisualCommand>(second, right));
    auto& both = p.push<CompositeCommand>(std::move(commands), "Apply Layout");
    CHECK(both.label() == "Apply Layout");
    CHECK(p.get(p.clip).visual.transform.x == -0.25);
    CHECK(p.get(second).visual.transform.x == 0.25);
    CHECK(p.stack.undoLabel() == "Apply Layout");
    p.stack.undo();
    CHECK(p.get(p.clip).visual.transform.x == 0);
    CHECK(p.get(second).visual.transform.x == 0);
    p.stack.redo();
    CHECK(p.get(second).visual.transform.x == 0.25);

    // One failing part undoes the ones before it and fails the whole.
    std::vector<std::unique_ptr<Command>> failing;
    ClipVisual moved;
    moved.transform.x = 0.4;
    failing.push_back(std::make_unique<SetClipVisualCommand>(p.clip, moved));
    failing.push_back(std::make_unique<SetClipVisualCommand>(Id{9999}, moved));
    CHECK(p.tryPush(std::make_unique<CompositeCommand>(std::move(failing), "x")).has_value());
    CHECK(p.get(p.clip).visual.transform.x == -0.25);
}
