#include "analysis/Highlights.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace tve {

namespace {

constexpr double kCell = 0.5; // seconds

Ticks ticksOf(double seconds)
{
    return Ticks(std::llround(seconds * double(kTicksPerSecond)));
}

double secondsOf(Ticks t)
{
    return double(t) / double(kTicksPerSecond);
}

// The value below which `fraction` of the values lie.
double percentile(std::vector<double> values, double fraction)
{
    if (values.empty())
        return 0;
    const auto at = values.begin() + std::ptrdiff_t(std::clamp(fraction, 0.0, 1.0) * double(values.size() - 1));
    std::nth_element(values.begin(), at, values.end());
    return *at;
}

// Values scaled so the file's quiet end is 0 and its lively end 1.
std::vector<double> normalized(const std::vector<double>& values, bool ignoreSilence = false)
{
    std::vector<double> sample;
    for (double v : values)
        if (!ignoreSilence || v > -100)
            sample.push_back(v);
    const double low = percentile(sample, 0.1);
    const double high = percentile(sample, 0.9);
    std::vector<double> out(values.size(), 0.0);
    if (high - low < 1e-6)
        return out;
    for (std::size_t i = 0; i < values.size(); ++i)
        out[i] = std::clamp((values[i] - low) / (high - low), 0.0, 1.0);
    return out;
}

bool endsSentence(const std::string& word)
{
    return !word.empty() && (word.back() == '.' || word.back() == '!' || word.back() == '?');
}

} // namespace

std::vector<Moment> findMoments(const AudioFeatures* audio, const VideoFeatures* video, const std::vector<TimedWord>& words,
                                Ticks duration, const HighlightOptions& options)
{
    const double total = secondsOf(duration);
    const std::size_t cells = std::size_t(std::ceil(std::max(total, kCell) / kCell));
    if (duration <= 0)
        return {};

    // What each cell is like.
    std::vector<double> loud(cells, -120.0), onset(cells, 0.0), motion(cells, 0.0), speech(cells, 0.0);
    std::vector<bool> silent(cells, false);
    if (audio && !audio->loudness.empty()) {
        const std::size_t perCell = std::size_t(kCell / AudioFeatures::kHop);
        for (std::size_t c = 0; c < cells; ++c) {
            double sum = 0, flux = 0;
            std::size_t n = 0;
            for (std::size_t i = c * perCell; i < std::min(audio->loudness.size(), (c + 1) * perCell); ++i, ++n) {
                sum += std::pow(10.0, audio->loudness[i] / 10.0); // power, then back to dB
                flux += i < audio->onset.size() ? audio->onset[i] : 0.0;
            }
            if (n > 0) {
                loud[c] = 10.0 * std::log10(std::max(sum / double(n), 1e-12));
                onset[c] = flux / double(n);
            }
            silent[c] = loud[c] < -45.0;
        }
    }
    std::set<std::size_t> cutCells;
    if (video && !video->times.empty()) {
        std::vector<int> counts(cells, 0);
        for (std::size_t i = 0; i < video->times.size(); ++i) {
            const std::size_t c = std::min(cells - 1, std::size_t(std::max(0.0, secondsOf(video->times[i])) / kCell));
            motion[c] += std::min<double>(video->change[i], 0.35); // a cut counts no more than lively movement
            ++counts[c];
        }
        for (std::size_t c = 0; c < cells; ++c)
            if (counts[c] > 0)
                motion[c] /= counts[c];
        for (Ticks cut : findSceneCuts(*video, {}))
            cutCells.insert(std::size_t(std::max(0.0, secondsOf(cut)) / kCell));
    }
    std::set<std::size_t> sentenceEnds;
    for (const TimedWord& word : words) {
        const double from = secondsOf(word.start), to = secondsOf(word.end);
        for (std::size_t c = std::size_t(std::max(0.0, from) / kCell); c < cells && double(c) * kCell < to; ++c) {
            const double overlap = std::min(to, double(c + 1) * kCell) - std::max(from, double(c) * kCell);
            speech[c] += std::max(0.0, overlap) / kCell;
        }
        if (endsSentence(word.text))
            sentenceEnds.insert(std::min(cells, std::size_t(std::ceil(std::max(0.0, to) / kCell))));
    }

    // Each cell's score, the parts weighed by what there is to go on.
    const bool hasAudio = audio && !audio->loudness.empty();
    const bool hasVideo = video && !video->times.empty();
    const bool hasWords = !words.empty();
    const std::vector<double> loudN = hasAudio ? normalized(loud, true) : std::vector<double>(cells, 0.0);
    const std::vector<double> onsetN = hasAudio ? normalized(onset) : std::vector<double>(cells, 0.0);
    const std::vector<double> motionN = hasVideo ? normalized(motion) : std::vector<double>(cells, 0.0);
    double wLoud = hasAudio ? 0.35 : 0, wOnset = hasAudio ? 0.15 : 0, wMotion = hasVideo ? 0.25 : 0, wSpeech = hasWords ? 0.25 : 0;
    const double weights = std::max(1e-6, wLoud + wOnset + wMotion + wSpeech);
    wLoud /= weights, wOnset /= weights, wMotion /= weights, wSpeech /= weights;
    std::vector<double> score(cells);
    for (std::size_t c = 0; c < cells; ++c) {
        score[c] = wLoud * loudN[c] + wOnset * onsetN[c] + wMotion * motionN[c] + wSpeech * std::min(1.0, speech[c]);
        if (silent[c] && speech[c] < 0.1)
            score[c] *= 0.5; // a pause is rarely the point
    }

    // Natural places to cut, between cells.
    std::vector<bool> boundary(cells + 1, false);
    boundary[0] = boundary[cells] = true;
    for (std::size_t c : cutCells)
        boundary[std::min(c, cells)] = true;
    for (std::size_t c : sentenceEnds)
        boundary[c] = true;
    for (std::size_t c = 1; c + 1 < cells; ++c)
        if (silent[c] && silent[c + 1] && !silent[c - 1])
            boundary[c + 1] = true; // after half a second of quiet

    // Runs between them, joined up to the minimum and split down to the maximum.
    const std::size_t minCells = std::max<std::size_t>(1, std::size_t(std::lround(options.minLength / kCell)));
    const std::size_t maxCells = std::max(minCells, std::size_t(std::lround(options.maxLength / kCell)));
    std::vector<std::pair<std::size_t, std::size_t>> runs;
    for (std::size_t from = 0; from < cells;) {
        std::size_t to = from + 1;
        while (to < cells && (!boundary[to] || to - from < minCells))
            ++to;
        while (to - from > maxCells) {
            // Too long: cut at the quietest cell edge within the allowed span (prefer a boundary).
            std::size_t best = from + maxCells;
            double lowest = 2.0;
            for (std::size_t c = from + minCells; c <= from + maxCells && c < to; ++c) {
                const double s = (boundary[c] ? -1.0 : 0.0) + score[c];
                if (s < lowest) {
                    lowest = s;
                    best = c;
                }
            }
            runs.push_back({from, best});
            from = best;
        }
        runs.push_back({from, to});
        from = to;
    }
    // A run shorter than the minimum at the end joins the one before it, if that stays short enough.
    if (runs.size() > 1 && runs.back().second - runs.back().first < minCells
        && runs.back().second - runs[runs.size() - 2].first <= maxCells) {
        runs[runs.size() - 2].second = runs.back().second;
        runs.pop_back();
    }

    std::vector<Moment> moments;
    for (const auto& [from, to] : runs) {
        if (double(to - from) * kCell + 1e-9 < options.minLength && runs.size() > 1)
            continue;
        double sum = 0, sumLoud = 0, sumMotion = 0, sumSpeech = 0;
        for (std::size_t c = from; c < to; ++c) {
            sum += score[c];
            sumLoud += loudN[c] + onsetN[c] * 0.5;
            sumMotion += motionN[c];
            sumSpeech += std::min(1.0, speech[c]);
        }
        const double n = double(to - from);
        Moment moment;
        moment.start = ticksOf(double(from) * kCell);
        moment.end = std::min(duration, ticksOf(double(to) * kCell));
        // Cut where the scene or sentence ends: a little better than cut mid-thought.
        moment.score = std::clamp(sum / n + (boundary[from] && boundary[to] ? 0.05 : 0.0), 0.0, 1.0);
        const double l = wLoud * sumLoud / n, m = wMotion * sumMotion / n, s = wSpeech * sumSpeech / n;
        moment.why = moment.score < 0.2 ? "calm" : s >= l && s >= m ? "talk" : m >= l ? "lively" : "loud";
        for (const TimedWord& word : words)
            if (word.start >= moment.start && word.start < moment.end)
                moment.text += (moment.text.empty() ? "" : " ") + word.text;
        moments.push_back(std::move(moment));
    }
    std::ranges::stable_sort(moments, [](const Moment& a, const Moment& b) { return a.score > b.score; });
    return moments;
}

std::vector<Moment> pickMoments(const std::vector<Moment>& moments, Ticks total)
{
    std::vector<Moment> picked;
    Ticks sum = 0;
    for (const Moment& moment : moments) {
        if (sum >= total)
            break;
        picked.push_back(moment);
        sum += moment.length();
    }
    std::ranges::sort(picked, {}, &Moment::start);
    return picked;
}

} // namespace tve
