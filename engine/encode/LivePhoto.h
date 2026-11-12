#pragma once

#include "model/Time.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

// Live Photos: a key photo and a short movie that Photos (on a Mac, iPhone or iPad) shows as
// one picture that plays when pressed. What pairs them is a content identifier both carry —
// the photo in its Apple maker note (key "17"), the movie as QuickTime metadata
// (com.apple.quicktime.content.identifier) — and the movie marks the key photo's moment with
// a timed metadata track (com.apple.quicktime.still-image-time).
namespace tve::livephoto {

// A new content identifier (a UUID).
std::string newIdentifier();

// Copies `movie` (a QuickTime or MP4 file) to `out` as a Live Photo's movie: the pictures and
// sound as they are (not encoded again), with `identifier` and the key photo at `stillTime`
// from its start. `out` is replaced.
bool writeMovie(const std::string& movie, const std::string& out, const std::string& identifier, Ticks stillTime,
                std::string* error = nullptr);

// Writes the key photo from 8-bit RGBA rows (opaque, sRGB) with `identifier`: HEIC, or JPEG
// when `path` ends in .jpg or .jpeg.
bool writeStill(const std::string& path, const std::uint8_t* rgba, int width, int height, std::size_t bytesPerRow,
                const std::string& identifier, std::string* error = nullptr);

// The content identifier a Live Photo's movie or key photo carries ("" when none).
std::string movieIdentifier(const std::string& movie);
std::string stillIdentifier(const std::string& still);
// When the movie's key photo is, if it marks one.
std::optional<Ticks> movieStillTime(const std::string& movie);

// Whether Photos itself takes the two files for one Live Photo (PHLivePhoto, without the
// library). Waits (running the current thread's run loop) up to `timeoutSeconds`.
bool photosAcceptsPair(const std::string& still, const std::string& movie, double timeoutSeconds = 20.0);

// Adds the pair to the Photos library as one Live Photo, asking for add-only access the first
// time. Blocks until Photos answers; call it off the main thread.
bool addToPhotos(const std::string& still, const std::string& movie, std::string* error = nullptr);

} // namespace tve::livephoto
