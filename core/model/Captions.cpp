#include "model/Captions.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <regex>
#include <sstream>

namespace tve {

namespace {

bool endsSentence(const std::string& word)
{
    return !word.empty() && (word.back() == '.' || word.back() == '?' || word.back() == '!');
}

// Breaks `words` into at most `lines` lines of about `length` characters, balanced.
std::string breakLines(const std::vector<std::string>& words, std::size_t length)
{
    std::string all;
    for (const std::string& w : words)
        all += (all.empty() ? "" : " ") + w;
    if (all.size() <= length || words.size() < 2)
        return all;
    // Two lines: the break between words nearest the middle.
    std::size_t best = 1;
    std::size_t bestDistance = all.size();
    std::size_t at = 0;
    for (std::size_t i = 0; i + 1 < words.size(); ++i) {
        at += words[i].size() + (i ? 1 : 0);
        const std::size_t distance = at > all.size() / 2 ? at - all.size() / 2 : all.size() / 2 - at;
        if (distance < bestDistance) {
            bestDistance = distance;
            best = i + 1;
        }
    }
    std::string text;
    for (std::size_t i = 0; i < words.size(); ++i)
        text += (i == 0 ? "" : i == best ? "\n" : " ") + words[i];
    return text;
}

std::string timestamp(Ticks t, char separator)
{
    const long long ms = (std::max<Ticks>(t, 0) * 1000 + kTicksPerSecond / 2) / kTicksPerSecond;
    char text[32];
    std::snprintf(text, sizeof text, "%02lld:%02lld:%02lld%c%03lld", ms / 3600000, ms / 60000 % 60, ms / 1000 % 60, separator,
                  ms % 1000);
    return text;
}

// "01:02:03,456", "01:02:03.456" or "02:03.456".
std::optional<Ticks> parseTime(const std::string& text)
{
    int h = 0, m = 0, s = 0, ms = 0;
    char sep = 0;
    if (std::sscanf(text.c_str(), "%d:%d:%d%c%d", &h, &m, &s, &sep, &ms) == 5 && (sep == ',' || sep == '.'))
        return Ticks(((h * 60LL + m) * 60 + s) * 1000 + ms) * kTicksPerSecond / 1000;
    if (std::sscanf(text.c_str(), "%d:%d%c%d", &m, &s, &sep, &ms) == 4 && (sep == ',' || sep == '.'))
        return Ticks((m * 60LL + s) * 1000 + ms) * kTicksPerSecond / 1000;
    return std::nullopt;
}

std::string trim(const std::string& text)
{
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos)
        return {};
    return text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);
}

} // namespace

std::vector<CaptionBlock> layoutCaptions(const std::vector<TimedWord>& words, const CaptionLayout& layout)
{
    std::vector<CaptionBlock> captions;
    const Ticks maxDuration = Ticks(layout.maxDuration * kTicksPerSecond);
    const Ticks pause = Ticks(layout.pause * kTicksPerSecond);
    const std::size_t maxChars = layout.lineLength * layout.lines;
    std::vector<TimedWord> current;
    std::size_t chars = 0;
    const auto flush = [&] {
        if (current.empty())
            return;
        std::vector<std::string> texts;
        for (const TimedWord& w : current)
            texts.push_back(w.text);
        captions.push_back({current.front().start, current.back().end, breakLines(texts, layout.lineLength), current});
        current.clear();
        chars = 0;
    };
    for (const TimedWord& word : words) {
        if (word.text.empty())
            continue;
        if (!current.empty()) {
            const bool paused = word.start - current.back().end >= pause;
            const bool full = chars + 1 + word.text.size() > maxChars;
            const bool tooLong = word.end - current.front().start > maxDuration;
            const bool sentence = endsSentence(current.back().text) && chars >= maxChars / 2;
            if (paused || full || tooLong || sentence)
                flush();
        }
        chars += (current.empty() ? 0 : 1) + word.text.size();
        current.push_back(word);
    }
    flush();
    // Short captions stay up longer, but never into the next one.
    const Ticks minimum = Ticks(layout.minDuration * kTicksPerSecond);
    for (std::size_t i = 0; i < captions.size(); ++i) {
        const Ticks limit = i + 1 < captions.size() ? captions[i + 1].start : captions[i].start + minimum;
        if (captions[i].end - captions[i].start < minimum)
            captions[i].end = std::min(captions[i].start + minimum, std::max(limit, captions[i].end));
    }
    return captions;
}

bool isFillerWord(const std::string& word)
{
    std::string w;
    for (unsigned char c : word)
        if (std::isalpha(c))
            w += char(std::tolower(c));
    static const char* const fillers[] = {"um", "umm", "uh", "uhh", "uhm", "erm", "er", "ah", "ahh", "hmm", "hm", "mm", "mhm", "eh"};
    return std::ranges::any_of(fillers, [&](const char* f) { return w == f; });
}

ClipText captionText()
{
    ClipText text;
    text.size = 0.055;
    text.bold = true;
    text.lineSpacing = 1.1;
    text.shape.kind = TitleShapeKind::Rounded;
    text.shape.color = {0.0, 0.0, 0.0, 0.62};
    text.shape.padding = 0.35;
    return text;
}

ClipVisual captionVisual()
{
    ClipVisual visual;
    visual.transform.y = 0.36; // the lower part of the picture
    return visual;
}

std::string writeSrt(const std::vector<CaptionBlock>& captions)
{
    std::ostringstream out;
    int n = 1;
    for (const CaptionBlock& c : captions)
        out << n++ << "\n" << timestamp(c.start, ',') << " --> " << timestamp(c.end, ',') << "\n" << c.text << "\n\n";
    return out.str();
}

std::string writeVtt(const std::vector<CaptionBlock>& captions)
{
    std::ostringstream out;
    out << "WEBVTT\n\n";
    for (const CaptionBlock& c : captions)
        out << timestamp(c.start, '.') << " --> " << timestamp(c.end, '.') << "\n" << c.text << "\n\n";
    return out.str();
}

std::optional<std::vector<CaptionBlock>> readSubtitles(const std::string& input, std::string* error)
{
    std::string text = input;
    if (text.starts_with("\xEF\xBB\xBF"))
        text.erase(0, 3);
    std::erase(text, '\r');
    std::istringstream in(text);
    std::vector<CaptionBlock> captions;
    const std::regex cueTime(R"(^\s*([0-9:.,]+)\s*-->\s*([0-9:.,]+))");
    const std::regex tags(R"(<[^>]*>|\{\\[^}]*\})");
    std::string line;
    std::optional<CaptionBlock> cue;
    const auto finish = [&] {
        if (cue && !cue->text.empty() && cue->end > cue->start)
            captions.push_back(*cue);
        cue.reset();
    };
    while (std::getline(in, line)) {
        std::smatch match;
        if (std::regex_search(line, match, cueTime)) {
            finish();
            const auto start = parseTime(match[1].str());
            const auto end = parseTime(match[2].str());
            if (start && end)
                cue = CaptionBlock{*start, *end, {}, {}};
            continue;
        }
        if (trim(line).empty()) {
            finish();
            continue;
        }
        if (cue) {
            const std::string clean = trim(std::regex_replace(line, tags, ""));
            if (!clean.empty())
                cue->text += (cue->text.empty() ? "" : "\n") + clean;
        }
    }
    finish();
    if (captions.empty()) {
        if (error)
            *error = "no captions found in the file";
        return std::nullopt;
    }
    std::ranges::sort(captions, {}, &CaptionBlock::start);
    return captions;
}

} // namespace tve
