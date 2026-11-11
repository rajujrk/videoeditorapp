#include "model/ClipAnimation.h"

#include "model/Translate.h"

#include <algorithm>
#include <cmath>

namespace tve {

namespace {

// A property's value at the preset's edge: `from = base · factor + offset`.
struct Move {
    std::string_view property;
    double factor;
    double offset;
};

struct Motion {
    std::string_view id;
    std::vector<Move> moves;
    bool bouncy = false; // overshoots into place (pops)
};

const std::vector<Motion>& inMotions()
{
    static const std::vector<Motion> motions{
        {"fade-in", {{property::kOpacity, 0, 0}}},
        {"grow-in", {{property::kScale, 0.3, 0}, {property::kOpacity, 0, 0}}},
        {"shrink-in", {{property::kScale, 1.8, 0}, {property::kOpacity, 0, 0}}},
        {"slide-in-left", {{property::kX, 1, -1.1}}},
        {"slide-in-right", {{property::kX, 1, 1.1}}},
        {"slide-in-top", {{property::kY, 1, -1.1}}},
        {"slide-in-bottom", {{property::kY, 1, 1.1}}},
        {"drop-in", {{property::kY, 1, -0.3}, {property::kOpacity, 0, 0}}},
        {"spin-in", {{property::kRotation, 1, -180}, {property::kScale, 0.2, 0}, {property::kOpacity, 0, 0}}},
        {"pop-in", {{property::kScale, 0.5, 0}, {property::kOpacity, 0, 0}}, true},
    };
    return motions;
}

const std::vector<Motion>& outMotions()
{
    static const std::vector<Motion> motions{
        {"fade-out", {{property::kOpacity, 0, 0}}},
        {"shrink-out", {{property::kScale, 0.3, 0}, {property::kOpacity, 0, 0}}},
        {"grow-out", {{property::kScale, 1.8, 0}, {property::kOpacity, 0, 0}}},
        {"slide-out-left", {{property::kX, 1, -1.1}}},
        {"slide-out-right", {{property::kX, 1, 1.1}}},
        {"slide-out-top", {{property::kY, 1, -1.1}}},
        {"slide-out-bottom", {{property::kY, 1, 1.1}}},
        {"drop-out", {{property::kY, 1, 0.3}, {property::kOpacity, 0, 0}}},
        {"spin-out", {{property::kRotation, 1, 180}, {property::kScale, 0.2, 0}, {property::kOpacity, 0, 0}}},
        {"pop-out", {{property::kScale, 0.5, 0}, {property::kOpacity, 0, 0}}, true},
    };
    return motions;
}

// Combos: an in and an out together.
struct Pair {
    std::string_view id;
    std::string_view in;
    std::string_view out;
};
const std::vector<Pair>& pairs()
{
    static const std::vector<Pair> list{
        {"fade-in-out", "fade-in", "fade-out"},
        {"pop-in-out", "pop-in", "pop-out"},
        {"slide-through", "slide-in-left", "slide-out-right"},
        {"grow-in-shrink-out", "grow-in", "shrink-out"},
    };
    return list;
}

double* staticField(Clip& clip, std::string_view id)
{
    ClipVisual& v = clip.visual;
    if (id == property::kX) return &v.transform.x;
    if (id == property::kY) return &v.transform.y;
    if (id == property::kScale) return &v.transform.scale;
    if (id == property::kRotation) return &v.transform.rotation;
    if (id == property::kOpacity) return &v.opacity;
    return nullptr;
}

// The property's value at a time inside the clip (from its start), animated or not.
double valueAt(Clip& clip, std::string_view id, Ticks local)
{
    const double fallback = *staticField(clip, id);
    const auto it = clip.keyframes.find(id);
    return it == clip.keyframes.end() || it->second.empty() ? fallback : evaluate(it->second, local, fallback);
}

double clamped(std::string_view id, double value)
{
    const AnimatableProperty* p = findAnimatable(id);
    return p ? std::clamp(value, p->min, p->max) : value;
}

void putKey(Keyframes& keys, Keyframe key)
{
    const auto at = std::ranges::lower_bound(keys, key.time, {}, &Keyframe::time);
    if (at != keys.end() && at->time == key.time)
        *at = key;
    else
        keys.insert(at, key);
}

// Overshooting into place, and pulling back before leaving.
constexpr EasingCurve kPopIn{0.34, 1.56, 0.64, 1.0};
constexpr EasingCurve kPopOut{0.36, 0.0, 0.66, -0.56};

void animateIn(Clip& clip, const Motion& motion, Ticks length)
{
    for (const Move& move : motion.moves) {
        const double base = valueAt(clip, move.property, length);
        Keyframes& keys = clip.keyframes[std::string(move.property)];
        std::erase_if(keys, [&](const Keyframe& k) { return k.time < length; });
        if (!keyframeAt(keys, length))
            putKey(keys, {length, base}); // (from here the clip is as it was)
        Keyframe first{0, clamped(move.property, base * move.factor + move.offset), motion.bouncy ? Easing::Curve : Easing::EaseOut};
        if (motion.bouncy)
            first.curve = kPopIn;
        putKey(keys, first);
    }
}

void animateOut(Clip& clip, const Motion& motion, Ticks length)
{
    const Ticks from = clip.duration - length;
    for (const Move& move : motion.moves) {
        const double base = valueAt(clip, move.property, from);
        Keyframes& keys = clip.keyframes[std::string(move.property)];
        std::erase_if(keys, [&](const Keyframe& k) { return k.time > from; });
        Keyframe start{from, base, motion.bouncy ? Easing::Curve : Easing::EaseIn};
        if (motion.bouncy)
            start.curve = kPopOut;
        putKey(keys, start);
        putKey(keys, {clip.duration, clamped(move.property, base * move.factor + move.offset)});
    }
}

const Motion* findMotion(const std::vector<Motion>& list, std::string_view id)
{
    const auto it = std::ranges::find(list, id, &Motion::id);
    return it == list.end() ? nullptr : &*it;
}

} // namespace

const std::vector<ClipAnimationPreset>& clipAnimationPresets()
{
    static const std::vector<ClipAnimationPreset> presets{
        {"fade-in", TVE_TR("core", "Fade in"), ClipAnimationKind::In},
        {"grow-in", TVE_TR("core", "Grow in"), ClipAnimationKind::In},
        {"shrink-in", TVE_TR("core", "Shrink in"), ClipAnimationKind::In},
        {"slide-in-left", TVE_TR("core", "Slide in from the left"), ClipAnimationKind::In},
        {"slide-in-right", TVE_TR("core", "Slide in from the right"), ClipAnimationKind::In},
        {"slide-in-top", TVE_TR("core", "Slide in from the top"), ClipAnimationKind::In},
        {"slide-in-bottom", TVE_TR("core", "Slide in from the bottom"), ClipAnimationKind::In},
        {"drop-in", TVE_TR("core", "Drop in"), ClipAnimationKind::In},
        {"spin-in", TVE_TR("core", "Spin in"), ClipAnimationKind::In},
        {"pop-in", TVE_TR("core", "Pop in"), ClipAnimationKind::In},
        {"fade-out", TVE_TR("core", "Fade out"), ClipAnimationKind::Out},
        {"shrink-out", TVE_TR("core", "Shrink out"), ClipAnimationKind::Out},
        {"grow-out", TVE_TR("core", "Grow out"), ClipAnimationKind::Out},
        {"slide-out-left", TVE_TR("core", "Slide out to the left"), ClipAnimationKind::Out},
        {"slide-out-right", TVE_TR("core", "Slide out to the right"), ClipAnimationKind::Out},
        {"slide-out-top", TVE_TR("core", "Slide out to the top"), ClipAnimationKind::Out},
        {"slide-out-bottom", TVE_TR("core", "Slide out to the bottom"), ClipAnimationKind::Out},
        {"drop-out", TVE_TR("core", "Drop out"), ClipAnimationKind::Out},
        {"spin-out", TVE_TR("core", "Spin out"), ClipAnimationKind::Out},
        {"pop-out", TVE_TR("core", "Pop out"), ClipAnimationKind::Out},
        {"fade-in-out", TVE_TR("core", "Fade in and out"), ClipAnimationKind::Combo},
        {"pop-in-out", TVE_TR("core", "Pop in and out"), ClipAnimationKind::Combo},
        {"slide-through", TVE_TR("core", "Slide through"), ClipAnimationKind::Combo},
        {"grow-in-shrink-out", TVE_TR("core", "Grow in, shrink out"), ClipAnimationKind::Combo},
        {"pulse", TVE_TR("core", "Pulse"), ClipAnimationKind::Combo},
        {"swing", TVE_TR("core", "Swing"), ClipAnimationKind::Combo},
    };
    return presets;
}

const std::vector<std::string_view>& clipAnimationProperties()
{
    static const std::vector<std::string_view> ids{property::kX, property::kY, property::kScale, property::kRotation,
                                                   property::kOpacity};
    return ids;
}

std::optional<std::string> applyClipAnimation(Clip& clip, std::string_view id, Ticks length)
{
    if (length <= 0)
        return TVE_TR("core", "an animation needs a length");
    Clip after = clip;
    if (const Motion* in = findMotion(inMotions(), id)) {
        animateIn(after, *in, std::min(length, after.duration));
    } else if (const Motion* out = findMotion(outMotions(), id)) {
        animateOut(after, *out, std::min(length, after.duration));
    } else if (const auto pair = std::ranges::find(pairs(), id, &Pair::id); pair != pairs().end()) {
        const Ticks each = std::min(length, after.duration / 2);
        animateIn(after, *findMotion(inMotions(), pair->in), each);
        animateOut(after, *findMotion(outMotions(), pair->out), each);
    } else if (id == "pulse" || id == "swing") {
        // Through the whole clip: a beat about once a second.
        const std::string_view property = id == "pulse" ? property::kScale : property::kRotation;
        const int beats = std::max(1, int(std::llround(toSeconds(after.duration))));
        const double base = valueAt(after, property, 0);
        Keyframes keys;
        for (int i = 0; i <= beats * 2; ++i) {
            const Ticks at = after.duration * i / (beats * 2);
            const double value = id == "pulse" ? (i % 2 ? base * 1.08 : base) : base + (i % 2 ? 4.0 : -4.0) * (i == 0 || i == beats * 2 ? 0.0 : 1.0);
            keys.push_back({at, clamped(property, value), Easing::EaseInOut});
        }
        after.keyframes[std::string(property)] = std::move(keys);
    } else {
        return TVE_TR("core", "there is no such animation");
    }
    clip = std::move(after);
    return std::nullopt;
}

void clearClipAnimation(Clip& clip)
{
    for (std::string_view id : clipAnimationProperties())
        if (const auto it = clip.keyframes.find(id); it != clip.keyframes.end())
            clip.keyframes.erase(it);
}

const std::vector<PanZoomPreset>& panZoomPresets()
{
    static const std::vector<PanZoomPreset> presets{
        {"zoom-in", TVE_TR("core", "Zoom in"), {1.0, 0.0, 0.0, 1.25, 0.0, 0.0}},
        {"zoom-out", TVE_TR("core", "Zoom out"), {1.25, 0.0, 0.0, 1.0, 0.0, 0.0}},
        {"pan-left", TVE_TR("core", "Pan left"), {1.2, 0.08, 0.0, 1.2, -0.08, 0.0}},
        {"pan-right", TVE_TR("core", "Pan right"), {1.2, -0.08, 0.0, 1.2, 0.08, 0.0}},
        {"pan-up", TVE_TR("core", "Pan up"), {1.2, 0.0, 0.08, 1.2, 0.0, -0.08}},
        {"pan-down", TVE_TR("core", "Pan down"), {1.2, 0.0, -0.08, 1.2, 0.0, 0.08}},
        {"zoom-in-left", TVE_TR("core", "Zoom in to the left"), {1.0, 0.0, 0.0, 1.3, 0.1, 0.0}},
        {"zoom-in-right", TVE_TR("core", "Zoom in to the right"), {1.0, 0.0, 0.0, 1.3, -0.1, 0.0}},
    };
    return presets;
}

std::optional<std::string> applyPanZoom(Clip& clip, const PanZoom& f)
{
    const auto valid = [](double scale, double x, double y) {
        return std::isfinite(scale) && scale >= 0.1 && scale <= 10.0 && std::abs(x) <= 2.0 && std::abs(y) <= 2.0;
    };
    if (!valid(f.fromScale, f.fromX, f.fromY) || !valid(f.toScale, f.toX, f.toY))
        return TVE_TR("core", "the pan and zoom framing is out of range");
    if (clip.duration <= 0)
        return TVE_TR("core", "an animation needs a length");
    const auto set = [&](std::string_view id, double from, double to) {
        clip.keyframes[std::string(id)] = {{0, from, Easing::Linear}, {clip.duration, to, Easing::Linear}};
    };
    set(property::kScale, f.fromScale, f.toScale);
    set(property::kX, f.fromX, f.toX);
    set(property::kY, f.fromY, f.toY);
    return std::nullopt;
}

} // namespace tve
