#pragma once

#include "model/Time.h"

#include <random>
#include <string>
#include <vector>

namespace tve {

// Consent to clone a voice (22a): the statement the speaker reads aloud, recorded in the app,
// and the check that the recording says it. The statement names the speaker, says what the
// copy is for and who makes it, has today's date and four code words chosen afresh (so an
// older recording can't stand in for it), then a short passage that gives the copy enough
// of the voice.
struct ConsentStatement {
    std::string speaker;
    std::string date;                   // as read, e.g. "the tenth of November, 2026"
    std::vector<std::string> codeWords; // four
    std::string text;                   // the whole statement
};

ConsentStatement consentStatement(const std::string& speaker, const std::string& date, std::mt19937& random);
// The date as it is read aloud: "the tenth of November, 2026".
std::string spokenDate(int year, int month, int day);

struct ConsentCheck {
    bool passed = false;
    double match = 0.0;               // the statement's words heard in order, 0..1
    std::vector<std::string> missing; // the code words and names not heard
    std::string why;                  // when it didn't pass, in words for the person
};

// Whether `heard` (the recording's transcript) says the statement: every code word, the
// speaker's name, and at least 80 % of the statement's words in order, in a recording of at
// least 15 seconds. Small differences in spelling (a letter in five) are let through.
ConsentCheck checkConsent(const ConsentStatement& statement, const std::string& heard, Ticks duration);

// Lower-case words, without punctuation (apostrophes dropped: "it's" is "its").
std::vector<std::string> consentWords(const std::string& text);

} // namespace tve
