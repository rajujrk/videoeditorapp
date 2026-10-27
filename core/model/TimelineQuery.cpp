#include "model/TimelineQuery.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace tve {

double TransitionWindow::progressAt(Ticks time) const
{
    if (end <= start)
        return 1.0;
    return std::clamp(double(time - start) / double(end - start), 0.0, 1.0);
}

std::optional<TransitionWindow> transitionInto(const Track& track, const Clip& clip)
{
    if (!clip.transitionIn || clip.transitionIn->transitionId.empty())
        return std::nullopt;
    auto it = std::ranges::find(track.clips, clip.id, &Clip::id);
    if (it == track.clips.end() || it == track.clips.begin())
        return std::nullopt;
    const Clip& previous = *std::prev(it);
    if (previous.end() != clip.start)
        return std::nullopt;

    const ClipTransition& spec = *clip.transitionIn;
    Ticks before = 0;
    Ticks after = 0;
    switch (spec.alignment) {
    case TransitionAlignment::Center:
        before = spec.duration / 2;
        after = spec.duration - before;
        break;
    case TransitionAlignment::EndsAtCut:
        before = spec.duration;
        break;
    case TransitionAlignment::StartsAtCut:
        after = spec.duration;
        break;
    }
    before = std::min(before, previous.duration / 2);
    after = std::min(after, clip.duration / 2);
    if (before + after <= 0)
        return std::nullopt;
    return TransitionWindow{&previous, &clip, &spec, clip.start, clip.start - before, clip.start + after};
}

std::optional<TransitionWindow> transitionOutOf(const Track& track, const Clip& clip)
{
    auto it = std::ranges::find(track.clips, clip.id, &Clip::id);
    if (it == track.clips.end() || std::next(it) == track.clips.end())
        return std::nullopt;
    return transitionInto(track, *std::next(it));
}

Ticks clampedSourceTime(const Project& project, const Clip& clip, Ticks time)
{
    Ticks source = clip.sourceTimeBeyond(time);
    if (const MediaItem* media = project.findMedia(clip.mediaId); media && media->duration > 0)
        source = std::min(source, media->duration - 1);
    return std::max<Ticks>(source, 0);
}

namespace {

// The layer a track shows at `time`: the clip covering it, or the pair in a transition.
std::optional<VisibleClip> trackLayerAt(const Project& project, const Track& track, Ticks time)
{
    auto it = std::ranges::upper_bound(track.clips, time, {}, &Clip::start);
    if (it == track.clips.begin())
        return std::nullopt;
    --it; // last clip starting at or before `time`
    if (time >= it->end())
        return std::nullopt;
    const Clip& clip = *it;

    std::optional<TransitionWindow> window = transitionOutOf(track, clip);
    if (!window || time < window->start) {
        window = transitionInto(track, clip);
        if (window && time >= window->end)
            window.reset();
    }
    if (!window)
        return VisibleClip{&track, &clip, clip.sourceTimeAt(time), std::nullopt};
    const TransitionLayer incoming{window->spec, window->to, clampedSourceTime(project, *window->to, time),
                                   window->progressAt(time)};
    return VisibleClip{&track, window->from, clampedSourceTime(project, *window->from, time), incoming};
}

} // namespace

std::optional<VisibleClip> visibleVideoAt(const Project& project, Ticks time)
{
    // Tracks are stored bottom to top, so search from the end.
    for (auto track = project.tracks.rbegin(); track != project.tracks.rend(); ++track) {
        if (track->kind != TrackKind::Video || track->hidden)
            continue;
        auto clip = std::ranges::upper_bound(track->clips, time, {}, &Clip::start);
        if (clip == track->clips.begin())
            continue;
        --clip; // last clip starting at or before `time`
        if (time < clip->end())
            return VisibleClip{&*track, &*clip, clip->sourceTimeAt(time)};
    }
    return std::nullopt;
}

std::vector<VisibleClip> visibleVideoLayersAt(const Project& project, Ticks time)
{
    std::vector<VisibleClip> layers;
    for (const Track& track : project.tracks) {
        if (track.kind != TrackKind::Video || track.hidden)
            continue;
        if (auto layer = trackLayerAt(project, track, time))
            layers.push_back(*layer);
    }
    return layers;
}

AudibleSpan audibleSpan(const Track& track, const Clip& clip)
{
    AudibleSpan span{clip.start, clip.end()};
    if (auto in = transitionInto(track, clip))
        span.start = in->start;
    if (auto out = transitionOutOf(track, clip))
        span.end = out->end;
    return span;
}

float transitionGainAt(const Track& track, const Clip& clip, Ticks time)
{
    constexpr double kQuarterTurn = std::numbers::pi / 2.0;
    double gain = 1.0;
    if (auto in = transitionInto(track, clip); in && time < in->end)
        gain *= std::sin(in->progressAt(time) * kQuarterTurn);
    if (auto out = transitionOutOf(track, clip); out && time >= out->start)
        gain *= std::cos(out->progressAt(time) * kQuarterTurn);
    return float(gain);
}

std::optional<Ticks> nextVideoClipStart(const Project& project, Ticks time)
{
    std::optional<Ticks> next;
    for (const Track& track : project.tracks) {
        if (track.kind != TrackKind::Video || track.hidden)
            continue;
        auto clip = std::ranges::upper_bound(track.clips, time, {}, &Clip::start);
        if (clip != track.clips.end() && (!next || clip->start < *next))
            next = clip->start;
    }
    return next;
}

std::vector<Ticks> editPoints(const Project& project)
{
    std::vector<Ticks> points{0};
    for (const Track& track : project.tracks) {
        for (const Clip& clip : track.clips) {
            points.push_back(clip.start);
            points.push_back(clip.end());
        }
    }
    std::ranges::sort(points);
    points.erase(std::unique(points.begin(), points.end()), points.end());
    return points;
}

} // namespace tve
