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
    TrackSample out{mediaTime, mix(a.x, b.x), mix(a.y, b.y), mix(a.width, b.width), mix(a.height, b.height),
                    mix(a.angle, b.angle)};
    if (a.hasCorners() && b.hasCorners())
        for (std::size_t i = 0; i < out.corners.size(); ++i)
            out.corners[i] = mix(a.corners[i], b.corners[i]);
    return out;
}

bool TrackSample::hasCorners() const
{
    return std::ranges::any_of(corners, [](double c) { return c != 0.0; });
}

TrackSample sampleOfQuad(Ticks time, const Quad& c)
{
    const double cx = (c[0] + c[2] + c[4] + c[6]) / 4, cy = (c[1] + c[3] + c[5] + c[7]) / 4;
    const double top = std::hypot(c[2] - c[0], c[3] - c[1]), bottom = std::hypot(c[4] - c[6], c[5] - c[7]);
    const double left = std::hypot(c[6] - c[0], c[7] - c[1]), right = std::hypot(c[4] - c[2], c[5] - c[3]);
    const double angle = std::atan2((c[3] - c[1]) + (c[5] - c[7]), (c[2] - c[0]) + (c[4] - c[6]));
    TrackSample s{time, cx, cy, (top + bottom) / 2, (left + right) / 2, angle};
    s.corners = c;
    return s;
}

// --- Homographies ---

std::pair<double, double> Homography::apply(double x, double y) const
{
    const double w = m[6] * x + m[7] * y + m[8];
    const double d = std::abs(w) < 1e-12 ? 1e-12 : w;
    return {(m[0] * x + m[1] * y + m[2]) / d, (m[3] * x + m[4] * y + m[5]) / d};
}

Quad Homography::apply(const Quad& q) const
{
    Quad out{};
    for (std::size_t i = 0; i < 4; ++i) {
        const auto [x, y] = apply(q[i * 2], q[i * 2 + 1]);
        out[i * 2] = x;
        out[i * 2 + 1] = y;
    }
    return out;
}

std::optional<Homography> Homography::inverse() const
{
    const auto& a = m;
    const double c00 = a[4] * a[8] - a[5] * a[7], c01 = a[5] * a[6] - a[3] * a[8], c02 = a[3] * a[7] - a[4] * a[6];
    const double det = a[0] * c00 + a[1] * c01 + a[2] * c02;
    if (std::abs(det) < 1e-15)
        return std::nullopt;
    Homography h;
    h.m = {c00 / det,
           (a[2] * a[7] - a[1] * a[8]) / det,
           (a[1] * a[5] - a[2] * a[4]) / det,
           c01 / det,
           (a[0] * a[8] - a[2] * a[6]) / det,
           (a[2] * a[3] - a[0] * a[5]) / det,
           c02 / det,
           (a[1] * a[6] - a[0] * a[7]) / det,
           (a[0] * a[4] - a[1] * a[3]) / det};
    return h;
}

Homography Homography::then(const Homography& next) const
{
    // next × this
    Homography h;
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c) {
            double v = 0.0;
            for (int k = 0; k < 3; ++k)
                v += next.m[std::size_t(r * 3 + k)] * m[std::size_t(k * 3 + c)];
            h.m[std::size_t(r * 3 + c)] = v;
        }
    return h;
}

namespace {

// Solves a small dense system A x = b (n unknowns, least squares when more rows) by the normal
// equations and Gaussian elimination with partial pivoting.
std::optional<std::vector<double>> solveLeastSquares(const std::vector<std::vector<double>>& rows, const std::vector<double>& b,
                                                     std::size_t n)
{
    std::vector<std::vector<double>> ata(n, std::vector<double>(n + 1, 0.0));
    for (std::size_t r = 0; r < rows.size(); ++r)
        for (std::size_t i = 0; i < n; ++i) {
            for (std::size_t j = 0; j < n; ++j)
                ata[i][j] += rows[r][i] * rows[r][j];
            ata[i][n] += rows[r][i] * b[r];
        }
    for (std::size_t col = 0; col < n; ++col) {
        std::size_t pivot = col;
        for (std::size_t r = col + 1; r < n; ++r)
            if (std::abs(ata[r][col]) > std::abs(ata[pivot][col]))
                pivot = r;
        if (std::abs(ata[pivot][col]) < 1e-14)
            return std::nullopt;
        std::swap(ata[col], ata[pivot]);
        for (std::size_t r = 0; r < n; ++r) {
            if (r == col)
                continue;
            const double f = ata[r][col] / ata[col][col];
            for (std::size_t c = col; c <= n; ++c)
                ata[r][c] -= f * ata[col][c];
        }
    }
    std::vector<double> x(n);
    for (std::size_t i = 0; i < n; ++i)
        x[i] = ata[i][n] / ata[i][i];
    return x;
}

} // namespace

std::optional<Homography> fitHomography(const std::vector<std::pair<double, double>>& from,
                                        const std::vector<std::pair<double, double>>& to)
{
    if (from.size() < 4 || from.size() != to.size())
        return std::nullopt;
    // Normalised (centred, unit spread) for a well-conditioned solve, then undone.
    const auto normaliser = [](const std::vector<std::pair<double, double>>& pts) {
        double cx = 0, cy = 0;
        for (const auto& [x, y] : pts)
            cx += x, cy += y;
        cx /= double(pts.size());
        cy /= double(pts.size());
        double spread = 0;
        for (const auto& [x, y] : pts)
            spread += std::hypot(x - cx, y - cy);
        spread = std::max(1e-12, spread / double(pts.size()));
        const double s = std::sqrt(2.0) / spread;
        Homography t;
        t.m = {s, 0, -s * cx, 0, s, -s * cy, 0, 0, 1};
        return t;
    };
    const Homography tf = normaliser(from), tt = normaliser(to);
    std::vector<std::vector<double>> rows;
    std::vector<double> b;
    for (std::size_t i = 0; i < from.size(); ++i) {
        const auto [x, y] = tf.apply(from[i].first, from[i].second);
        const auto [u, v] = tt.apply(to[i].first, to[i].second);
        rows.push_back({x, y, 1, 0, 0, 0, -u * x, -u * y});
        b.push_back(u);
        rows.push_back({0, 0, 0, x, y, 1, -v * x, -v * y});
        b.push_back(v);
    }
    const auto h = solveLeastSquares(rows, b, 8);
    if (!h)
        return std::nullopt;
    Homography n;
    n.m = {(*h)[0], (*h)[1], (*h)[2], (*h)[3], (*h)[4], (*h)[5], (*h)[6], (*h)[7], 1.0};
    const auto back = tt.inverse();
    if (!back)
        return std::nullopt;
    return tf.then(n).then(*back);
}

namespace {

// Whether a quad is usable: no three corners (nearly) in a line.
bool properQuad(const Quad& q)
{
    double scale = 0.0;
    for (std::size_t i = 0; i < 4; ++i)
        for (std::size_t j = i + 1; j < 4; ++j)
            scale = std::max(scale, std::hypot(q[i * 2] - q[j * 2], q[i * 2 + 1] - q[j * 2 + 1]));
    if (scale <= 0.0)
        return false;
    for (std::size_t skip = 0; skip < 4; ++skip) {
        std::array<std::size_t, 3> c{};
        std::size_t n = 0;
        for (std::size_t i = 0; i < 4; ++i)
            if (i != skip)
                c[n++] = i;
        const double area = (q[c[1] * 2] - q[c[0] * 2]) * (q[c[2] * 2 + 1] - q[c[0] * 2 + 1])
                          - (q[c[2] * 2] - q[c[0] * 2]) * (q[c[1] * 2 + 1] - q[c[0] * 2 + 1]);
        if (std::abs(area) < 1e-6 * scale * scale)
            return false;
    }
    return true;
}

} // namespace

std::optional<Homography> squareToQuad(const Quad& q)
{
    if (!properQuad(q))
        return std::nullopt;
    return fitHomography({{0, 0}, {1, 0}, {1, 1}, {0, 1}}, {{q[0], q[1]}, {q[2], q[3]}, {q[4], q[5]}, {q[6], q[7]}});
}

std::optional<Homography> quadToQuad(const Quad& from, const Quad& to)
{
    if (!properQuad(from) || !properQuad(to))
        return std::nullopt;
    return fitHomography({{from[0], from[1]}, {from[2], from[3]}, {from[4], from[5]}, {from[6], from[7]}},
                         {{to[0], to[1]}, {to[2], to[3]}, {to[4], to[5]}, {to[6], to[7]}});
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
            // A plane's corners too (its perspective can change while its centre does not).
            if (s.hasCorners() && p.hasCorners() && q.hasCorners())
                for (std::size_t c = 0; c < s.corners.size(); ++c) {
                    const double d = s.corners[c] - (p.corners[c] + (q.corners[c] - p.corners[c]) * u);
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
