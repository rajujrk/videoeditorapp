#include "commands/CommandStack.h"
#include "commands/EditCommands.h"
#include "io/ProjectSerializer.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <stdexcept>

using namespace tve;

TEST_CASE("project survives a save/load round trip")
{
    Project project;
    project.settings = {.width = 3840, .height = 2160, .frameRate = {60000, 1001}, .sampleRate = 48000};
    CommandStack stack(project);

    auto track = std::make_unique<AddTrackCommand>(TrackKind::Video, "Main");
    auto* trackCmd = track.get();
    REQUIRE_FALSE(stack.push(std::move(track)));
    const Id trackId = trackCmd->trackId();

    auto media = std::make_unique<ImportMediaCommand>("/media/ünïcode clip.mov", 30 * kTicksPerSecond);
    auto* mediaCmd = media.get();
    REQUIRE_FALSE(stack.push(std::move(media)));
    REQUIRE_FALSE(stack.push(std::make_unique<AddClipCommand>(trackId, mediaCmd->mediaId(), 0, 10 * kTicksPerSecond, kTicksPerSecond)));
    project.findTrack(trackId)->muted = true;

    const auto path = std::filesystem::temp_directory_path() / "tve_roundtrip_test.tve.json";
    saveProject(project, path);
    const Project loaded = loadProject(path);
    std::filesystem::remove(path);

    CHECK(loaded == project);
    CHECK(loaded.nextId() == project.nextId());
}

TEST_CASE("loader rejects foreign and newer files")
{
    CHECK_THROWS_AS(projectFromJson(nlohmann::json{{"hello", "world"}}), std::runtime_error);
    CHECK_THROWS_AS(projectFromJson(nlohmann::json{{"schema", kProjectSchemaName}, {"version", kProjectSchemaVersion + 1}}),
                    std::runtime_error);
}
