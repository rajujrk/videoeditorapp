#include "model/Consent.h"

#include <catch2/catch_test_macros.hpp>

#include <set>

using namespace tve;

namespace {

constexpr Ticks kSec = kTicksPerSecond;

ConsentStatement statement(unsigned seed = 7)
{
    std::mt19937 random(seed);
    return consentStatement("Ada Okafor", spokenDate(2026, 11, 10), random);
}

} // namespace

TEST_CASE("a consent statement names the speaker, says what it is for, and has fresh code words")
{
    const ConsentStatement s = statement();
    CHECK(s.text.rfind("My name is Ada Okafor. I agree that this recording of my voice may be sent to ElevenLabs", 0) == 0);
    CHECK(s.text.find("at least eighteen") != std::string::npos);
    CHECK(s.text.find("Today is the tenth of November, 2026.") != std::string::npos);
    REQUIRE(s.codeWords.size() == 4);
    CHECK(std::set<std::string>(s.codeWords.begin(), s.codeWords.end()).size() == 4);
    for (const std::string& w : s.codeWords)
        CHECK(s.text.find(w) != std::string::npos);
    // The same seed, the same words; another, others.
    CHECK(statement(7).codeWords == s.codeWords);
    CHECK(statement(8).codeWords != s.codeWords);
    CHECK(spokenDate(2027, 1, 23) == "the twenty-third of January, 2027");
    CHECK(consentWords("It's  Ada, OKAFOR!") == std::vector<std::string>{"its", "ada", "okafor"});
}

TEST_CASE("a recording passes only if it says the statement: its code words, the name, most of it in order")
{
    const ConsentStatement s = statement();
    // Read as written (punctuation and case as a transcript has them).
    ConsentCheck check = checkConsent(s, s.text, 40 * kSec);
    CHECK(check.passed);
    CHECK(check.match > 0.99);
    // Small slips of the recogniser are let through.
    std::string slips = s.text;
    slips.replace(slips.find("Okafor"), 6, "Okafur");
    slips.replace(slips.find("morning"), 7, "mornin");
    check = checkConsent(s, slips, 40 * kSec);
    CHECK(check.passed);
    // A name split differently (as Whisper heard it).
    std::string split = s.text;
    split.replace(split.find("Ada Okafor"), 10, "Adoroka for");
    CHECK(checkConsent(s, split, 40 * kSec).passed);
    // A code word not said: named.
    std::string missing = s.text;
    const std::string word = s.codeWords[1];
    missing.replace(missing.find(word), word.size(), "something");
    check = checkConsent(s, missing, 40 * kSec);
    CHECK_FALSE(check.passed);
    CHECK(check.missing == std::vector<std::string>{word});
    CHECK(check.why.find("“" + word + "”") != std::string::npos);
    // Another name.
    std::string other = s.text;
    other.replace(other.find("Ada Okafor"), 10, "Bea Morgan");
    check = checkConsent(s, other, 40 * kSec);
    CHECK_FALSE(check.passed);
    CHECK(check.missing == std::vector<std::string>{"ada", "okafor"});
    // The code words alone, without the statement.
    std::string words = "My name is Ada Okafor";
    for (const std::string& w : s.codeWords)
        words += " " + w;
    check = checkConsent(s, words, 40 * kSec);
    CHECK_FALSE(check.passed);
    CHECK(check.match < 0.8);
    CHECK(check.why.find("doesn't say the statement") != std::string::npos);
    // Too short to be a reading of it.
    check = checkConsent(s, s.text, 5 * kSec);
    CHECK_FALSE(check.passed);
    CHECK(check.why.find("too short") != std::string::npos);
    // Nothing heard.
    CHECK_FALSE(checkConsent(s, "", 40 * kSec).passed);
}
