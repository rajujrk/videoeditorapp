#include "encode/LivePhoto.h"

#import <AVFoundation/AVFoundation.h>
#import <CoreMedia/CoreMedia.h>
#import <Foundation/Foundation.h>
#import <ImageIO/ImageIO.h>
#import <Photos/Photos.h>

#include <vector>

namespace tve::livephoto {

namespace {

NSString* const kContentIdentifierKey = @"com.apple.quicktime.content.identifier";
NSString* const kStillImageTimeKey = @"com.apple.quicktime.still-image-time";
NSString* const kMakerNoteIdentifierKey = @"17"; // in the Apple maker note

void setError(std::string* error, const std::string& message)
{
    if (error)
        *error = message;
}

std::string text(NSString* string)
{
    return string ? std::string(string.UTF8String) : std::string();
}

NSURL* fileUrl(const std::string& path)
{
    return [NSURL fileURLWithPath:[NSString stringWithUTF8String:path.c_str()]];
}

// Waits for an asynchronous AVFoundation load (macOS 12's loaders take a completion block).
void waitFor(void (^load)(dispatch_block_t finished))
{
    dispatch_semaphore_t done = dispatch_semaphore_create(0);
    load(^{ dispatch_semaphore_signal(done); });
    dispatch_semaphore_wait(done, DISPATCH_TIME_FOREVER);
}

NSArray<AVAssetTrack*>* tracksOf(AVAsset* asset, AVMediaType type)
{
    __block NSArray<AVAssetTrack*>* found = @[];
    waitFor(^(dispatch_block_t finished) {
        [asset loadTracksWithMediaType:type
                     completionHandler:^(NSArray<AVAssetTrack*>* tracks, NSError*) {
                         if (tracks)
                             found = tracks;
                         finished();
                     }];
    });
    return found;
}

NSArray<AVMetadataItem*>* quickTimeMetadata(AVAsset* asset)
{
    __block NSArray<AVMetadataItem*>* found = @[];
    waitFor(^(dispatch_block_t finished) {
        [asset loadMetadataForFormat:AVMetadataFormatQuickTimeMetadata
                   completionHandler:^(NSArray<AVMetadataItem*>* items, NSError*) {
                       if (items)
                           found = items;
                       finished();
                   }];
    });
    return found;
}

} // namespace

std::string newIdentifier()
{
    return text(NSUUID.UUID.UUIDString);
}

bool writeMovie(const std::string& movie, const std::string& out, const std::string& identifier, Ticks stillTime,
                std::string* error)
{
    @autoreleasepool {
        AVURLAsset* asset = [AVURLAsset URLAssetWithURL:fileUrl(movie) options:nil];
        NSMutableArray<AVAssetTrack*>* tracks = [NSMutableArray array];
        [tracks addObjectsFromArray:tracksOf(asset, AVMediaTypeVideo)];
        [tracks addObjectsFromArray:tracksOf(asset, AVMediaTypeAudio)];
        if (tracks.count == 0 || tracksOf(asset, AVMediaTypeVideo).count == 0) {
            setError(error, "no pictures in " + movie);
            return false;
        }

        NSError* failure = nil;
        AVAssetReader* reader = [AVAssetReader assetReaderWithAsset:asset error:&failure];
        NSURL* outUrl = fileUrl(out);
        [NSFileManager.defaultManager removeItemAtURL:outUrl error:nil];
        AVAssetWriter* writer = [AVAssetWriter assetWriterWithURL:outUrl fileType:AVFileTypeQuickTimeMovie error:&failure];
        if (!reader || !writer) {
            setError(error, text(failure.localizedDescription));
            return false;
        }

        // Each track copied as it is: its samples straight from the reader to the writer.
        NSMutableArray<AVAssetReaderTrackOutput*>* outputs = [NSMutableArray array];
        NSMutableArray<AVAssetWriterInput*>* inputs = [NSMutableArray array];
        for (AVAssetTrack* track in tracks) {
            waitFor(^(dispatch_block_t finished) {
                [track loadValuesAsynchronouslyForKeys:@[ @"formatDescriptions", @"preferredTransform" ]
                                     completionHandler:finished];
            });
            AVAssetReaderTrackOutput* output = [AVAssetReaderTrackOutput assetReaderTrackOutputWithTrack:track
                                                                                          outputSettings:nil];
            output.alwaysCopiesSampleData = NO;
            id hint = track.formatDescriptions.firstObject;
            AVAssetWriterInput* input =
                [AVAssetWriterInput assetWriterInputWithMediaType:track.mediaType
                                                   outputSettings:nil
                                                 sourceFormatHint:(__bridge CMFormatDescriptionRef)hint];
            input.expectsMediaDataInRealTime = NO;
            if ([track.mediaType isEqualToString:AVMediaTypeVideo])
                input.transform = track.preferredTransform;
            if (![reader canAddOutput:output] || ![writer canAddInput:input]) {
                setError(error, "cannot copy the tracks of " + movie);
                return false;
            }
            [reader addOutput:output];
            [writer addInput:input];
            [outputs addObject:output];
            [inputs addObject:input];
        }

        // The key photo's moment: a timed metadata track with one sample there.
        NSDictionary* specification = @{
            (__bridge NSString*)kCMMetadataFormatDescriptionMetadataSpecificationKey_Identifier :
                [@"mdta/" stringByAppendingString:kStillImageTimeKey],
            (__bridge NSString*)kCMMetadataFormatDescriptionMetadataSpecificationKey_DataType :
                (__bridge NSString*)kCMMetadataBaseDataType_SInt8,
        };
        CMFormatDescriptionRef metadataFormat = nullptr;
        CMMetadataFormatDescriptionCreateWithMetadataSpecifications(
            kCFAllocatorDefault, kCMMetadataFormatType_Boxed, (__bridge CFArrayRef) @[ specification ], &metadataFormat);
        AVAssetWriterInput* metadataInput = [AVAssetWriterInput assetWriterInputWithMediaType:AVMediaTypeMetadata
                                                                               outputSettings:nil
                                                                             sourceFormatHint:metadataFormat];
        if (metadataFormat)
            CFRelease(metadataFormat);
        AVAssetWriterInputMetadataAdaptor* adaptor =
            [AVAssetWriterInputMetadataAdaptor assetWriterInputMetadataAdaptorWithAssetWriterInput:metadataInput];
        if (![writer canAddInput:metadataInput]) {
            setError(error, "cannot add the key photo's time");
            return false;
        }
        [writer addInput:metadataInput];

        // The identifier, for the whole movie.
        AVMutableMetadataItem* identifierItem = [AVMutableMetadataItem metadataItem];
        identifierItem.keySpace = AVMetadataKeySpaceQuickTimeMetadata;
        identifierItem.key = kContentIdentifierKey;
        identifierItem.value = [NSString stringWithUTF8String:identifier.c_str()];
        identifierItem.dataType = (__bridge NSString*)kCMMetadataBaseDataType_UTF8;
        writer.metadata = @[ identifierItem ];

        if (![reader startReading] || ![writer startWriting]) {
            setError(error, text((reader.error ? reader.error : writer.error).localizedDescription));
            return false;
        }
        [writer startSessionAtSourceTime:kCMTimeZero];

        AVMutableMetadataItem* stillItem = [AVMutableMetadataItem metadataItem];
        stillItem.keySpace = AVMetadataKeySpaceQuickTimeMetadata;
        stillItem.key = kStillImageTimeKey;
        stillItem.value = @0;
        stillItem.dataType = (__bridge NSString*)kCMMetadataBaseDataType_SInt8;
        const CMTime at = CMTimeMake(microsFromTicks(std::max<Ticks>(stillTime, 0)), 1'000'000);
        AVTimedMetadataGroup* group = [[AVTimedMetadataGroup alloc] initWithItems:@[ stillItem ]
                                                                        timeRange:CMTimeRangeMake(at, CMTimeMake(1, 100))];
        while (!metadataInput.readyForMoreMediaData)
            [NSThread sleepForTimeInterval:0.005];
        const bool marked = [adaptor appendTimedMetadataGroup:group];
        [metadataInput markAsFinished];

        // Every track fed on its own queue, so the writer can interleave them.
        dispatch_group_t copying = dispatch_group_create();
        __block bool copyFailed = !marked; // (only ever set, from the copying queues)
        for (NSUInteger i = 0; i < inputs.count; ++i) {
            AVAssetWriterInput* input = inputs[i];
            AVAssetReaderTrackOutput* output = outputs[i];
            dispatch_group_enter(copying);
            dispatch_queue_t queue = dispatch_queue_create("tve.livephoto.copy", DISPATCH_QUEUE_SERIAL);
            __block bool finished = false;
            [input requestMediaDataWhenReadyOnQueue:queue
                                         usingBlock:^{
                                             while (!finished && input.readyForMoreMediaData) {
                                                 CMSampleBufferRef sample = [output copyNextSampleBuffer];
                                                 if (!sample || ![input appendSampleBuffer:sample]) {
                                                     if (sample)
                                                         copyFailed = true;
                                                     finished = true;
                                                     [input markAsFinished];
                                                     dispatch_group_leave(copying);
                                                 }
                                                 if (sample)
                                                     CFRelease(sample);
                                             }
                                         }];
        }
        dispatch_group_wait(copying, DISPATCH_TIME_FOREVER);
        const bool ok = !copyFailed && reader.status != AVAssetReaderStatusFailed;

        waitFor(^(dispatch_block_t finished) {
            [writer finishWritingWithCompletionHandler:finished];
        });
        if (!ok || writer.status != AVAssetWriterStatusCompleted) {
            NSError* why = writer.error ? writer.error : reader.error;
            setError(error, why ? text(why.localizedDescription) : std::string("could not write the Live Photo's movie"));
            [NSFileManager.defaultManager removeItemAtURL:outUrl error:nil];
            return false;
        }
        return true;
    }
}

bool writeStill(const std::string& path, const std::uint8_t* rgba, int width, int height, std::size_t bytesPerRow,
                const std::string& identifier, std::string* error)
{
    @autoreleasepool {
        CGColorSpaceRef srgb = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
        CGContextRef context = CGBitmapContextCreate(const_cast<std::uint8_t*>(rgba), std::size_t(width),
                                                     std::size_t(height), 8, bytesPerRow, srgb,
                                                     CGBitmapInfo(kCGImageAlphaNoneSkipLast) | kCGBitmapByteOrder32Big);
        CGImageRef image = context ? CGBitmapContextCreateImage(context) : nullptr;
        const std::string lower = [[NSString stringWithUTF8String:path.c_str()].lowercaseString UTF8String];
        const bool jpeg = lower.ends_with(".jpg") || lower.ends_with(".jpeg");
        CGImageDestinationRef destination =
            image ? CGImageDestinationCreateWithURL((__bridge CFURLRef)fileUrl(path),
                                                    jpeg ? CFSTR("public.jpeg") : CFSTR("public.heic"), 1, nullptr)
                  : nullptr;
        bool ok = false;
        if (destination) {
            NSDictionary* properties = @{
                (__bridge NSString*)kCGImagePropertyMakerAppleDictionary :
                    @{kMakerNoteIdentifierKey : [NSString stringWithUTF8String:identifier.c_str()]},
                (__bridge NSString*)kCGImageDestinationLossyCompressionQuality : @0.9,
            };
            CGImageDestinationAddImage(destination, image, (__bridge CFDictionaryRef)properties);
            ok = CGImageDestinationFinalize(destination);
            CFRelease(destination);
        }
        if (image)
            CGImageRelease(image);
        if (context)
            CGContextRelease(context);
        CGColorSpaceRelease(srgb);
        if (!ok)
            setError(error, "could not write the key photo " + path);
        return ok;
    }
}

std::string movieIdentifier(const std::string& movie)
{
    @autoreleasepool {
        AVURLAsset* asset = [AVURLAsset URLAssetWithURL:fileUrl(movie) options:nil];
        for (AVMetadataItem* item in quickTimeMetadata(asset)) {
            if ([item.identifier isEqualToString:[@"mdta/" stringByAppendingString:kContentIdentifierKey]]) {
                __block NSString* value = nil;
                waitFor(^(dispatch_block_t finished) {
                    [item loadValuesAsynchronouslyForKeys:@[ @"stringValue" ] completionHandler:finished];
                });
                value = item.stringValue;
                return text(value);
            }
        }
        return {};
    }
}

std::string stillIdentifier(const std::string& still)
{
    @autoreleasepool {
        CGImageSourceRef source = CGImageSourceCreateWithURL((__bridge CFURLRef)fileUrl(still), nullptr);
        if (!source)
            return {};
        NSDictionary* properties = CFBridgingRelease(CGImageSourceCopyPropertiesAtIndex(source, 0, nullptr));
        CFRelease(source);
        NSDictionary* maker = properties[(__bridge NSString*)kCGImagePropertyMakerAppleDictionary];
        id value = maker[kMakerNoteIdentifierKey];
        return [value isKindOfClass:NSString.class] ? text(value) : std::string();
    }
}

std::optional<Ticks> movieStillTime(const std::string& movie)
{
    @autoreleasepool {
        AVURLAsset* asset = [AVURLAsset URLAssetWithURL:fileUrl(movie) options:nil];
        for (AVAssetTrack* track in tracksOf(asset, AVMediaTypeMetadata)) {
            NSError* failure = nil;
            AVAssetReader* reader = [AVAssetReader assetReaderWithAsset:asset error:&failure];
            AVAssetReaderTrackOutput* output = [AVAssetReaderTrackOutput assetReaderTrackOutputWithTrack:track
                                                                                          outputSettings:nil];
            if (!reader || ![reader canAddOutput:output])
                continue;
            [reader addOutput:output];
            AVAssetReaderOutputMetadataAdaptor* adaptor =
                [AVAssetReaderOutputMetadataAdaptor assetReaderOutputMetadataAdaptorWithAssetReaderTrackOutput:output];
            if (![reader startReading])
                continue;
            while (AVTimedMetadataGroup* group = [adaptor nextTimedMetadataGroup]) {
                for (AVMetadataItem* item in group.items)
                    if ([item.identifier isEqualToString:[@"mdta/" stringByAppendingString:kStillImageTimeKey]]) {
                        [reader cancelReading];
                        return ticksFromMicros(std::int64_t(CMTimeGetSeconds(group.timeRange.start) * 1e6 + 0.5));
                    }
            }
        }
        return std::nullopt;
    }
}

bool photosAcceptsPair(const std::string& still, const std::string& movie, double timeoutSeconds)
{
    @autoreleasepool {
        __block bool answered = false;
        __block bool accepted = false;
        [PHLivePhoto requestLivePhotoWithResourceFileURLs:@[ fileUrl(still), fileUrl(movie) ]
                                         placeholderImage:nil
                                               targetSize:CGSizeZero
                                              contentMode:PHImageContentModeAspectFit
                                            resultHandler:^(PHLivePhoto* livePhoto, NSDictionary* info) {
                                                if ([info[PHLivePhotoInfoIsDegradedKey] boolValue])
                                                    return; // a first, rough answer: the full one follows
                                                accepted = livePhoto != nil && info[PHLivePhotoInfoErrorKey] == nil;
                                                answered = true;
                                            }];
        // (The answer may come on this thread: its run loop turns while waiting.)
        NSDate* deadline = [NSDate dateWithTimeIntervalSinceNow:timeoutSeconds];
        while (!answered && [deadline timeIntervalSinceNow] > 0)
            [NSRunLoop.currentRunLoop runMode:NSDefaultRunLoopMode beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.05]];
        return accepted;
    }
}

bool addToPhotos(const std::string& still, const std::string& movie, std::string* error)
{
    @autoreleasepool {
        __block PHAuthorizationStatus status = [PHPhotoLibrary authorizationStatusForAccessLevel:PHAccessLevelAddOnly];
        if (status == PHAuthorizationStatusNotDetermined) {
            waitFor(^(dispatch_block_t finished) {
                [PHPhotoLibrary requestAuthorizationForAccessLevel:PHAccessLevelAddOnly
                                                           handler:^(PHAuthorizationStatus answer) {
                                                               status = answer;
                                                               finished();
                                                           }];
            });
        }
        if (status != PHAuthorizationStatusAuthorized && status != PHAuthorizationStatusLimited) {
            setError(error, "TechieVideoEditor is not allowed to add to Photos: turn it on in System Settings, "
                            "Privacy & Security, Photos");
            return false;
        }
        __block bool ok = false;
        __block NSError* failure = nil;
        NSURL* stillUrl = fileUrl(still);
        NSURL* movieUrl = fileUrl(movie);
        waitFor(^(dispatch_block_t finished) {
            [PHPhotoLibrary.sharedPhotoLibrary
                performChanges:^{
                    PHAssetCreationRequest* request = [PHAssetCreationRequest creationRequestForAsset];
                    [request addResourceWithType:PHAssetResourceTypePhoto fileURL:stillUrl options:nil];
                    [request addResourceWithType:PHAssetResourceTypePairedVideo fileURL:movieUrl options:nil];
                }
                completionHandler:^(BOOL success, NSError* why) {
                    ok = success;
                    failure = why;
                    finished();
                }];
        });
        if (!ok)
            setError(error, failure ? text(failure.localizedDescription) : std::string("Photos did not take the Live Photo"));
        return ok;
    }
}

} // namespace tve::livephoto
