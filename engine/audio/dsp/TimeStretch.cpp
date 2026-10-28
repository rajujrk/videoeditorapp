#include "audio/dsp/TimeStretch.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace tve::dsp {

namespace {

// A periodic Hann window: grains half a window apart add up to exactly 1.
const std::vector<float>& window()
{
    static const std::vector<float> w = [] {
        std::vector<float> v(TimeStretcher::kWindow);
        for (int i = 0; i < TimeStretcher::kWindow; ++i)
            v[std::size_t(i)] = float(0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * i / TimeStretcher::kWindow));
        return v;
    }();
    return w;
}

} // namespace

void TimeStretcher::reset(std::int64_t inputStart, double position, double rate)
{
    m_input.clear();
    m_inputStart = inputStart;
    // The first grain is only faded in, so it is dropped; the second lines up with
    // `position` near the middle of its first half.
    m_position = position - kHop * rate - kHop * 0.5 * (1.0 - rate);
    m_previous = -1;
    std::ranges::fill(m_overlap, 0.0f);
    m_ready.clear();
    m_readyAt = 0;
    m_discard = kHop;
    m_finished = false;
}

void TimeStretcher::push(const float* stereo, int frames)
{
    m_input.insert(m_input.end(), stereo, stereo + std::size_t(frames) * 2);
}

float TimeStretcher::at(std::int64_t index, int channel) const
{
    const std::int64_t i = index - m_inputStart;
    if (i < 0 || i >= std::int64_t(m_input.size() / 2))
        return 0.0f;
    return m_input[std::size_t(i) * 2 + std::size_t(channel)];
}

bool TimeStretcher::canPull(int frames, double rate) const
{
    if (m_finished)
        return true;
    const int ready = int(m_ready.size() / 2 - m_readyAt) - m_discard;
    const int grains = std::max(0, (frames - ready + kHop - 1) / kHop);
    if (grains == 0)
        return true;
    // The last grain reaches at most kSearch past its nominal start, plus a window; the
    // stretch it is matched against (a hop after the grain before) lies within that too.
    const double lastNominal = m_position + double(grains - 1) * kHop * rate;
    return inputEnd() >= std::int64_t(std::ceil(lastNominal)) + kSearch + kWindow;
}

std::int64_t TimeStretcher::bestStart(std::int64_t nominal) const
{
    if (m_previous < 0)
        return nominal;
    // The grain that would continue the previous one seamlessly starts a hop after it; find
    // the start near `nominal` whose overlap looks most like that (normalized correlation of
    // the mono signal, every second sample).
    const std::int64_t natural = m_previous + kHop;
    constexpr int kOverlap = kWindow - kHop;
    double best = -2.0;
    std::int64_t bestAt = nominal;
    for (int offset = -kSearch; offset <= kSearch; offset += 2) {
        const std::int64_t candidate = nominal + offset;
        double dot = 0.0, energy = 0.0, target = 0.0;
        for (int i = 0; i < kOverlap; i += 2) {
            const float a = at(candidate + i, 0) + at(candidate + i, 1);
            const float b = at(natural + i, 0) + at(natural + i, 1);
            dot += double(a) * b;
            energy += double(a) * a;
            target += double(b) * b;
        }
        const double score = energy > 1e-12 && target > 1e-12 ? dot / std::sqrt(energy * target) : 0.0;
        if (score > best) {
            best = score;
            bestAt = candidate;
        }
    }
    return bestAt;
}

void TimeStretcher::addGrain(double rate)
{
    const std::int64_t start = bestStart(std::int64_t(std::llround(m_position)));
    const std::vector<float>& w = window();
    for (int i = 0; i < kWindow; ++i) {
        for (int c = 0; c < 2; ++c)
            m_overlap[std::size_t(i) * 2 + std::size_t(c)] += at(start + i, c) * w[std::size_t(i)];
    }
    // The first hop is complete: hand it out and slide the rest along.
    m_ready.insert(m_ready.end(), m_overlap.begin(), m_overlap.begin() + kHop * 2);
    std::copy(m_overlap.begin() + kHop * 2, m_overlap.end(), m_overlap.begin());
    std::fill(m_overlap.end() - kHop * 2, m_overlap.end(), 0.0f);
    m_previous = start;
    m_position += kHop * rate;

    // Forget input no grain can reach any more.
    const std::int64_t keepFrom = std::min(std::int64_t(m_position) - kSearch, m_previous + kHop) - 16;
    const std::int64_t drop = keepFrom - m_inputStart;
    if (drop > 8192) {
        m_input.erase(m_input.begin(), m_input.begin() + std::ptrdiff_t(drop) * 2);
        m_inputStart += drop;
    }
}

void TimeStretcher::pull(float* stereo, int frames, double rate)
{
    int written = 0;
    while (written < frames) {
        if (m_readyAt >= m_ready.size() / 2) {
            m_ready.clear();
            m_readyAt = 0;
            addGrain(rate);
        }
        std::size_t available = m_ready.size() / 2 - m_readyAt;
        if (m_discard > 0) {
            const std::size_t dropped = std::min<std::size_t>(available, std::size_t(m_discard));
            m_readyAt += dropped;
            m_discard -= int(dropped);
            continue;
        }
        const std::size_t n = std::min<std::size_t>(available, std::size_t(frames - written));
        std::copy_n(m_ready.begin() + std::ptrdiff_t(m_readyAt * 2), n * 2, stereo + std::size_t(written) * 2);
        m_readyAt += n;
        written += int(n);
    }
}

} // namespace tve::dsp
