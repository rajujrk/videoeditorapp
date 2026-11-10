#include "model/Consent.h"
#include "model/Translate.h"

#include <algorithm>
#include <array>
#include <cctype>

namespace tve {

namespace {

// Plain, distinct words that speech recognition hears reliably.
constexpr std::array<const char*, 48> kCodeWords{
    "apple",  "anchor", "basket", "candle", "carpet", "cherry", "copper", "dolphin", "engine", "feather", "forest", "garden",
    "guitar", "harbor", "island", "jacket", "kettle", "ladder", "lemon",  "marble", "meadow", "mirror", "orange", "pencil",
    "pepper", "pillow", "planet", "pocket", "rabbit", "ribbon", "river",  "rocket", "saddle", "silver", "spider", "summer",
    "sunset", "tiger",  "tomato", "tunnel", "turtle", "valley", "violin", "wagon",  "walnut", "window", "winter", "yellow"};

constexpr const char* kPassage =
    "The morning light came slowly over the hills, and the town woke up one window at a time. A baker opened her "
    "shop, a dog trotted down the empty street, and somewhere a radio played a song that nobody could quite name. "
    "It was going to be a good day, and everyone seemed to know it.";

constexpr std::array<const char*, 12> kMonths{"January", "February", "March",     "April",   "May",      "June",
                                              "July",    "August",   "September", "October", "November", "December"};

int distance(const std::string& a, const std::string& b)
{
    std::vector<int> row(b.size() + 1);
    for (std::size_t j = 0; j <= b.size(); ++j)
        row[j] = int(j);
    for (std::size_t i = 1; i <= a.size(); ++i) {
        int diagonal = row[0];
        row[0] = int(i);
        for (std::size_t j = 1; j <= b.size(); ++j) {
            const int above = row[j];
            row[j] = std::min({row[j] + 1, row[j - 1] + 1, diagonal + (a[i - 1] == b[j - 1] ? 0 : 1)});
            diagonal = above;
        }
    }
    return row[b.size()];
}

// The same word, allowing a letter's difference (one in five for long words; short words exactly).
bool sameWord(const std::string& wanted, const std::string& heard)
{
    if (wanted == heard)
        return true;
    return wanted.size() >= 4 && distance(wanted, heard) <= std::max(1, int(wanted.size()) / 5);
}

bool heardWord(const std::string& wanted, const std::vector<std::string>& heard)
{
    return std::ranges::any_of(heard, [&](const std::string& h) { return sameWord(wanted, h); });
}

// A name heard however the recogniser split it ("Ada Okafor" as "Adoroka for"): its letters
// somewhere in the heard letters, a letter's difference allowed in four.
bool heardName(const std::string& name, const std::vector<std::string>& heard)
{
    std::string letters, all;
    for (const std::string& w : consentWords(name))
        letters += w;
    for (const std::string& w : heard)
        all += w;
    if (letters.size() < 6)
        return false; // (a short name could turn up in anything: its words are looked for instead)
    const int allowed = std::max(1, int(letters.size()) / 4);
    const std::size_t n = letters.size();
    for (std::size_t start = 0; start < all.size(); ++start)
        for (std::size_t length = n > std::size_t(allowed) ? n - std::size_t(allowed) : 1; length <= n + std::size_t(allowed); ++length)
            if (start + length <= all.size() && distance(letters, all.substr(start, length)) <= allowed)
                return true;
    return false;
}

} // namespace

std::vector<std::string> consentWords(const std::string& text)
{
    std::vector<std::string> words;
    std::string word;
    const auto flush = [&] {
        if (!word.empty())
            words.push_back(word);
        word.clear();
    };
    for (unsigned char c : text) {
        if (std::isalnum(c) || c >= 0x80)
            word += char(std::tolower(c));
        else if (c != '\'')
            flush();
    }
    flush();
    return words;
}

std::string spokenDate(int year, int month, int day)
{
    static const std::array<const char*, 31> ordinals{
        "first",          "second",        "third",          "fourth",        "fifth",         "sixth",
        "seventh",        "eighth",        "ninth",          "tenth",         "eleventh",      "twelfth",
        "thirteenth",     "fourteenth",    "fifteenth",      "sixteenth",     "seventeenth",   "eighteenth",
        "nineteenth",     "twentieth",     "twenty-first",   "twenty-second", "twenty-third",  "twenty-fourth",
        "twenty-fifth",   "twenty-sixth",  "twenty-seventh", "twenty-eighth", "twenty-ninth",  "thirtieth",
        "thirty-first"};
    return std::string("the ") + ordinals[std::size_t(std::clamp(day, 1, 31) - 1)] + " of "
         + kMonths[std::size_t(std::clamp(month, 1, 12) - 1)] + ", " + std::to_string(year);
}

ConsentStatement consentStatement(const std::string& speaker, const std::string& date, std::mt19937& random)
{
    ConsentStatement s;
    s.speaker = speaker;
    s.date = date;
    std::vector<std::string> pool(kCodeWords.begin(), kCodeWords.end());
    std::ranges::shuffle(pool, random);
    s.codeWords.assign(pool.begin(), pool.begin() + 4);
    s.text = "My name is " + speaker + ". I agree that this recording of my voice may be sent to ElevenLabs to make a "
             "copy of my voice, for speech in my own videos made with TechieVideoEditor. I am at least eighteen years "
             "old. Today is " + date + ". My four words are " + s.codeWords[0] + ", " + s.codeWords[1] + ", "
           + s.codeWords[2] + " and " + s.codeWords[3] + ". " + kPassage;
    return s;
}

ConsentCheck checkConsent(const ConsentStatement& statement, const std::string& heard, Ticks duration)
{
    ConsentCheck check;
    const std::vector<std::string> said = consentWords(heard);
    const std::vector<std::string> wanted = consentWords(statement.text);
    for (const std::string& word : statement.codeWords)
        if (!heardWord(word, said))
            check.missing.push_back(word);
    if (!heardName(statement.speaker, said))
        for (const std::string& part : consentWords(statement.speaker))
            if (!heardWord(part, said))
                check.missing.push_back(part);
    // The statement's words heard in order (longest common subsequence).
    std::vector<int> row(said.size() + 1, 0);
    for (const std::string& w : wanted) {
        int diagonal = 0;
        for (std::size_t j = 1; j <= said.size(); ++j) {
            const int above = row[j];
            row[j] = sameWord(w, said[j - 1]) ? diagonal + 1 : std::max(row[j], row[j - 1]);
            diagonal = above;
        }
    }
    check.match = wanted.empty() ? 0.0 : double(row[said.size()]) / double(wanted.size());

    if (duration < 15 * kTicksPerSecond)
        check.why = TVE_TR("core", "The recording is too short: read the whole statement, at an easy pace.");
    else if (!check.missing.empty()) {
        std::string list;
        for (std::size_t i = 0; i < check.missing.size(); ++i)
            list += (i == 0 ? "" : i + 1 == check.missing.size() ? " and " : ", ") + ("“" + check.missing[i] + "”");
        check.why = "These weren't heard in the recording: " + list + ". Record the statement again, clearly.";
    } else if (check.match < 0.8)
        check.why = TVE_TR("core", "The recording doesn't say the statement as it is written. Record it again, reading every word.");
    check.passed = check.why.empty();
    return check;
}

} // namespace tve
