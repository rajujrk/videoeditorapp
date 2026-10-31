#include "audio/SoundPacks.h"

#include "encode/WavWriter.h"

#include <CommonCrypto/CommonDigest.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>

namespace tve {

namespace {

using json = nlohmann::json;
namespace fs = std::filesystem;

constexpr int kRate = 48000;

// A file inside the pack's folder, or empty if the name would leave it.
std::string insidePack(const fs::path& folder, const std::string& name)
{
    if (name.empty())
        return {};
    const fs::path relative(name);
    if (relative.is_absolute() || std::ranges::any_of(relative, [](const fs::path& part) { return part == ".."; }))
        return {};
    return (folder / relative).lexically_normal().string();
}

std::string text(const json& value, const char* key)
{
    const auto it = value.find(key);
    if (it == value.end())
        return {};
    if (it->is_string())
        return it->get<std::string>();
    if (it->is_object() && it->contains("en") && (*it)["en"].is_string())
        return (*it)["en"].get<std::string>();
    return {};
}

} // namespace

std::string fileSha256(const std::string& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
        return {};
    CC_SHA256_CTX context;
    CC_SHA256_Init(&context);
    std::vector<char> buffer(1 << 16);
    while (file) {
        file.read(buffer.data(), std::streamsize(buffer.size()));
        if (file.gcount() > 0)
            CC_SHA256_Update(&context, buffer.data(), CC_LONG(file.gcount()));
    }
    unsigned char digest[CC_SHA256_DIGEST_LENGTH];
    CC_SHA256_Final(digest, &context);
    static constexpr char kHex[] = "0123456789abcdef";
    std::string hex;
    for (unsigned char byte : digest) {
        hex += kHex[byte >> 4];
        hex += kHex[byte & 15];
    }
    return hex;
}

bool writeSoundFile(const std::string& path, const std::vector<float>& samples, std::string* error)
{
    std::error_code ec;
    fs::create_directories(fs::path(path).parent_path(), ec);
    // Written beside and moved into place, so a reader never sees half a file.
    const std::string partial = path + ".part";
    auto writer = WavWriter::open(partial, kRate, 2, error);
    if (!writer)
        return false;
    if (!writer->write(samples.data(), std::int64_t(samples.size() / 2)) || !writer->finish()) {
        if (error)
            *error = "The sound could not be written.";
        fs::remove(partial, ec);
        return false;
    }
    fs::rename(partial, path, ec);
    if (ec && error)
        *error = ec.message();
    return !ec;
}

std::optional<SoundPack> loadSoundPack(const std::string& folder, std::string* error)
{
    const fs::path root(folder);
    std::ifstream file(root / "manifest.json");
    if (!file) {
        if (error)
            *error = "The pack has no manifest.";
        return std::nullopt;
    }
    const json manifest = json::parse(file, nullptr, false);
    if (!manifest.is_object() || manifest.value("format", std::string()) != "tve-sound-pack"
        || !manifest.contains("sounds") || !manifest["sounds"].is_array()) {
        if (error)
            *error = "The manifest is not a sound pack's.";
        return std::nullopt;
    }
    if (manifest.value("version", 0) != 1) {
        if (error)
            *error = "The pack is a newer version than this app reads.";
        return std::nullopt;
    }
    SoundPack pack;
    pack.id = manifest.value("id", std::string());
    pack.title = text(manifest, "title");
    pack.license = manifest.value("license", std::string());
    pack.folder = root.lexically_normal().string();
    if (pack.id.empty()) {
        if (error)
            *error = "The pack has no id.";
        return std::nullopt;
    }
    for (const json& entry : manifest["sounds"]) {
        if (!entry.is_object())
            continue;
        PackSound sound;
        sound.id = entry.value("id", std::string());
        sound.kind = entry.value("kind", std::string("sfx"));
        sound.title = text(entry, "title");
        sound.category = entry.value("category", std::string());
        sound.duration = entry.value("duration", 0.0);
        const std::string name = sound.title.empty() ? sound.id : sound.title;
        sound.path = insidePack(root, entry.value("file", std::string()));
        if (sound.id.empty() || sound.path.empty()) {
            pack.problems.push_back(name + ": no file");
            continue;
        }
        if (sound.kind != "music" && sound.kind != "sfx")
            sound.kind = "sfx";
        if (fileSha256(sound.path) != entry.value("sha256", std::string())) {
            pack.problems.push_back(name + ": the file is missing or not the one the pack lists");
            continue;
        }
        sound.preview = insidePack(root, entry.value("preview", std::string()));
        if (!sound.preview.empty() && fileSha256(sound.preview) != entry.value("previewSha256", std::string()))
            sound.preview.clear(); // the sound itself previews instead
        if (sound.title.empty())
            sound.title = sound.id;
        pack.sounds.push_back(std::move(sound));
    }
    return pack;
}

std::vector<SoundPack> loadSoundPacks(const std::string& root)
{
    std::vector<SoundPack> packs;
    std::error_code ec;
    std::vector<fs::path> folders;
    for (const auto& entry : fs::directory_iterator(root, ec))
        if (entry.is_directory(ec))
            folders.push_back(entry.path());
    std::ranges::sort(folders);
    for (const fs::path& folder : folders)
        if (auto pack = loadSoundPack(folder.string()))
            packs.push_back(std::move(*pack));
    return packs;
}

bool writeSoundPack(const std::string& folder, const std::string& id, const std::string& title,
                    const std::vector<SoundRecipe>& recipes, double previewSeconds, std::string* error)
{
    const fs::path root(folder);
    json sounds = json::array();
    for (const SoundRecipe& recipe : recipes) {
        auto samples = renderSound(recipe, error);
        if (!samples)
            return false;
        const std::string name = recipe.id.substr(recipe.id.rfind('.') + 1);
        const std::string file = (recipe.kind == "music" ? "music/" : "sfx/") + name + ".wav";
        if (!writeSoundFile((root / file).string(), *samples, error))
            return false;
        const double duration = double(samples->size() / 2) / kRate;
        json entry = {{"id", recipe.id},           {"kind", recipe.kind},
                      {"title", recipe.title},     {"category", recipe.category},
                      {"file", file},              {"sha256", fileSha256((root / file).string())},
                      {"duration", duration}};
        if (duration > previewSeconds + 1) {
            // The opening, faded out over its last second.
            const std::size_t frames = std::size_t(previewSeconds * kRate);
            std::vector<float> preview(samples->begin(), samples->begin() + std::ptrdiff_t(frames * 2));
            for (std::size_t f = frames - std::min<std::size_t>(frames, kRate); f < frames; ++f) {
                const float gain = float(frames - f) / float(kRate);
                preview[f * 2] *= gain;
                preview[f * 2 + 1] *= gain;
            }
            const std::string previewFile = "previews/" + name + ".wav";
            if (!writeSoundFile((root / previewFile).string(), preview, error))
                return false;
            entry["preview"] = previewFile;
            entry["previewSha256"] = fileSha256((root / previewFile).string());
        }
        sounds.push_back(std::move(entry));
    }
    const json manifest = {{"format", "tve-sound-pack"}, {"version", 1},         {"id", id},
                           {"title", title},             {"license", "TVE-Original"}, {"sounds", sounds}};
    std::ofstream out(root / "manifest.json");
    out << manifest.dump(2) << '\n';
    if (!out && error)
        *error = "The manifest could not be written.";
    return bool(out);
}

} // namespace tve
