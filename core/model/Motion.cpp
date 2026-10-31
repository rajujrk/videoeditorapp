#include "model/Motion.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numeric>

namespace tve {

namespace {

GrayImage half(const GrayImage& image)
{
    GrayImage out;
    out.width = image.width / 2;
    out.height = image.height / 2;
    out.pixels.resize(std::size_t(out.width) * std::size_t(out.height));
    for (int y = 0; y < out.height; ++y)
        for (int x = 0; x < out.width; ++x)
            out.pixels[std::size_t(y) * std::size_t(out.width) + std::size_t(x)] =
                0.25f * (image.at(2 * x, 2 * y) + image.at(2 * x + 1, 2 * y) + image.at(2 * x, 2 * y + 1)
                         + image.at(2 * x + 1, 2 * y + 1));
    return out;
}

// Mean absolute difference between the block at (x, y) in `a` and at (x + dx, y + dy) in
// `b` (top-left corners, `size` pixels square); infinite where it leaves `b`.
double blockCost(const GrayImage& a, const GrayImage& b, int x, int y, int size, int dx, int dy)
{
    if (x + dx < 0 || y + dy < 0 || x + dx + size > b.width || y + dy + size > b.height)
        return std::numeric_limits<double>::infinity();
    double sum = 0.0;
    for (int j = 0; j < size; ++j) {
        const float* pa = &a.pixels[std::size_t(y + j) * std::size_t(a.width) + std::size_t(x)];
        const float* pb = &b.pixels[std::size_t(y + dy + j) * std::size_t(b.width) + std::size_t(x + dx)];
        for (int i = 0; i < size; ++i)
            sum += std::abs(pa[i] - pb[i]);
    }
    return sum / double(size * size);
}

double blockSpread(const GrayImage& a, int x, int y, int size)
{
    double sum = 0.0, squares = 0.0;
    for (int j = 0; j < size; ++j)
        for (int i = 0; i < size; ++i) {
            const double v = a.at(x + i, y + j);
            sum += v;
            squares += v * v;
        }
    const double n = double(size * size);
    return std::sqrt(std::max(0.0, squares / n - (sum / n) * (sum / n)));
}

struct Match {
    double x, y;   // block centre in `previous` (pixels)
    double dx, dy; // where it went
};

// Every textured block of a grid over `previous`, searched within `radius` of `around`;
// sub-pixel by a parabola through the costs either side of the best.
std::vector<Match> matchBlocks(const GrayImage& previous, const GrayImage& next, int size, int columns, int rows,
                               int radius, std::array<int, 2> around)
{
    std::vector<Match> matches;
    const int margin = radius + std::max(std::abs(around[0]), std::abs(around[1])) + 1;
    const int spanX = previous.width - 2 * margin - size;
    const int spanY = previous.height - 2 * margin - size;
    if (spanX <= 0 || spanY <= 0)
        return matches;
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < columns; ++c) {
            const int x = margin + (columns > 1 ? spanX * c / (columns - 1) : spanX / 2);
            const int y = margin + (rows > 1 ? spanY * r / (rows - 1) : spanY / 2);
            if (blockSpread(previous, x, y, size) < 0.02)
                continue; // flat: nothing to match
            double best = std::numeric_limits<double>::infinity();
            int bx = 0, by = 0;
            for (int dy = around[1] - radius; dy <= around[1] + radius; ++dy)
                for (int dx = around[0] - radius; dx <= around[0] + radius; ++dx) {
                    const double cost = blockCost(previous, next, x, y, size, dx, dy);
                    if (cost < best) {
                        best = cost;
                        bx = dx;
                        by = dy;
                    }
                }
            if (!std::isfinite(best))
                continue;
            const auto refine = [&](double minus, double plus) {
                const double curve = minus - 2.0 * best + plus;
                if (!std::isfinite(curve) || curve <= 1e-12)
                    return 0.0;
                return std::clamp(0.5 * (minus - plus) / curve, -0.5, 0.5);
            };
            const double fx = refine(blockCost(previous, next, x, y, size, bx - 1, by),
                                     blockCost(previous, next, x, y, size, bx + 1, by));
            const double fy = refine(blockCost(previous, next, x, y, size, bx, by - 1),
                                     blockCost(previous, next, x, y, size, bx, by + 1));
            matches.push_back({x + size / 2.0, y + size / 2.0, bx + fx, by + fy});
        }
    }
    return matches;
}

double median(std::vector<double> values)
{
    if (values.empty())
        return 0.0;
    const auto mid = values.begin() + std::ptrdiff_t(values.size() / 2);
    std::nth_element(values.begin(), mid, values.end());
    return *mid;
}

} // namespace

FrameMotion estimateMotion(const GrayImage& previous, const GrayImage& next)
{
    FrameMotion motion;
    if (previous.width != next.width || previous.height != next.height || previous.width < 64 || previous.height < 36)
        return motion;
    // Coarse: the overall shift at half size, from the median of a few blocks.
    const GrayImage a = half(previous);
    const GrayImage b = half(next);
    const auto coarse = matchBlocks(a, b, 12, 5, 3, 10, {0, 0});
    std::vector<double> xs, ys;
    for (const Match& m : coarse) {
        xs.push_back(m.dx);
        ys.push_back(m.dy);
    }
    const std::array<int, 2> around{int(std::lround(2.0 * median(xs))), int(std::lround(2.0 * median(ys)))};
    // Fine: a grid of blocks around it at full size.
    const auto fine = matchBlocks(previous, next, 16, 8, 5, 3, around);
    if (fine.size() < 4)
        return motion;

    // A similarity fitted to the matches (centred coordinates), refitted without the outliers.
    const double cx = previous.width / 2.0, cy = previous.height / 2.0;
    std::vector<bool> inlier(fine.size(), true);
    double a0 = 1.0, b0 = 0.0, tx = 0.0, ty = 0.0;
    for (int pass = 0; pass < 4; ++pass) {
        double n = 0, px = 0, py = 0, qx = 0, qy = 0;
        for (std::size_t i = 0; i < fine.size(); ++i) {
            if (!inlier[i])
                continue;
            n += 1;
            px += fine[i].x - cx;
            py += fine[i].y - cy;
            qx += fine[i].x - cx + fine[i].dx;
            qy += fine[i].y - cy + fine[i].dy;
        }
        if (n < 3)
            break;
        px /= n, py /= n, qx /= n, qy /= n;
        double dot = 0, cross = 0, norm = 0;
        for (std::size_t i = 0; i < fine.size(); ++i) {
            if (!inlier[i])
                continue;
            const double Px = fine[i].x - cx - px, Py = fine[i].y - cy - py;
            const double Qx = fine[i].x - cx + fine[i].dx - qx, Qy = fine[i].y - cy + fine[i].dy - qy;
            dot += Px * Qx + Py * Qy;
            cross += Px * Qy - Py * Qx;
            norm += Px * Px + Py * Py;
        }
        if (norm <= 0)
            break;
        a0 = dot / norm;
        b0 = cross / norm;
        tx = qx - (a0 * px - b0 * py);
        ty = qy - (b0 * px + a0 * py);
        // Residuals: drop what does not follow the camera (more than 2.5 × the median, and a
        // pixel at least).
        std::vector<double> residuals(fine.size());
        for (std::size_t i = 0; i < fine.size(); ++i) {
            const double x = fine[i].x - cx, y = fine[i].y - cy;
            residuals[i] = std::hypot(a0 * x - b0 * y + tx - (x + fine[i].dx), b0 * x + a0 * y + ty - (y + fine[i].dy));
        }
        std::vector<double> kept;
        for (std::size_t i = 0; i < fine.size(); ++i)
            if (inlier[i])
                kept.push_back(residuals[i]);
        const double limit = std::max(1.0, 2.5 * median(kept));
        for (std::size_t i = 0; i < fine.size(); ++i)
            inlier[i] = residuals[i] <= limit;
    }
    const auto agreed = std::ranges::count(inlier, true);
    motion.dx = tx;
    motion.dy = ty;
    motion.angle = std::atan2(b0, a0);
    motion.scale = std::hypot(a0, b0);
    motion.confidence = double(agreed) / double(fine.size());
    return motion;
}

SteadySample Stabilization::at(Ticks mediaTime) const
{
    if (samples.empty())
        return {mediaTime};
    if (mediaTime <= samples.front().time)
        return samples.front();
    if (mediaTime >= samples.back().time)
        return samples.back();
    const auto next = std::ranges::upper_bound(samples, mediaTime, {}, &SteadySample::time);
    const SteadySample& b = *next;
    const SteadySample& a = *(next - 1);
    const double u = double(mediaTime - a.time) / double(std::max<Ticks>(1, b.time - a.time));
    return {mediaTime, a.dx + (b.dx - a.dx) * u, a.dy + (b.dy - a.dy) * u, a.angle + (b.angle - a.angle) * u,
            a.scale + (b.scale - a.scale) * u};
}

float Deflicker::gainAt(Ticks mediaTime) const
{
    if (samples.empty())
        return 1.0f;
    if (mediaTime <= samples.front().time)
        return samples.front().gain;
    if (mediaTime >= samples.back().time)
        return samples.back().gain;
    const auto next = std::ranges::upper_bound(samples, mediaTime, {}, &GainSample::time);
    const GainSample& b = *next;
    const GainSample& a = *(next - 1);
    const double u = double(mediaTime - a.time) / double(std::max<Ticks>(1, b.time - a.time));
    return float(a.gain + (b.gain - a.gain) * u);
}

namespace {

// Each value eased over time: a straight line fitted with Gaussian weights (σ in seconds)
// around it, taken where it is. Unlike a plain average, a steady pan stays exactly as it is,
// right to the ends of the shot.
std::vector<double> gaussianOverTime(const std::vector<Ticks>& times, const std::vector<double>& values, double sigma)
{
    std::vector<double> out(values.size());
    const double s = std::max(1e-3, sigma);
    std::size_t from = 0;
    for (std::size_t i = 0; i < values.size(); ++i) {
        const double t = toSeconds(times[i]);
        while (from < i && t - toSeconds(times[from]) > 3.0 * s)
            ++from;
        double w0 = 0, w1 = 0, w2 = 0, v0 = 0, v1 = 0;
        for (std::size_t j = from; j < values.size(); ++j) {
            const double d = toSeconds(times[j]) - t;
            if (d > 3.0 * s)
                break;
            const double w = std::exp(-0.5 * d * d / (s * s));
            w0 += w;
            w1 += w * d;
            w2 += w * d * d;
            v0 += w * values[j];
            v1 += w * d * values[j];
        }
        // value(t + d) ≈ a + b·d by weighted least squares; a is the answer.
        const double det = w0 * w2 - w1 * w1;
        out[i] = det > 1e-9 * w0 * w0 ? (v0 * w2 - v1 * w1) / det : v0 / w0;
    }
    return out;
}

} // namespace

std::optional<TrackSample> MotionTrack::at(Ticks mediaTime) const
{
    // A frame's slack either side, so the first and last frames are in.
    constexpr Ticks kSlack = kTicksPerSecond / 20;
    if (samples.empty() || mediaTime < samples.front().time - kSlack || mediaTime > samples.back().time + kSlack)
        return std::nullopt;
    if (mediaTime <= samples.front().time)
        return samples.front();
    if (mediaTime >= samples.back().time)
        return samples.back();
    const auto next = std::ranges::upper_bound(samples, mediaTime, {}, &TrackSample::time);
    const TrackSample& b = *next;
    const TrackSample& a = *(next - 1);
    const double u = double(mediaTime - a.time) / double(std::max<Ticks>(1, b.time - a.time));
    const auto mix = [u](double p, double q) { return p + (q - p) * u; };
    return TrackSample{mediaTime, mix(a.x, b.x), mix(a.y, b.y), mix(a.width, b.width), mix(a.height, b.height),
                       mix(a.angle, b.angle)};
}

std::vector<TrackSample> thinTrack(const std::vector<TrackSample>& samples, double tolerance)
{
    if (samples.size() <= 2)
        return samples;
    // Ramer–Douglas–Peucker over time, on every value.
    std::vector<bool> keep(samples.size(), false);
    keep.front() = keep.back() = true;
    std::vector<std::pair<std::size_t, std::size_t>> spans{{0, samples.size() - 1}};
    while (!spans.empty()) {
        const auto [a, b] = spans.back();
        spans.pop_back();
        if (b <= a + 1)
            continue;
        const TrackSample& p = samples[a];
        const TrackSample& q = samples[b];
        double worst = 0.0;
        std::size_t at = a;
        for (std::size_t i = a + 1; i < b; ++i) {
            const double u = double(samples[i].time - p.time) / double(std::max<Ticks>(1, q.time - p.time));
            const TrackSample& s = samples[i];
            for (const double d : {s.x - (p.x + (q.x - p.x) * u), s.y - (p.y + (q.y - p.y) * u),
                                   s.width - (p.width + (q.width - p.width) * u),
                                   s.height - (p.height + (q.height - p.height) * u),
                                   s.angle - (p.angle + (q.angle - p.angle) * u)}) {
                if (std::abs(d) > worst) {
                    worst = std::abs(d);
                    at = i;
                }
            }
        }
        if (worst > tolerance) {
            keep[at] = true;
            spans.push_back({a, at});
            spans.push_back({at, b});
        }
    }
    std::vector<TrackSample> kept;
    for (std::size_t i = 0; i < samples.size(); ++i)
        if (keep[i])
            kept.push_back(samples[i]);
    return kept;
}

Stabilization stabilize(const MotionAnalysis& analysis, double smoothness, bool lock, double maxZoom)
{
    Stabilization result;
    result.enabled = true;
    result.smoothness = std::clamp(smoothness, 0.0, 1.0);
    result.lock = lock;
    result.maxZoom = std::clamp(maxZoom, 1.0, 2.0);
    const std::size_t n = analysis.times.size();
    if (n == 0 || analysis.width <= 0 || analysis.height <= 0)
        return result;
    // The camera's path: each frame's motion added up (motion the blocks did not agree on
    // counts as none).
    std::array<std::vector<double>, 4> path;
    for (auto& p : path)
        p.assign(n, 0.0);
    for (std::size_t k = 1; k < n; ++k) {
        const FrameMotion& m = analysis.motion[k];
        const bool trusted = m.confidence >= 0.3;
        path[0][k] = path[0][k - 1] + (trusted ? m.dx : 0.0);
        path[1][k] = path[1][k - 1] + (trusted ? m.dy : 0.0);
        path[2][k] = path[2][k - 1] + (trusted ? m.angle : 0.0);
        path[3][k] = path[3][k - 1] + (trusted ? std::log(m.scale) : 0.0);
    }
    // Where it should have been: eased, or (locked) the same all through.
    std::array<std::vector<double>, 4> smooth;
    for (std::size_t c = 0; c < 4; ++c) {
        if (lock)
            smooth[c].assign(n, std::accumulate(path[c].begin(), path[c].end(), 0.0) / double(n));
        else
            smooth[c] = gaussianOverTime(analysis.times, path[c], 0.15 + 1.85 * result.smoothness);
    }
    const double w = analysis.width, h = analysis.height;
    result.samples.reserve(n);
    double zoom = 1.0;
    for (std::size_t k = 0; k < n; ++k) {
        const SteadySample s{analysis.times[k], (smooth[0][k] - path[0][k]) / w, (smooth[1][k] - path[1][k]) / h,
                             smooth[2][k] - path[2][k], std::exp(smooth[3][k] - path[3][k])};
        result.samples.push_back(s);
        // The least zoom at which the corrected frame still covers the picture: every corner
        // of the output, taken back through the correction, lands inside the frame.
        const auto covers = [&](double z) {
            const double c = std::cos(s.angle), si = std::sin(s.angle);
            for (const double ux : {-0.5, 0.5})
                for (const double uy : {-0.5, 0.5}) {
                    const double px = ux * w / z - s.dx * w, py = uy * h / z - s.dy * h;
                    const double rx = (c * px + si * py) / s.scale, ry = (-si * px + c * py) / s.scale;
                    if (std::abs(rx) > w / 2 + 1e-6 || std::abs(ry) > h / 2 + 1e-6)
                        return false;
                }
            return true;
        };
        if (covers(zoom))
            continue;
        double lo = zoom, hi = result.maxZoom;
        if (!covers(hi)) {
            zoom = hi;
            continue;
        }
        for (int i = 0; i < 30; ++i) {
            const double mid = 0.5 * (lo + hi);
            (covers(mid) ? hi : lo) = mid;
        }
        zoom = hi;
    }
    result.zoom = zoom;
    return result;
}

Deflicker deflicker(const MotionAnalysis& analysis, double strength)
{
    Deflicker result;
    result.enabled = true;
    result.strength = std::clamp(strength, 0.0, 1.0);
    const std::size_t n = analysis.brightness.size();
    if (n == 0 || analysis.times.size() != n)
        return result;
    std::vector<double> level(n);
    for (std::size_t k = 0; k < n; ++k)
        level[k] = std::log(std::max(0.02, double(analysis.brightness[k])));
    const auto steady = gaussianOverTime(analysis.times, level, 0.1 + 1.9 * result.strength);
    result.samples.reserve(n);
    for (std::size_t k = 0; k < n; ++k) {
        const float gain = analysis.brightness[k] < 0.02f ? 1.0f : float(std::clamp(std::exp(steady[k] - level[k]), 0.5, 2.0));
        result.samples.push_back({analysis.times[k], gain});
    }
    return result;
}

} // namespace tve
