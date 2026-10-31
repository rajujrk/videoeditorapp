#include "io/MediaRelink.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <vector>

namespace tve {

namespace {

namespace fs = std::filesystem;

constexpr std::size_t kSample = 64 * 1024;

std::uint64_t fnv1a(std::uint64_t hash, const char* data, std::size_t size)
{
    for (std::size_t i = 0; i < size; ++i) {
        hash ^= static_cast<unsigned char>(data[i]);
        hash *= 0x100000001b3ull;
    }
    return hash;
}

std::string lower(std::string text)
{
    std::ranges::transform(text, text.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return text;
}

// The size a fingerprint records ("v1:<size>:<hash>"), or nullopt.
std::optional<std::uintmax_t> fingerprintSize(const std::string& fingerprint)
{
    unsigned long long size = 0;
    if (std::sscanf(fingerprint.c_str(), "v1:%llu:", &size) != 1)
        return std::nullopt;
    return std::uintmax_t(size);
}

} // namespace

std::optional<std::string> fingerprintFile(const std::string& path)
{
    std::error_code error;
    const std::uintmax_t size = fs::file_size(path, error);
    if (error)
        return std::nullopt;
    std::ifstream file(path, std::ios::binary);
    if (!file)
        return std::nullopt;
    std::vector<char> buffer(kSample);
    std::uint64_t hash = 0xcbf29ce484222325ull;
    file.read(buffer.data(), std::streamsize(std::min<std::uintmax_t>(size, kSample)));
    hash = fnv1a(hash, buffer.data(), std::size_t(file.gcount()));
    if (size > kSample) {
        const std::uintmax_t tail = std::min<std::uintmax_t>(size - kSample, kSample);
        file.seekg(std::streamoff(size - tail));
        file.read(buffer.data(), std::streamsize(tail));
        hash = fnv1a(hash, buffer.data(), std::size_t(file.gcount()));
    }
    if (!file && !file.eof())
        return std::nullopt;
    char text[64];
    std::snprintf(text, sizeof text, "v1:%llu:%016llx", static_cast<unsigned long long>(size),
                  static_cast<unsigned long long>(hash));
    return std::string(text);
}

bool sameMedia(const MediaItem& item, const std::string& path)
{
    if (item.fingerprint.empty())
        return true;
    const auto found = fingerprintFile(path);
    return found && *found == item.fingerprint;
}

std::vector<const MediaItem*> missingMedia(const Project& project)
{
    std::vector<const MediaItem*> missing;
    for (const MediaItem& item : project.media) {
        if (item.path.starts_with(":/"))
            continue; // built into the app (stickers)
        std::error_code error;
        if (!fs::is_regular_file(item.path, error))
            missing.push_back(&item);
    }
    return missing;
}

std::map<Id, std::string> findMissingMedia(const std::vector<const MediaItem*>& missing, const std::string& folder,
                                           const RelinkSearch& limits)
{
    std::map<Id, std::string> found;
    if (missing.empty())
        return found;
    // What we are looking for: by file name, and by size (for content matches).
    std::multimap<std::string, const MediaItem*> byName;
    std::multimap<std::uintmax_t, const MediaItem*> bySize;
    for (const MediaItem* item : missing) {
        byName.emplace(lower(fs::path(item->path).filename().string()), item);
        if (const auto size = fingerprintSize(item->fingerprint))
            bySize.emplace(*size, item);
    }

    std::error_code error;
    auto it = fs::recursive_directory_iterator(folder, fs::directory_options::skip_permission_denied, error);
    if (error)
        return found;
    std::vector<std::pair<fs::path, std::uintmax_t>> sized; // candidates for content matching
    std::size_t looked = 0;
    for (; it != fs::recursive_directory_iterator() && looked < limits.maxFiles; it.increment(error)) {
        if (error)
            break;
        const fs::directory_entry& entry = *it;
        std::error_code ignored;
        if (!entry.is_regular_file(ignored))
            continue;
        ++looked;
        const std::string path = entry.path().string();
        const auto [first, last] = byName.equal_range(lower(entry.path().filename().string()));
        for (auto match = first; match != last; ++match) {
            const MediaItem* item = match->second;
            if (!found.contains(item->id) && sameMedia(*item, path))
                found.emplace(item->id, path);
        }
        if (!bySize.empty()) {
            const std::uintmax_t size = entry.file_size(ignored);
            if (!ignored && bySize.contains(size))
                sized.emplace_back(entry.path(), size);
        }
    }
    // Renamed copies: the same size and content under another name.
    for (const auto& [path, size] : sized) {
        const auto [first, last] = bySize.equal_range(size);
        std::optional<std::string> fingerprint;
        for (auto match = first; match != last; ++match) {
            const MediaItem* item = match->second;
            if (found.contains(item->id))
                continue;
            if (!fingerprint)
                fingerprint = fingerprintFile(path.string());
            if (fingerprint && *fingerprint == item->fingerprint)
                found.emplace(item->id, path.string());
        }
    }
    return found;
}

} // namespace tve
