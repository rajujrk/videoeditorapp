#include "commands/EditCommands.h"

#include <algorithm>
#include <cassert>
#include <cmath>
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

// Keeps keyframes over the same moments of a clip whose start moves by `delta` on the timeline.
// Every keyframe list of a clip: its properties' and its effects' parameters'.
template <typename F>
void forEachKeyframes(Clip& clip, F f)
{
    for (auto& [id, keys] : clip.keyframes)
        f(keys);
    for (ClipEffect& effect : clip.effects) {
        for (auto& [id, keys] : effect.keyframes)
            f(keys);
    }
}

void shiftKeyframes(Clip& clip, Ticks delta)
{
    forEachKeyframes(clip, [&](Keyframes& keys) {
        for (Keyframe& k : keys)
            k.time -= delta;
    });
}

// Stretches keyframe times with a clip whose length changes by `factor` (speed changes).
void scaleKeyframes(Clip& clip, double factor)
{
    forEachKeyframes(clip, [&](Keyframes& keys) {
        for (Keyframe& k : keys)
            k.time = Ticks(std::llround(double(k.time) * factor));
        // Rounding cannot reorder them, but it can merge neighbours.
        keys.erase(std::unique(keys.begin(), keys.end(), [](const Keyframe& a, const Keyframe& b) { return a.time == b.time; }),
                   keys.end());
    });
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

// --- RelinkMediaCommand ---

std::optional<std::string> RelinkMediaCommand::redo(Project& project)
{
    if (m_items.empty())
        return "no media to relink";
    for (const MediaItem& item : m_items) {
        if (item.path.empty() || item.duration <= 0)
            return "the new file has no pictures or sound";
        if (!project.findMedia(item.id))
            return "media not found";
    }
    m_previous.clear();
    for (const MediaItem& item : m_items) {
        auto media = std::ranges::find(project.media, item.id, &MediaItem::id);
        m_previous.push_back(*media);
        *media = item;
    }
    return std::nullopt;
}

void RelinkMediaCommand::undo(Project& project)
{
    for (const MediaItem& item : m_previous)
        *std::ranges::find(project.media, item.id, &MediaItem::id) = item;
}

// --- AddVoiceOverCommand ---

std::optional<std::string> AddVoiceOverCommand::redo(Project& project)
{
    if (m_item.path.empty() || m_item.duration <= 0 || !m_item.hasAudio)
        return "the recording has no sound";
    if (m_start < 0)
        return "clip times are out of range";
    Track* track = nullptr;
    if (m_trackId != kNoId) {
        track = project.findTrack(m_trackId);
        if (auto error = checkEditable(track))
            return error;
        if (track->kind != TrackKind::Audio)
            return "voice-overs go on audio tracks";
        if (track->overlaps(m_start, m_item.duration))
            return "clip overlaps an existing clip";
    }

    if (m_item.id == kNoId)
        m_item.id = project.allocateId();
    if (m_clipId == kNoId)
        m_clipId = project.allocateId();
    if (!track) {
        // Tracks are stored bottom to top: the new one goes under everything.
        if (m_newTrackId == kNoId)
            m_newTrackId = project.allocateId();
        const auto existing = std::ranges::count_if(
            project.tracks, [](const Track& t) { return t.name.starts_with("Voice-over"); });
        std::string name = existing == 0 ? "Voice-over" : "Voice-over " + std::to_string(existing + 1);
        project.tracks.insert(project.tracks.begin(),
                              Track{.id = m_newTrackId, .kind = TrackKind::Audio, .name = std::move(name)});
        track = &project.tracks.front();
    }
    project.media.push_back(m_item);
    track->insertClip(Clip{.id = m_clipId, .mediaId = m_item.id, .start = m_start, .duration = m_item.duration});
    return std::nullopt;
}

void AddVoiceOverCommand::undo(Project& project)
{
    if (m_trackId == kNoId)
        std::erase_if(project.tracks, [&](const Track& t) { return t.id == m_newTrackId; });
    else
        project.findTrack(m_trackId)->removeClip(m_clipId);
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
    if (!project.findMedia(clip.mediaId) && !clip.generated() && !clip.compound())
        return "media not found";

    Clip trimmed = clip;
    if (m_edge == Edge::Start) {
        const Ticks delta = m_newTime - clip.start;
        trimmed.start = m_newTime;
        trimmed.duration = clip.duration - delta;
        // Forward clips lose (or regain) source at their start; reversed ones at the top
        // of their span, which needs no in-point change.
        if (!clip.freeze && !clip.reversed && project.sourceLimit(clip)) // a still shows the same picture throughout
            trimmed.sourceIn = clip.sourceIn + Ticks(std::llround(double(delta) * clip.speed));
        shiftKeyframes(trimmed, delta);
    } else {
        trimmed.duration = m_newTime - clip.start;
        if (!clip.freeze && clip.reversed)
            trimmed.sourceIn = clip.sourceIn + Ticks(std::llround(double(clip.duration - trimmed.duration) * clip.speed));
    }
    if (trimmed.duration <= 0)
        return "trim would leave the clip empty";
    clampFades(trimmed);
    // Titles, adjustment layers and stills can be any length; compound clips as long as
    // their sequence.
    const std::optional<Ticks> limit = project.sourceLimit(clip);
    const bool pastMedia = !limit ? false
                         : trimmed.freeze ? trimmed.sourceIn >= *limit
                                          : trimmed.sourceIn + trimmed.sourceSpan() > *limit;
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
    if (m_sourceIn + m_duration > media->duration && !media->still)
        return "clip extends past the end of its media";
    if (auto error = checkMediaFitsTrack(*media, *track))
        return error;
    if (track->overlaps(m_start, m_duration))
        return "clip overlaps an existing clip";

    if (m_clipId == kNoId)
        m_clipId = project.allocateId();
    track->insertClip(Clip{.id = m_clipId, .mediaId = m_mediaId, .start = m_start, .duration = m_duration,
                           .sourceIn = m_sourceIn, .visual = m_visual});
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
    right.keepPitch = clip.keepPitch;
    if (!clip.freeze) {
        // Forward: the right part continues further into the source. Reversed: the left
        // part keeps the top of the span and the right part the bottom.
        Ticks leftSpan = Ticks(std::llround(double(leftDuration) * clip.speed));
        Ticks rightSpan = Ticks(std::llround(double(rightDuration) * clip.speed));
        if (!clip.speedRamp.empty()) {
            // Each half keeps its part of the ramp, so every frame stays where it was.
            const double u = double(leftDuration) / double(clip.duration);
            const Ticks span = clip.sourceSpan();
            leftSpan = Ticks(std::llround(double(span) * rampProgress(clip.speedRamp, u)));
            rightSpan = span - leftSpan;
            right.speedRamp = subRamp(clip.speedRamp, u, 1.0);
            clip.speedRamp = subRamp(clip.speedRamp, 0.0, u);
            right.speed = double(rightSpan) / double(rightDuration);
            clip.speed = double(leftSpan) / double(leftDuration);
        }
        if (clip.reversed)
            clip.sourceIn += rightSpan;
        else
            right.sourceIn = clip.sourceIn + leftSpan;
    }
    right.audioEnabled = clip.audioEnabled;
    right.gainDb = clip.gainDb;
    right.audioEffects = clip.audioEffects;
    right.enhancedAudio = clip.enhancedAudio;
    right.visual = clip.visual;
    right.effects = clip.effects;
    right.color = clip.color;
    right.look = clip.look;
    right.chromaKey = clip.chromaKey;
    right.masks = clip.masks;
    right.matte = clip.matte;
    right.cutout = clip.cutout;
    right.faceMosaic = clip.faceMosaic; // face tracks are in media time: the same for both halves
    right.text = clip.text;
    right.adjustment = clip.adjustment;
    right.sequenceId = clip.sequenceId;
    right.groupId = clip.groupId;
    right.keyframes = clip.keyframes; // both halves animate as the whole did
    shiftKeyframes(right, leftDuration);
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

std::optional<std::string> validateVisual(const ClipVisual& v)
{
    if (v.transform.scale < 0.01 || v.transform.scale > 20.0)
        return "scale must be between 1% and 2000%";
    if (v.opacity < 0.0 || v.opacity > 1.0)
        return "opacity must be between 0 and 100%";
    const auto inRange = [](double c) { return c >= 0.0 && c <= 0.9; };
    if (!inRange(v.crop.left) || !inRange(v.crop.right) || !inRange(v.crop.top) || !inRange(v.crop.bottom)
        || v.crop.left + v.crop.right > 0.95 || v.crop.top + v.crop.bottom > 0.95)
        return "crop would leave too little of the picture";
    return std::nullopt;
}

std::optional<std::string> SetClipVisualCommand::redo(Project& project)
{
    auto location = project.locateClip(m_clipId);
    if (!location)
        return "clip not found";
    if (auto error = checkEditable(location->track))
        return error;
    if (location->track->kind != TrackKind::Video)
        return "only video clips have a picture";
    if (auto error = validateVisual(m_visual))
        return error;
    m_previous = location->clip->visual;
    location->clip->visual = m_visual;
    return std::nullopt;
}

void SetClipVisualCommand::undo(Project& project)
{
    project.locateClip(m_clipId)->clip->visual = m_previous;
}

// --- SetClipColorCommand ---

std::optional<std::string> SetClipColorCommand::redo(Project& project)
{
    if (m_clipIds.empty())
        return "no clip to colour";
    if (auto error = validateGrade(m_grade))
        return error;
    for (Id id : m_clipIds) {
        auto location = project.locateClip(id);
        if (!location)
            return "clip not found";
        if (auto error = checkEditable(location->track))
            return error;
        if (location->track->kind != TrackKind::Video || location->clip->text)
            return "colour applies to video clips";
    }
    m_previous.clear();
    for (Id id : m_clipIds) {
        Clip& clip = *project.locateClip(id)->clip;
        m_previous.push_back(clip.color);
        clip.color = m_grade;
    }
    return std::nullopt;
}

void SetClipColorCommand::undo(Project& project)
{
    for (std::size_t i = 0; i < m_clipIds.size(); ++i)
        project.locateClip(m_clipIds[i])->clip->color = m_previous[i];
}

// --- SetClipKeyingCommand ---

std::optional<std::string> SetClipKeyingCommand::redo(Project& project)
{
    auto location = project.locateClip(m_clipId);
    if (!location)
        return "clip not found";
    if (auto error = checkEditable(location->track))
        return error;
    if (location->track->kind != TrackKind::Video || location->clip->adjustment)
        return "masks and keys apply to pictures";
    if (auto error = validateMasks(m_keying.masks))
        return error;
    if (auto error = validateChromaKey(m_keying.chromaKey))
        return error;
    Clip& clip = *location->clip;
    m_previous = {clip.masks, clip.chromaKey, clip.matte};
    clip.masks = m_keying.masks;
    clip.chromaKey = m_keying.chromaKey;
    clip.matte = m_keying.matte;
    return std::nullopt;
}

void SetClipKeyingCommand::undo(Project& project)
{
    Clip& clip = *project.locateClip(m_clipId)->clip;
    clip.masks = m_previous.masks;
    clip.chromaKey = m_previous.chromaKey;
    clip.matte = m_previous.matte;
}

// --- CompositeCommand ---

std::optional<std::string> CompositeCommand::redo(Project& project)
{
    for (std::size_t i = 0; i < m_commands.size(); ++i) {
        if (auto error = m_commands[i]->redo(project)) {
            while (i-- > 0)
                m_commands[i]->undo(project);
            return error;
        }
    }
    return std::nullopt;
}

void CompositeCommand::undo(Project& project)
{
    for (auto it = m_commands.rbegin(); it != m_commands.rend(); ++it)
        (*it)->undo(project);
}

// --- SetClipLookCommand ---

std::optional<std::string> SetClipLookCommand::redo(Project& project)
{
    auto location = project.locateClip(m_clipId);
    if (!location)
        return "clip not found";
    if (auto error = checkEditable(location->track))
        return error;
    if (location->track->kind != TrackKind::Video || location->clip->text)
        return "looks apply to video clips";
    if (m_look) {
        if (m_look->id.empty())
            return "the look has no id";
        if (!std::isfinite(m_look->intensity) || m_look->intensity < 0.0 || m_look->intensity > 1.0)
            return "look intensity must be between 0 and 100%";
        if (auto error = validateGrade(m_look->grade))
            return error;
    }
    m_previous = location->clip->look;
    location->clip->look = m_look;
    return std::nullopt;
}

void SetClipLookCommand::undo(Project& project)
{
    project.locateClip(m_clipId)->clip->look = m_previous;
}

// --- SetClipKeyframesCommand ---

std::optional<std::string> validateEffects(const std::vector<ClipEffect>& effects)
{
    for (const ClipEffect& effect : effects) {
        if (effect.effectId.empty())
            return "effect id is empty";
        for (const auto& [param, keys] : effect.keyframes) {
            if (auto error = validateKeyframeTimes(keys))
                return error;
        }
    }
    return std::nullopt;
}

std::optional<std::string> SetClipKeyframesCommand::redo(Project& project)
{
    auto location = project.locateClip(m_clipId);
    if (!location)
        return "clip not found";
    if (auto error = checkEditable(location->track))
        return error;
    Clip& clip = *location->clip;
    const bool hasPicture = location->track->kind == TrackKind::Video;
    const MediaItem* media = project.findMedia(clip.mediaId);
    const bool hasSound = media && media->hasAudio && !clip.freeze;
    for (const auto& [id, keys] : m_changes) {
        if (auto error = validateKeyframes(id, keys))
            return error;
        const bool picture = findAnimatable(id)->picture;
        if (picture && !hasPicture)
            return "only video clips have a picture";
        if (!picture && !hasSound)
            return "this clip has no sound";
    }
    if (m_visual) {
        if (!hasPicture)
            return "only video clips have a picture";
        if (auto error = validateVisual(*m_visual))
            return error;
    }
    if (m_gainDb && (*m_gainDb < kMinGainDb || *m_gainDb > kMaxGainDb))
        return "volume must be between -60 dB and +12 dB";
    if (m_effects) {
        if (!hasPicture)
            return "effects apply to video clips";
        if (auto error = validateEffects(*m_effects))
            return error;
    }

    m_previous = clip;
    for (const auto& [id, keys] : m_changes) {
        if (keys.empty())
            clip.keyframes.erase(id);
        else
            clip.keyframes[id] = keys;
    }
    if (m_visual)
        clip.visual = *m_visual;
    if (m_gainDb)
        clip.gainDb = *m_gainDb;
    if (m_effects)
        clip.effects = *m_effects;
    return std::nullopt;
}

void SetClipKeyframesCommand::undo(Project& project)
{
    Clip& clip = *project.locateClip(m_clipId)->clip;
    clip.keyframes = m_previous.keyframes;
    clip.visual = m_previous.visual;
    clip.gainDb = m_previous.gainDb;
    clip.effects = m_previous.effects;
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
    if (auto error = validateEffects(m_effects))
        return error;
    m_previous = location->clip->effects;
    location->clip->effects = m_effects;
    return std::nullopt;
}

void SetClipEffectsCommand::undo(Project& project)
{
    project.locateClip(m_clipId)->clip->effects = m_previous;
}

// --- SetClipAudioEffectsCommand ---

namespace {

// Why a clip cannot take audio effects or a pitch setting, if it cannot.
std::optional<std::string> soundError(const Project& project, const Track& track, const Clip& clip)
{
    if (clip.generated() || track.kind == TrackKind::Title)
        return "this clip has no sound";
    const MediaItem* media = project.findMedia(clip.mediaId);
    if (!media || !media->hasAudio)
        return "this clip has no sound";
    if (track.kind == TrackKind::Video && !clip.audioEnabled)
        return "this clip's sound is on an audio track now";
    return std::nullopt;
}

} // namespace

std::optional<std::string> SetClipAudioEffectsCommand::redo(Project& project)
{
    auto location = project.locateClip(m_clipId);
    if (!location)
        return "clip not found";
    if (auto error = checkEditable(location->track))
        return error;
    if (auto error = soundError(project, *location->track, *location->clip))
        return error;
    if (auto error = validateEffects(m_effects))
        return error;
    m_previous = location->clip->audioEffects;
    location->clip->audioEffects = m_effects;
    return std::nullopt;
}

void SetClipAudioEffectsCommand::undo(Project& project)
{
    project.locateClip(m_clipId)->clip->audioEffects = m_previous;
}

// --- SetClipKeepPitchCommand ---

std::optional<std::string> SetClipKeepPitchCommand::redo(Project& project)
{
    auto location = project.locateClip(m_clipId);
    if (!location)
        return "clip not found";
    if (auto error = checkEditable(location->track))
        return error;
    if (auto error = soundError(project, *location->track, *location->clip))
        return error;
    m_previous = location->clip->keepPitch;
    location->clip->keepPitch = m_keep;
    return std::nullopt;
}

void SetClipKeepPitchCommand::undo(Project& project)
{
    project.locateClip(m_clipId)->clip->keepPitch = m_previous;
}

// --- SetClipPeopleCommand ---

std::optional<std::string> SetClipPeopleCommand::redo(Project& project)
{
    auto location = project.locateClip(m_clipId);
    if (!location)
        return "clip not found";
    if (auto error = checkEditable(location->track))
        return error;
    const Clip& clip = *location->clip;
    const MediaItem* media = project.findMedia(clip.mediaId);
    if (clip.generated() || clip.compound() || !media || !media->hasVideo || media->still || media->lottie)
        return "only clips of video files can find people";
    if (m_cutout && (!std::isfinite(m_cutout->softness) || m_cutout->softness < 0.0 || m_cutout->softness > 1.0
                     || !std::isfinite(m_cutout->shift) || m_cutout->shift < -1.0 || m_cutout->shift > 1.0))
        return "cutout setting out of range";
    if (m_faces && (!std::isfinite(m_faces->amount) || m_faces->amount < 0.0 || m_faces->amount > 1.0
                    || !std::isfinite(m_faces->expand) || m_faces->expand < 0.0 || m_faces->expand > 2.0))
        return "face cover setting out of range";
    m_previousCutout = clip.cutout;
    m_previousFaces = clip.faceMosaic;
    if (m_cutout)
        location->clip->cutout = *m_cutout;
    if (m_faces)
        location->clip->faceMosaic = *m_faces;
    return std::nullopt;
}

void SetClipPeopleCommand::undo(Project& project)
{
    Clip& clip = *project.locateClip(m_clipId)->clip;
    clip.cutout = m_previousCutout;
    clip.faceMosaic = m_previousFaces;
}

// --- SetClipEnhancedAudioCommand ---

std::optional<std::string> SetClipEnhancedAudioCommand::redo(Project& project)
{
    auto location = project.locateClip(m_clipId);
    if (!location)
        return "clip not found";
    if (auto error = checkEditable(location->track))
        return error;
    if (auto error = soundError(project, *location->track, *location->clip))
        return error;
    m_previous = location->clip->enhancedAudio;
    location->clip->enhancedAudio = m_enhanced;
    return std::nullopt;
}

void SetClipEnhancedAudioCommand::undo(Project& project)
{
    project.locateClip(m_clipId)->clip->enhancedAudio = m_previous;
}

// --- SetTrackMixCommand ---

std::optional<std::string> SetTrackMixCommand::redo(Project& project)
{
    Track* track = project.findTrack(m_trackId);
    if (!track)
        return "track not found";
    if (track->kind == TrackKind::Title)
        return "title tracks have no sound";
    if (!std::isfinite(m_mix.gainDb) || m_mix.gainDb < kMinGainDb || m_mix.gainDb > kMaxGainDb)
        return "volume is out of range";
    if (!std::isfinite(m_mix.pan) || m_mix.pan < -1.0f || m_mix.pan > 1.0f)
        return "balance must be between left and right";
    m_previous = {track->gainDb, track->pan, track->solo};
    track->gainDb = m_mix.gainDb;
    track->pan = m_mix.pan;
    track->solo = m_mix.solo;
    return std::nullopt;
}

void SetTrackMixCommand::undo(Project& project)
{
    Track& track = *project.findTrack(m_trackId);
    track.gainDb = m_previous.gainDb;
    track.pan = m_previous.pan;
    track.solo = m_previous.solo;
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
    if (location->clip->generated())
        return location->clip->text ? "titles have no speed" : "adjustment layers have no speed";
    if (location->clip->compound())
        return "compound clips have no speed";
    if (const MediaItem* media = project.findMedia(location->clip->mediaId); media && media->still)
        return "a still image has no speed";
    if (m_speed < kMinSpeed || m_speed > kMaxSpeed)
        return "speed must be between 0.1× and 10×";

    // The clip keeps playing the same source; its length on the timeline changes, and later
    // clips on the track move with its end.
    const Ticks newDuration = std::max<Ticks>(1, Ticks(std::llround(double(clip.sourceSpan()) / m_speed)));
    m_trackId = location->track->id;
    m_original = clip;
    m_delta = newDuration - clip.duration;
    const Ticks oldEnd = clip.end();
    clip.speedRamp.clear(); // constant from here
    clip.speed = m_speed;
    clip.reversed = m_reversed;
    scaleKeyframes(clip, double(newDuration) / double(clip.duration));
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

// --- SetClipSpeedRampCommand ---

std::optional<std::string> SetClipSpeedRampCommand::redo(Project& project)
{
    auto location = project.locateClip(m_clipId);
    if (!location)
        return "clip not found";
    if (auto error = checkEditable(location->track))
        return error;
    Clip& clip = *location->clip;
    if (clip.freeze)
        return "a freeze frame has no speed";
    if (clip.generated())
        return clip.text ? "titles have no speed" : "adjustment layers have no speed";
    if (clip.compound())
        return "compound clips have no speed";
    if (const MediaItem* media = project.findMedia(clip.mediaId); media && media->still)
        return "a still image has no speed";
    if (!m_ramp.empty()) {
        if (auto error = validateSpeedRamp(m_ramp))
            return error;
    } else if (clip.speedRamp.empty()) {
        return "this clip has no speed ramp";
    }

    // Same source span, new average speed: the length follows.
    const Ticks span = clip.sourceSpan();
    const double average = m_ramp.empty() ? clip.speed : rampAverage(m_ramp);
    const Ticks newDuration = std::max<Ticks>(1, Ticks(std::llround(double(span) / average)));
    m_trackId = location->track->id;
    m_original = clip;
    m_delta = newDuration - clip.duration;
    const Ticks oldEnd = clip.end();
    scaleKeyframes(clip, double(newDuration) / double(clip.duration));
    clip.speedRamp = m_ramp;
    clip.speed = double(span) / double(newDuration);
    if (m_reversed)
        clip.reversed = *m_reversed;
    clip.duration = newDuration;
    clampFades(clip);
    shiftClipsFrom(*location->track, oldEnd, m_delta);
    return std::nullopt;
}

void SetClipSpeedRampCommand::undo(Project& project)
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
    if (location->clip->generated())
        return "freeze frames are made from video clips";
    if (location->clip->compound())
        return "compound clips have no speed";
    if (const MediaItem* media = project.findMedia(location->clip->mediaId); media && media->still)
        return "a still image is already still";
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
    // The still holds the picture as it was placed at that moment.
    still.visual = clip.visualAt(m_at);
    still.effects = clip.effectsAt(m_at);
    still.keyframes.clear();
    still.id = m_freezeClipId;
    still.start = m_at;
    still.duration = m_hold;
    still.sourceIn = clip.sourceTimeAt(m_at);
    still.freeze = true;
    still.speed = 1.0;
    still.speedRamp.clear();
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
        const auto& clips = location->track->clips;
        const auto previous = std::ranges::find(clips, clip.start, &Clip::end);
        if (clip.generated() || (previous != clips.end() && previous->generated()))
            return "transitions go between video clips";
        if (clip.compound() || (previous != clips.end() && previous->compound()))
            return "transitions do not go on compound clips yet";
        if (m_transition->duration < kMinTransitionDuration || m_transition->duration > kMaxTransitionDuration)
            return "transition length is out of range";
        const bool touching = previous != clips.end();
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

// --- Titles ---

std::optional<std::string> validateText(const ClipText& text)
{
    if (text.text.size() > kMaxTextLength)
        return "the text is too long";
    if (text.size < kMinTextSize || text.size > kMaxTextSize)
        return "text size is out of range";
    if (text.lineSpacing < 0.5 || text.lineSpacing > 3.0)
        return "line spacing is out of range";
    if (text.letterSpacing < -0.2 || text.letterSpacing > 1.0)
        return "letter spacing is out of range";
    if (text.outlineWidth < 0.0 || text.outlineWidth > 0.3)
        return "outline width is out of range";
    if (!std::isfinite(text.shape.padding) || text.shape.padding < 0.0 || text.shape.padding > 2.0)
        return "shape padding is out of range";
    if (!std::isfinite(text.shape.tail) || text.shape.tail < 0.0 || text.shape.tail > 1.0)
        return "a bubble's tail must be along its edge";
    for (const auto& [animation, role] : {std::pair{&text.animationIn, TextAnimationRole::In},
                                          std::pair{&text.animationOut, TextAnimationRole::Out},
                                          std::pair{&text.animationLoop, TextAnimationRole::Loop}}) {
        if (auto error = validateTextAnimation(*animation, role))
            return error;
    }
    for (const ColorRgba& c : {text.color, text.outlineColor, text.shape.color}) {
        for (double v : {c.r, c.g, c.b, c.a}) {
            if (v < 0.0 || v > 1.0)
                return "colours must be between 0 and 1";
        }
    }
    return std::nullopt;
}

std::optional<std::string> AddTitleCommand::redo(Project& project)
{
    if (auto error = validateText(m_text))
        return error;
    if (m_start < 0 || m_duration <= 0)
        return "clip times are out of range";
    Track* track = nullptr;
    if (m_trackId != kNoId) {
        track = project.findTrack(m_trackId);
        if (auto error = checkEditable(track))
            return error;
        if (track->kind == TrackKind::Audio)
            return "titles go on video or title tracks";
        if (track->overlaps(m_start, m_duration))
            return "clip overlaps an existing clip";
    } else {
        // A new video track on top (tracks are stored bottom to top).
        if (m_newTrackId == kNoId)
            m_newTrackId = project.allocateId();
        const auto videoTracks = std::ranges::count(project.tracks, TrackKind::Video, &Track::kind);
        project.tracks.push_back(
            Track{.id = m_newTrackId, .kind = TrackKind::Video, .name = "Video " + std::to_string(videoTracks + 1)});
        track = &project.tracks.back();
    }

    if (m_clipId == kNoId)
        m_clipId = project.allocateId();
    Clip clip{.id = m_clipId, .start = m_start, .duration = m_duration};
    clip.audioEnabled = false;
    clip.visual = m_visual;
    clip.text = m_text;
    track->insertClip(std::move(clip));
    return std::nullopt;
}

void AddTitleCommand::undo(Project& project)
{
    if (m_trackId == kNoId) {
        std::erase_if(project.tracks, [&](const Track& t) { return t.id == m_newTrackId; });
        return;
    }
    Track* track = project.findTrack(m_trackId);
    assert(track);
    track->removeClip(m_clipId);
}

// --- AddAdjustmentCommand ---

std::optional<std::string> AddAdjustmentCommand::redo(Project& project)
{
    if (m_start < 0 || m_duration <= 0)
        return "clip times are out of range";
    Track* track = nullptr;
    if (m_trackId != kNoId) {
        track = project.findTrack(m_trackId);
        if (auto error = checkEditable(track))
            return error;
        if (track->kind == TrackKind::Audio)
            return "adjustment layers go on video tracks";
        if (track->overlaps(m_start, m_duration))
            return "clip overlaps an existing clip";
    } else {
        // A new video track on top (tracks are stored bottom to top).
        if (m_newTrackId == kNoId)
            m_newTrackId = project.allocateId();
        const auto videoTracks = std::ranges::count(project.tracks, TrackKind::Video, &Track::kind);
        project.tracks.push_back(
            Track{.id = m_newTrackId, .kind = TrackKind::Video, .name = "Video " + std::to_string(videoTracks + 1)});
        track = &project.tracks.back();
    }
    if (m_clipId == kNoId)
        m_clipId = project.allocateId();
    Clip clip{.id = m_clipId, .start = m_start, .duration = m_duration};
    clip.audioEnabled = false;
    clip.adjustment = true;
    track->insertClip(std::move(clip));
    return std::nullopt;
}

void AddAdjustmentCommand::undo(Project& project)
{
    if (m_trackId == kNoId) {
        std::erase_if(project.tracks, [&](const Track& t) { return t.id == m_newTrackId; });
        return;
    }
    project.findTrack(m_trackId)->removeClip(m_clipId);
}

std::optional<std::string> SetClipTextCommand::redo(Project& project)
{
    auto location = project.locateClip(m_clipId);
    if (!location)
        return "clip not found";
    if (auto error = checkEditable(location->track))
        return error;
    if (!location->clip->text)
        return "only titles have text";
    if (auto error = validateText(m_text))
        return error;
    m_previous = *location->clip->text;
    location->clip->text = m_text;
    return std::nullopt;
}

void SetClipTextCommand::undo(Project& project)
{
    project.locateClip(m_clipId)->clip->text = m_previous;
}

} // namespace tve
