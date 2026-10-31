#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace tve {

// Places to import media from besides files: the Photos library (PhotoKit), the Music
// library (iTunesLibrary), and devices (ImageCaptureCore: iPhones, cameras, memory cards).
// Each has collections (albums, playlists, devices) of items; an item is fetched into a
// folder (Photos and devices copy it there; Music items are files already). A test source
// stands in for each, serving the folders under a directory as collections of their files.

enum class ImportAccess { Granted, Denied, Undetermined };

struct ImportCollection {
    std::string id;
    std::string title;
    int count = -1; // -1: not known yet
};

struct ImportItem {
    std::string id;
    std::string title;
    std::string detail;     // an artist, an original file name…
    std::string kind;       // "photo", "video" or "audio"
    double duration = 0;    // seconds (0 for photos)
    int width = 0;
    int height = 0;
    double created = 0;     // seconds since 1970; 0 if not known
    std::string localPath;  // the item's own file, when it is one on this Mac already
};

struct ImportPicture {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba; // premultiplied, width × height × 4
};

class ImportSource {
public:
    virtual ~ImportSource() = default;

    virtual ImportAccess access() const = 0;
    // Asks the person (when not yet asked); `done` gets the answer, on any thread.
    virtual void requestAccess(std::function<void(bool granted)> done) = 0;

    virtual std::vector<ImportCollection> collections() = 0;
    virtual std::vector<ImportItem> items(const std::string& collectionId) = 0;
    // A small picture of an item, longest side about `size` (blocks; call off the main thread).
    virtual std::optional<ImportPicture> thumbnail(const std::string& itemId, int size) = 0;
    // Puts an item in `folder` (made if missing) under a name not taken there; `done` gets
    // its path, or empty and an error, on any thread.
    virtual void fetch(const std::string& itemId, const std::string& folder,
                       std::function<void(std::string path, std::string error)> done) = 0;

    // Called (on the main thread) when collections come or go (devices plugged in or out).
    void onChanged(std::function<void()> changed) { m_changed = std::move(changed); }

protected:
    void notifyChanged() const
    {
        if (m_changed)
            m_changed();
    }

private:
    std::function<void()> m_changed;
};

std::unique_ptr<ImportSource> makePhotosSource();
std::unique_ptr<ImportSource> makeMusicSource();
std::unique_ptr<ImportSource> makeDeviceSource();
// Folders under `root` as collections of their media files. `copies`: fetching copies the
// file (as Photos and devices do); otherwise the file itself is the result (as Music).
std::unique_ptr<ImportSource> makeFolderSource(const std::string& root, bool copies);

// A free name for `name` in `folder`: "name", "name 2", "name 3"… (keeping the extension).
std::string freeFileName(const std::string& folder, const std::string& name);

} // namespace tve
