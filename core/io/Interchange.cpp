#include "io/Interchange.h"
#include "model/Translate.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <set>
#include <stdexcept>

namespace tve {

namespace interchange {

namespace {

const char* const kDissolve = "tve.transition.dissolve";

bool endsWith(const std::string& text, const std::string& suffix)
{
    if (text.size() < suffix.size())
        return false;
    return std::equal(suffix.rbegin(), suffix.rend(), text.rbegin(),
                      [](char wanted, char c) { return std::tolower(static_cast<unsigned char>(c)) == wanted; });
}

// What a clip uses that no interchange format carries.
void noteClip(const Clip& clip, const MediaItem* media, bool videoTrack, Notes& notes)
{
    if (videoTrack) {
        const ClipVisual plain;
        ClipVisual visual = clip.visual;
        visual.sphere = plain.sphere;
        if (visual != plain)
            notes.add(TVE_TR("core", "Position, size, crop, opacity and blend modes"));
        if (media && media->spherical && clip.visual.sphere != plain.sphere)
            notes.add(TVE_TR("core", "360° reframing"));
        if (!clip.color.isNeutral() || clip.look)
            notes.add(TVE_TR("core", "Colour grades and looks"));
        if (!clip.effects.empty())
            notes.add(TVE_TR("core", "Video effects and plug-ins"));
        if (clip.chromaKey.enabled || !clip.masks.empty() || clip.matte != MatteMode::None)
            notes.add(TVE_TR("core", "Keys, masks and mattes"));
        if (clip.cutout.enabled || clip.faceMosaic.active() || clip.stabilization.enabled || clip.deflicker.enabled
            || clip.denoise > 0.0 || clip.cursor.shows() || clip.processedVideo)
            notes.add(TVE_TR("core", "Cutouts, covered faces, stabilization, denoise and cursor effects"));
    }
    if (clip.gainDb != 0.0f || clip.fadeIn > 0 || clip.fadeOut > 0)
        notes.add(TVE_TR("core", "Clip volume and fades"));
    if (!clip.audioEffects.empty() || clip.enhancedAudio)
        notes.add(TVE_TR("core", "Audio effects and enhanced speech"));
    if (!clip.keyframes.empty())
        notes.add(TVE_TR("core", "Keyframes"));
    if (!clip.speedRamp.empty())
        notes.add(TVE_TR("core", "Speed ramps (as their average speed)"));
}

Item::Kind kindOf(const Clip& clip)
{
    return clip.text ? Item::Kind::Title : Item::Kind::Media;
}

} // namespace

Ticks Timeline::duration() const
{
    Ticks end = 0;
    for (const Track& track : tracks)
        if (!track.items.empty())
            end = std::max(end, track.items.back().start + track.items.back().duration);
    return end;
}

void Notes::add(const std::string& what)
{
    if (m_counts[what]++ == 0)
        m_order.push_back(what);
}

void Notes::addLine(const std::string& line)
{
    if (std::ranges::find(m_lines, line) == m_lines.end())
        m_lines.push_back(line);
}

std::vector<std::string> Notes::lines() const
{
    std::vector<std::string> out = m_lines;
    for (const std::string& what : m_order) {
        const int n = m_counts.at(what);
        out.push_back(what + " — " + std::to_string(n) + (n == 1 ? " clip" : " clips"));
    }
    return out;
}

Timeline fromProject(const Project& project, const std::string& name, Notes& notes)
{
    Timeline timeline;
    timeline.name = name;
    timeline.rate = project.settings.frameRate;
    timeline.width = project.settings.width;
    timeline.height = project.settings.height;
    timeline.markers = project.markers;
    if (isHdr(project.settings.colour))
        notes.addLine(TVE_TR("core", "The project's HDR colour: the other editor's timeline is set up as its own"));
    if (project.settings.spherical)
        notes.addLine(TVE_TR("core", "The 360° project setting"));

    std::vector<Track> video, audio;
    int videoCount = 0, audioCount = 0;
    for (const tve::Track& source : project.tracks) {
        const bool isVideo = source.kind == TrackKind::Video;
        Track track;
        track.audio = !isVideo;
        track.name = !source.name.empty() ? source.name
                                          : (isVideo ? "Video " + std::to_string(++videoCount)
                                                     : "Audio " + std::to_string(++audioCount));
        track.enabled = !source.hidden && !source.muted;
        if (source.gainDb != 0.0f || source.pan != 0.0f || !source.audioEffects.empty())
            notes.addLine(TVE_TR("core", "Track volume, balance and effects"));
        if (source.locked || source.solo)
            notes.addLine(TVE_TR("core", "Locked and soloed tracks (they come unlocked, not soloed)"));
        for (const Clip& clip : source.clips) {
            const MediaItem* media = project.findMedia(clip.mediaId);
            if (clip.drawing) {
                notes.add(TVE_TR("core", "Drawings (left as space)"));
                continue;
            }
            if (clip.adjustment) {
                notes.add(TVE_TR("core", "Adjustment layers (left as space)"));
                continue;
            }
            if (clip.compound()) {
                notes.add(TVE_TR("core", "Compound clips (left as space)"));
                continue;
            }
            if (!clip.text && (!media || media->lottie)) {
                if (media && media->lottie)
                    notes.add(TVE_TR("core", "Stickers (left as space)"));
                continue;
            }
            Item item;
            item.kind = kindOf(clip);
            item.start = clip.start;
            item.duration = clip.duration;
            if (item.kind == Item::Kind::Title) {
                item.text = clip.text->text;
                item.name = "Title";
                notes.add(TVE_TR("core", "Titles' look and animation (they come as placeholders with their text)"));
            } else {
                item.path = media->path;
                item.name = fileName(media->path);
                item.hasVideo = isVideo && media->hasVideo;
                item.hasAudio = media->hasAudio && clip.audioEnabled;
                item.sourceIn = clip.sourceIn;
                item.speed = clip.speed;
                item.reversed = clip.reversed;
                item.freeze = clip.freeze;
                item.mediaDuration = media->duration;
                if (media->still) {
                    item.hasAudio = false;
                    item.still = true;
                }
            }
            if (clip.transitionIn) {
                const ClipTransition& t = *clip.transitionIn;
                Transition transition;
                switch (t.alignment) {
                case TransitionAlignment::Center:
                    transition.before = t.duration / 2;
                    transition.after = t.duration - transition.before;
                    break;
                case TransitionAlignment::EndsAtCut: transition.before = t.duration; break;
                case TransitionAlignment::StartsAtCut: transition.after = t.duration; break;
                }
                if (t.transitionId != kDissolve)
                    notes.add(TVE_TR("core", "Transitions other than cross dissolves (as cross dissolves)"));
                item.transitionIn = transition;
            }
            noteClip(clip, media, isVideo, notes);
            track.items.push_back(std::move(item));
        }
        (isVideo ? video : audio).push_back(std::move(track));
    }
    for (Track& t : video)
        timeline.tracks.push_back(std::move(t));
    for (Track& t : audio)
        timeline.tracks.push_back(std::move(t));
    return timeline;
}

Project toProject(const Timeline& timeline, Notes& notes)
{
    Project project;
    const bool stated = timeline.width > 0 && timeline.height > 0;
    project.settings.width = stated ? std::clamp(timeline.width - timeline.width % 2, 16, 16384) : 1920;
    project.settings.height = stated ? std::clamp(timeline.height - timeline.height % 2, 16, 16384) : 1080;
    project.settings.frameRate = timeline.rate;

    // Media by path, long enough for every clip of it.
    std::map<std::string, Id> mediaByPath;
    const auto mediaFor = [&](const Item& item) {
        auto [it, added] = mediaByPath.try_emplace(item.path, kNoId);
        if (added) {
            MediaItem media;
            media.id = project.allocateId();
            media.path = item.path;
            media.hasVideo = false;
            media.still = item.still;
            project.media.push_back(media);
            it->second = media.id;
        }
        MediaItem& media = *std::ranges::find(project.media, it->second, &MediaItem::id);
        media.hasVideo = media.hasVideo || item.hasVideo;
        media.hasAudio = media.hasAudio || item.hasAudio;
        media.duration = std::max(media.duration, item.sourceIn + std::max<Ticks>(item.sourceSpan(), 1));
        return media.id;
    };

    // Audio tracks first (below), then video tracks from the bottom; one of each at least.
    std::vector<const Track*> audio, video;
    for (const Track& track : timeline.tracks)
        (track.audio ? audio : video).push_back(&track);
    const Track noVideo{.audio = false, .name = "Video 1"};
    const Track noAudio{.audio = true, .name = "Audio 1"};
    if (video.empty())
        video.push_back(&noVideo);
    if (audio.empty())
        audio.push_back(&noAudio);

    // Where a video clip's sound is also on an audio track (another editor's linked audio),
    // the video clip's own sound stays off.
    const auto soundElsewhere = [&](const Item& item) {
        for (const Track* track : audio)
            for (const Item& other : track->items)
                if (other.kind == Item::Kind::Media && other.path == item.path && other.start < item.start + item.duration
                    && item.start < other.start + other.duration)
                    return true;
        return false;
    };

    int videoCount = 0, audioCount = 0;
    const auto addTrack = [&](const Track& source) {
        tve::Track track;
        track.id = project.allocateId();
        track.kind = source.audio ? TrackKind::Audio : TrackKind::Video;
        track.name = !source.name.empty() ? source.name
                                          : (source.audio ? "Audio " + std::to_string(++audioCount)
                                                          : "Video " + std::to_string(++videoCount));
        track.hidden = !source.enabled && !source.audio;
        track.muted = !source.enabled && source.audio;
        Ticks end = 0;
        const Clip* previous = nullptr;
        for (const Item& item : source.items) {
            if (item.kind == Item::Kind::Gap || item.duration <= 0)
                continue;
            if (item.start < end) {
                notes.add(TVE_TR("core", "Overlapping clips on one track (the later one left out)"));
                continue;
            }
            Clip clip;
            clip.id = project.allocateId();
            clip.start = item.start;
            clip.duration = item.duration;
            if (item.kind == Item::Kind::Title) {
                if (source.audio) {
                    notes.add(TVE_TR("core", "Titles on audio tracks (left out)"));
                    continue;
                }
                clip.text = ClipText{};
                clip.text->text = item.text.empty() ? "Title" : item.text;
            } else {
                if (item.path.empty()) {
                    notes.add(TVE_TR("core", "Clips without a media file (left out)"));
                    continue;
                }
                clip.mediaId = mediaFor(item);
                clip.sourceIn = std::max<Ticks>(item.sourceIn, 0);
                double speed = std::abs(item.speed);
                if (!item.freeze && (speed < 0.05 || speed > 100.0)) {
                    notes.add(TVE_TR("core", "Speeds outside 5%–10,000% (kept at the nearest)"));
                    speed = std::clamp(speed, 0.05, 100.0);
                }
                clip.speed = item.freeze ? 1.0 : speed;
                clip.reversed = item.reversed;
                clip.freeze = item.freeze;
                if (!source.audio)
                    clip.audioEnabled = item.hasAudio && !soundElsewhere(item);
            }
            if (item.transitionIn) {
                const Transition& t = *item.transitionIn;
                if (!previous || previous->end() != clip.start) {
                    notes.add(TVE_TR("core", "Transitions not between two touching clips (left out)"));
                } else {
                    ClipTransition transition;
                    transition.transitionId = kDissolve;
                    transition.duration = std::clamp(t.before + t.after, kMinTransitionDuration, kMaxTransitionDuration);
                    transition.alignment = t.before == 0 ? TransitionAlignment::StartsAtCut
                                         : t.after == 0  ? TransitionAlignment::EndsAtCut
                                                         : TransitionAlignment::Center;
                    if (t.before > 0 && t.after > 0 && std::llabs(t.before - t.after) > kTicksPerSecond / 100)
                        notes.add(TVE_TR("core", "Off-centre transitions (centred on the cut)"));
                    if (t.kind != "dissolve")
                        notes.add(TVE_TR("core", "Transitions other than cross dissolves (as cross dissolves)"));
                    clip.transitionIn = transition;
                }
            }
            end = clip.end();
            track.clips.push_back(std::move(clip));
            previous = &track.clips.back();
        }
        project.tracks.push_back(std::move(track));
    };
    for (const Track* track : audio)
        addTrack(*track);
    for (const Track* track : video)
        addTrack(*track);

    for (Marker marker : timeline.markers) {
        marker.id = project.allocateId();
        marker.time = std::max<Ticks>(marker.time, 0);
        marker.duration = std::max<Ticks>(marker.duration, 0);
        if (marker.note.size() > kMaxMarkerNote)
            marker.note.resize(kMaxMarkerNote);
        project.markers.push_back(marker);
    }
    std::ranges::sort(project.markers, {}, &Marker::time);
    return project;
}

std::int64_t framesOf(Ticks ticks, FrameRate rate)
{
    const Ticks frame = frameDuration(rate);
    return ticks >= 0 ? (ticks + frame / 2) / frame : -((-ticks + frame / 2) / frame);
}

Ticks ticksOfFrames(std::int64_t frames, FrameRate rate)
{
    return frames * frameDuration(rate);
}

std::string fileUrl(const std::string& path)
{
    std::string url = "file://";
    for (unsigned char c : path) {
        if (std::isalnum(c) || std::string_view("/-._~").find(char(c)) != std::string_view::npos) {
            url += char(c);
        } else {
            static const char* const hex = "0123456789ABCDEF";
            url += '%';
            url += hex[c >> 4];
            url += hex[c & 15];
        }
    }
    return url;
}

std::string pathFromUrl(const std::string& url, const std::string& base)
{
    std::string path = url;
    if (path.rfind("file://", 0) == 0) {
        path = path.substr(7);
        if (path.rfind("localhost/", 0) == 0)
            path = path.substr(9);
    }
    std::string decoded;
    for (std::size_t i = 0; i < path.size(); ++i) {
        if (path[i] == '%' && i + 2 < path.size() && std::isxdigit(static_cast<unsigned char>(path[i + 1]))
            && std::isxdigit(static_cast<unsigned char>(path[i + 2]))) {
            decoded += char(std::stoi(path.substr(i + 1, 2), nullptr, 16));
            i += 2;
        } else {
            decoded += path[i];
        }
    }
    if (!decoded.empty() && decoded.front() != '/' && !base.empty())
        decoded = base + (base.back() == '/' ? "" : "/") + decoded;
    return decoded;
}

std::string fileName(const std::string& path)
{
    const auto slash = path.find_last_of('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

} // namespace interchange

std::optional<InterchangeFormat> interchangeFormatOf(const std::string& path)
{
    if (interchange::endsWith(path, ".otio"))
        return InterchangeFormat::Otio;
    if (interchange::endsWith(path, ".fcpxml") || interchange::endsWith(path, ".fcpxmld"))
        return InterchangeFormat::Fcpxml;
    if (interchange::endsWith(path, ".edl"))
        return InterchangeFormat::Edl;
    return std::nullopt;
}

ExportedTimeline exportTimeline(const Project& project, InterchangeFormat format, const std::string& name)
{
    interchange::Notes notes;
    const interchange::Timeline timeline = interchange::fromProject(project, name, notes);
    ExportedTimeline out;
    switch (format) {
    case InterchangeFormat::Otio: out.text = interchange::writeOtio(timeline, notes); break;
    case InterchangeFormat::Fcpxml: out.text = interchange::writeFcpxml(timeline, notes); break;
    case InterchangeFormat::Edl: out.text = interchange::writeEdl(timeline, notes); break;
    }
    out.notes = notes.lines();
    return out;
}

ImportedTimeline importTimeline(const std::string& text, InterchangeFormat format, const std::string& baseDirectory)
{
    interchange::Notes notes;
    interchange::Timeline timeline;
    switch (format) {
    case InterchangeFormat::Otio: timeline = interchange::readOtio(text, baseDirectory, notes); break;
    case InterchangeFormat::Fcpxml: timeline = interchange::readFcpxml(text, baseDirectory, notes); break;
    case InterchangeFormat::Edl: throw std::runtime_error("EDLs can be exported, not opened");
    }
    ImportedTimeline out;
    out.canvasStated = timeline.width > 0 && timeline.height > 0;
    out.project = interchange::toProject(timeline, notes);
    out.notes = notes.lines();
    return out;
}

} // namespace tve
