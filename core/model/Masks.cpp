#include "model/Masks.h"

#include <cmath>

namespace tve {

namespace {

bool inRange(double v, double lo, double hi)
{
    return std::isfinite(v) && v >= lo && v <= hi;
}

} // namespace

std::optional<std::string> validateMasks(const std::vector<ClipMask>& masks)
{
    if (masks.size() > kMaxMasks)
        return "a clip has at most " + std::to_string(kMaxMasks) + " masks";
    std::size_t points = 0;
    for (const ClipMask& m : masks) {
        // Positions may reach past the picture (a mask partly outside it), within reason.
        if (!inRange(m.x, -1.0, 2.0) || !inRange(m.y, -1.0, 2.0) || !inRange(m.width, 0.0, 4.0)
            || !inRange(m.height, 0.0, 4.0) || !inRange(m.rotation, -3600.0, 3600.0) || !inRange(m.roundness, 0.0, 1.0)
            || !inRange(m.feather, 0.0, 0.5))
            return "a mask setting is out of range";
        if (m.shape == MaskShape::Pen) {
            if (m.points.size() < 3)
                return "a pen mask needs at least three points";
            for (const auto& p : m.points) {
                if (!inRange(p[0], -1.0, 2.0) || !inRange(p[1], -1.0, 2.0))
                    return "a pen mask point is out of range";
            }
            points += m.points.size();
        } else if (!m.points.empty()) {
            return "only pen masks have points";
        }
    }
    if (points > kMaxPenPoints)
        return "pen masks have at most " + std::to_string(kMaxPenPoints) + " points in all";
    return std::nullopt;
}

std::optional<std::string> validateChromaKey(const ChromaKey& k)
{
    for (double c : k.color) {
        if (!inRange(c, 0.0, 1.0))
            return "the key colour is out of range";
    }
    if (!inRange(k.tolerance, 0.0, 1.0) || !inRange(k.softness, 0.0, 1.0) || !inRange(k.spill, 0.0, 1.0))
        return "chroma key settings must be between 0 and 100%";
    return std::nullopt;
}

} // namespace tve
