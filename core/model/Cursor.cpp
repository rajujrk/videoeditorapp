#include "model/Cursor.h"

#include <algorithm>
#include <cmath>

namespace tve {

std::optional<CursorSample> CursorEffects::at(Ticks mediaTime) const
{
    if (path.empty())
        return std::nullopt;
    if (mediaTime <= path.front().time)
        return path.front();
    if (mediaTime >= path.back().time)
        return path.back();
    const auto next = std::ranges::upper_bound(path, mediaTime, {}, &CursorSample::time);
    const CursorSample& b = *next;
    const CursorSample& a = *(next - 1);
    const double u = double(mediaTime - a.time) / double(std::max<Ticks>(1, b.time - a.time));
    return CursorSample{mediaTime, a.x + (b.x - a.x) * u, a.y + (b.y - a.y) * u};
}

std::optional<double> CursorEffects::sinceClick(Ticks mediaTime, double within) const
{
    const auto after = std::ranges::upper_bound(clicks, mediaTime);
    if (after == clicks.begin())
        return std::nullopt;
    const double since = toSeconds(mediaTime - *(after - 1));
    if (since > within)
        return std::nullopt;
    return since;
}

std::vector<CursorSample> thinCursorPath(const std::vector<CursorSample>& path, double tolerance)
{
    if (path.size() <= 2)
        return path;
    std::vector<bool> keep(path.size(), false);
    keep.front() = keep.back() = true;
    std::vector<std::pair<std::size_t, std::size_t>> spans{{0, path.size() - 1}};
    while (!spans.empty()) {
        const auto [a, b] = spans.back();
        spans.pop_back();
        if (b <= a + 1)
            continue;
        const CursorSample& p = path[a];
        const CursorSample& q = path[b];
        double worst = 0.0;
        std::size_t at = a;
        for (std::size_t i = a + 1; i < b; ++i) {
            const double u = double(path[i].time - p.time) / double(std::max<Ticks>(1, q.time - p.time));
            const double d = std::hypot(path[i].x - (p.x + (q.x - p.x) * u), path[i].y - (p.y + (q.y - p.y) * u));
            if (d > worst) {
                worst = d;
                at = i;
            }
        }
        if (worst > tolerance) {
            keep[at] = true;
            spans.push_back({a, at});
            spans.push_back({at, b});
        }
    }
    std::vector<CursorSample> kept;
    for (std::size_t i = 0; i < path.size(); ++i)
        if (keep[i])
            kept.push_back(path[i]);
    return kept;
}

} // namespace tve
