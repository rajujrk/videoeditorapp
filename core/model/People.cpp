#include "model/People.h"

#include <algorithm>
#include <cmath>
#include <functional>

namespace tve {

namespace {

// A face is taken to be there a little before its first sample and after its last (a frame
// or two of slack either way), so a cover never lets a face show at its edges.
constexpr Ticks kSlack = kTicksPerSecond / 10;

FaceBox lerp(const FaceBox& a, const FaceBox& b, double t)
{
    return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.width + (b.width - a.width) * t,
            a.height + (b.height - a.height) * t};
}

double overlap(const FaceBox& a, const FaceBox& b)
{
    const double w = std::min(a.x + a.width / 2, b.x + b.width / 2) - std::max(a.x - a.width / 2, b.x - b.width / 2);
    const double h = std::min(a.y + a.height / 2, b.y + b.height / 2) - std::max(a.y - a.height / 2, b.y - b.height / 2);
    if (w <= 0.0 || h <= 0.0)
        return 0.0;
    const double both = w * h;
    return both / (a.width * a.height + b.width * b.height - both);
}

// Ramer–Douglas–Peucker over time: the indices of the points to keep so that straight lines
// between them stay within `tolerance` of every value of every point.
template <typename Point>
std::vector<std::size_t> simplify(const std::vector<Point>& points, const std::function<Ticks(const Point&)>& timeOf,
                                  const std::function<std::vector<double>(const Point&)>& valuesOf, double tolerance)
{
    if (points.size() <= 2) {
        std::vector<std::size_t> all(points.size());
        for (std::size_t i = 0; i < all.size(); ++i)
            all[i] = i;
        return all;
    }
    std::vector<bool> keep(points.size(), false);
    keep.front() = keep.back() = true;
    std::vector<std::pair<std::size_t, std::size_t>> spans{{0, points.size() - 1}};
    while (!spans.empty()) {
        const auto [a, b] = spans.back();
        spans.pop_back();
        if (b <= a + 1)
            continue;
        const auto va = valuesOf(points[a]);
        const auto vb = valuesOf(points[b]);
        const double ta = double(timeOf(points[a]));
        const double span = double(timeOf(points[b])) - ta;
        double worst = 0.0;
        std::size_t at = a;
        for (std::size_t i = a + 1; i < b; ++i) {
            const double t = span > 0.0 ? (double(timeOf(points[i])) - ta) / span : 0.0;
            const auto v = valuesOf(points[i]);
            for (std::size_t k = 0; k < v.size(); ++k) {
                const double d = std::abs(v[k] - (va[k] + (vb[k] - va[k]) * t));
                if (d > worst) {
                    worst = d;
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
    std::vector<std::size_t> kept;
    for (std::size_t i = 0; i < keep.size(); ++i)
        if (keep[i])
            kept.push_back(i);
    return kept;
}

} // namespace

std::optional<FaceBox> FaceTrack::boxAt(Ticks mediaTime) const
{
    if (samples.empty() || mediaTime < start() - kSlack || mediaTime > end() + kSlack)
        return std::nullopt;
    if (mediaTime <= samples.front().time)
        return samples.front().box;
    if (mediaTime >= samples.back().time)
        return samples.back().box;
    const auto next = std::ranges::upper_bound(samples, mediaTime, {}, &FaceSample::time);
    const FaceSample& b = *next;
    const FaceSample& a = *(next - 1);
    const double t = b.time > a.time ? double(mediaTime - a.time) / double(b.time - a.time) : 0.0;
    return lerp(a.box, b.box, t);
}

std::vector<FaceBox> FaceMosaic::boxesAt(Ticks mediaTime) const
{
    std::vector<FaceBox> boxes;
    for (const FaceTrack& track : faces) {
        if (boxes.size() == kMaxCoveredFaces)
            break;
        if (auto box = track.boxAt(mediaTime)) {
            box->width *= 1.0 + expand;
            box->height *= 1.0 + expand;
            boxes.push_back(*box);
        }
    }
    return boxes;
}

std::vector<FaceTrack> linkFaces(const std::vector<FaceFrame>& frames, const FaceTrackOptions& options)
{
    struct Open {
        FaceTrack track;
        FaceBox last;
        Ticks lastTime = 0;
    };
    std::vector<Open> open;
    std::vector<FaceTrack> done;
    for (const FaceFrame& frame : frames) {
        // Tracks lost for too long are finished.
        for (auto it = open.begin(); it != open.end();) {
            if (frame.time - it->lastTime > options.maxGap) {
                done.push_back(std::move(it->track));
                it = open.erase(it);
            } else {
                ++it;
            }
        }
        // Best matches first: the pair that overlaps most, then the next among the rest. A face
        // that moved too fast to overlap enough still matches a track within a face's size.
        struct Pair {
            double score;
            std::size_t track;
            std::size_t face;
        };
        std::vector<Pair> pairs;
        for (std::size_t t = 0; t < open.size(); ++t) {
            for (std::size_t f = 0; f < frame.faces.size(); ++f) {
                const FaceBox& a = open[t].last;
                const FaceBox& b = frame.faces[f];
                double score = overlap(a, b);
                if (score < options.minOverlap) {
                    const double distance = std::hypot(a.x - b.x, a.y - b.y);
                    const double reach = std::max({a.width, a.height, b.width, b.height});
                    score = distance < reach ? options.minOverlap * (1.0 - distance / reach) : 0.0;
                    if (score <= 0.0)
                        continue;
                }
                pairs.push_back({score, t, f});
            }
        }
        std::ranges::sort(pairs, std::greater<>{}, &Pair::score);
        std::vector<bool> trackTaken(open.size(), false);
        std::vector<bool> faceTaken(frame.faces.size(), false);
        for (const Pair& p : pairs) {
            if (trackTaken[p.track] || faceTaken[p.face])
                continue;
            trackTaken[p.track] = faceTaken[p.face] = true;
            Open& o = open[p.track];
            o.track.samples.push_back({frame.time, frame.faces[p.face]});
            o.last = frame.faces[p.face];
            o.lastTime = frame.time;
        }
        for (std::size_t f = 0; f < frame.faces.size(); ++f) {
            if (!faceTaken[f])
                open.push_back({FaceTrack{0, {{frame.time, frame.faces[f]}}}, frame.faces[f], frame.time});
        }
    }
    for (Open& o : open)
        done.push_back(std::move(o.track));

    std::erase_if(done, [&](const FaceTrack& t) { return t.samples.size() < 2 || t.end() - t.start() < options.minLength; });
    std::ranges::sort(done, {}, &FaceTrack::start);
    for (std::size_t i = 0; i < done.size(); ++i) {
        FaceTrack& t = done[i];
        t.id = int(i) + 1;
        const auto kept = simplify<FaceSample>(
            t.samples, [](const FaceSample& s) { return s.time; },
            [](const FaceSample& s) { return std::vector<double>{s.box.x, s.box.y, s.box.width, s.box.height}; },
            options.tolerance);
        std::vector<FaceSample> samples;
        for (const std::size_t k : kept)
            samples.push_back(t.samples[k]);
        t.samples = std::move(samples);
    }
    return done;
}

ReframePath reframe(const std::vector<SubjectSample>& subject, double pictureWidth, double pictureHeight,
                    double canvasWidth, double canvasHeight, const ReframeOptions& options)
{
    ReframePath path;
    if (pictureWidth <= 0 || pictureHeight <= 0 || canvasWidth <= 0 || canvasHeight <= 0)
        return path;
    // Scaled to fill the canvas (placeLayer fits it inside, so scale = fill / fit).
    const double fit = std::min(canvasWidth / pictureWidth, canvasHeight / pictureHeight);
    const double fill = std::max(canvasWidth / pictureWidth, canvasHeight / pictureHeight);
    path.scale = fill / fit;
    const double shownW = pictureWidth * fill;
    const double shownH = pictureHeight * fill;
    // How far the picture can move each way before an edge shows (in canvas sizes).
    const double roomX = (shownW - canvasWidth) / (2.0 * canvasWidth);
    const double roomY = (shownH - canvasHeight) / (2.0 * canvasHeight);
    if (subject.empty())
        return path;

    // The subject's centre on an even 10 Hz grid, then eased with a Gaussian (σ = smoothing s).
    constexpr Ticks kStep = kTicksPerSecond / 10;
    const Ticks first = subject.front().time;
    const Ticks last = subject.back().time;
    std::vector<Ticks> times;
    for (Ticks t = first; t < last; t += kStep)
        times.push_back(t);
    times.push_back(last);
    std::vector<double> cx(times.size()), cy(times.size());
    std::size_t at = 0;
    for (std::size_t i = 0; i < times.size(); ++i) {
        while (at + 1 < subject.size() && subject[at + 1].time <= times[i])
            ++at;
        const SubjectSample& a = subject[at];
        const SubjectSample& b = subject[std::min(at + 1, subject.size() - 1)];
        const double u = b.time > a.time ? std::clamp(double(times[i] - a.time) / double(b.time - a.time), 0.0, 1.0) : 0.0;
        cx[i] = a.box.x + (b.box.x - a.box.x) * u;
        cy[i] = a.box.y + (b.box.y - a.box.y) * u;
    }
    const double sigma = std::max(1e-3, options.smoothing) * 10.0; // in grid steps
    const int reach = int(std::ceil(sigma * 3.0));
    std::vector<std::pair<Ticks, std::array<double, 2>>> positions;
    for (std::size_t i = 0; i < times.size(); ++i) {
        double sx = 0.0, sy = 0.0, sw = 0.0;
        for (int k = -reach; k <= reach; ++k) {
            const auto j = std::ptrdiff_t(i) + k;
            if (j < 0 || j >= std::ptrdiff_t(times.size()))
                continue;
            const double w = std::exp(-0.5 * (k * k) / (sigma * sigma));
            sx += cx[std::size_t(j)] * w;
            sy += cy[std::size_t(j)] * w;
            sw += w;
        }
        // The subject centred: the picture moved the other way by its offset from the middle.
        const double x = std::clamp(-(sx / sw - 0.5) * shownW / canvasWidth, -roomX, roomX);
        const double y = std::clamp(-(sy / sw - 0.5) * shownH / canvasHeight, -roomY, roomY);
        positions.push_back({times[i], {x, y}});
    }
    using Position = std::pair<Ticks, std::array<double, 2>>;
    const auto kept = simplify<Position>(
        positions, [](const Position& p) { return p.first; },
        [](const Position& p) { return std::vector<double>{p.second[0], p.second[1]}; }, options.tolerance);
    for (const std::size_t k : kept)
        path.positions.push_back(positions[k]);
    return path;
}

} // namespace tve
