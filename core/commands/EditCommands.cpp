#include "commands/EditCommands.h"

#include <algorithm>
#include <cassert>
#include <iterator>

namespace tve {

namespace {

std::optional<std::string> checkEditable(const Track* track)
{
    if (!track)
        return "track not found";
    if (track->locked)
        return "track is locked";
    return std::nullopt;
}

std::optional<std::string> checkMediaFitsTrack(const MediaItem& media, const Track& track)
{
    if (track.kind == TrackKind::Audio && !media.hasAudio)
        return "this media has no sound for an audio track";
    if (track.kind == TrackKind::Video && !media.hasVideo)
        return "audio-only media goes on an audio track";
    return std::nullopt;
}

// Keeps fades inside the clip after its duration changes, shortening the fade-out first.
void clampFades(Clip& clip)
{
    clip.fadeIn = std::min(clip.fadeIn, clip.duration);
    clip.fadeOut = std::min(clip.fadeOut, clip.duration - clip.fadeIn);
}

// Shifts every clip on the track that starts at or after `from` by `delta`.
void shiftClipsFrom(Track& track, Ticks from, Ticks delta)
{
    for (Clip& clip : track.clips) {
        if (clip.start >= from)
            clip.start += delta;
    }
}

} // namespace

// --- AddTrackCommand ---

std::optional<std::string> AddTrackCommand::redo(Project& project)
{
    if (m_trackId == kNoId)
        m_trackId = project.allocateId();
    project.tracks.push_back(Track{.id = m_trackId, .kind = m_kind, .name = m_name});
    return std::nullopt;
}

void AddTrackCommand::undo(Project& project)
{
    std::erase_if(project.tracks, [&](const Track& track) { return track.id == m_trackId; });
}

// --- ImportMediaCommand ---

std::optional<std::string> ImportMediaCommand::redo(Project& project)
{
    if (m_item.path.empty())
        return "media path is empty";
    if (m_item.duration <= 0)
        return "media duration must be positive";
    if (m_item.id == kNoId)
        m_item.id = project.allocateId();
    project.media.push_back(m_item);
    return std::nullopt;
}

void ImportMediaCommand::undo(Project& project)
{
    std::erase_if(project.media, [&](const MediaItem& item) { return item.id == m_item.id; });
}

// --- TrimClipCommand ---

std::optional<std::string> TrimClipCommand::redo(Project& project)
{
    auto location = project.locateClip(m_clipId);
    if (!location)
        return "clip not found";
    if (auto error = checkEditable(location->track))
        return error;
    const Clip& clip = *location->clip;
    const MediaItem* media = project.findMedia(clip.mediaId);
    if (!media)
        return "media not found";

    Clip trimmed = clip;
    if (m_edge == Edge::Start) {
        const Ticks delta = m_newTime - clip.start;
        trimmed.start = m_newTime;
        trimmed.duration = clip.duration - delta;
        // Forward clips lose (or regain) source at their start; reversed ones at the top
        // of their span, which needs no in-point change.
        if (!clip.freeze && !clip.reversed)
            trimmed.sourceIn = clip.sourceIn + Ticks(std::llround(double(delta) * clip.speed));
    } else {
        trimmed.duration = m_newTime - clip.start;
        if (!clip.freeze && clip.reversed)
            trimmed.sourceIn = clip.sourceIn + Ticks(std::llround(double(clip.duration - trimmed.duration) * clip.speed));
    }
    if (trimmed.duration <= 0)
        return "trim would leave the clip empty";
    clampFades(trimmed);
    const bool pastMedia = trimmed.freeze ? trimmed.sourceIn >= media->duration
                                          : trimmed.sourceIn + trimmed.sourceSpan() > media->duration;
    if (trimmed.start < 0 || trimmed.sourceIn < 0 || pastMedia)
        return "trim goes past the media's start or end";
    if (location->track->overlaps(trimmed.start, trimmed.duration, m_clipId))
        return "trim would overlap a neighboring clip";

    m_trackId = location->track->id;
    m_original = clip;
    location->track->removeClip(m_clipId);
    location->track->insertClip(trimmed);
    return std::nullopt;
}

void TrimClipCommand::undo(Project& project)
{
    Track* track = project.findTrack(m_trackId);
    assert(track);
    track->removeClip(m_clipId);
    track->insertClip(m_original);
}

// --- AddClipCommand ---

std::optional<std::string> AddClipCommand::redo(Project& project)
{
    Track* track = project.findTrack(m_trackId);
    if (auto error = checkEditable(track))
        return error;
    const MediaItem* media = project.findMedia(m_mediaId);
    if (!media)
        return "media not found";
    if (m_start < 0 || m_duration <= 0 || m_sourceIn < 0)
        return "clip times are out of range";
    if (m_sourceIn + m_duration > media->duration)
        return "clip extends past the end of its media";
    if (auto error = checkMediaFitsTrack(*media, *track))
        return error;
    if (track->overlaps(m_start, m_duration))
        return "clip overlaps an existing clip";

    if (m_clipId == kNoId)
        m_clipId = project.allocateId();
    track->insertClip(Clip{.id = m_clipId, .mediaId = m_mediaId, .start = m_start, .duration = m_duration, .sourceIn = m_sourceIn});
    return std::nullopt;
}

void AddClipCommand::undo(Project& project)
{
    Track* track = project.findTrack(m_trackId);
    assert(track);
    track->removeClip(m_clipId);
}

// --- MoveClipCommand ---

std::optional<std::string> MoveClipCommand::redo(Project& project)
{
    auto location = project.locateClip(m_clipId);
    if (!location)
        return "clip not found";
    Track* from = location->track;
    Track* to = project.findTrack(m_toTrackId);
    if (auto error = checkEditable(from))
        return error;
    if (auto error = checkEditable(to))
        return error;
    if (from->kind != to->kind)
        return "clips can only move between tracks of the same kind";
    if (m_newStart < 0)
        return "clip cannot start before zero";
    if (to->overlaps(m_newStart, location->clip->duration, m_clipId))
        return "clip overlaps an existing clip";

    m_fromTrackId = from->id;
    m_oldStart = location->clip->start;
    m_ownTransition.reset();
    m_followerId = kNoId;
    m_followerTransition.reset();
    const bool moves = m_toTrackId != from->id || m_newStart != m_oldStart;
    if (moves) {
        m_ownTransition = std::exchange(location->clip->transitionIn, std::nullopt);
        auto follower = std::ranges::find(from->clips, location->clip->end(), &Clip::start);
        if (follower != from->clips.end() && follower->transitionIn) {
            m_followerId = follower->id;
            m_followerTransition = std::exchange(follower->transitionIn, std::nullopt);
        }
    }
    Clip clip = *from->removeClip(m_clipId);
    clip.start = m_newStart;
    to->insertClip(std::move(clip));
    return std::nullopt;
}

void MoveClipCommand::undo(Project& project)
{
    Track* to = project.findTrack(m_toTrackId);
    Track* from = project.findTrack(m_fromTrackId);
    assert(to && from);
    Clip clip = *to->removeClip(m_clipId);
    clip.start = m_oldStart;
    clip.transitionIn = m_ownTransition;
    from->insertClip(std::move(clip));
    if (m_followerId != kNoId)
        from->findClip(m_followerId)->transitionIn = m_followerTransition;
}

// --- SplitClipCommand ---

std::optional<std::string> SplitClipCommand::redo(Project& project)
{
    auto location = project.locateClip(m_clipId);
    if (!location)
        return "clip not found";
    if (auto error = checkEditable(location->track))
        return error;
    Clip& clip = *location->clip;
    if (m_at <= clip.start || m_at >= clip.end())
        return "split point must be inside the clip";

    m_trackId = location->track->id;
    m_original = clip;
    if (m_rightClipId == kNoId)
        m_rightClipId = project.allocateId();

    const Ticks leftDuration = m_at - clip.start;
    const Ticks rightDuration = clip.duration - leftDuration;
    Clip right{
        .id = m_rightClipId,
        .mediaId = clip.mediaId,
        .start = m_at,
        .duration = rightDuration,
        .sourceIn = clip.sourceIn,
    };
    right.speed = clip.speed;
    right.reversed = clip.reversed;
    right.freeze = clip.freeze;
    if (!clip.freeze) {
        // Forward: the right part continues further into the source. Reversed: the left
        // part keeps the top of the span and the right part the bottom.
        const Ticks leftSpan = Ticks(std::llround(double(leftDuration) * clip.speed));
        const Ticks rightSpan = Ticks(std::llround(double(rightDuration) * clip.speed));
        if (clip.reversed)
            clip.sourceIn += rightSpan;
        else
            right.sourceIn = clip.sourceIn + leftSpan;
    }
    right.audioEnabled = clip.audioEnabled;
    right.gainDb = clip.gainDb;
    right.visual = clip.visual;
    right.effects = clip.effects;
    right.fadeOut = clip.fadeOut;
    clip.duration = leftDuration;
    clip.fadeOut = 0;
    clampFades(clip);
    clampFades(right);
    location->track->insertClip(std::move(right));
    return std::nullopt;
}

void SplitClipCommand::undo(Project& project)
{
    Track* track = project.findTrack(m_trackId);
    assert(track);
    track->removeClip(m_rightClipId);
    *track->findClip(m_clipId) = m_original;
}

// --- DeleteClipCommand ---

std::optional<std::string> DeleteClipCommand::redo(Project& project)
{
    auto location = project.locateClip(m_clipId);
    if (!location)
        return "clip not found";
    if (auto error = checkEditable(location->track))
        return error;

    m_trackId = location->track->id;
    m_followerId = kNoId;
    m_followerTransition.reset();
    Track& track = *location->track;
    auto follower = std::ranges::find(track.clips, location->clip->end(), &Clip::start);
    if (follower != track.clips.end() && follower->transitionIn) {
        m_followerId = follower->id;
        m_followerTransition = std::exchange(follower->transitionIn, std::nullopt);
    }
    m_removed = *location->track->removeClip(m_clipId);
    if (m_ripple)
        shiftClipsFrom(*location->track, m_removed.end(), -m_removed.duration);
    return std::nullopt;
}

void DeleteClipCommand::undo(Project& project)
{
    Track* track = project.findTrack(m_trackId);
    assert(track);
    if (m_ripple)
        shiftClipsFrom(*track, m_removed.start, m_removed.duration);
    track->insertClip(m_removed);
    if (m_followerId != kNoId)
        track->findClip(m_followerId)->transitionIn = m_followerTransition;
}

// --- SetClipAudioCommand ---

std::optional<std::string> SetClipAudioCommand::redo(Project& project)
{
    auto location = project.locateClip(m_clipId);
    if (!location)
        return "clip not found";
    if (auto error = checkEditable(location->track))
        return error;
    Clip& clip = *location->clip;
    if (m_audio.gainDb < kMinGainDb || m_audio.gainDb > kMaxGainDb)
        return "volume is out of range";
    if (m_audio.fadeIn < 0 || m_audio.fadeOut < 0 || m_audio.fadeIn + m_audio.fadeOut > clip.duration)
        return "fades must fit inside the clip";
    m_previous = {clip.gainDb, clip.fadeIn, clip.fadeOut};
    clip.gainDb = m_audio.gainDb;
    clip.fadeIn = m_audio.fadeIn;
    clip.fadeOut = m_audio.fadeOut;
    return std::nullopt;
}

void SetClipAudioCommand::undo(Project& project)
{
    Clip& clip = *project.locateClip(m_clipId)->clip;
    clip.gainDb = m_previous.gainDb;
    clip.fadeIn = m_previous.fadeIn;
    clip.fadeOut = m_previous.fadeOut;
}

// --- SetTrackFlagCommand ---

std::string SetTrackFlagCommand::label() const
{
    switch (m_flag) {
    case Flag::Locked: return m_value ? "Lock Track" : "Unlock Track";
    case Flag::Hidden: return m_value ? "Hide Track" : "Show Track";
    case Flag::Muted: return m_value ? "Mute Track" : "Unmute Track";
    }
    return "Change Track";
}

namespace {

bool& trackFlag(Track& track, SetTrackFlagCommand::Flag flag)
{
    switch (flag) {
    case SetTrackFlagCommand::Flag::Locked: return track.locked;
    case SetTrackFlagCommand::Flag::Hidden: return track.hidden;
    case SetTrackFlagCommand::Flag::Muted: return track.muted;
    }
    return track.locked;
}

} // namespace

std::optional<std::string> SetTrackFlagCommand::redo(Project& project)
{
    Track* track = project.findTrack(m_trackId);
    if (!track)
        return "track not found";
    if (m_flag == Flag::Hidden && track->kind == TrackKind::Audio)
        return "audio tracks can be muted, not hidden";
    bool& flag = trackFlag(*track, m_flag);
    m_previous = flag;
    flag = m_value;
    return std::nullopt;
}

void SetTrackFlagCommand::undo(Project& project)
{
    trackFlag(*project.findTrack(m_trackId), m_flag) = m_previous;
}

// --- DetachAudioCommand ---

std::optional<std::string> DetachAudioCommand::redo(Project& project)
{
    auto location = project.locateClip(m_clipId);
    if (!location)
        return "clip not found";
    if (auto error = checkEditable(location->track))
        return error;
    Track* audioTrack = project.findTrack(m_audioTrackId);
    if (auto error = checkEditable(audioTrack))
        return error;
    const Clip& clip = *location->clip;
    const MediaItem* media = project.findMedia(clip.mediaId);
    if (location->track->kind != TrackKind::Video || audioTrack->kind != TrackKind::Audio)
        return "detach moves sound from a video clip to an audio track";
    if (!media || !media->hasAudio)
        return "this clip has no sound";
    if (!clip.audioEnabled)
        return "this clip's sound is already detached";
    if (clip.freeze)
        return "a freeze frame has no sound";
    if (audioTrack->overlaps(clip.start, clip.duration))
        return "the audio track is not free at this clip's position";

    if (m_audioClipId == kNoId)
        m_audioClipId = project.allocateId();
    Clip audio = clip;
    audio.id = m_audioClipId;
    location->clip->audioEnabled = false;
    audioTrack->insertClip(std::move(audio));
    return std::nullopt;
}

void DetachAudioCommand::undo(Project& project)
{
    for (Track& track : project.tracks)
        track.removeClip(m_audioClipId);
    project.locateClip(m_clipId)->clip->audioEnabled = true;
}

// --- SetClipVisualCommand ---

std::optional<std::string> SetClipVisualCommand::redo(Project& project)
{
    auto location = project.locateClip(m_clipId);
    if (!location)
        return "clip not found";
    if (auto error = checkEditable(location->track))
        return error;
    if (location->track->kind != TrackKind::Video)
        return "only video clips have a picture";
    const ClipVisual& v = m_visual;
    if (v.transform.scale < 0.01 || v.transform.scale > 20.0)
        return "scale must be between 1% and 2000%";
    if (v.opacity < 0.0 || v.opacity > 1.0)
        return "opacity must be between 0 and 100%";
    const auto inRange = [](double c) { return c >= 0.0 && c <= 0.9; };
    if (!inRange(v.crop.left) || !inRange(v.crop.right) || !inRange(v.crop.top) || !inRange(v.crop.bottom)
        || v.crop.left + v.crop.right > 0.95 || v.crop.top + v.crop.bottom > 0.95)
        return "crop would leave too little of the picture";
    m_previous = location->clip->visual;
    location->clip->visual = m_visual;
    return std::nullopt;
}

void SetClipVisualCommand::undo(Project& project)
{
    project.locateClip(m_clipId)->clip->visual = m_previous;
}

// --- SetClipEffectsCommand ---

std::optional<std::string> SetClipEffectsCommand::redo(Project& project)
{
    auto location = project.locateClip(m_clipId);
    if (!location)
        return "clip not found";
    if (auto error = checkEditable(location->track))
        return error;
    if (location->track->kind != TrackKind::Video)
        return "effects apply to video clips";
    for (const ClipEffect& effect : m_effects) {
        if (effect.effectId.empty())
            return "effect id is empty";
    }
    m_previous = location->clip->effects;
    location->clip->effects = m_effects;
    return std::nullopt;
}

void SetClipEffectsCommand::undo(Project& project)
{
    project.locateClip(m_clipId)->clip->effects = m_previous;
}

// --- SetProjectSettingsCommand ---

std::optional<std::string> SetProjectSettingsCommand::redo(Project& project)
{
    const ProjectSettings& s = m_settings;
    if (s.width < 16 || s.height < 16 || s.width > 16384 || s.height > 16384)
        return "canvas size is out of range";
    if (s.frameRate.num <= 0 || s.frameRate.den <= 0)
        return "frame rate must be positive";
    m_previous = project.settings;
    project.settings = m_settings;
    return std::nullopt;
}

void SetProjectSettingsCommand::undo(Project& project)
{
    project.settings = m_previous;
}

// --- SetClipSpeedCommand ---

std::optional<std::string> SetClipSpeedCommand::redo(Project& project)
{
    auto location = project.locateClip(m_clipId);
    if (!location)
        return "clip not found";
    if (auto error = checkEditable(location->track))
        return error;
    Clip& clip = *location->clip;
    if (clip.freeze)
        return "a freeze frame has no speed";
    if (m_speed < kMinSpeed || m_speed > kMaxSpeed)
        return "speed must be between 0.1× and 10×";

    // The clip keeps playing the same source; its length on the timeline changes, and later
    // clips on the track move with its end.
    const Ticks newDuration = std::max<Ticks>(1, Ticks(std::llround(double(clip.sourceSpan()) / m_speed)));
    m_trackId = location->track->id;
    m_original = clip;
    m_delta = newDuration - clip.duration;
    const Ticks oldEnd = clip.end();
    clip.speed = m_speed;
    clip.reversed = m_reversed;
    clip.duration = newDuration;
    clampFades(clip);
    shiftClipsFrom(*location->track, oldEnd, m_delta);
    return std::nullopt;
}

void SetClipSpeedCommand::undo(Project& project)
{
    Track* track = project.findTrack(m_trackId);
    assert(track);
    shiftClipsFrom(*track, m_original.end() + m_delta, -m_delta);
    *track->findClip(m_clipId) = m_original;
}

// --- FreezeFrameCommand ---

std::optional<std::string> FreezeFrameCommand::redo(Project& project)
{
    auto location = project.locateClip(m_clipId);
    if (!location)
        return "clip not found";
    Track& track = *location->track;
    if (auto error = checkEditable(&track))
        return error;
    if (track.kind != TrackKind::Video)
        return "freeze frames are made from video clips";
    const Clip clip = *location->clip;
    if (m_at < clip.start || m_at >= clip.end())
        return "put the playhead inside the clip to freeze a frame";
    if (m_hold <= 0)
        return "the freeze must last some time";

    m_trackId = track.id;
    m_original = clip;
    if (m_rightClipId == kNoId && m_at > clip.start)
        m_rightClipId = project.allocateId();
    if (m_freezeClipId == kNoId)
        m_freezeClipId = project.allocateId();

    // Split at the playhead (unless it is at the clip's start), push everything from there
    // right by the hold, and put the still in the gap.
    if (m_at > clip.start) {
        SplitClipCommand split(m_clipId, m_at);
        split.setRightClipId(m_rightClipId);
        if (auto error = split.redo(project))
            return error;
    }
    shiftClipsFrom(track, m_at, m_hold);
    Clip still = clip;
    still.id = m_freezeClipId;
    still.start = m_at;
    still.duration = m_hold;
    still.sourceIn = clip.sourceTimeAt(m_at);
    still.freeze = true;
    still.speed = 1.0;
    still.reversed = false;
    still.audioEnabled = false;
    still.fadeIn = still.fadeOut = 0;
    track.insertClip(std::move(still));
    return std::nullopt;
}

void FreezeFrameCommand::undo(Project& project)
{
    Track& track = *project.findTrack(m_trackId);
    track.removeClip(m_freezeClipId);
    shiftClipsFrom(track, m_at + m_hold, -m_hold);
    if (m_at > m_original.start)
        track.removeClip(m_rightClipId);
    *track.findClip(m_clipId) = m_original;
}

// --- SetClipTransitionCommand ---

std::string SetClipTransitionCommand::label() const
{
    if (!m_transition)
        return "Remove Transition";
    return m_previous ? "Change Transition" : "Add Transition";
}

std::optional<std::string> SetClipTransitionCommand::redo(Project& project)
{
    auto location = project.locateClip(m_clipId);
    if (!location)
        return "clip not found";
    if (auto error = checkEditable(location->track))
        return error;
    Clip& clip = *location->clip;
    if (m_transition) {
        if (location->track->kind != TrackKind::Video)
            return "transitions go between video clips";
        if (m_transition->transitionId.empty())
            return "no transition chosen";
        if (m_transition->duration < kMinTransitionDuration || m_transition->duration > kMaxTransitionDuration)
            return "transition length is out of range";
        const auto& clips = location->track->clips;
        const bool touching = std::ranges::any_of(clips, [&](const Clip& c) { return c.end() == clip.start; });
        if (!touching)
            return "a transition needs a clip ending where this one starts";
    }
    m_previous = clip.transitionIn;
    clip.transitionIn = m_transition;
    return std::nullopt;
}

void SetClipTransitionCommand::undo(Project& project)
{
    project.locateClip(m_clipId)->clip->transitionIn = m_previous;
}

} // namespace tve
