#pragma once

#include "model/Project.h"

#include <optional>
#include <string>
#include <vector>

namespace tve {

// A word with its time on the timeline (from speech recognition).
struct TimedWord {
    Ticks start = 0;
    Ticks end = 0;
    std::string text;

    friend bool operator==(const TimedWord&, const TimedWord&) = default;
};

// One caption: what shows from `start` to `end` (lines joined by '\n'), and its words.
struct CaptionBlock {
    Ticks start = 0;
    Ticks end = 0;
    std::string text;
    std::vector<TimedWord> words; // empty for captions read from a file

    friend bool operator==(const CaptionBlock&, const CaptionBlock&) = default;
};

struct CaptionLayout {
    std::size_t lineLength = 42; // characters per line, the usual subtitle limit
    std::size_t lines = 2;
    double maxDuration = 6.0;    // seconds on screen at most
    double pause = 0.7;          // a pause this long between words starts a new caption
    double minDuration = 1.0;    // short captions stay up this long (if the next one allows)
};

// Words into captions: a new caption at a pause, a sentence end once the caption is half
// full, or when it would grow past its lines or its time; lines broken between words.
std::vector<CaptionBlock> layoutCaptions(const std::vector<TimedWord>& words, const CaptionLayout& layout = {});

// A filler sound ("um", "uh", "hmm"…), whatever its case and punctuation.
bool isFillerWord(const std::string& word);

// How new captions look: white on a dark box near the bottom of the picture.
ClipText captionText();
ClipVisual captionVisual();

// --- SRT and WebVTT ---

std::string writeSrt(const std::vector<CaptionBlock>& captions);
std::string writeVtt(const std::vector<CaptionBlock>& captions);
// Reads SubRip or WebVTT (told apart by the "WEBVTT" header). Cue settings, styling tags and
// timestamps inside cues are dropped. Nullopt (with `error`) when no caption can be read.
std::optional<std::vector<CaptionBlock>> readSubtitles(const std::string& text, std::string* error = nullptr);

} // namespace tve
