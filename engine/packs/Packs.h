#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace tve {

// Content packs from a static host: a signed catalogue lists the packs; each pack is a
// folder on the host whose pack.json lists every file in it with its size and SHA-256. The
// catalogue gives each pack.json's SHA-256, so its one signature covers every file.
//
// Catalogue, catalogue.json (signed: catalogue.json.sig beside it):
//   {"format": "tve-pack-catalogue", "version": 1, "sequence": 7, "published": "2026-11-01",
//    "packs": [{"id", "title", "description", "author", "license", "version": 2,
//               "contents": ["sounds", "templates", "stickers", "effects", "transitions"],
//               "size": bytes, "manifest": "packs/<id>/2/pack.json", "sha256": "…",
//               "preview"?: "packs/<id>/2/preview.png", "previewSha256"?: "…"}]}
// Pack, pack.json:
//   {"format": "tve-pack", "version": 1, "id", "packVersion": 2, "title", "description",
//    "author", "license", "files": [{"path": "sounds/manifest.json", "size", "sha256"}]}
// A pack's folders say what it holds: sounds/ (a sound pack, tve-sound-pack), templates/
// (.tvet folders), stickers/ (stickers.json and Lottie files), effects/ and transitions/
// (a folder each with manifest.json and compiled .qsb shaders). Paths are relative, with
// "/" between parts, and may not leave the folder they are in.
//
// Signatures are ECDSA P-256 over SHA-256, DER-encoded (as `openssl dgst -sha256 -sign`
// writes them); public keys are DER SubjectPublicKeyInfo (`openssl ec -pubout -outform DER`).

inline constexpr int kPackFormatVersion = 1;
inline constexpr std::int64_t kMostPackBytes = 2LL << 30;   // 2 GB
inline constexpr std::size_t kMostPackFiles = 5000;
inline constexpr std::size_t kMostCatalogueBytes = 4 << 20; // 4 MB

struct PackFile {
    std::string path;
    std::int64_t size = 0;
    std::string sha256;
};

struct PackManifest {
    std::string id;
    int version = 0;
    std::string title;
    std::string description;
    std::string author;
    std::string license;
    std::vector<PackFile> files;
};

struct CatalogueEntry {
    std::string id;
    int version = 0;
    std::string title;
    std::string description;
    std::string author;
    std::string license;
    std::vector<std::string> contents;
    std::int64_t size = 0;
    std::string manifest;       // relative to the catalogue
    std::string manifestSha256;
    std::string preview;        // relative to the catalogue, or empty
    std::string previewSha256;
};

struct Catalogue {
    std::int64_t sequence = 0; // goes up with every catalogue published
    std::string published;
    std::vector<CatalogueEntry> packs;
};

// A relative path made of ordinary parts ("a/b.wav"; not "", "/a", "a/../b", "a\b").
bool safeRelativePath(const std::string& path);
// An id of letters, digits, '.', '-' and '_', 1 to 100 long, starting with a letter or digit.
bool validPackId(const std::string& id);

std::optional<PackManifest> readPackManifest(const std::string& json, std::string* error = nullptr);
std::string packManifestJson(const PackManifest& manifest);
std::optional<Catalogue> readCatalogue(const std::string& json, std::string* error = nullptr);
std::string catalogueJson(const Catalogue& catalogue);

// Every file in `folder` (but its pack.json) with its size and SHA-256, in path order.
std::vector<PackFile> listPackFiles(const std::string& folder, std::string* error = nullptr);
// Why the files in `folder` are not the pack's (missing, a different size or SHA-256), or
// nullopt when they all are.
std::optional<std::string> checkPackFolder(const std::string& folder, const PackManifest& manifest);
// What a pack's files hold, from their folders: "sounds", "templates", "stickers",
// "effects", "transitions".
std::vector<std::string> packContents(const PackManifest& manifest);

// Publishing. A site folder is what the host serves beside catalogue.json: each pack's
// version at <id>/<version>/ (its files and pack.json), its preview at <id>/<version>-preview.png.
// Copies the pack in `source` into `site` as `meta` (id, version, title…; its files are
// listed here), with an optional PNG preview; why not, or nullopt.
std::optional<std::string> publishPack(const std::string& source, const std::string& site, const PackManifest& meta,
                                       const std::string& previewPng = {});
// The catalogue of a site: the newest version of each pack in it.
Catalogue siteCatalogue(const std::string& site, std::int64_t sequence, const std::string& published);

// The SHA-256 of some bytes, lower-case hex.
std::string bytesSha256(const std::string& data);

// Whether `signature` is `publicKey`'s signature of `data`.
bool verifySignature(const std::string& data, const std::string& signature, const std::string& publicKey);

// A new P-256 key pair, in memory: the private key (ANSI X9.63) and the public key (DER
// SubjectPublicKeyInfo). For tests and tools; keep private keys out of the repository.
struct SigningKey {
    std::string privateKey;
    std::string publicKey;
};
std::optional<SigningKey> makeSigningKey();
// The private key's signature of `data` (DER), empty if it cannot sign.
std::string signData(const std::string& data, const std::string& privateKey);

} // namespace tve
