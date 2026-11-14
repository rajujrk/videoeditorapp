#pragma once

#include "model/Captions.h"
#include "model/Project.h"
#include "model/Time.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace tve {

// What a media shows, for finding it again: labels for what is in the picture and the text
// on screen, about once a second (one frame for a still). What finds them (Vision's image
// classifier and text recognizer) is the engine's (analysis/ContentVision); this is what is
// kept and how it is searched, with what is said (the media's transcript) beside it.

// A label for what is in a picture ("dog", "beach", "sky"…; Vision's identifiers, which are
// English with underscores for spaces), and how sure the classifier is (0..1).
struct ContentLabel {
    std::string name;
    float confidence = 0.0f;

    friend bool operator==(const ContentLabel&, const ContentLabel&) = default;
};

// A line of text seen on screen, its box as fractions of the picture (origin top left).
struct ScreenText {
    std::string text;
    double x = 0.0; // centre
    double y = 0.0;
    double width = 0.0;
    double height = 0.0;
    float confidence = 0.0f;

    friend bool operator==(const ScreenText&, const ScreenText&) = default;
};

struct ContentFrame {
    Ticks time = 0; // media time
    std::vector<ContentLabel> labels; // most sure first
    std::vector<ScreenText> text;     // top to bottom

    friend bool operator==(const ContentFrame&, const ContentFrame&) = default;
};

struct ContentIndex {
    static constexpr int kVersion = 1;
    static constexpr Ticks kSpacing = kTicksPerSecond; // between frames looked at
    std::vector<ContentFrame> frames;

    friend bool operator==(const ContentIndex&, const ContentIndex&) = default;
};

// Labels less sure than this are not kept, nor searched.
constexpr float kLabelThreshold = 0.3f;

// A label's name for people ("sea_ocean" → "sea ocean").
std::string labelText(std::string_view name);

// The labels seen most through a media: each with the share of its frames that show it
// (0..1), most first; `count` of them at most.
struct LabelShare {
    std::string name;
    double share = 0.0;

    friend bool operator==(const LabelShare&, const LabelShare&) = default;
};
std::vector<LabelShare> topLabels(const ContentIndex& index, std::size_t count = 8);

// --- Smart search ---

// How a moment was found (bits of SearchHit::found).
enum SearchFound : unsigned {
    kFoundSeen = 1u,  // a label: it is in the picture
    kFoundSaid = 2u,  // in what is said
    kFoundShown = 4u, // in the text on screen
};

// A media to search, with what is known of it (either may be null or empty).
struct SearchableMedia {
    Id mediaId = kNoId;
    Ticks duration = 0;
    bool still = false;
    const ContentIndex* content = nullptr;
    const std::vector<TimedWord>* words = nullptr; // what is said, media time
};

// A moment of a media matching a search (media time).
struct SearchHit {
    Id mediaId = kNoId;
    Ticks start = 0;
    Ticks end = 0;
    unsigned found = 0;  // SearchFound bits
    std::string snippet; // what matched: the words said, the text on screen, or the labels
    double score = 0.0;  // higher is better

    friend bool operator==(const SearchHit&, const SearchHit&) = default;
};

// The search's words: lower case, without punctuation, small words ("the", "a", "of"…)
// left out unless there is nothing else.
std::vector<std::string> searchTerms(std::string_view query);

// Whether a word answers a search term: the same word, the same word but for a plural or an
// ending ("dogs", "running"), or (terms of 4 letters or more) the start of a longer word.
// 0 when not; 1 the same; less for the looser matches.
double termMatch(std::string_view term, std::string_view word);

// The moments of these media matching every word of `query` — each word in a label, in what
// is said or in the text on screen, within a second of the others — best first, `limit` at
// most. Consecutive matching seconds make one moment; a still matches as a whole.
std::vector<SearchHit> searchMedia(std::string_view query, const std::vector<SearchableMedia>& media,
                                   std::size_t limit = 50);

} // namespace tve
