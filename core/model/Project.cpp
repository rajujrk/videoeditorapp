#include "model/Project.h"

#include <algorithm>
#include <cmath>

namespace tve {

const Keyframes* Clip::keyframesOf(std::string_view propertyId) const
{
    const auto it = keyframes.find(propertyId);
    return it != keyframes.end() && !it->second.empty() ? &it->second : nullptr;
}

float Clip::gainDbAt(Ticks time) const
{
    const Keyframes* animated = keyframesOf(property::kVolume);
    if (!animated)
        return gainDb;
    // A curve may overshoot its keyframes; stay in range.
    return float(std::clamp(evaluate(*animated, time - start, gainDb), double(kMinGainDb), double(kMaxGainDb)));
}

std::vector<ClipEffect> Clip::effectsAt(Ticks time) const
{
    std::vector<ClipEffect> evaluated = effects;
    for (ClipEffect& effect : evaluated) {
        for (const auto& [param, keys] : effect.keyframes) {
            if (!keys.empty())
                effect.values[param] = {float(evaluate(keys, time - start, 0.0))};
        }
        effect.keyframes.clear();
    }
    return evaluated;
}

ClipVisual Clip::visualAt(Ticks time) const
{
    if (keyframes.empty())
        return visual;
    ClipVisual v = visual;
    const Ticks local = time - start;
    const auto apply = [&](std::string_view id, double& value) {
        if (const Keyframes* animated = keyframesOf(id)) {
            const AnimatableProperty* p = findAnimatable(id);
            value = std::clamp(evaluate(*animated, local, value), p->min, p->max); // curves may overshoot
        }
    };
    apply(property::kX, v.transform.x);
    apply(property::kY, v.transform.y);
    apply(property::kScale, v.transform.scale);
    apply(property::kRotation, v.transform.rotation);
    apply(property::kOpacity, v.opacity);
    apply(property::kCropLeft, v.crop.left);
    apply(property::kCropTop, v.crop.top);
    apply(property::kCropRight, v.crop.right);
    apply(property::kCropBottom, v.crop.bottom);
    // Edges keyed apart can meet; keep a sliver of picture as static crops do.
    const auto keepSome = [](double& a, double& b) {
        const double sum = a + b;
        if (sum > 0.95) {
            a *= 0.95 / sum;
            b *= 0.95 / sum;
        }
    };
    keepSome(v.crop.left, v.crop.right);
    keepSome(v.crop.top, v.crop.bottom);
    return v;
}

float Clip::gainAt(Ticks time) const
{
    const float db = gainDbAt(time);
    if (db <= kMinGainDb)
        return 0.0f;
    float gain = std::pow(10.0f, db / 20.0f);
    const Ticks intoClip = time - start;
    const Ticks untilEnd = end() - time;
    if (fadeIn > 0 && intoClip < fadeIn)
        gain *= float(std::max<Ticks>(intoClip, 0)) / float(fadeIn);
    if (fadeOut > 0 && untilEnd < fadeOut)
        gain *= float(std::max<Ticks>(untilEnd, 0)) / float(fadeOut);
    return gain;
}

Ticks Clip::sourceSpan() const
{
    return freeze ? 0 : Ticks(std::llround(double(duration) * speed));
}

namespace {

// How far into the clip's source span a timeline time is (before reversing). Past the
// clip's edges (transition handles) the speed at that edge carries on.
Ticks spanOffset(const Clip& clip, Ticks time)
{
    if (clip.speedRamp.empty() || clip.duration <= 0)
        return Ticks(std::llround(double(time - clip.start) * clip.speed));
    if (time <= clip.start)
        return Ticks(std::llround(double(time - clip.start) * clip.speedRamp.front().speed));
    if (time >= clip.end())
        return clip.sourceSpan() + Ticks(std::llround(double(time - clip.end()) * clip.speedRamp.back().speed));
    const double u = double(time - clip.start) / double(clip.duration);
    return Ticks(std::llround(double(clip.sourceSpan()) * rampProgress(clip.speedRamp, u)));
}

} // namespace

Ticks Clip::sourceTimeAt(Ticks time) const
{
    if (freeze)
        return sourceIn;
    const Ticks offset = spanOffset(*this, time);
    if (!reversed)
        return sourceIn + offset;
    // Backwards from the end of the span; one tick inside it so the frame shown at the
    // clip's start is the span's last frame, not the first frame after it.
    return std::max(sourceIn, sourceIn + sourceSpan() - offset - 1);
}

Ticks Clip::sourceTimeBeyond(Ticks time) const
{
    if (time >= start && time < end())
        return sourceTimeAt(time);
    if (freeze)
        return sourceIn;
    const Ticks offset = spanOffset(*this, time);
    return reversed ? sourceIn + sourceSpan() - offset - 1 : sourceIn + offset;
}

const Clip* Track::findClip(Id clipId) const
{
    auto it = std::ranges::find(clips, clipId, &Clip::id);
    return it == clips.end() ? nullptr : &*it;
}

Clip* Track::findClip(Id clipId)
{
    auto it = std::ranges::find(clips, clipId, &Clip::id);
    return it == clips.end() ? nullptr : &*it;
}

bool Track::overlaps(Ticks start, Ticks duration, Id ignoreId) const
{
    const Ticks end = start + duration;
    return std::ranges::any_of(clips, [&](const Clip& clip) {
        return clip.id != ignoreId && start < clip.end() && clip.start < end;
    });
}

void Track::insertClip(Clip clip)
{
    auto pos = std::ranges::upper_bound(clips, clip.start, {}, &Clip::start);
    clips.insert(pos, std::move(clip));
}

std::optional<Clip> Track::removeClip(Id clipId)
{
    auto it = std::ranges::find(clips, clipId, &Clip::id);
    if (it == clips.end())
        return std::nullopt;
    Clip removed = std::move(*it);
    clips.erase(it);
    return removed;
}

Track* Project::findTrack(Id trackId)
{
    auto it = std::ranges::find(tracks, trackId, &Track::id);
    return it == tracks.end() ? nullptr : &*it;
}

const Track* Project::findTrack(Id trackId) const
{
    auto it = std::ranges::find(tracks, trackId, &Track::id);
    return it == tracks.end() ? nullptr : &*it;
}

std::optional<std::size_t> Project::trackIndex(Id trackId) const
{
    auto it = std::ranges::find(tracks, trackId, &Track::id);
    if (it == tracks.end())
        return std::nullopt;
    return static_cast<std::size_t>(it - tracks.begin());
}

const MediaItem* Project::findMedia(Id mediaId) const
{
    auto it = std::ranges::find(media, mediaId, &MediaItem::id);
    return it == media.end() ? nullptr : &*it;
}

std::optional<ClipLocation> Project::locateClip(Id clipId)
{
    for (Track& track : tracks) {
        if (Clip* clip = track.findClip(clipId))
            return ClipLocation{&track, clip};
    }
    return std::nullopt;
}

const Sequence* Project::findSequence(Id sequenceId) const
{
    auto it = std::ranges::find(sequences, sequenceId, &Sequence::id);
    return it == sequences.end() ? nullptr : &*it;
}

Marker* Project::findMarker(Id markerId)
{
    auto it = std::ranges::find(markers, markerId, &Marker::id);
    return it == markers.end() ? nullptr : &*it;
}

std::optional<Ticks> Project::sourceLimit(const Clip& clip) const
{
    if (clip.compound()) {
        const Sequence* sequence = findSequence(clip.sequenceId);
        return sequence ? sequence->duration() : 0;
    }
    if (clip.generated())
        return std::nullopt;
    const MediaItem* item = findMedia(clip.mediaId);
    if (!item)
        return 0;
    return item->still ? std::nullopt : std::optional(item->duration);
}

std::vector<Chapter> Project::chapters() const
{
    const Ticks end = duration();
    std::vector<Chapter> list;
    for (const Marker& marker : markers) {
        if (!marker.chapter || marker.time >= end)
            continue;
        if (!list.empty() && list.back().start == marker.time)
            continue; // two chapters cannot start at once
        if (!list.empty())
            list.back().end = marker.time;
        list.push_back({marker.time, end,
                        marker.note.empty() ? "Chapter " + std::to_string(list.size() + 1) : marker.note});
    }
    if (!list.empty() && list.front().start > 0)
        list.insert(list.begin(), Chapter{0, list.front().start, "Opening"});
    return list;
}

std::vector<Chapter> Project::chapters(Ticks from, Ticks to) const
{
    std::vector<Chapter> list;
    for (Chapter chapter : chapters()) {
        chapter.start = std::max(chapter.start, from) - from;
        chapter.end = std::min(chapter.end, to) - from;
        if (chapter.end > chapter.start)
            list.push_back(std::move(chapter));
    }
    return list;
}

Ticks Sequence::duration() const
{
    Ticks end = 0;
    for (const Track& track : tracks) {
        if (!track.clips.empty())
            end = std::max(end, track.clips.back().end());
    }
    return end;
}

Ticks Project::duration() const
{
    Ticks end = 0;
    for (const Track& track : tracks) {
        if (!track.clips.empty())
            end = std::max(end, track.clips.back().end());
    }
    return end;
}

} // namespace tve
