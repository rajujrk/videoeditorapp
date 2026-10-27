#include "model/TimelineQuery.h"

#include <algorithm>

namespace tve {

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
            return VisibleClip{&*track, &*clip, clip->sourceIn + (time - clip->start)};
    }
    return std::nullopt;
}

std::vector<VisibleClip> visibleVideoLayersAt(const Project& project, Ticks time)
{
    std::vector<VisibleClip> layers;
    for (const Track& track : project.tracks) {
        if (track.kind != TrackKind::Video || track.hidden)
            continue;
        auto clip = std::ranges::upper_bound(track.clips, time, {}, &Clip::start);
        if (clip == track.clips.begin())
            continue;
        --clip;
        if (time < clip->end())
            layers.push_back(VisibleClip{&track, &*clip, clip->sourceIn + (time - clip->start)});
    }
    return layers;
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
