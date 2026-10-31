#include "commands/CommandStack.h"
#include "commands/EditCommands.h"
#include "io/MediaRelink.h"
#include "io/ProjectSerializer.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>

#include <unistd.h>

using namespace tve;
namespace fs = std::filesystem;

namespace {

constexpr Ticks kSec = kTicksPerSecond;

// A scratch folder of this process's own (test cases run in parallel), removed afterwards.
struct Scratch {
    fs::path root = fs::temp_directory_path() / ("tve_relink_test_" + std::to_string(::getpid()));
    Scratch()
    {
        fs::remove_all(root);
        fs::create_directories(root);
    }
    ~Scratch() { fs::remove_all(root); }

    // A file of `size` bytes whose content depends on `seed`.
    std::string write(const fs::path& relative, std::size_t size, char seed) const
    {
        const fs::path path = root / relative;
        fs::create_directories(path.parent_path());
        std::ofstream out(path, std::ios::binary);
        for (std::size_t i = 0; i < size; ++i)
            out.put(char(seed + char(i * 7 % 251)));
        return path.string();
    }
};

MediaItem itemFor(Id id, const std::string& path)
{
    MediaItem item{.id = id, .path = path, .duration = 10 * kSec};
    item.fingerprint = fingerprintFile(path).value_or("");
    return item;
}

} // namespace

TEST_CASE("fingerprints tell files apart by content, not by name", "[relink]")
{
    Scratch scratch;
    const std::string a = scratch.write("a.mov", 300'000, 1);
    const std::string copy = scratch.write("elsewhere/renamed.mov", 300'000, 1);
    const std::string other = scratch.write("b.mov", 300'000, 2);
    const std::string small = scratch.write("small.wav", 1000, 1);
    REQUIRE(fingerprintFile(a));
    CHECK(fingerprintFile(a) == fingerprintFile(copy));
    CHECK(fingerprintFile(a) != fingerprintFile(other));
    CHECK(fingerprintFile(small)->starts_with("v1:1000:"));
    CHECK_FALSE(fingerprintFile((scratch.root / "none.mov").string()));

    const MediaItem item = itemFor(1, a);
    CHECK(sameMedia(item, copy));
    CHECK_FALSE(sameMedia(item, other));
    MediaItem unknown = item;
    unknown.fingerprint.clear(); // imported before fingerprints: any file will do
    CHECK(sameMedia(unknown, other));
}

TEST_CASE("missing media is found in a folder by name, then by content", "[relink]")
{
    Scratch scratch;
    Project project;
    // Imported from "old/", which then went away.
    const std::string original = scratch.write("old/interview.mov", 200'000, 3);
    const std::string renamedOriginal = scratch.write("old/b-roll.mp4", 150'000, 4);
    const std::string clash = scratch.write("old/music.wav", 5000, 5);
    project.media = {itemFor(1, original), itemFor(2, renamedOriginal), itemFor(3, clash),
                     MediaItem{.id = 4, .path = (scratch.root / "old/notes.png").string(), .duration = kSec}};
    CHECK(missingMedia(project).empty() == false); // notes.png never existed
    fs::rename(scratch.root / "old", scratch.root / "moved");
    REQUIRE(missingMedia(project).size() == 4);

    // interview.mov: same name and content. b-roll.mp4: renamed, found by content.
    // music.wav: a different file under that name, not taken. notes.png: no fingerprint, by name.
    fs::create_directories(scratch.root / "moved/deep");
    fs::rename(scratch.root / "moved/b-roll.mp4", scratch.root / "moved/deep/clip 7.mp4");
    fs::remove(scratch.root / "moved/music.wav");
    scratch.write("moved/music.wav", 5000, 9);
    scratch.write("moved/sub/notes.png", 10, 1);
    const auto found = findMissingMedia(missingMedia(project), scratch.root.string());
    CHECK(found.at(1) == (scratch.root / "moved/interview.mov").string());
    CHECK(found.at(2) == (scratch.root / "moved/deep/clip 7.mp4").string());
    CHECK_FALSE(found.contains(3));
    CHECK(found.at(4) == (scratch.root / "moved/sub/notes.png").string());

    // A search that gives up early finds less, and a missing folder nothing.
    CHECK(findMissingMedia(missingMedia(project), scratch.root.string(), {.maxFiles = 1}).size() <= 1);
    CHECK(findMissingMedia(missingMedia(project), (scratch.root / "nowhere").string()).empty());
}

TEST_CASE("relinking points media at new files in one undo step", "[relink]")
{
    Project project;
    CommandStack stack(project);
    auto import = std::make_unique<ImportMediaCommand>(MediaItem{.path = "/gone/a.mov", .duration = 10 * kSec, .width = 1920});
    auto* importCmd = import.get();
    REQUIRE_FALSE(stack.push(std::move(import)));
    const Id id = importCmd->mediaId();

    MediaItem moved = *project.findMedia(id);
    moved.path = "/new/a.mov";
    moved.width = 3840; // what probing the new file found
    REQUIRE_FALSE(stack.push(std::make_unique<RelinkMediaCommand>(std::vector{moved})));
    CHECK(project.findMedia(id)->path == "/new/a.mov");
    CHECK(project.findMedia(id)->width == 3840);
    CHECK(stack.undoLabel() == "Relink Media");
    stack.undo();
    CHECK(project.findMedia(id)->path == "/gone/a.mov");
    CHECK(project.findMedia(id)->width == 1920);

    MediaItem stranger = moved;
    stranger.id = 999;
    CHECK(RelinkMediaCommand({stranger}).redo(project) == "media not found");
    MediaItem empty = moved;
    empty.duration = 0;
    CHECK(RelinkMediaCommand({empty}).redo(project));

    // Fingerprints are saved; older files without them still load.
    MediaItem printed = moved;
    printed.fingerprint = "v1:10:00000000000000ff";
    project.media = {printed};
    const Project loaded = projectFromJson(projectToJson(project));
    CHECK(loaded.media[0].fingerprint == printed.fingerprint);
    auto json = projectToJson(project);
    json["media"][0].erase("fingerprint");
    CHECK(projectFromJson(json).media[0].fingerprint.empty());
}
