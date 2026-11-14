#include "model/Captions.h"
#include "model/ContentSearch.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace tve;
using Catch::Approx;

namespace {

constexpr Ticks kSec = kTicksPerSecond;

// Ten seconds of a beach: a dog from 3 s to 5 s, a sign reading "Harbour Café" at 7 s.
ContentIndex beach()
{
    ContentIndex index;
    for (int s = 0; s < 10; ++s) {
        ContentFrame frame{s * kSec, {{"beach", 0.9f}, {"sky", 0.8f}, {"sea_ocean", 0.6f}}, {}};
        if (s >= 3 && s <= 5)
            frame.labels.insert(frame.labels.begin(), ContentLabel{"dog", 0.85f});
        if (s == 8)
            frame.labels.push_back({"cat", 0.2f}); // not sure enough to count
        if (s == 7)
            frame.text.push_back({"Harbour Café", 0.5, 0.3, 0.4, 0.08, 0.95f});
        index.frames.push_back(std::move(frame));
    }
    return index;
}

// What is said: "look at the puppy" at 4 s, "lunch at the harbour" at 7.5 s.
std::vector<TimedWord> talk()
{
    return {{4 * kSec, 4 * kSec + kSec / 4, "Look"},          {4 * kSec + kSec / 4, 4 * kSec + kSec / 2, "at"},
            {4 * kSec + kSec / 2, 4 * kSec + 3 * kSec / 4, "the"}, {4 * kSec + 3 * kSec / 4, 5 * kSec, "puppy!"},
            {7 * kSec + kSec / 2, 7 * kSec + 3 * kSec / 4, "Lunch"}, {7 * kSec + 3 * kSec / 4, 8 * kSec, "at"},
            {8 * kSec, 8 * kSec + kSec / 4, "the"},              {8 * kSec + kSec / 4, 8 * kSec + kSec / 2, "harbour."}};
}

} // namespace

TEST_CASE("Search terms drop punctuation and small words", "[search]")
{
    CHECK(searchTerms("The Dog, on a BEACH!") == std::vector<std::string>{"dog", "beach"});
    CHECK(searchTerms("don't stop") == std::vector<std::string>{"dont", "stop"});
    CHECK(searchTerms("dog dog") == std::vector<std::string>{"dog"});
    // Only small words: they are the search.
    CHECK(searchTerms("to be or not") == std::vector<std::string>{"be", "not"});
    CHECK(searchTerms("the") == std::vector<std::string>{"the"});
    // Other scripts are kept whole.
    CHECK(searchTerms("கடல் அலை") == std::vector<std::string>{"கடல்", "அலை"});
}

TEST_CASE("Words match a term exactly, by their stem or by their start", "[search]")
{
    CHECK(termMatch("dog", "dog") == 1.0);
    CHECK(termMatch("dog", "dogs") == Approx(0.9));
    CHECK(termMatch("puppies", "puppy") == Approx(0.9));
    CHECK(termMatch("run", "running") == 0.0); // short terms do not match by their start…
    CHECK(termMatch("walk", "walkway") == Approx(0.7)); // …longer ones do
    CHECK(termMatch("boxes", "box") == Approx(0.9));
    CHECK(termMatch("glass", "glas") == 0.0);
    CHECK(termMatch("cat", "dog") == 0.0);
}

TEST_CASE("Labels read as words and their shares are counted", "[search]")
{
    CHECK(labelText("sea_ocean") == "sea ocean");
    const auto top = topLabels(beach(), 4);
    REQUIRE(top.size() == 4);
    // As often as one another: the surest first.
    CHECK(top[0].name == "beach");
    CHECK(top[0].share == Approx(1.0));
    CHECK(top[1].name == "sky");
    CHECK(top[2].name == "sea_ocean");
    CHECK(top[3].name == "dog");
    CHECK(top[3].share == Approx(0.3));
    // A label never sure enough is not counted.
    for (const LabelShare& l : topLabels(beach(), 20))
        CHECK(l.name != "cat");
}

TEST_CASE("Search finds what is seen as moments", "[search]")
{
    const ContentIndex index = beach();
    const std::vector<SearchableMedia> media{{7, 10 * kSec, false, &index, nullptr}};
    const auto hits = searchMedia("dogs", media);
    REQUIRE(hits.size() == 1);
    CHECK(hits[0].mediaId == 7);
    CHECK(hits[0].start == 3 * kSec);
    CHECK(hits[0].end == 6 * kSec);
    CHECK(hits[0].found == kFoundSeen);
    CHECK(hits[0].snippet == "dog");

    // A label with two words matches either.
    const auto ocean = searchMedia("ocean", media);
    REQUIRE(ocean.size() == 1);
    CHECK(ocean[0].start == 0);
    CHECK(ocean[0].end == 10 * kSec);
    CHECK(ocean[0].snippet == "sea ocean");

    CHECK(searchMedia("cat", media).empty());
    CHECK(searchMedia("", media).empty());
}

TEST_CASE("Search finds what is said and shown, and every word near the others", "[search]")
{
    const ContentIndex index = beach();
    const std::vector<TimedWord> words = talk();
    const std::vector<SearchableMedia> media{{7, 10 * kSec, false, &index, &words}};

    const auto puppy = searchMedia("puppy", media);
    REQUIRE(puppy.size() == 1);
    CHECK(puppy[0].found == kFoundSaid);
    CHECK(puppy[0].start == 4 * kSec);
    CHECK(puppy[0].end == 5 * kSec);
    CHECK(puppy[0].snippet == "Look at the puppy!");

    // "harbour" is said at 8 s and on the sign at 7 s: one moment, found both ways.
    const auto harbour = searchMedia("Harbour", media);
    REQUIRE(harbour.size() == 1);
    CHECK(harbour[0].start == 7 * kSec);
    CHECK(harbour[0].end == 9 * kSec);
    CHECK(harbour[0].found == (kFoundSaid | kFoundShown));
    CHECK(harbour[0].snippet == "Lunch at the harbour.");

    // The sign's other word, with accents as they are.
    const auto cafe = searchMedia("café", media);
    REQUIRE(cafe.size() == 1);
    CHECK(cafe[0].found == kFoundShown);
    CHECK(cafe[0].snippet == "Harbour Café");

    // Words from different sources, within a second of one another.
    const auto dogPuppy = searchMedia("dog puppy", media);
    REQUIRE(dogPuppy.size() == 1);
    CHECK(dogPuppy[0].found == (kFoundSeen | kFoundSaid));
    CHECK(dogPuppy[0].start == 3 * kSec);
    CHECK(dogPuppy[0].end == 6 * kSec);

    // Not near one another: nothing.
    CHECK(searchMedia("dog lunch", media).empty());
    // A word found nowhere: nothing.
    CHECK(searchMedia("dog zebra", media).empty());
}

TEST_CASE("Search ranks closer matches first, and a still matches whole", "[search]")
{
    ContentIndex photo;
    photo.frames.push_back({0, {{"dogsled", 0.9f}}, {}});
    ContentIndex other;
    other.frames.push_back({0, {{"dog", 0.95f}}, {}});
    const std::vector<SearchableMedia> media{{1, 5 * kSec, true, &photo, nullptr}, {2, 5 * kSec, true, &other, nullptr}};
    // "dog" itself first, then the word it starts.
    const auto hits = searchMedia("dogs", media);
    REQUIRE(hits.size() == 2);
    CHECK(hits[0].mediaId == 2);
    CHECK(hits[0].start == 0);
    CHECK(hits[0].end == 5 * kSec);
    CHECK(hits[1].mediaId == 1);
    CHECK(hits[1].score < hits[0].score);

    const auto loose = searchMedia("dogsl dog", media);
    CHECK(loose.empty()); // every word must be found in the same media
    const auto both = searchMedia("dog", media);
    REQUIRE(both.size() == 1);

    // The limit.
    std::vector<SearchableMedia> many;
    for (Id id = 1; id <= 5; ++id)
        many.push_back({id, 5 * kSec, true, &other, nullptr});
    CHECK(searchMedia("dog", many, 3).size() == 3);
}

TEST_CASE("A corrected caption keeps its words' timing", "[captions]")
{
    const std::vector<CaptionWord> words{{0, 10, "Teh"}, {10, 20, "dog"}, {20, 40, "runned"}};
    const auto same = retimeWords(words, "The dog ran");
    REQUIRE(same.size() == 3);
    CHECK(same[0] == CaptionWord{0, 10, "The"});
    CHECK(same[2] == CaptionWord{20, 40, "ran"});

    // More words: spread over the same time by their length.
    const auto more = retimeWords(words, "The dog\nwas running");
    REQUIRE(more.size() == 4);
    CHECK(more.front().start == 0);
    CHECK(more.back().end == 40);
    CHECK(more[1].text == "dog");
    for (std::size_t i = 1; i < more.size(); ++i)
        CHECK(more[i].start == more[i - 1].end);
    CHECK(more[3].end - more[3].start > more[1].end - more[1].start);

    CHECK(retimeWords({}, "Anything").empty());
}
