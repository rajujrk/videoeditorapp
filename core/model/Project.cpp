#include "model/Project.h"

#include <algorithm>

namespace tve {

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
