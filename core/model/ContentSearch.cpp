#include "model/ContentSearch.h"

#include <algorithm>
#include <cctype>
#include <map>

namespace tve {

namespace {

// The words of a text, lower case: letters and digits (and any non-ASCII character, so
// other scripts are kept whole); an apostrophe joins ("don't" → "dont"); anything else
// separates.
std::vector<std::string> wordsOf(std::string_view text)
{
    std::vector<std::string> words;
    std::string word;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const auto c = static_cast<unsigned char>(text[i]);
        if (c == '\'')
            continue;
        // ’ (U+2019) joins too.
        if (c == 0xE2 && i + 2 < text.size() && static_cast<unsigned char>(text[i + 1]) == 0x80
            && static_cast<unsigned char>(text[i + 2]) == 0x99) {
            i += 2;
            continue;
        }
        if (c >= 0x80 || std::isalnum(c)) {
            word += c < 0x80 ? char(std::tolower(c)) : char(c);
            continue;
        }
        if (!word.empty())
            words.push_back(std::move(word));
        word.clear();
    }
    if (!word.empty())
        words.push_back(std::move(word));
    return words;
}

bool endsWith(std::string_view s, std::string_view end)
{
    return s.size() >= end.size() && s.substr(s.size() - end.size()) == end;
}

// A word without a plural or a common ending (one rule at most).
std::string stem(std::string_view w)
{
    std::string s(w);
    if (s.size() > 4 && endsWith(s, "ies"))
        return s.substr(0, s.size() - 3) + "y";
    if (s.size() > 4 && (endsWith(s, "ches") || endsWith(s, "shes") || endsWith(s, "sses") || endsWith(s, "xes") || endsWith(s, "zes")))
        return s.substr(0, s.size() - 2);
    if (s.size() > 5 && endsWith(s, "ing"))
        return s.substr(0, s.size() - 3);
    if (s.size() > 4 && endsWith(s, "ed"))
        return s.substr(0, s.size() - 2);
    if (s.size() > 3 && endsWith(s, "s") && !endsWith(s, "ss"))
        return s.substr(0, s.size() - 1);
    return s;
}

bool isSmallWord(std::string_view w)
{
    static const char* const small[] = {"a", "an", "the", "of", "and", "or", "in", "on", "at", "to", "with",
                                        "for", "is", "are", "by", "from", "some", "my", "our"};
    return std::ranges::any_of(small, [&](const char* s) { return w == s; });
}

struct Bucket {
    std::vector<double> strength; // per term, the best match here
    unsigned found = 0;
    std::vector<std::string> labels;     // labels matching here
    std::vector<std::string> shownLines; // lines of on-screen text matching here
};

} // namespace

std::string labelText(std::string_view name)
{
    std::string text(name);
    std::ranges::replace(text, '_', ' ');
    return text;
}

std::vector<LabelShare> topLabels(const ContentIndex& index, std::size_t count)
{
    // Frames showing it and their confidence summed: shown most first, then surest.
    std::map<std::string, std::pair<std::size_t, double>> seen;
    for (const ContentFrame& frame : index.frames)
        for (const ContentLabel& label : frame.labels)
            if (label.confidence >= kLabelThreshold) {
                ++seen[label.name].first;
                seen[label.name].second += label.confidence;
            }
    std::vector<std::pair<LabelShare, double>> ranked;
    const double frames = double(std::max<std::size_t>(1, index.frames.size()));
    for (const auto& [name, n] : seen)
        ranked.push_back({{name, double(n.first) / frames}, n.second / double(n.first)});
    std::ranges::stable_sort(ranked, [](const auto& a, const auto& b) {
        return a.first.share != b.first.share ? a.first.share > b.first.share : a.second > b.second;
    });
    std::vector<LabelShare> shares;
    for (auto& [share, confidence] : ranked)
        shares.push_back(std::move(share));
    if (shares.size() > count)
        shares.resize(count);
    return shares;
}

std::vector<std::string> searchTerms(std::string_view query)
{
    std::vector<std::string> all = wordsOf(query);
    std::vector<std::string> terms;
    for (const std::string& w : all)
        if (!isSmallWord(w) && std::ranges::find(terms, w) == terms.end())
            terms.push_back(w);
    if (terms.empty())
        for (const std::string& w : all)
            if (std::ranges::find(terms, w) == terms.end())
                terms.push_back(w);
    return terms;
}

double termMatch(std::string_view term, std::string_view word)
{
    if (term.empty() || word.empty())
        return 0.0;
    if (term == word)
        return 1.0;
    if (stem(term) == stem(word))
        return 0.9;
    if (term.size() >= 4 && word.size() > term.size() && word.substr(0, term.size()) == term)
        return 0.7;
    return 0.0;
}

std::vector<SearchHit> searchMedia(std::string_view query, const std::vector<SearchableMedia>& media, std::size_t limit)
{
    const std::vector<std::string> terms = searchTerms(query);
    std::vector<SearchHit> hits;
    if (terms.empty())
        return hits;
    const std::size_t T = terms.size();
    const Ticks second = ContentIndex::kSpacing;

    for (const SearchableMedia& m : media) {
        const bool hasContent = m.content && !m.content->frames.empty();
        const bool hasWords = m.words && !m.words->empty();
        if (!hasContent && !hasWords)
            continue;
        const std::size_t count = m.still ? 1 : std::size_t(std::max<Ticks>(1, (m.duration + second - 1) / second));
        std::vector<Bucket> buckets(count, Bucket{std::vector<double>(T, 0.0), 0, {}, {}});
        const auto bucketOf = [&](Ticks t) {
            if (m.still)
                return std::size_t(0);
            return std::size_t(std::clamp<Ticks>(t / second, 0, Ticks(count) - 1));
        };
        const auto note = [&](Bucket& b, std::size_t term, double strength, unsigned kind) {
            if (strength <= 0)
                return false;
            b.strength[term] = std::max(b.strength[term], strength);
            b.found |= kind;
            return true;
        };

        if (hasContent) {
            for (const ContentFrame& frame : m.content->frames) {
                Bucket& b = buckets[bucketOf(frame.time)];
                for (const ContentLabel& label : frame.labels) {
                    if (label.confidence < kLabelThreshold)
                        continue;
                    bool matched = false;
                    const std::vector<std::string> parts = wordsOf(labelText(label.name));
                    for (std::size_t t = 0; t < T; ++t) {
                        double best = 0;
                        for (const std::string& part : parts)
                            best = std::max(best, termMatch(terms[t], part));
                        matched |= note(b, t, best * label.confidence, kFoundSeen);
                    }
                    if (matched && std::ranges::find(b.labels, label.name) == b.labels.end())
                        b.labels.push_back(label.name);
                }
                for (const ScreenText& line : frame.text) {
                    bool matched = false;
                    const std::vector<std::string> parts = wordsOf(line.text);
                    for (std::size_t t = 0; t < T; ++t) {
                        double best = 0;
                        for (const std::string& part : parts)
                            best = std::max(best, termMatch(terms[t], part));
                        matched |= note(b, t, best, kFoundShown);
                    }
                    if (matched && std::ranges::find(b.shownLines, line.text) == b.shownLines.end())
                        b.shownLines.push_back(line.text);
                }
            }
        }
        if (hasWords) {
            for (const TimedWord& word : *m.words) {
                Bucket& b = buckets[bucketOf(word.start)];
                const std::vector<std::string> parts = wordsOf(word.text);
                for (std::size_t t = 0; t < T; ++t) {
                    double best = 0;
                    for (const std::string& part : parts)
                        best = std::max(best, termMatch(terms[t], part));
                    note(b, t, best, kFoundSaid);
                }
            }
        }

        // A second matches when it holds a match itself and every term is found within a
        // second of it.
        const auto windowOf = [&](std::size_t i) {
            return std::pair(i == 0 ? 0 : i - 1, std::min(count - 1, i + 1));
        };
        std::vector<bool> matches(count, false);
        for (std::size_t i = 0; i < count; ++i) {
            if (buckets[i].found == 0)
                continue;
            const auto [from, to] = windowOf(i);
            bool all = true;
            for (std::size_t t = 0; t < T && all; ++t) {
                double best = 0;
                for (std::size_t j = from; j <= to; ++j)
                    best = std::max(best, buckets[j].strength[t]);
                all = best > 0;
            }
            matches[i] = all;
        }

        for (std::size_t i = 0; i < count;) {
            if (!matches[i]) {
                ++i;
                continue;
            }
            std::size_t last = i;
            while (last + 1 < count && matches[last + 1])
                ++last;
            const std::size_t from = i == 0 ? 0 : i - 1;
            const std::size_t to = std::min(count - 1, last + 1);

            SearchHit hit;
            hit.mediaId = m.mediaId;
            hit.start = m.still ? 0 : Ticks(i) * second;
            hit.end = m.still ? m.duration : std::min(m.duration, Ticks(last + 1) * second);
            double total = 0;
            for (std::size_t t = 0; t < T; ++t) {
                double best = 0;
                for (std::size_t j = from; j <= to; ++j)
                    best = std::max(best, buckets[j].strength[t]);
                total += best;
            }
            std::vector<std::string> labels;
            std::string shown;
            for (std::size_t j = from; j <= to; ++j) {
                hit.found |= buckets[j].found;
                for (const std::string& l : buckets[j].labels)
                    if (std::ranges::find(labels, l) == labels.end())
                        labels.push_back(l);
                if (shown.empty() && !buckets[j].shownLines.empty())
                    shown = buckets[j].shownLines.front();
            }
            hit.score = total / double(T) + 0.01 * double(std::min<std::size_t>(last - i + 1, 10));

            // What matched, in words: what is said there (around the first word that
            // matches), else the text on screen, else the labels.
            if (hit.found & kFoundSaid) {
                const Ticks windowStart = m.still ? 0 : Ticks(from) * second;
                const Ticks windowEnd = m.still ? m.duration : Ticks(to + 1) * second;
                std::vector<const TimedWord*> said;
                std::size_t first = 0;
                bool firstFound = false;
                for (const TimedWord& w : *m.words) {
                    if (w.start < windowStart || w.start >= windowEnd)
                        continue;
                    if (!firstFound) {
                        for (const std::string& part : wordsOf(w.text))
                            for (const std::string& term : terms)
                                if (termMatch(term, part) > 0)
                                    firstFound = true;
                        if (firstFound)
                            first = said.size();
                    }
                    said.push_back(&w);
                }
                constexpr std::size_t kAround = 6;
                const std::size_t begin = first > kAround ? first - kAround : 0;
                const std::size_t end = std::min(said.size(), first + kAround + 1);
                std::string text;
                for (std::size_t k = begin; k < end; ++k) {
                    std::string w = said[k]->text;
                    while (!w.empty() && w.front() == ' ')
                        w.erase(w.begin());
                    if (!text.empty())
                        text += ' ';
                    text += w;
                }
                if (begin > 0)
                    text = "… " + text;
                if (end < said.size())
                    text += " …";
                hit.snippet = text;
            }
            if (hit.snippet.empty() && !shown.empty())
                hit.snippet = shown;
            if (hit.snippet.empty()) {
                for (const std::string& l : labels) {
                    if (!hit.snippet.empty())
                        hit.snippet += ", ";
                    hit.snippet += labelText(l);
                }
            }
            hits.push_back(std::move(hit));
            i = last + 1;
        }
    }

    std::ranges::stable_sort(hits, [](const SearchHit& a, const SearchHit& b) { return a.score > b.score; });
    if (hits.size() > limit)
        hits.resize(limit);
    return hits;
}

} // namespace tve
