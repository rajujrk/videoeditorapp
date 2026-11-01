#include "packs/Packs.h"

#include "audio/SoundPacks.h"

#include <CommonCrypto/CommonDigest.h>
#include <Security/Security.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <set>

namespace fs = std::filesystem;
using nlohmann::json;

namespace tve {

namespace {

// DER SubjectPublicKeyInfo of a P-256 key, before its 65-byte point: SEQUENCE {
// SEQUENCE { id-ecPublicKey, prime256v1 }, BIT STRING }.
constexpr std::array<unsigned char, 26> kP256SpkiPrefix{0x30, 0x59, 0x30, 0x13, 0x06, 0x07, 0x2a, 0x86, 0x48,
                                                        0xce, 0x3d, 0x02, 0x01, 0x06, 0x08, 0x2a, 0x86, 0x48,
                                                        0xce, 0x3d, 0x03, 0x01, 0x07, 0x03, 0x42, 0x00};

std::string text(const json& j, const char* key)
{
    const auto it = j.find(key);
    return it != j.end() && it->is_string() ? it->get<std::string>() : std::string();
}

bool hex64(const std::string& s)
{
    return s.size() == 64 && std::ranges::all_of(s, [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}

CFDataRef dataOf(const std::string& bytes)
{
    return CFDataCreate(nullptr, reinterpret_cast<const UInt8*>(bytes.data()), CFIndex(bytes.size()));
}

std::string bytesOf(CFDataRef data)
{
    return std::string(reinterpret_cast<const char*>(CFDataGetBytePtr(data)), std::size_t(CFDataGetLength(data)));
}

// A P-256 key from its external representation (ANSI X9.63), public or private.
SecKeyRef ecKey(const std::string& x963, bool isPrivate)
{
    const int bits = 256;
    CFNumberRef size = CFNumberCreate(nullptr, kCFNumberIntType, &bits);
    const void* keys[] = {kSecAttrKeyType, kSecAttrKeyClass, kSecAttrKeySizeInBits};
    const void* values[] = {kSecAttrKeyTypeECSECPrimeRandom, isPrivate ? kSecAttrKeyClassPrivate : kSecAttrKeyClassPublic, size};
    CFDictionaryRef attributes = CFDictionaryCreate(nullptr, keys, values, 3, &kCFTypeDictionaryKeyCallBacks,
                                                    &kCFTypeDictionaryValueCallBacks);
    CFDataRef data = dataOf(x963);
    SecKeyRef key = SecKeyCreateWithData(data, attributes, nullptr);
    CFRelease(data);
    CFRelease(attributes);
    CFRelease(size);
    return key;
}

} // namespace

bool safeRelativePath(const std::string& path)
{
    if (path.empty() || path.size() > 1000 || path.front() == '/' || path.find('\\') != std::string::npos
        || path.find('\0') != std::string::npos)
        return false;
    std::size_t start = 0;
    while (start <= path.size()) {
        const std::size_t end = std::min(path.find('/', start), path.size());
        const std::string part = path.substr(start, end - start);
        if (part.empty() || part == "." || part == "..")
            return false;
        start = end + 1;
    }
    return true;
}

bool validPackId(const std::string& id)
{
    if (id.empty() || id.size() > 100 || !std::isalnum(static_cast<unsigned char>(id.front())))
        return false;
    return std::ranges::all_of(id, [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '-' || c == '_'; });
}

std::optional<PackManifest> readPackManifest(const std::string& source, std::string* error)
{
    const auto fail = [&](const std::string& why) -> std::optional<PackManifest> {
        if (error)
            *error = why;
        return std::nullopt;
    };
    const json j = json::parse(source, nullptr, false);
    if (!j.is_object() || text(j, "format") != "tve-pack")
        return fail("not a pack manifest");
    if (j.value("version", 0) != kPackFormatVersion)
        return fail("a pack format this version cannot read");
    PackManifest m;
    m.id = text(j, "id");
    m.version = j.value("packVersion", 0);
    m.title = text(j, "title");
    m.description = text(j, "description");
    m.author = text(j, "author");
    m.license = text(j, "license");
    if (!validPackId(m.id) || m.version <= 0)
        return fail("the pack's id or version is not valid");
    const auto files = j.find("files");
    if (files == j.end() || !files->is_array() || files->size() > kMostPackFiles)
        return fail("the pack's file list is missing or too long");
    std::set<std::string> seen;
    std::int64_t total = 0;
    for (const json& f : *files) {
        PackFile file{.path = text(f, "path"), .size = f.value("size", std::int64_t(-1)), .sha256 = text(f, "sha256")};
        if (!safeRelativePath(file.path) || file.path == "pack.json" || file.size < 0 || !hex64(file.sha256)
            || !seen.insert(file.path).second)
            return fail("the pack lists a file it may not: " + file.path);
        total += file.size;
        m.files.push_back(std::move(file));
    }
    if (total > kMostPackBytes)
        return fail("the pack is too big");
    return m;
}

std::string packManifestJson(const PackManifest& m)
{
    json files = json::array();
    for (const PackFile& f : m.files)
        files.push_back({{"path", f.path}, {"size", f.size}, {"sha256", f.sha256}});
    return json{{"format", "tve-pack"}, {"version", kPackFormatVersion}, {"id", m.id}, {"packVersion", m.version},
                {"title", m.title}, {"description", m.description}, {"author", m.author}, {"license", m.license},
                {"files", files}}
        .dump(2);
}

std::optional<Catalogue> readCatalogue(const std::string& source, std::string* error)
{
    const auto fail = [&](const std::string& why) -> std::optional<Catalogue> {
        if (error)
            *error = why;
        return std::nullopt;
    };
    if (source.size() > kMostCatalogueBytes)
        return fail("the catalogue is too big");
    const json j = json::parse(source, nullptr, false);
    if (!j.is_object() || text(j, "format") != "tve-pack-catalogue")
        return fail("not a pack catalogue");
    if (j.value("version", 0) != kPackFormatVersion)
        return fail("a catalogue format this version cannot read");
    Catalogue c;
    c.sequence = j.value("sequence", std::int64_t(0));
    c.published = text(j, "published");
    const auto packs = j.find("packs");
    if (packs == j.end() || !packs->is_array())
        return fail("the catalogue lists no packs");
    std::set<std::string> ids;
    for (const json& p : *packs) {
        CatalogueEntry e;
        e.id = text(p, "id");
        e.version = p.value("version", 0);
        e.title = text(p, "title");
        e.description = text(p, "description");
        e.author = text(p, "author");
        e.license = text(p, "license");
        e.size = p.value("size", std::int64_t(0));
        e.manifest = text(p, "manifest");
        e.manifestSha256 = text(p, "sha256");
        e.preview = text(p, "preview");
        e.previewSha256 = text(p, "previewSha256");
        if (const auto contents = p.find("contents"); contents != p.end() && contents->is_array())
            for (const json& kind : *contents)
                if (kind.is_string())
                    e.contents.push_back(kind.get<std::string>());
        // A pack that can't be fetched safely is left out; the rest stand.
        if (!validPackId(e.id) || e.version <= 0 || !safeRelativePath(e.manifest) || !hex64(e.manifestSha256)
            || (!e.preview.empty() && (!safeRelativePath(e.preview) || !hex64(e.previewSha256)))
            || !ids.insert(e.id).second)
            continue;
        c.packs.push_back(std::move(e));
    }
    return c;
}

std::string catalogueJson(const Catalogue& c)
{
    json packs = json::array();
    for (const CatalogueEntry& e : c.packs) {
        json p{{"id", e.id}, {"version", e.version}, {"title", e.title}, {"description", e.description},
               {"author", e.author}, {"license", e.license}, {"contents", e.contents}, {"size", e.size},
               {"manifest", e.manifest}, {"sha256", e.manifestSha256}};
        if (!e.preview.empty()) {
            p["preview"] = e.preview;
            p["previewSha256"] = e.previewSha256;
        }
        packs.push_back(std::move(p));
    }
    return json{{"format", "tve-pack-catalogue"}, {"version", kPackFormatVersion}, {"sequence", c.sequence},
                {"published", c.published}, {"packs", packs}}
        .dump(2);
}

std::vector<PackFile> listPackFiles(const std::string& folder, std::string* error)
{
    std::vector<PackFile> files;
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(folder, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (!it->is_regular_file())
            continue;
        const std::string path = fs::relative(it->path(), folder).generic_string();
        const std::string name = it->path().filename().string();
        if (path == "pack.json" || name == ".DS_Store")
            continue;
        files.push_back({path, std::int64_t(it->file_size()), fileSha256(it->path().string())});
    }
    if (ec && error)
        *error = ec.message();
    std::ranges::sort(files, {}, &PackFile::path);
    return files;
}

std::optional<std::string> checkPackFolder(const std::string& folder, const PackManifest& manifest)
{
    for (const PackFile& file : manifest.files) {
        const fs::path path = fs::path(folder) / file.path;
        std::error_code ec;
        if (!fs::is_regular_file(path, ec))
            return file.path + " is missing";
        if (std::int64_t(fs::file_size(path, ec)) != file.size)
            return file.path + " is not the size it should be";
        if (fileSha256(path.string()) != file.sha256)
            return file.path + " is not the file it should be";
    }
    return std::nullopt;
}

std::vector<std::string> packContents(const PackManifest& manifest)
{
    std::vector<std::string> kinds;
    for (const char* kind : {"sounds", "templates", "stickers", "effects", "transitions"})
        if (std::ranges::any_of(manifest.files, [&](const PackFile& f) { return f.path.starts_with(std::string(kind) + "/"); }))
            kinds.emplace_back(kind);
    return kinds;
}

std::optional<std::string> publishPack(const std::string& source, const std::string& site, const PackManifest& meta,
                                       const std::string& previewPng)
{
    if (!validPackId(meta.id) || meta.version <= 0)
        return "the pack's id or version is not valid";
    std::error_code ec;
    if (!fs::is_directory(source, ec))
        return source + " is not a folder";
    const fs::path target = fs::path(site) / meta.id / std::to_string(meta.version);
    if (fs::exists(target, ec))
        return "version " + std::to_string(meta.version) + " of " + meta.id + " is already published; publish a new version";
    PackManifest manifest = meta;
    std::string error;
    manifest.files = listPackFiles(source, &error);
    if (!error.empty())
        return error;
    if (manifest.files.empty())
        return "the pack has no files";
    for (const PackFile& file : manifest.files)
        if (!safeRelativePath(file.path))
            return "a file's name can't be published: " + file.path;
    fs::create_directories(target, ec);
    for (const PackFile& file : manifest.files) {
        fs::create_directories((target / file.path).parent_path(), ec);
        fs::copy_file(fs::path(source) / file.path, target / file.path, ec);
        if (ec)
            return "can't copy " + file.path + ": " + ec.message();
    }
    std::ofstream(target / "pack.json", std::ios::binary) << packManifestJson(manifest);
    if (!previewPng.empty()) {
        fs::copy_file(previewPng, fs::path(site) / meta.id / (std::to_string(meta.version) + "-preview.png"), ec);
        if (ec)
            return "can't copy the preview: " + ec.message();
    }
    return std::nullopt;
}

Catalogue siteCatalogue(const std::string& site, std::int64_t sequence, const std::string& published)
{
    Catalogue catalogue{.sequence = sequence, .published = published};
    std::error_code ec;
    std::vector<fs::path> packs;
    for (const auto& entry : fs::directory_iterator(site, ec))
        if (entry.is_directory() && validPackId(entry.path().filename().string()))
            packs.push_back(entry.path());
    std::ranges::sort(packs);
    for (const fs::path& pack : packs) {
        // Its newest version.
        int newest = 0;
        for (const auto& entry : fs::directory_iterator(pack, ec)) {
            const std::string name = entry.path().filename().string();
            if (entry.is_directory() && !name.empty() && std::ranges::all_of(name, ::isdigit) && fs::exists(entry.path() / "pack.json"))
                newest = std::max(newest, std::stoi(name));
        }
        if (newest == 0)
            continue;
        const std::string version = std::to_string(newest);
        std::ifstream in(pack / version / "pack.json", std::ios::binary);
        const std::string json((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        const auto manifest = readPackManifest(json);
        if (!manifest)
            continue;
        CatalogueEntry entry{.id = manifest->id, .version = manifest->version, .title = manifest->title,
                             .description = manifest->description, .author = manifest->author, .license = manifest->license,
                             .contents = packContents(*manifest),
                             .manifest = manifest->id + "/" + version + "/pack.json", .manifestSha256 = bytesSha256(json)};
        for (const PackFile& file : manifest->files)
            entry.size += file.size;
        if (const fs::path preview = pack / (version + "-preview.png"); fs::exists(preview)) {
            entry.preview = manifest->id + "/" + version + "-preview.png";
            entry.previewSha256 = fileSha256(preview.string());
        }
        catalogue.packs.push_back(std::move(entry));
    }
    return catalogue;
}

std::string bytesSha256(const std::string& data)
{
    unsigned char digest[CC_SHA256_DIGEST_LENGTH];
    CC_SHA256(data.data(), CC_LONG(data.size()), digest);
    static constexpr char kHex[] = "0123456789abcdef";
    std::string hex;
    for (unsigned char byte : digest) {
        hex += kHex[byte >> 4];
        hex += kHex[byte & 15];
    }
    return hex;
}

bool verifySignature(const std::string& data, const std::string& signature, const std::string& publicKey)
{
    if (publicKey.size() != kP256SpkiPrefix.size() + 65
        || !std::equal(kP256SpkiPrefix.begin(), kP256SpkiPrefix.end(), reinterpret_cast<const unsigned char*>(publicKey.data()))
        || signature.empty())
        return false;
    SecKeyRef key = ecKey(publicKey.substr(kP256SpkiPrefix.size()), false);
    if (!key)
        return false;
    CFDataRef message = dataOf(data);
    CFDataRef sig = dataOf(signature);
    const bool ok = SecKeyVerifySignature(key, kSecKeyAlgorithmECDSASignatureMessageX962SHA256, message, sig, nullptr);
    CFRelease(sig);
    CFRelease(message);
    CFRelease(key);
    return ok;
}

std::optional<SigningKey> makeSigningKey()
{
    const int bits = 256;
    CFNumberRef size = CFNumberCreate(nullptr, kCFNumberIntType, &bits);
    const void* keys[] = {kSecAttrKeyType, kSecAttrKeySizeInBits};
    const void* values[] = {kSecAttrKeyTypeECSECPrimeRandom, size};
    CFDictionaryRef attributes = CFDictionaryCreate(nullptr, keys, values, 2, &kCFTypeDictionaryKeyCallBacks,
                                                    &kCFTypeDictionaryValueCallBacks);
    // Not kept in any keychain: only in memory.
    SecKeyRef privateKey = SecKeyCreateRandomKey(attributes, nullptr);
    CFRelease(attributes);
    CFRelease(size);
    if (!privateKey)
        return std::nullopt;
    SecKeyRef publicKey = SecKeyCopyPublicKey(privateKey);
    CFDataRef privateData = SecKeyCopyExternalRepresentation(privateKey, nullptr);
    CFDataRef publicData = publicKey ? SecKeyCopyExternalRepresentation(publicKey, nullptr) : nullptr;
    std::optional<SigningKey> pair;
    if (privateData && publicData)
        pair = SigningKey{bytesOf(privateData),
                          std::string(kP256SpkiPrefix.begin(), kP256SpkiPrefix.end()) + bytesOf(publicData)};
    if (privateData)
        CFRelease(privateData);
    if (publicData)
        CFRelease(publicData);
    if (publicKey)
        CFRelease(publicKey);
    CFRelease(privateKey);
    return pair;
}

std::string signData(const std::string& data, const std::string& privateKey)
{
    SecKeyRef key = ecKey(privateKey, true);
    if (!key)
        return {};
    CFDataRef message = dataOf(data);
    CFDataRef signature = SecKeyCreateSignature(key, kSecKeyAlgorithmECDSASignatureMessageX962SHA256, message, nullptr);
    std::string result = signature ? bytesOf(signature) : std::string();
    if (signature)
        CFRelease(signature);
    CFRelease(message);
    CFRelease(key);
    return result;
}

} // namespace tve
