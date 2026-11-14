#include "commands/SequenceEdits.h"

#include "commands/EditCommands.h"
#include "model/Translate.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <map>

namespace tve {

namespace {

// Each compound clip playing `sequenceId` (on the timeline, in any sequence) fitted to the
// sequence's new length.
void fitCompoundClips(Project& project, Id sequenceId, Ticks oldLength, Ticks newLength)
{
    const Ticks frame = frameDuration(project.settings.frameRate);
    const auto fit = [&](Track& track) {
        for (std::size_t i = 0; i < track.clips.size(); ++i) {
            Clip& clip = track.clips[i];
            if (clip.sequenceId != sequenceId || clip.freeze || clip.speed <= 0.0)
                continue;
            const Ticks end = clip.sourceIn + clip.sourceSpan();
            Ticks duration = clip.duration;
            if (end > newLength) {
                const Ticks left = newLength - clip.sourceIn;
                duration = left <= 0 ? frame : std::max<Ticks>(frame, Ticks(std::llround(double(left) / clip.speed)));
            } else if (end == oldLength && newLength > oldLength && clip.speedRamp.empty()) {
                duration = Ticks(std::llround(double(newLength - clip.sourceIn) / clip.speed));
                if (i + 1 < track.clips.size())
                    duration = std::min(duration, track.clips[i + 1].start - clip.start);
            }
            if (duration == clip.duration || duration <= 0)
                continue;
            clip.duration = duration;
            clip.fadeIn = std::min(clip.fadeIn, duration);
            clip.fadeOut = std::min(clip.fadeOut, duration - clip.fadeIn);
        }
    };
    for (Track& track : project.tracks)
        fit(track);
    for (Sequence& sequence : project.sequences)
        for (Track& track : sequence.tracks)
            fit(track);
}

std::string stem(const std::string& path)
{
    return std::filesystem::path(path).stem().string();
}

std::string newTrackName(const Project& project, TrackKind kind)
{
    const auto count = std::ranges::count(project.tracks, kind, &Track::kind);
    return std::string(kind == TrackKind::Audio ? "Audio " : kind == TrackKind::Title ? "Title " : "Video ")
         + std::to_string(count + 1);
}

// A picture track with room for [at, at + length), the lowest first; a new one on top if none.
Id pictureTrackWithRoom(Project& project, Ticks at, Ticks length)
{
    for (const Track& track : project.tracks)
        if (track.kind == TrackKind::Video && !track.locked && !track.overlaps(at, length))
            return track.id;
    Track added{.id = project.allocateId(), .kind = TrackKind::Video, .name = newTrackName(project, TrackKind::Video)};
    project.tracks.push_back(std::move(added));
    return project.tracks.back().id;
}

// Splits the clip on `track` strictly containing `at` (if one does).
std::optional<std::string> splitAt(Project& project, Id trackId, Ticks at)
{
    const Track* track = project.findTrack(trackId);
    for (const Clip& clip : track->clips) {
        if (clip.start < at && at < clip.end()) {
            SplitClipCommand split(clip.id, at);
            return split.redo(project);
        }
    }
    return std::nullopt;
}

} // namespace

// --- A compound clip opened on the timeline ---

std::optional<std::string> openSequence(Project& project, Id compoundClipId, OpenedSequence* opened)
{
    const auto location = project.locateClip(compoundClipId);
    if (!location)
        return TVE_TR("core", "clip not found");
    if (!location->clip->compound())
        return TVE_TR("core", "only a compound clip opens on the timeline");
    const Sequence* sequence = project.findSequence(location->clip->sequenceId);
    if (!sequence)
        return TVE_TR("core", "the compound clip's insides are missing");
    OpenedSequence open{sequence->id, compoundClipId, kNoId, project.markers};
    std::vector<Track> inside = sequence->tracks;
    open.holderId = project.allocateId();
    // The timeline keeps a picture track (the main track) even for a compound clip of sound.
    if (std::ranges::none_of(inside, [](const Track& t) { return t.kind == TrackKind::Video; }))
        inside.insert(inside.begin(), Track{.id = project.allocateId(), .kind = TrackKind::Video, .name = "Video 1"});
    project.sequences.push_back(Sequence{open.holderId, kHolderSequenceName, std::move(project.tracks)});
    project.tracks = std::move(inside);
    project.markers.clear();
    if (opened)
        *opened = std::move(open);
    return std::nullopt;
}

void closeSequence(Project& project, const OpenedSequence& opened)
{
    Sequence* holder = project.findSequence(opened.holderId);
    if (!holder)
        return;
    std::vector<Track> outside = std::move(holder->tracks);
    std::erase_if(project.sequences, [&](const Sequence& s) { return s.id == opened.holderId; });
    Ticks oldLength = 0, newLength = 0;
    if (Sequence* sequence = project.findSequence(opened.sequenceId)) {
        oldLength = sequence->duration();
        sequence->tracks = std::move(project.tracks);
        newLength = sequence->duration();
    }
    project.tracks = std::move(outside);
    project.markers = opened.markers;
    if (newLength != oldLength)
        fitCompoundClips(project, opened.sequenceId, oldLength, newLength);
}

Project withSequencesClosed(Project project, const std::vector<OpenedSequence>& open)
{
    for (auto it = open.rbegin(); it != open.rend(); ++it)
        closeSequence(project, *it);
    return project;
}

// --- Multicam ---

std::optional<std::string> makeMulticam(Project& project, const std::vector<Id>& clipIds, Id* madeClipId)
{
    struct Chosen {
        Clip clip;
        Id trackId;
        std::size_t trackIndex;
        bool picture;
    };
    std::vector<Chosen> chosen;
    for (Id id : clipIds) {
        if (std::ranges::any_of(chosen, [&](const Chosen& c) { return c.clip.id == id; }))
            continue;
        const auto location = project.locateClip(id);
        if (!location)
            return TVE_TR("core", "clip not found");
        if (location->track->locked)
            return TVE_TR("core", "track is locked");
        const Clip& clip = *location->clip;
        if (clip.generated() || clip.multicam())
            return TVE_TR("core", "titles, drawings, adjustment layers and multicam clips cannot be angles");
        const MediaItem* media = project.findMedia(clip.mediaId);
        const bool picture = location->track->kind == TrackKind::Video && (clip.compound() || (media && media->hasVideo));
        if (!picture && location->track->kind != TrackKind::Audio)
            return TVE_TR("core", "only picture and sound clips make a multicam clip");
        chosen.push_back({clip, location->track->id, *project.trackIndex(location->track->id), picture});
    }
    const auto pictures = std::ranges::count(chosen, true, &Chosen::picture);
    if (pictures < 2)
        return TVE_TR("core", "a multicam clip needs two or more picture clips (one per camera)");

    Ticks earliest = chosen.front().clip.start, latest = chosen.front().clip.end();
    std::size_t lowest = project.tracks.size();
    Id target = kNoId;
    for (const Chosen& c : chosen) {
        earliest = std::min(earliest, c.clip.start);
        latest = std::max(latest, c.clip.end());
        if (c.picture && c.trackIndex < lowest) {
            lowest = c.trackIndex;
            target = c.trackId;
        }
    }
    Project after = project;
    for (const Chosen& c : chosen)
        after.findTrack(c.trackId)->removeClip(c.clip.id);
    Track* targetTrack = after.findTrack(target);
    if (targetTrack->overlaps(earliest, latest - earliest))
        return "there is no room for the multicam clip on " + targetTrack->name;

    const auto placed = [&](Clip clip) {
        clip.start -= earliest;
        clip.groupId = kNoId;
        clip.transitionIn.reset();
        return clip;
    };
    Clip multicam;
    int number = 0;
    for (const Chosen& c : chosen) {
        if (!c.picture)
            continue;
        ++number;
        Sequence angle{after.allocateId(), {}, {}};
        if (c.clip.compound()) {
            const Sequence* inside = after.findSequence(c.clip.sequenceId);
            angle.name = inside ? inside->name : "Angle " + std::to_string(number);
        } else {
            angle.name = stem(after.findMedia(c.clip.mediaId)->path);
        }
        angle.tracks.push_back(Track{.id = after.allocateId(), .kind = TrackKind::Video, .name = "Video 1", .clips = {placed(c.clip)}});
        // The sound clips play under every angle (copies of their own).
        int sounds = 0;
        for (const Chosen& s : chosen) {
            if (s.picture)
                continue;
            Clip sound = placed(s.clip);
            if (number > 1)
                sound.id = after.allocateId();
            angle.tracks.push_back(
                Track{.id = after.allocateId(), .kind = TrackKind::Audio, .name = "Audio " + std::to_string(++sounds), .clips = {sound}});
        }
        multicam.angles.push_back(angle.id);
        after.sequences.push_back(std::move(angle));
    }
    multicam.id = after.allocateId();
    multicam.start = earliest;
    multicam.duration = latest - earliest;
    multicam.sequenceId = multicam.angles.front();
    after.findTrack(target)->insertClip(multicam);
    project = std::move(after);
    if (madeClipId)
        *madeClipId = multicam.id;
    return std::nullopt;
}

std::optional<std::string> setAngle(Project& project, Id clipId, int angle)
{
    const auto location = project.locateClip(clipId);
    if (!location)
        return TVE_TR("core", "clip not found");
    Clip& clip = *location->clip;
    if (!clip.multicam())
        return TVE_TR("core", "only a multicam clip has angles");
    if (angle < 0 || angle >= int(clip.angles.size()))
        return "the multicam clip has no angle " + std::to_string(angle + 1);
    if (location->track->locked)
        return TVE_TR("core", "track is locked");
    if (!project.findSequence(clip.angles[std::size_t(angle)]))
        return TVE_TR("core", "that angle is missing");
    clip.sequenceId = clip.angles[std::size_t(angle)];
    return std::nullopt;
}

std::optional<std::string> cutToAngle(Project& project, Id clipId, Ticks at, int angle, Id* rightClipId)
{
    const auto location = project.locateClip(clipId);
    if (!location)
        return TVE_TR("core", "clip not found");
    const Clip& clip = *location->clip;
    if (!clip.multicam())
        return TVE_TR("core", "only a multicam clip has angles");
    if (at < clip.start || at >= clip.end())
        return TVE_TR("core", "the playhead is not over the multicam clip");
    Project after = project;
    Id part = clipId;
    if (at > clip.start) {
        SplitClipCommand split(clipId, at);
        if (auto error = split.redo(after))
            return error;
        part = split.rightClipId();
    }
    if (auto error = setAngle(after, part, angle))
        return error;
    project = std::move(after);
    if (rightClipId)
        *rightClipId = part;
    return std::nullopt;
}

// --- A project as a compound clip ---

std::optional<std::string> importProjectAsCompound(Project& project, const Project& other, const std::string& name,
                                                   Ticks at, Id* madeClipId)
{
    Sequence inside{kNoId, name.empty() ? "Project" : name, other.tracks};
    if (inside.duration() <= 0)
        return TVE_TR("core", "that project's timeline is empty");
    Project after = project;
    // Its ids come after this project's.
    const Id offset = after.nextId();
    const auto renumber = [&](Id id) { return id == kNoId ? kNoId : id + offset; };
    std::map<Id, Id> media;
    for (const MediaItem& item : other.media) {
        const auto same = std::ranges::find(after.media, item.path, &MediaItem::path);
        if (same != after.media.end() && !item.path.empty()) {
            media[item.id] = same->id;
            continue;
        }
        MediaItem copy = item;
        copy.id = renumber(item.id);
        media[item.id] = copy.id;
        after.media.push_back(std::move(copy));
    }
    const auto renumberTracks = [&](std::vector<Track>& tracks) {
        for (Track& track : tracks) {
            track.id = renumber(track.id);
            for (Clip& clip : track.clips) {
                clip.id = renumber(clip.id);
                if (clip.mediaId != kNoId)
                    clip.mediaId = media.contains(clip.mediaId) ? media[clip.mediaId] : kNoId;
                clip.sequenceId = renumber(clip.sequenceId);
                clip.groupId = renumber(clip.groupId);
                for (Id& angle : clip.angles)
                    angle = renumber(angle);
            }
        }
    };
    for (const Sequence& sequence : other.sequences) {
        if (sequence.name == kHolderSequenceName)
            continue;
        Sequence copy = sequence;
        copy.id = renumber(sequence.id);
        renumberTracks(copy.tracks);
        after.sequences.push_back(std::move(copy));
    }
    renumberTracks(inside.tracks);
    after.setNextId(std::max(after.nextId(), other.nextId() + offset));
    inside.id = after.allocateId();
    const Ticks length = inside.duration();
    Clip compound;
    compound.id = after.allocateId();
    compound.sequenceId = inside.id;
    compound.start = std::max<Ticks>(at, 0);
    compound.duration = length;
    after.sequences.push_back(std::move(inside));
    const Id target = pictureTrackWithRoom(after, compound.start, length);
    after.findTrack(target)->insertClip(compound);
    project = std::move(after);
    if (madeClipId)
        *madeClipId = compound.id;
    return std::nullopt;
}

// --- From the source monitor ---

std::optional<std::string> placeMedia(Project& project, Id trackId, Id mediaId, Ticks sourceIn, Ticks duration,
                                      Ticks at, PlaceMode mode, Id* madeClipId)
{
    const MediaItem* media = project.findMedia(mediaId);
    if (!media)
        return TVE_TR("core", "media not found");
    const Track* track = project.findTrack(trackId);
    if (!track)
        return TVE_TR("core", "track not found");
    if (track->locked)
        return TVE_TR("core", "track is locked");
    if (duration <= 0 || sourceIn < 0 || (!media->still && sourceIn + duration > media->duration))
        return TVE_TR("core", "choose a part of the media to place (In before Out)");
    at = std::max<Ticks>(at, 0);
    Project after = project;
    if (mode == PlaceMode::Append) {
        at = track->clips.empty() ? 0 : track->clips.back().end();
    } else if (mode == PlaceMode::Insert) {
        if (auto error = splitAt(after, trackId, at))
            return error;
        for (Clip& clip : after.findTrack(trackId)->clips)
            if (clip.start >= at)
                clip.start += duration;
    } else {
        for (Ticks edge : {at, at + duration})
            if (auto error = splitAt(after, trackId, edge))
                return error;
        std::erase_if(after.findTrack(trackId)->clips,
                      [&](const Clip& clip) { return clip.start >= at && clip.end() <= at + duration; });
    }
    AddClipCommand add(trackId, mediaId, at, duration, sourceIn);
    if (auto error = add.redo(after))
        return error;
    project = std::move(after);
    if (madeClipId)
        *madeClipId = add.clipId();
    return std::nullopt;
}

std::optional<std::string> insertTime(Project& project, Ticks at, Ticks length)
{
    if (length <= 0)
        return std::nullopt;
    Project after = project;
    for (Track& track : after.tracks) {
        const bool moves = std::ranges::any_of(track.clips, [&](const Clip& c) { return c.start >= at; });
        if (!moves)
            continue;
        if (track.locked)
            return TVE_TR("core", "track is locked");
        for (Clip& clip : track.clips)
            if (clip.start >= at)
                clip.start += length;
    }
    for (Marker& marker : after.markers)
        if (marker.time >= at)
            marker.time += length;
    project = std::move(after);
    return std::nullopt;
}

std::optional<std::string> fitClipToPieces(Project& project, Id clipId, const MusicFit& fit, std::vector<Id>* made)
{
    constexpr Ticks kJoinFade = kTicksPerSecond / 100; // no click at a join
    Project after = project;
    Track* track = nullptr;
    const Clip* found = nullptr;
    for (Track& t : after.tracks)
        if (const auto c = std::ranges::find(t.clips, clipId, &Clip::id); c != t.clips.end()) {
            track = &t;
            found = &*c;
        }
    if (!found)
        return TVE_TR("core", "clip not found");
    if (track->locked)
        return TVE_TR("core", "track is locked");
    if (found->speed != 1.0 || found->reversed || found->freeze || !found->speedRamp.empty())
        return TVE_TR("core", "music can be fitted only at normal speed");
    if (fit.pieces.empty() || fit.speed <= 0)
        return TVE_TR("core", "nothing to fit");
    const Clip original = *found;
    std::vector<Clip> pieces;
    Ticks at = original.start;
    const Id group = original.groupId != kNoId ? original.groupId : (fit.pieces.size() > 1 ? after.allocateId() : kNoId);
    for (std::size_t i = 0; i < fit.pieces.size(); ++i) {
        const auto [from, to] = fit.pieces[i];
        Clip clip = original;
        clip.id = i == 0 ? original.id : after.allocateId();
        clip.start = at;
        clip.sourceIn = from;
        clip.duration = std::max<Ticks>(1, Ticks(std::llround(double(to - from) / fit.speed)));
        clip.speed = fit.speed;
        clip.keepPitch = true;
        clip.keyframes.clear();
        clip.groupId = group;
        const bool first = i == 0, last = i + 1 == fit.pieces.size();
        clip.fadeIn = first ? original.fadeIn : kJoinFade;
        clip.fadeOut = last ? std::max(original.fadeOut, fit.fadeOut) : kJoinFade;
        if (!first)
            clip.transitionIn.reset();
        at += clip.duration;
        pieces.push_back(std::move(clip));
    }
    std::erase_if(track->clips, [&](const Clip& c) { return c.id == clipId; });
    for (const Clip& c : track->clips)
        if (c.start < at && c.end() > original.start)
            return TVE_TR("core", "there is not room for the music on its track");
    if (made)
        for (const Clip& c : pieces)
            made->push_back(c.id);
    for (Clip& c : pieces)
        track->clips.push_back(std::move(c));
    std::ranges::sort(track->clips, {}, &Clip::start);
    project = std::move(after);
    return std::nullopt;
}

} // namespace tve
