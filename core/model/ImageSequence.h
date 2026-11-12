#pragma once

#include "model/Time.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace tve {

// Numbered pictures in one folder played as video: shot_0001.png, shot_0002.png… A media
// item of one has its first frame as its path (a real file, so finding, fingerprinting and
// relinking it work as for any file) and the run here; the frames are its folder's.
struct ImageSequence {
    // The file names with the number as a printf field: "shot_%04d.png" (padded to four
    // digits) or "frame%d.exr" (not padded).
    std::string pattern;
    std::int64_t first = 0; // the first and last numbers found
    std::int64_t last = 0;

    std::int64_t length() const { return last - first + 1; }

    friend bool operator==(const ImageSequence&, const ImageSequence&) = default;
};

// The sequence `framePath` is one frame of: every file in its folder named like it with
// another number (the last run of digits in its name), from the lowest number to the
// highest. Nullopt when it has no number or no other frame is there.
std::optional<ImageSequence> findImageSequence(const std::string& framePath);

// The file of frame `number` in `folder`.
std::string sequenceFrameFile(const std::string& folder, const ImageSequence& sequence, std::int64_t number);

// Every frame's file in order, first to last. A number with no file (a gap) shows the
// frame before it; any missing at the start, the first one there.
std::vector<std::string> sequenceFrameFiles(const std::string& firstFramePath, const ImageSequence& sequence);

// The name shown for one: the pattern with #s for the number ("shot_####.png").
std::string sequenceDisplayName(const ImageSequence& sequence);

// How decoders know a first frame starts a sequence (they are given only a path): the
// project's sequences are registered with their frame rates as they are imported or
// loaded, and VideoDecoder::open plays a registered path as the sequence. Thread-safe.
struct RegisteredSequence {
    ImageSequence sequence;
    FrameRate rate;
};
void registerImageSequence(const std::string& firstFramePath, const ImageSequence& sequence, FrameRate rate);
std::optional<RegisteredSequence> registeredImageSequence(const std::string& path);

} // namespace tve
