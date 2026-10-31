#include "imports/ImportSources.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

#include <unistd.h>

using namespace tve;
namespace fs = std::filesystem;

namespace {

void touch(const fs::path& path)
{
    fs::create_directories(path.parent_path());
    std::ofstream(path) << "x";
}

} // namespace

TEST_CASE("a free file name keeps its extension and counts up")
{
    const fs::path folder = fs::temp_directory_path() / ("tve_names_" + std::to_string(::getpid()));
    fs::remove_all(folder);
    CHECK(freeFileName(folder.string(), "IMG_1.HEIC") == "IMG_1.HEIC");
    touch(folder / "IMG_1.HEIC");
    CHECK(freeFileName(folder.string(), "IMG_1.HEIC") == "IMG_1 2.HEIC");
    touch(folder / "IMG_1 2.HEIC");
    CHECK(freeFileName(folder.string(), "IMG_1.HEIC") == "IMG_1 3.HEIC");
    // Only the name of a path counts; an empty one gets a name.
    CHECK(freeFileName(folder.string(), "../elsewhere/clip.mov") == "clip.mov");
    CHECK(freeFileName(folder.string(), "") == "Imported");
    fs::remove_all(folder);
}

TEST_CASE("a folder source serves sub-folders as collections of their media")
{
    const fs::path root = fs::temp_directory_path() / ("tve_source_" + std::to_string(::getpid()));
    fs::remove_all(root);
    touch(root / "Trip" / "b.mov");
    touch(root / "Trip" / "a.jpg");
    touch(root / "Trip" / "notes.txt");
    touch(root / "Trip" / "song.m4a");
    touch(root / "Empty" / "readme.md");
    const fs::path into = root.string() + "_into";

    auto source = makeFolderSource(root.string(), true);
    CHECK(source->access() == ImportAccess::Granted);
    const auto collections = source->collections();
    REQUIRE(collections.size() == 2);
    CHECK(collections[0].title == "Empty");
    CHECK(collections[0].count == 0);
    CHECK(collections[1].title == "Trip");
    CHECK(collections[1].count == 3);
    const auto items = source->items("Trip");
    REQUIRE(items.size() == 3);
    CHECK(items[0].id == "Trip/a.jpg");
    CHECK(items[0].kind == "photo");
    CHECK(items[1].kind == "video");
    CHECK(items[2].kind == "audio");
    CHECK(items[0].created > 0);
    CHECK(source->items("../Trip").empty());

    // Copies come out under free names; a missing or outside file is an error.
    std::string got, why;
    source->fetch("Trip/a.jpg", into.string(), [&](std::string path, std::string error) { got = path; why = error; });
    CHECK(got == (into / "a.jpg").string());
    CHECK(fs::exists(got));
    source->fetch("Trip/a.jpg", into.string(), [&](std::string path, std::string) { got = path; });
    CHECK(got == (into / "a 2.jpg").string());
    source->fetch("Trip/missing.jpg", into.string(), [&](std::string path, std::string error) { got = path; why = error; });
    CHECK(got.empty());
    CHECK(!why.empty());
    source->fetch("../outside.jpg", into.string(), [&](std::string path, std::string) { got = path; });
    CHECK(got.empty());

    // A source that does not copy hands back the file itself.
    auto music = makeFolderSource(root.string(), false);
    music->fetch("Trip/song.m4a", into.string(), [&](std::string path, std::string) { got = path; });
    CHECK(got == (root / "Trip" / "song.m4a").string());
    fs::remove_all(root);
    fs::remove_all(into);
}
