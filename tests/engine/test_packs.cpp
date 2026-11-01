#include "packs/Packs.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

using namespace tve;
namespace fs = std::filesystem;

namespace {

void write(const fs::path& path, const std::string& bytes)
{
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << bytes;
}

} // namespace

TEST_CASE("pack paths and ids may not leave their folder")
{
    CHECK(safeRelativePath("sounds/manifest.json"));
    CHECK(safeRelativePath("a"));
    CHECK_FALSE(safeRelativePath(""));
    CHECK_FALSE(safeRelativePath("/etc/passwd"));
    CHECK_FALSE(safeRelativePath("a/../../b"));
    CHECK_FALSE(safeRelativePath("a//b"));
    CHECK_FALSE(safeRelativePath("./a"));
    CHECK_FALSE(safeRelativePath("a\\b"));
    CHECK(validPackId("tve.music.lofi-2"));
    CHECK_FALSE(validPackId(".hidden"));
    CHECK_FALSE(validPackId("a/b"));
    CHECK_FALSE(validPackId(""));
}

TEST_CASE("a pack's files are listed, checked and say what it holds")
{
    const fs::path folder = fs::temp_directory_path() / "tve_pack_test";
    fs::remove_all(folder);
    write(folder / "sounds/manifest.json", "{}");
    write(folder / "stickers/stickers.json", "{\"stickers\": []}");
    write(folder / "stickers/star.json", "lottie");
    write(folder / "pack.json", "ignored");
    PackManifest manifest{.id = "tve.test", .version = 1, .title = "Test"};
    manifest.files = listPackFiles(folder.string());
    REQUIRE(manifest.files.size() == 3);
    CHECK(manifest.files[0].path == "sounds/manifest.json");
    CHECK(manifest.files[0].size == 2);
    CHECK(manifest.files[0].sha256 == bytesSha256("{}"));
    CHECK(packContents(manifest) == std::vector<std::string>{"sounds", "stickers"});
    CHECK_FALSE(checkPackFolder(folder.string(), manifest));

    // Read back from its JSON.
    const auto back = readPackManifest(packManifestJson(manifest));
    REQUIRE(back);
    CHECK(back->id == "tve.test");
    CHECK(back->files.size() == 3);

    // A changed file, then a missing one.
    write(folder / "stickers/star.json", "LOTTIE");
    CHECK(checkPackFolder(folder.string(), manifest).value_or("") == "stickers/star.json is not the file it should be");
    fs::remove(folder / "stickers/star.json");
    CHECK(checkPackFolder(folder.string(), manifest).value_or("") == "stickers/star.json is missing");

    // Manifests that try to write outside the folder, or list a file twice, are refused.
    PackManifest bad = manifest;
    bad.files[0].path = "../escape";
    std::string error;
    CHECK_FALSE(readPackManifest(packManifestJson(bad), &error));
    CHECK(error.find("may not") != std::string::npos);
    bad = manifest;
    bad.files.push_back(bad.files[0]);
    CHECK_FALSE(readPackManifest(packManifestJson(bad)));
    CHECK_FALSE(readPackManifest("{\"format\": \"tve-pack\", \"version\": 2}"));
    fs::remove_all(folder);
}

TEST_CASE("a catalogue reads back, leaving out packs it can't fetch safely")
{
    Catalogue catalogue{.sequence = 4, .published = "2026-11-01"};
    catalogue.packs.push_back({.id = "tve.music", .version = 2, .title = "Music", .contents = {"sounds"}, .size = 100,
                               .manifest = "packs/tve.music/2/pack.json", .manifestSha256 = bytesSha256("x")});
    catalogue.packs.push_back({.id = "tve.bad", .version = 1, .title = "Bad", .manifest = "../../pack.json",
                               .manifestSha256 = bytesSha256("y")});
    catalogue.packs.push_back({.id = "tve.music", .version = 3, .title = "Again", .manifest = "packs/again/pack.json",
                               .manifestSha256 = bytesSha256("z")});
    const auto back = readCatalogue(catalogueJson(catalogue));
    REQUIRE(back);
    CHECK(back->sequence == 4);
    REQUIRE(back->packs.size() == 1);
    CHECK(back->packs[0].contents == std::vector<std::string>{"sounds"});
    CHECK(back->packs[0].version == 2);
    CHECK_FALSE(readCatalogue("[]"));
    CHECK_FALSE(readCatalogue(std::string(kMostCatalogueBytes + 1, ' ')));
}

TEST_CASE("catalogue signatures: P-256 with SHA-256")
{
    const auto key = makeSigningKey();
    REQUIRE(key);
    CHECK(key->publicKey.size() == 91);
    const std::string data = "{\"format\": \"tve-pack-catalogue\"}";
    const std::string signature = signData(data, key->privateKey);
    REQUIRE_FALSE(signature.empty());
    CHECK(verifySignature(data, signature, key->publicKey));
    // Changed data, another key, or a damaged signature: not it.
    CHECK_FALSE(verifySignature(data + " ", signature, key->publicKey));
    const auto other = makeSigningKey();
    REQUIRE(other);
    CHECK_FALSE(verifySignature(data, signature, other->publicKey));
    std::string damaged = signature;
    damaged[damaged.size() / 2] ^= 0x40;
    CHECK_FALSE(verifySignature(data, damaged, key->publicKey));
    CHECK_FALSE(verifySignature(data, signature, "not a key"));
    CHECK_FALSE(verifySignature(data, "", key->publicKey));
}
