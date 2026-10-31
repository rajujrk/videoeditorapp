#include "imports/ImportSources.h"

#include "decode/AudioDecoder.h"
#include "decode/VideoDecoder.h"

#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>
#import <ImageCaptureCore/ImageCaptureCore.h>
#import <ImageIO/ImageIO.h>
#import <Photos/Photos.h>
#import <iTunesLibrary/iTunesLibrary.h>

#include <algorithm>
#include <filesystem>
#include <map>
#include <mutex>

namespace fs = std::filesystem;

namespace tve {

namespace {

constexpr int kMostItems = 2000;

std::string str(NSString* s)
{
    return s ? std::string(s.UTF8String) : std::string();
}

std::string lower(std::string s)
{
    std::ranges::transform(s, s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return s;
}

// "photo", "video", "audio" or "" by a file's extension.
std::string kindOfFile(const std::string& name)
{
    static const std::vector<std::string> photos{".jpg", ".jpeg", ".png", ".heic", ".heif", ".tif", ".tiff", ".webp", ".gif", ".dng"};
    static const std::vector<std::string> videos{".mov", ".mp4", ".m4v", ".avi", ".mts", ".m2ts", ".mkv", ".3gp"};
    static const std::vector<std::string> sounds{".wav", ".mp3", ".m4a", ".aac", ".aif", ".aiff", ".flac", ".caf"};
    const std::string extension = lower(fs::path(name).extension().string());
    if (std::ranges::find(photos, extension) != photos.end())
        return "photo";
    if (std::ranges::find(videos, extension) != videos.end())
        return "video";
    if (std::ranges::find(sounds, extension) != sounds.end())
        return "audio";
    return {};
}

// A CGImage drawn into an RGBA picture, longest side at most `size`.
std::optional<ImportPicture> pictureOf(CGImageRef image, int size)
{
    if (!image)
        return std::nullopt;
    const double width = double(CGImageGetWidth(image));
    const double height = double(CGImageGetHeight(image));
    if (width < 1 || height < 1)
        return std::nullopt;
    const double scale = std::min(1.0, double(size) / std::max(width, height));
    ImportPicture picture;
    picture.width = std::max(1, int(std::lround(width * scale)));
    picture.height = std::max(1, int(std::lround(height * scale)));
    picture.rgba.assign(std::size_t(picture.width) * std::size_t(picture.height) * 4, 0);
    CGColorSpaceRef space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CGContextRef context = CGBitmapContextCreate(picture.rgba.data(), std::size_t(picture.width), std::size_t(picture.height), 8,
                                                 std::size_t(picture.width) * 4, space,
                                                 CGBitmapInfo(kCGImageAlphaPremultipliedLast) | CGBitmapInfo(kCGBitmapByteOrder32Big));
    CGColorSpaceRelease(space);
    if (!context)
        return std::nullopt;
    CGContextSetInterpolationQuality(context, kCGInterpolationHigh);
    CGContextDrawImage(context, CGRectMake(0, 0, picture.width, picture.height), image);
    CGContextRelease(context);
    return picture;
}

std::optional<ImportPicture> pictureOf(NSImage* image, int size)
{
    if (!image)
        return std::nullopt;
    return pictureOf([image CGImageForProposedRect:nullptr context:nil hints:nil], size);
}

std::optional<ImportPicture> pictureOf(NSData* data, int size)
{
    if (!data)
        return std::nullopt;
    CGImageSourceRef source = CGImageSourceCreateWithData((CFDataRef)data, nullptr);
    if (!source)
        return std::nullopt;
    CGImageRef image = CGImageSourceCreateImageAtIndex(source, 0, nullptr);
    CFRelease(source);
    auto picture = pictureOf(image, size);
    if (image)
        CGImageRelease(image);
    return picture;
}

ImportAccess photosAccess()
{
    switch ([PHPhotoLibrary authorizationStatusForAccessLevel:PHAccessLevelReadWrite]) {
    case PHAuthorizationStatusAuthorized:
    case PHAuthorizationStatusLimited:
        return ImportAccess::Granted;
    case PHAuthorizationStatusNotDetermined:
        return ImportAccess::Undetermined;
    default:
        return ImportAccess::Denied;
    }
}

} // namespace

std::string freeFileName(const std::string& folder, const std::string& name)
{
    const fs::path file(name.empty() ? std::string("Imported") : fs::path(name).filename().string());
    const std::string stem = file.stem().string();
    const std::string extension = file.extension().string();
    std::string candidate = file.string();
    for (int n = 2; fs::exists(fs::path(folder) / candidate); ++n)
        candidate = stem + " " + std::to_string(n) + extension;
    return candidate;
}

// --- Photos (PhotoKit) ---

namespace {

class PhotosSource final : public ImportSource {
public:
    ImportAccess access() const override { return photosAccess(); }

    void requestAccess(std::function<void(bool)> done) override
    {
        [PHPhotoLibrary requestAuthorizationForAccessLevel:PHAccessLevelReadWrite
                                                   handler:^(PHAuthorizationStatus status) {
                                                       done(status == PHAuthorizationStatusAuthorized
                                                            || status == PHAuthorizationStatusLimited);
                                                   }];
    }

    std::vector<ImportCollection> collections() override
    {
        std::vector<ImportCollection> list;
        if (photosAccess() != ImportAccess::Granted)
            return list;
        @autoreleasepool {
            PHFetchOptions* everything = mediaOptions();
            list.push_back({"all", "All photos and videos", int([PHAsset fetchAssetsWithOptions:everything].count)});
            const auto add = [&](PHFetchResult<PHAssetCollection*>* found) {
                for (PHAssetCollection* album in found) {
                    const NSUInteger count = [PHAsset fetchAssetsInAssetCollection:album options:mediaOptions()].count;
                    if (count > 0)
                        list.push_back({str(album.localIdentifier), str(album.localizedTitle), int(count)});
                }
            };
            add([PHAssetCollection fetchAssetCollectionsWithType:PHAssetCollectionTypeSmartAlbum
                                                         subtype:PHAssetCollectionSubtypeSmartAlbumFavorites options:nil]);
            add([PHAssetCollection fetchAssetCollectionsWithType:PHAssetCollectionTypeSmartAlbum
                                                         subtype:PHAssetCollectionSubtypeSmartAlbumVideos options:nil]);
            add([PHAssetCollection fetchAssetCollectionsWithType:PHAssetCollectionTypeAlbum
                                                         subtype:PHAssetCollectionSubtypeAny options:nil]);
        }
        return list;
    }

    std::vector<ImportItem> items(const std::string& collectionId) override
    {
        std::vector<ImportItem> list;
        if (photosAccess() != ImportAccess::Granted)
            return list;
        @autoreleasepool {
            PHFetchOptions* options = mediaOptions();
            options.fetchLimit = kMostItems;
            PHFetchResult<PHAsset*>* assets = nil;
            if (collectionId == "all") {
                assets = [PHAsset fetchAssetsWithOptions:options];
            } else {
                PHAssetCollection* album = [PHAssetCollection
                    fetchAssetCollectionsWithLocalIdentifiers:@[[NSString stringWithUTF8String:collectionId.c_str()]]
                                                      options:nil].firstObject;
                if (!album)
                    return list;
                assets = [PHAsset fetchAssetsInAssetCollection:album options:options];
            }
            for (PHAsset* asset in assets) {
                ImportItem item;
                item.id = str(asset.localIdentifier);
                item.kind = asset.mediaType == PHAssetMediaTypeVideo ? "video" : "photo";
                item.duration = asset.duration;
                item.width = int(asset.pixelWidth);
                item.height = int(asset.pixelHeight);
                item.created = asset.creationDate ? asset.creationDate.timeIntervalSince1970 : 0;
                item.title = item.kind == "video" ? "Video" : "Photo";
                list.push_back(std::move(item));
            }
        }
        return list;
    }

    std::optional<ImportPicture> thumbnail(const std::string& itemId, int size) override
    {
        __block std::optional<ImportPicture> picture;
        @autoreleasepool {
            PHAsset* asset = find(itemId);
            if (!asset)
                return std::nullopt;
            PHImageRequestOptions* options = [[[PHImageRequestOptions alloc] init] autorelease];
            options.synchronous = YES;
            options.deliveryMode = PHImageRequestOptionsDeliveryModeHighQualityFormat;
            options.resizeMode = PHImageRequestOptionsResizeModeFast;
            options.networkAccessAllowed = YES;
            [[PHImageManager defaultManager] requestImageForAsset:asset
                                                       targetSize:CGSizeMake(size, size)
                                                      contentMode:PHImageContentModeAspectFit
                                                          options:options
                                                    resultHandler:^(NSImage* image, NSDictionary*) {
                                                        if (image)
                                                            picture = pictureOf(image, size);
                                                    }];
        }
        return picture;
    }

    void fetch(const std::string& itemId, const std::string& folder,
               std::function<void(std::string, std::string)> done) override
    {
        @autoreleasepool {
            PHAsset* asset = find(itemId);
            if (!asset) {
                done({}, "The item is no longer in the Photos library.");
                return;
            }
            // The picture as Photos shows it (edits included), named after the original.
            NSArray<PHAssetResource*>* resources = [PHAssetResource assetResourcesForAsset:asset];
            const bool video = asset.mediaType == PHAssetMediaTypeVideo;
            PHAssetResource* original = nil;
            PHAssetResource* edited = nil;
            for (PHAssetResource* resource in resources) {
                if (resource.type == (video ? PHAssetResourceTypeVideo : PHAssetResourceTypePhoto))
                    original = resource;
                if (resource.type == (video ? PHAssetResourceTypeFullSizeVideo : PHAssetResourceTypeFullSizePhoto))
                    edited = resource;
            }
            PHAssetResource* chosen = edited ? edited : original;
            if (!chosen) {
                done({}, "Photos has no file for the item.");
                return;
            }
            const fs::path originalName(fileName(original ? original : chosen));
            const std::string extension = fs::path(fileName(chosen)).extension().string();
            std::error_code ec;
            fs::create_directories(folder, ec);
            const std::string path = (fs::path(folder) / freeFileName(folder, originalName.stem().string() + extension)).string();
            PHAssetResourceRequestOptions* options = [[[PHAssetResourceRequestOptions alloc] init] autorelease];
            options.networkAccessAllowed = YES;
            auto* callback = new std::function<void(std::string, std::string)>(std::move(done));
            [[PHAssetResourceManager defaultManager]
                writeDataForAssetResource:chosen
                                   toFile:[NSURL fileURLWithPath:[NSString stringWithUTF8String:path.c_str()]]
                                  options:options
                        completionHandler:^(NSError* error) {
                            if (error)
                                (*callback)({}, str(error.localizedDescription));
                            else
                                (*callback)(path, {});
                            delete callback;
                        }];
        }
    }

private:
    static PHFetchOptions* mediaOptions()
    {
        PHFetchOptions* options = [[[PHFetchOptions alloc] init] autorelease];
        options.predicate = [NSPredicate predicateWithFormat:@"mediaType == %d || mediaType == %d",
                                                             int(PHAssetMediaTypeImage), int(PHAssetMediaTypeVideo)];
        options.sortDescriptors = @[[NSSortDescriptor sortDescriptorWithKey:@"creationDate" ascending:NO]];
        return options;
    }

    // A resource's file name (renamed in macOS 27).
    static std::string fileName(PHAssetResource* resource)
    {
        if (@available(macOS 27, *))
            return str(resource.filename);
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        return str(resource.originalFilename);
#pragma clang diagnostic pop
    }

    static PHAsset* find(const std::string& itemId)
    {
        return [PHAsset fetchAssetsWithLocalIdentifiers:@[[NSString stringWithUTF8String:itemId.c_str()]] options:nil].firstObject;
    }
};

// --- Music (iTunesLibrary) ---

class MusicSource final : public ImportSource {
public:
    ~MusicSource() override { [m_library release]; }

    // There is no asking without opening: undetermined until the library has been tried.
    ImportAccess access() const override
    {
        std::lock_guard lock(m_mutex);
        return m_library ? ImportAccess::Granted : m_tried ? ImportAccess::Denied : ImportAccess::Undetermined;
    }

    void requestAccess(std::function<void(bool)> done) override
    {
        auto* callback = new std::function<void(bool)>(std::move(done));
        dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
            const bool open = this->open();
            (*callback)(open);
            delete callback;
        });
    }

    std::vector<ImportCollection> collections() override
    {
        std::vector<ImportCollection> list;
        std::lock_guard lock(m_mutex);
        if (!m_library)
            return list;
        @autoreleasepool {
            list.push_back({"all", "Songs", int(playable(m_library.allMediaItems).size())});
            for (ITLibPlaylist* playlist in m_library.allPlaylists) {
                if (!playlist.visible || playlist.primary || playlist.kind == ITLibPlaylistKindFolder)
                    continue;
                if (playlist.distinguishedKind != ITLibDistinguishedPlaylistKindNone
                    && playlist.distinguishedKind != ITLibDistinguishedPlaylistKindMusic)
                    continue;
                const int count = int(playable(playlist.items).size());
                if (count > 0)
                    list.push_back({str(playlist.persistentID.stringValue), str(playlist.name), count});
            }
        }
        return list;
    }

    std::vector<ImportItem> items(const std::string& collectionId) override
    {
        std::vector<ImportItem> list;
        std::lock_guard lock(m_mutex);
        if (!m_library)
            return list;
        @autoreleasepool {
            NSArray<ITLibMediaItem*>* source = m_library.allMediaItems;
            if (collectionId != "all") {
                source = @[];
                for (ITLibPlaylist* playlist in m_library.allPlaylists)
                    if (str(playlist.persistentID.stringValue) == collectionId)
                        source = playlist.items;
            }
            for (ITLibMediaItem* song : playable(source)) {
                ImportItem item;
                item.id = str(song.persistentID.stringValue);
                item.title = str(song.title);
                item.detail = str(song.artist.name);
                item.kind = song.mediaKind == ITLibMediaItemMediaKindSong ? "audio" : "video";
                item.duration = double(song.totalTime) / 1000.0;
                item.created = song.addedDate ? song.addedDate.timeIntervalSince1970 : 0;
                item.localPath = str(song.location.path);
                list.push_back(std::move(item));
                if (list.size() >= std::size_t(kMostItems))
                    break;
            }
        }
        return list;
    }

    std::optional<ImportPicture> thumbnail(const std::string& itemId, int size) override
    {
        std::lock_guard lock(m_mutex);
        if (!m_library)
            return std::nullopt;
        @autoreleasepool {
            for (ITLibMediaItem* song in m_library.allMediaItems)
                if (str(song.persistentID.stringValue) == itemId)
                    return song.artworkAvailable ? pictureOf(song.artwork.image, size) : std::nullopt;
        }
        return std::nullopt;
    }

    void fetch(const std::string& itemId, const std::string&, std::function<void(std::string, std::string)> done) override
    {
        std::string path;
        {
            std::lock_guard lock(m_mutex);
            if (m_library) {
                @autoreleasepool {
                    for (ITLibMediaItem* song in m_library.allMediaItems)
                        if (str(song.persistentID.stringValue) == itemId)
                            path = str(song.location.path);
                }
            }
        }
        if (path.empty() || !fs::exists(path))
            done({}, "The song's file is not on this Mac.");
        else
            done(path, {});
    }

private:
    bool open()
    {
        std::lock_guard lock(m_mutex);
        if (!m_library) {
            NSError* error = nil;
            m_library = [[ITLibrary alloc] initWithAPIVersion:@"1.1" error:&error];
            m_tried = true;
        }
        return m_library != nil;
    }

    // Songs and videos that are files here (not in the cloud, not copy-protected).
    static std::vector<ITLibMediaItem*> playable(NSArray<ITLibMediaItem*>* items)
    {
        std::vector<ITLibMediaItem*> list;
        for (ITLibMediaItem* item in items) {
            const auto kind = item.mediaKind;
            if ((kind == ITLibMediaItemMediaKindSong || kind == ITLibMediaItemMediaKindMusicVideo
                 || kind == ITLibMediaItemMediaKindHomeVideo || kind == ITLibMediaItemMediaKindMovie)
                && item.locationType == ITLibMediaItemLocationTypeFile && item.location && !item.drmProtected && !item.cloud)
                list.push_back(item);
        }
        return list;
    }

    mutable std::mutex m_mutex;
    ITLibrary* m_library = nil;
    bool m_tried = false;
};

} // namespace

} // namespace tve

// --- Devices (ImageCaptureCore) ---

// Keeps the cameras the browser finds, with sessions open on them, and says when they change.
@interface TVEDeviceWatcher : NSObject <ICDeviceBrowserDelegate, ICCameraDeviceDelegate> {
@public
    ICDeviceBrowser* browser;
    NSMutableArray<ICCameraDevice*>* cameras;
    std::function<void()> changed;
}
@end

@implementation TVEDeviceWatcher

- (instancetype)init
{
    if ((self = [super init])) {
        cameras = [[NSMutableArray alloc] init];
        browser = [[ICDeviceBrowser alloc] init];
        browser.delegate = self;
        browser.browsedDeviceTypeMask = ICDeviceTypeMask(unsigned(ICDeviceTypeMaskCamera) | unsigned(ICDeviceLocationTypeMaskLocal));
        [browser start];
    }
    return self;
}

- (void)dealloc
{
    [browser stop];
    browser.delegate = nil;
    for (ICCameraDevice* camera in cameras) {
        camera.delegate = nil;
        [camera requestCloseSession];
    }
    [browser release];
    [cameras release];
    [super dealloc];
}

- (void)notify
{
    if (changed)
        changed();
}

- (void)deviceBrowser:(ICDeviceBrowser*)b didAddDevice:(ICDevice*)device moreComing:(BOOL)moreComing
{
    if (![device isKindOfClass:[ICCameraDevice class]])
        return;
    ICCameraDevice* camera = (ICCameraDevice*)device;
    [cameras addObject:camera];
    camera.delegate = self;
    [camera requestOpenSession];
    [self notify];
}

- (void)deviceBrowser:(ICDeviceBrowser*)b didRemoveDevice:(ICDevice*)device moreGoing:(BOOL)moreGoing
{
    device.delegate = nil;
    [cameras removeObject:(ICCameraDevice*)device];
    [self notify];
}

- (void)didRemoveDevice:(ICDevice*)device
{
}
- (void)device:(ICDevice*)device didOpenSessionWithError:(NSError*)error
{
    [self notify];
}
- (void)device:(ICDevice*)device didCloseSessionWithError:(NSError*)error
{
}
- (void)cameraDevice:(ICCameraDevice*)camera didAddItems:(NSArray<ICCameraItem*>*)items
{
}
- (void)cameraDevice:(ICCameraDevice*)camera didRemoveItems:(NSArray<ICCameraItem*>*)items
{
    [self notify];
}
- (void)cameraDevice:(ICCameraDevice*)camera didReceiveThumbnail:(CGImageRef)thumbnail forItem:(ICCameraItem*)item error:(NSError*)error
{
}
- (void)cameraDevice:(ICCameraDevice*)camera didReceiveMetadata:(NSDictionary*)metadata forItem:(ICCameraItem*)item error:(NSError*)error
{
}
- (void)cameraDevice:(ICCameraDevice*)camera didRenameItems:(NSArray<ICCameraItem*>*)items
{
}
- (void)cameraDeviceDidChangeCapability:(ICCameraDevice*)camera
{
}
- (void)cameraDevice:(ICCameraDevice*)camera didReceivePTPEvent:(NSData*)eventData
{
}
- (void)deviceDidBecomeReadyWithCompleteContentCatalog:(ICCameraDevice*)device
{
    [self notify];
}
- (void)cameraDeviceDidRemoveAccessRestriction:(ICDevice*)device
{
    [self notify];
}
- (void)cameraDeviceDidEnableAccessRestriction:(ICDevice*)device
{
    [self notify];
}

@end

namespace tve {

namespace {

class DeviceSource final : public ImportSource {
public:
    DeviceSource()
    {
        // The browser reports on the main thread's run loop.
        __block TVEDeviceWatcher* watcher = nil;
        void (^make)(void) = ^{
            watcher = [[TVEDeviceWatcher alloc] init];
        };
        if (NSThread.isMainThread)
            make();
        else
            dispatch_sync(dispatch_get_main_queue(), make);
        m_watcher = watcher;
        m_watcher->changed = [this] { notifyChanged(); };
    }
    ~DeviceSource() override
    {
        TVEDeviceWatcher* watcher = m_watcher;
        watcher->changed = nullptr;
        if (NSThread.isMainThread)
            [watcher release];
        else
            dispatch_async(dispatch_get_main_queue(), ^{
                [watcher release];
            });
    }

    // macOS asks nothing of the person for cameras and cards (an iPhone asks on itself to
    // trust this Mac).
    ImportAccess access() const override { return ImportAccess::Granted; }
    void requestAccess(std::function<void(bool)> done) override { done(true); }

    std::vector<ImportCollection> collections() override
    {
        __block std::vector<ImportCollection> list;
        onMain(^{
            for (ICCameraDevice* camera in m_watcher->cameras) {
                const bool ready = camera.hasOpenSession && camera.contentCatalogPercentCompleted >= 100;
                list.push_back({str(camera.UUIDString), str(camera.name), ready ? int(camera.mediaFiles.count) : -1});
            }
        });
        return list;
    }

    std::vector<ImportItem> items(const std::string& collectionId) override
    {
        __block std::vector<ImportItem> list;
        onMain(^{
            ICCameraDevice* camera = cameraWith(collectionId);
            if (!camera)
                return;
            for (ICCameraItem* entry in camera.mediaFiles) {
                if (![entry isKindOfClass:[ICCameraFile class]])
                    continue;
                ICCameraFile* file = (ICCameraFile*)entry;
                ImportItem item;
                item.title = str(file.name);
                item.kind = kindOfFile(item.title);
                if (item.kind.empty())
                    continue;
                item.created = file.creationDate ? file.creationDate.timeIntervalSince1970 : 0;
                item.id = collectionId + "/" + item.title + "/" + std::to_string(std::int64_t(item.created));
                item.width = int(file.width);
                item.height = int(file.height);
                item.duration = file.duration;
                list.push_back(std::move(item));
                if (list.size() >= std::size_t(kMostItems))
                    break;
            }
        });
        std::ranges::sort(list, [](const ImportItem& a, const ImportItem& b) { return a.created > b.created; });
        return list;
    }

    std::optional<ImportPicture> thumbnail(const std::string& itemId, int size) override
    {
        __block ICCameraFile* file = nil;
        onMain(^{
            file = [fileWith(itemId) retain];
        });
        if (!file)
            return std::nullopt;
        __block NSData* data = nil;
        dispatch_semaphore_t done = dispatch_semaphore_create(0);
        [file requestThumbnailDataWithOptions:@{(NSString*)kCGImageSourceThumbnailMaxPixelSize: @(size)}
                                   completion:^(NSData* thumbnail, NSError*) {
                                       data = [thumbnail retain];
                                       dispatch_semaphore_signal(done);
                                   }];
        // A device that never answers leaves the tile without a picture.
        const bool answered = dispatch_semaphore_wait(done, dispatch_time(DISPATCH_TIME_NOW, 10 * NSEC_PER_SEC)) == 0;
        dispatch_release(done);
        [file release];
        if (!answered)
            return std::nullopt;
        auto picture = pictureOf(data, size);
        [data release];
        return picture;
    }

    void fetch(const std::string& itemId, const std::string& folder,
               std::function<void(std::string, std::string)> done) override
    {
        __block ICCameraFile* file = nil;
        onMain(^{
            file = [fileWith(itemId) retain];
        });
        if (!file) {
            done({}, "The device or the file is no longer there.");
            return;
        }
        std::error_code ec;
        fs::create_directories(folder, ec);
        const std::string name = freeFileName(folder, str(file.name));
        auto* callback = new std::function<void(std::string, std::string)>(std::move(done));
        const std::string target = folder;
        NSDictionary* options = @{
            ICDownloadsDirectoryURL: [NSURL fileURLWithPath:[NSString stringWithUTF8String:folder.c_str()] isDirectory:YES],
            ICSaveAsFilename: [NSString stringWithUTF8String:name.c_str()],
        };
        [file requestDownloadWithOptions:options
                              completion:^(NSString* filename, NSError* error) {
                                  if (error || !filename)
                                      (*callback)({}, error ? str(error.localizedDescription) : "The file did not download.");
                                  else
                                      (*callback)((fs::path(target) / str(filename)).string(), {});
                                  delete callback;
                              }];
        [file release];
    }

private:
    static void onMain(void (^work)(void))
    {
        if (NSThread.isMainThread)
            work();
        else
            dispatch_sync(dispatch_get_main_queue(), work);
    }

    ICCameraDevice* cameraWith(const std::string& id) const
    {
        for (ICCameraDevice* camera in m_watcher->cameras)
            if (str(camera.UUIDString) == id)
                return camera;
        return nil;
    }

    ICCameraFile* fileWith(const std::string& itemId) const
    {
        const auto slash = itemId.find('/');
        ICCameraDevice* camera = cameraWith(itemId.substr(0, slash));
        for (ICCameraItem* entry in camera.mediaFiles) {
            if (![entry isKindOfClass:[ICCameraFile class]])
                continue;
            const double created = entry.creationDate ? entry.creationDate.timeIntervalSince1970 : 0;
            if (itemId.substr(0, slash) + "/" + str(entry.name) + "/" + std::to_string(std::int64_t(created)) == itemId)
                return (ICCameraFile*)entry;
        }
        return nil;
    }

    TVEDeviceWatcher* m_watcher = nil;
};

// --- Test folders ---

class FolderSource final : public ImportSource {
public:
    FolderSource(std::string root, bool copies) : m_root(std::move(root)), m_copies(copies) {}

    ImportAccess access() const override { return ImportAccess::Granted; }
    void requestAccess(std::function<void(bool)> done) override { done(true); }

    std::vector<ImportCollection> collections() override
    {
        std::vector<ImportCollection> list;
        std::error_code ec;
        std::vector<fs::path> folders;
        for (const auto& entry : fs::directory_iterator(m_root, ec))
            if (entry.is_directory(ec))
                folders.push_back(entry.path());
        std::ranges::sort(folders);
        for (const fs::path& folder : folders)
            list.push_back({folder.filename().string(), folder.filename().string(), int(files(folder).size())});
        return list;
    }

    std::vector<ImportItem> items(const std::string& collectionId) override
    {
        std::vector<ImportItem> list;
        if (collectionId.find("..") != std::string::npos)
            return list;
        for (const fs::path& file : files(fs::path(m_root) / collectionId)) {
            ImportItem item;
            item.id = collectionId + "/" + file.filename().string();
            item.title = file.stem().string();
            item.detail = file.filename().string();
            item.kind = kindOfFile(file.string());
            item.localPath = file.string();
            if (item.kind == "video") {
                if (auto video = VideoDecoder::open(file.string())) {
                    item.width = video->info().width;
                    item.height = video->info().height;
                    item.duration = double(video->info().duration) / double(kTicksPerSecond);
                }
            } else if (item.kind == "audio") {
                if (auto sound = AudioDecoder::open(file.string()))
                    item.duration = double(sound->info().duration) / double(kTicksPerSecond);
            }
            std::error_code ec;
            const auto written = fs::last_write_time(file, ec);
            if (!ec)
                item.created = double(std::chrono::duration_cast<std::chrono::seconds>(
                                          std::chrono::file_clock::to_sys(written).time_since_epoch())
                                          .count());
            list.push_back(std::move(item));
        }
        return list;
    }

    std::optional<ImportPicture> thumbnail(const std::string&, int) override { return std::nullopt; }

    void fetch(const std::string& itemId, const std::string& folder,
               std::function<void(std::string, std::string)> done) override
    {
        const fs::path source = fs::path(m_root) / itemId;
        std::error_code ec;
        if (itemId.find("..") != std::string::npos || !fs::is_regular_file(source, ec)) {
            done({}, "The file is no longer there.");
            return;
        }
        if (!m_copies) {
            done(source.string(), {});
            return;
        }
        fs::create_directories(folder, ec);
        const fs::path target = fs::path(folder) / freeFileName(folder, source.filename().string());
        fs::copy_file(source, target, ec);
        if (ec)
            done({}, ec.message());
        else
            done(target.string(), {});
    }

private:
    static std::vector<fs::path> files(const fs::path& folder)
    {
        std::vector<fs::path> list;
        std::error_code ec;
        for (const auto& entry : fs::directory_iterator(folder, ec))
            if (entry.is_regular_file(ec) && !kindOfFile(entry.path().string()).empty())
                list.push_back(entry.path());
        std::ranges::sort(list);
        return list;
    }

    std::string m_root;
    bool m_copies;
};

} // namespace

std::unique_ptr<ImportSource> makePhotosSource()
{
    return std::make_unique<PhotosSource>();
}

std::unique_ptr<ImportSource> makeMusicSource()
{
    return std::make_unique<MusicSource>();
}

std::unique_ptr<ImportSource> makeDeviceSource()
{
    return std::make_unique<DeviceSource>();
}

std::unique_ptr<ImportSource> makeFolderSource(const std::string& root, bool copies)
{
    return std::make_unique<FolderSource>(root, copies);
}

} // namespace tve
