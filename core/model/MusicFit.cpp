#include "model/MusicFit.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

namespace tve {

namespace {

constexpr Ticks kBlock = 2 * kTicksPerSecond; // without a beat
constexpr Ticks kMostFade = 2 * kTicksPerSecond;
constexpr int kMostRepeats = 64;

// Mean loudness (dB) over media time [from, to); nullopt without loudness there.
std::optional<double> meanDb(const MusicFitInput& input, Ticks from, Ticks to)
{
    if (input.loudness.empty() || to <= from)
        return std::nullopt;
    const auto index = [&](Ticks t) {
        return std::clamp<long long>(std::llround(toSeconds(t) / input.hop), 0, (long long)input.loudness.size() - 1);
    };
    const long long a = index(from), b = std::max(index(to), a + 1);
    double sum = 0;
    for (long long i = a; i < b && i < (long long)input.loudness.size(); ++i)
        sum += std::max(-60.0f, input.loudness[std::size_t(i)]);
    return sum / double(b - a);
}

// How unlike the music is either side of a join from `leave` to `arrive` (0 alike .. 1):
// what comes before each, and what comes after each, compared.
double joinCost(const MusicFitInput& input, Ticks leave, Ticks arrive)
{
    constexpr Ticks window = kTicksPerSecond * 2 / 5;
    double total = 0;
    int n = 0;
    for (const auto& [a, b] : {std::pair{leave - window, arrive - window}, std::pair{leave, arrive}}) {
        const auto x = meanDb(input, a, a + window);
        const auto y = meanDb(input, b, b + window);
        if (x && y) {
            total += std::abs(*x - *y);
            ++n;
        }
    }
    return n == 0 ? 0.0 : std::min(1.0, total / n / 20.0);
}

// Where the music may be joined: its start, every bar's first beat inside it (or two-second
// blocks without a beat), its end.
std::vector<Ticks> boundaries(const MusicFitInput& input)
{
    std::vector<Ticks> b{input.in};
    std::vector<Ticks> inside;
    for (Ticks t : input.beats)
        if (t > input.in && t < input.out)
            inside.push_back(t);
    const int perBar = std::max(1, input.beatsPerBar);
    if (inside.size() >= std::size_t(perBar) * 2) {
        for (std::size_t i = 0; i < inside.size(); i += std::size_t(perBar))
            if (inside[i] - b.back() > kTicksPerSecond / 4)
                b.push_back(inside[i]);
    } else {
        for (Ticks t = input.in + kBlock; t < input.out - kBlock / 4; t += kBlock)
            b.push_back(t);
    }
    if (input.out - b.back() < kTicksPerSecond / 4 && b.size() > 1)
        b.back() = input.out;
    else
        b.push_back(input.out);
    return b;
}

// Ending short of the length (more than the speed can make up) is worse than running over:
// over, the end is trimmed and faded to land on it exactly.
double shortfall(Ticks made, Ticks target)
{
    return double(made) < double(target) * (1.0 - kMostFitStretch) ? 0.5 : 0.0;
}

Ticks total(const std::vector<std::pair<Ticks, Ticks>>& pieces)
{
    Ticks sum = 0;
    for (const auto& [a, b] : pieces)
        sum += b - a;
    return sum;
}

// The last difference: a touch of speed, else the end trimmed and faded (too long), else
// as close as the speed allows (too short).
void finish(MusicFit& fit, Ticks target)
{
    const Ticks made = total(fit.pieces);
    const double ratio = double(made) / double(target);
    if (std::abs(ratio - 1.0) <= kMostFitStretch) {
        fit.speed = ratio;
        fit.length = target;
        return;
    }
    if (made > target) {
        Ticks over = made - target;
        while (over > 0 && !fit.pieces.empty()) {
            auto& last = fit.pieces.back();
            const Ticks cut = std::min(over, last.second - last.first);
            last.second -= cut;
            over -= cut;
            if (last.second <= last.first)
                fit.pieces.pop_back();
        }
        fit.fadeOut = std::min(kMostFade, target / 4);
        fit.length = target;
        return;
    }
    fit.speed = 1.0 - kMostFitStretch;
    fit.length = Ticks(std::llround(double(made) / fit.speed));
}

} // namespace

MusicFit planMusicFit(const MusicFitInput& input)
{
    MusicFit fit;
    const Ticks S = input.out - input.in;
    const Ticks T = input.target;
    if (S <= 0 || T <= 0)
        return fit;
    // Close enough already: only the speed.
    if (std::abs(double(S) / double(T) - 1.0) <= kMostFitStretch) {
        fit.pieces = {{input.in, input.out}};
        finish(fit, T);
        return fit;
    }
    const std::vector<Ticks> b = boundaries(input);
    const std::size_t n = b.size() - 1; // bars (or blocks)
    const double mid = double(input.in) + double(S) / 2;

    // A typical bar (or block).
    const Ticks bar = n > 2 ? (b[n - 1] - b[1]) / Ticks(n - 2) : S;

    if (T < S) {
        // Bars i..j-1 taken out, the first and last kept.
        double best = std::numeric_limits<double>::max();
        std::size_t bi = 0, bj = 0;
        Ticks bestMade = 0;
        for (std::size_t i = 1; i + 1 < n; ++i)
            for (std::size_t j = i + 1; j < n; ++j) {
                const Ticks made = S - (b[j] - b[i]);
                if (made <= 0)
                    continue;
                const double centre = std::abs((double(b[i]) + double(b[j])) / 2 - mid) / double(S);
                const double cost = std::abs(double(made - T)) / double(T) + 0.5 * joinCost(input, b[i], b[j]) + 0.05 * centre
                                  + shortfall(made, T);
                if (cost < best) {
                    best = cost;
                    bi = i;
                    bj = j;
                    bestMade = made;
                }
            }
        // Under four bars, or no join near enough the length: too short to cut by bars.
        if (bj == 0 || T < 4 * bar || std::abs(double(bestMade - T)) / double(T) > 0.15) {
            // Too short to cut by bars: the start, trimmed and faded.
            fit.pieces = {{input.in, input.in + T}};
            fit.fadeOut = std::min(kMostFade, T / 4);
            fit.length = T;
            return fit;
        }
        fit.pieces = {{input.in, b[bi]}, {b[bj], input.out}};
        fit.barsChanged = -int(bj - bi);
        finish(fit, T);
        return fit;
    }

    // Bars i..j-1 played k more times.
    double best = std::numeric_limits<double>::max();
    std::size_t bi = 0, bj = 0;
    int bk = 0;
    const std::size_t first = n >= 3 ? 1 : 0;
    const std::size_t last = n >= 3 ? n - 1 : n;
    for (std::size_t i = first; i < last; ++i)
        for (std::size_t j = i + 1; j <= last; ++j) {
            const Ticks length = b[j] - b[i];
            const int k = std::clamp<int>(int(std::llround(double(T - S) / double(length))), 1, kMostRepeats);
            const Ticks made = S + Ticks(k) * length;
            const double cost = std::abs(double(made - T)) / double(T) + 0.5 * joinCost(input, b[j], b[i]) + 0.02 * k
                              + shortfall(made, T);
            if (cost < best) {
                best = cost;
                bi = i;
                bj = j;
                bk = k;
            }
        }
    fit.pieces.push_back({input.in, b[bj]});
    for (int r = 1; r < bk; ++r)
        fit.pieces.push_back({b[bi], b[bj]});
    fit.pieces.push_back({b[bi], input.out});
    std::erase_if(fit.pieces, [](const auto& p) { return p.second <= p.first; });
    fit.barsChanged = bk * int(bj - bi);
    finish(fit, T);
    return fit;
}

} // namespace tve
