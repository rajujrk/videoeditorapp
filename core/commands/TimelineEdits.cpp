#include "commands/TimelineEdits.h"

#include "commands/EditCommands.h"
#include "commands/TemplateCommands.h"
#include "model/Keyframes.h"
#include "model/Translate.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>

namespace tve {

namespace {

bool picturesKind(TrackKind kind)
{
    return kind == TrackKind::Video || kind == TrackKind::Title;
}

// Every track on the timeline and inside compound clips.
template <typename F>
void eachTrack(Project& project, F&& f)
{
    for (Track& track : project.tracks)
        f(track);
    for (Sequence& sequence : project.sequences)
        for (Track& track : sequence.tracks)
            f(track);
}

std::optional<std::string> editableClip(Project& project, Id clipId, ClipLocation* found)
{
    const auto location = project.locateClip(clipId);
    if (!location)
        return TVE_TR("core", "Select a clip");
    if (location->track->locked)
        return TVE_TR("core", "The clip's track is locked");
    *found = *location;
    return std::nullopt;
}

} // namespace

std::optional<std::string> replaceClipMedia(Project& project, Id clipId, Id mediaId, bool* shortened)
{
    if (shortened)
        *shortened = false;
    ClipLocation location;
    if (auto error = editableClip(project, clipId, &location))
        return error;
    const Clip& clip = *location.clip;
    if (clip.generated() || clip.compound())
        return TVE_TR("core", "Only a clip of media can play other media");
    const MediaItem* media = project.findMedia(mediaId);
    if (!media)
        return TVE_TR("core", "That media is not in the project");
    const TrackKind kind = location.track->kind;
    if (kind == TrackKind::Audio && !media->hasAudio)
        return TVE_TR("core", "That media has no sound for an audio track");
    if (kind != TrackKind::Audio && !media->hasVideo)
        return TVE_TR("core", "Sound-only media goes on an audio track");
    if (clip.mediaId == mediaId)
        return std::nullopt;

    // A speed ramp belongs to the old pictures: the new media plays at the ramp's average.
    const double speed = clip.speed > 0 ? clip.speed : 1.0;
    Ticks sourceIn = 0;
    Ticks duration = clip.duration;
    if (!media->still) {
        const Ticks needed = Ticks(std::llround(double(clip.duration) * speed));
        if (needed <= media->duration) {
            sourceIn = std::clamp<Ticks>(clip.freeze || clip.reversed ? 0 : clip.sourceIn, 0, media->duration - needed);
        } else {
            duration = Ticks(std::floor(double(media->duration) / speed));
            if (duration <= 0)
                return TVE_TR("core", "That media is too short");
            if (shortened)
                *shortened = true;
        }
    }
    if (duration != clip.duration) {
        TrimClipCommand trim(clipId, TrimClipCommand::Edge::End, clip.start + duration);
        if (auto error = trim.redo(project))
            return error;
    }
    ReplaceClipSourceCommand replace(clipId, mediaId, sourceIn, speed, std::nullopt, std::nullopt, {});
    return replace.redo(project);
}

std::optional<std::string> setClipsEnabled(Project& project, const std::vector<Id>& clipIds, bool enabled)
{
    if (clipIds.empty())
        return TVE_TR("core", "Select a clip");
    for (Id id : clipIds) {
        ClipLocation location;
        if (auto error = editableClip(project, id, &location))
            return error;
        location.clip->enabled = enabled;
    }
    return std::nullopt;
}

Id nearestEdit(const Project& project, Ticks at)
{
    Id best = kNoId;
    Ticks distance = std::numeric_limits<Ticks>::max();
    for (const Track& track : project.tracks) {
        if (track.locked)
            continue;
        for (const Clip& clip : track.clips)
            for (Ticks edge : {clip.start, clip.end()})
                if (std::abs(edge - at) < distance) {
                    distance = std::abs(edge - at);
                    best = clip.id;
                }
    }
    return best;
}

std::optional<std::string> extendEdit(Project& project, Id clipId, Ticks to)
{
    ClipLocation location;
    if (auto error = editableClip(project, clipId, &location))
        return error;
    const Clip clip = *location.clip;
    const Track& track = *location.track;
    const bool endEdge = std::abs(clip.end() - to) <= std::abs(clip.start - to);
    const Ticks edge = endEdge ? clip.end() : clip.start;
    if (edge == to)
        return std::nullopt;
    // The clip on the other side of the edge, if it touches it.
    const Clip* other = nullptr;
    for (const Clip& c : track.clips)
        if (c.id != clip.id && (endEdge ? c.start == edge : c.end() == edge))
            other = &c;
    if (endEdge ? to <= clip.start : to >= clip.end())
        return TVE_TR("core", "That would leave the clip empty");
    if (other && (endEdge ? to >= other->end() : to <= other->start))
        return TVE_TR("core", "That would leave the next clip empty");

    using Edge = TrimClipCommand::Edge;
    const auto trim = [&](Id id, Edge e) -> std::optional<std::string> {
        TrimClipCommand command(id, e, to);
        if (auto error = command.redo(project))
            return (error->find("media") != std::string::npos) ? TVE_TR("core", "There is no more of the media there")
                                                              : TVE_TR("core", "There is another clip in the way");
        return std::nullopt;
    };
    // Make room first: the clip giving time up goes before the one taking it.
    const bool growing = endEdge ? to > edge : to < edge;
    const Edge clipEdge = endEdge ? Edge::End : Edge::Start;
    const Edge otherEdge = endEdge ? Edge::Start : Edge::End;
    if (other && growing) {
        if (auto error = trim(other->id, otherEdge))
            return error;
    }
    if (auto error = trim(clip.id, clipEdge))
        return error;
    if (other && !growing) {
        if (auto error = trim(other->id, otherEdge))
            return error;
    }
    return std::nullopt;
}

std::vector<Id> clipsFrom(const Project& project, Ticks at, int direction, Id trackId)
{
    std::vector<Id> ids;
    for (const Track& track : project.tracks) {
        if (trackId != kNoId ? track.id != trackId : track.locked)
            continue;
        for (const Clip& clip : track.clips)
            if (direction > 0 ? clip.end() > at : clip.start < at)
                ids.push_back(clip.id);
    }
    return ids;
}

std::optional<Ticks> nextEdit(const Project& project, Ticks at, int direction)
{
    std::set<Ticks> points{0};
    for (const Track& track : project.tracks) {
        if (track.hidden && track.kind != TrackKind::Audio)
            continue;
        for (const Clip& clip : track.clips) {
            points.insert(clip.start);
            points.insert(clip.end());
        }
    }
    points.insert(project.duration());
    if (direction > 0) {
        const auto it = points.upper_bound(at);
        if (it == points.end())
            return std::nullopt;
        return *it;
    }
    auto it = points.lower_bound(at);
    if (it == points.begin())
        return std::nullopt;
    return *--it;
}

std::optional<std::string> moveTrack(Project& project, Id trackId, int direction)
{
    const auto index = project.trackIndex(trackId);
    if (!index)
        return TVE_TR("core", "That track is not on the timeline");
    const bool pictures = picturesKind(project.tracks[*index].kind);
    // Tracks are stored bottom first: up is a later index. Skip over the other kind group.
    std::optional<std::size_t> swapWith;
    if (direction > 0) {
        for (std::size_t i = *index + 1; i < project.tracks.size() && !swapWith; ++i)
            if (picturesKind(project.tracks[i].kind) == pictures)
                swapWith = i;
    } else {
        for (std::size_t i = *index; i-- > 0 && !swapWith;)
            if (picturesKind(project.tracks[i].kind) == pictures)
                swapWith = i;
    }
    if (!swapWith)
        return direction > 0 ? TVE_TR("core", "The track is at the top already")
                             : TVE_TR("core", "The track is at the bottom already");
    std::swap(project.tracks[*index], project.tracks[*swapWith]);
    return std::nullopt;
}

int setAllTracksLocked(Project& project, bool locked)
{
    int changed = 0;
    for (Track& track : project.tracks)
        if (track.locked != locked) {
            track.locked = locked;
            ++changed;
        }
    return changed;
}

int removeAllTransitions(Project& project)
{
    int removed = 0;
    eachTrack(project, [&](Track& track) {
        if (track.locked)
            return;
        for (Clip& clip : track.clips)
            if (clip.transitionIn) {
                clip.transitionIn.reset();
                ++removed;
            }
    });
    return removed;
}

int removeAllMarkers(Project& project)
{
    const int removed = int(project.markers.size());
    project.markers.clear();
    return removed;
}

std::optional<std::string> rotateClip(Project& project, Id clipId, double degrees)
{
    ClipLocation location;
    if (auto error = editableClip(project, clipId, &location))
        return error;
    if (location.track->kind == TrackKind::Audio)
        return TVE_TR("core", "Select a picture to turn");
    Clip& clip = *location.clip;
    double& r = clip.visual.transform.rotation;
    r = std::remainder(r + degrees, 360.0);
    if (r <= -180.0)
        r += 360.0;
    if (auto it = clip.keyframes.find(property::kRotation); it != clip.keyframes.end())
        for (Keyframe& k : it->second)
            k.value += degrees;
    return std::nullopt;
}

std::optional<std::string> flipClip(Project& project, Id clipId, bool horizontal)
{
    ClipLocation location;
    if (auto error = editableClip(project, clipId, &location))
        return error;
    if (location.track->kind == TrackKind::Audio)
        return TVE_TR("core", "Select a picture to flip");
    ClipTransform& t = location.clip->visual.transform;
    (horizontal ? t.flipH : t.flipV) = !(horizontal ? t.flipH : t.flipV);
    return std::nullopt;
}

} // namespace tve
