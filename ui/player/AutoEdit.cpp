#include "AutoEdit.h"

#include "LookLibrary.h"
#include "TemplateLibrary.h"
#include "TitleRenderer.h"
#include "decode/AudioDecoder.h"
#include "model/Keyframes.h"

#include <QCoreApplication>
#include <QImage>

#include <algorithm>
#include <cmath>

using namespace tve;

namespace {

Ticks ticksOf(double seconds)
{
    return Ticks(std::llround(seconds * double(kTicksPerSecond)));
}

QVariantMap anim(const char* preset, const char* unit, double duration, double stagger = 0)
{
    return {{QStringLiteral("preset"), QLatin1String(preset)}, {QStringLiteral("unit"), QLatin1String(unit)},
            {QStringLiteral("duration"), duration}, {QStringLiteral("stagger"), stagger}};
}

QVariantList rgba(double r, double g, double b, double a = 1)
{
    return {r, g, b, a};
}

} // namespace

const std::vector<AutoEditStyle>& autoEditStyles()
{
    static const std::vector<AutoEditStyle> styles{
        {QStringLiteral("cinematic"), QStringLiteral(QT_TRANSLATE_NOOP("AutoEdit", "Cinematic")), QStringLiteral(QT_TRANSLATE_NOOP("AutoEdit", "Long, slow cuts in teal and orange.")),
         QStringLiteral("tve.music.horizon"), 8, 8, QStringLiteral("tve.transition.dissolve"), 1, QStringLiteral("teal_orange"),
         {{QStringLiteral("size"), 0.08}, {QStringLiteral("fontFamily"), QStringLiteral("Didot")}, {QStringLiteral("letterSpacing"), 0.3},
          {QStringLiteral("shadow"), true}, {QStringLiteral("animationIn"), anim("tracking", "character", 1.6)},
          {QStringLiteral("animationOut"), anim("fade", "whole", 0.8)}}},
        {QStringLiteral("upbeat"), QStringLiteral(QT_TRANSLATE_NOOP("AutoEdit", "Upbeat")), QStringLiteral(QT_TRANSLATE_NOOP("AutoEdit", "Bright, quick cuts with flashes.")),
         QStringLiteral("tve.music.bright_side"), 4, 4, QStringLiteral("tve.transition.flash"), 0.5, QStringLiteral("vivid"),
         {{QStringLiteral("size"), 0.1}, {QStringLiteral("fontFamily"), QStringLiteral("Impact")}, {QStringLiteral("color"), rgba(1, 0.85, 0.1)},
          {QStringLiteral("shadow"), true}, {QStringLiteral("animationIn"), anim("pop", "word", 0.7, 0.5)},
          {QStringLiteral("animationOut"), anim("zoom", "whole", 0.4)}}},
        {QStringLiteral("calm"), QStringLiteral(QT_TRANSLATE_NOOP("AutoEdit", "Calm")), QStringLiteral(QT_TRANSLATE_NOOP("AutoEdit", "Gentle fades through white, soft colour.")),
         QStringLiteral("tve.music.still_water"), 4, 4, QStringLiteral("tve.transition.dip_to_white"), 1, QStringLiteral("matte_pastel"),
         {{QStringLiteral("size"), 0.08}, {QStringLiteral("fontFamily"), QStringLiteral("Georgia")}, {QStringLiteral("italic"), true},
          {QStringLiteral("shadow"), true}, {QStringLiteral("animationIn"), anim("blur", "whole", 1.4)},
          {QStringLiteral("animationOut"), anim("fade", "whole", 1.0)}}},
        {QStringLiteral("energetic"), QStringLiteral(QT_TRANSLATE_NOOP("AutoEdit", "Energetic")), QStringLiteral(QT_TRANSLATE_NOOP("AutoEdit", "Very fast cuts and glitches.")),
         QStringLiteral("tve.music.neon_run"), 2, 2, QStringLiteral("tve.transition.glitch"), 0.5, QStringLiteral("cool_night"),
         {{QStringLiteral("size"), 0.1}, {QStringLiteral("bold"), true}, {QStringLiteral("fontFamily"), QStringLiteral("Futura")},
          {QStringLiteral("color"), rgba(0.55, 1, 1)}, {QStringLiteral("outlineWidth"), 0.04},
          {QStringLiteral("outlineColor"), rgba(1, 0.2, 0.75)}, {QStringLiteral("animationIn"), anim("fade", "whole", 0.4)},
          {QStringLiteral("animationLoop"), anim("flicker", "character", 2.0)}}},
        {QStringLiteral("vintage"), QStringLiteral(QT_TRANSLATE_NOOP("AutoEdit", "Vintage")), QStringLiteral(QT_TRANSLATE_NOOP("AutoEdit", "Faded film colour, unhurried, through black.")),
         QStringLiteral("tve.music.late_study"), 4, 4, QStringLiteral("tve.transition.dip_to_black"), 1, QStringLiteral("vintage_fade"),
         {{QStringLiteral("size"), 0.07}, {QStringLiteral("fontFamily"), QStringLiteral("American Typewriter")},
          {QStringLiteral("color"), rgba(1, 0.95, 0.85)}, {QStringLiteral("animationIn"), anim("typewriter", "character", 1.2, 1.0)},
          {QStringLiteral("animationOut"), anim("fade", "whole", 0.8)}}},
        {QStringLiteral("clean"), QStringLiteral(QT_TRANSLATE_NOOP("AutoEdit", "Clean")), QStringLiteral(QT_TRANSLATE_NOOP("AutoEdit", "Straightforward cuts that slide, natural colour.")),
         QStringLiteral("tve.music.teamwork"), 4, 4, QStringLiteral("tve.transition.slide"), 1, QString(),
         {{QStringLiteral("size"), 0.07}, {QStringLiteral("fontFamily"), QStringLiteral("Avenir Next")}, {QStringLiteral("letterSpacing"), 0.15},
          {QStringLiteral("shadow"), true}, {QStringLiteral("animationIn"), anim("rise", "word", 0.8, 0.4)},
          {QStringLiteral("animationOut"), anim("fade", "whole", 0.6)}}},
    };
    return styles;
}

const AutoEditStyle* findAutoEditStyle(const QString& id)
{
    for (const AutoEditStyle& style : autoEditStyles())
        if (style.id == id)
            return &style;
    return nullptr;
}

QVariantList autoEditStyleList()
{
    QVariantList list;
    for (const AutoEditStyle& style : autoEditStyles())
        list << QVariantMap{{QStringLiteral("id"), style.id}, {QStringLiteral("name"), QCoreApplication::translate("AutoEdit", style.name.toUtf8().constData())},
                            {QStringLiteral("description"), QCoreApplication::translate("AutoEdit", style.description.toUtf8().constData())}, {QStringLiteral("look"), style.look}};
    return list;
}

bool followSubject(Clip& clip, const MediaItem& media, const std::vector<SubjectSample>& subject, int canvasWidth,
                   int canvasHeight, double smoothing)
{
    if (subject.empty() || media.width <= 0 || media.height <= 0)
        return false;
    const ReframePath path = reframe(subject, media.width, media.height, canvasWidth, canvasHeight, {.smoothing = smoothing});
    if (path.positions.empty())
        return false;
    const auto positionAt = [&](Ticks time) {
        const auto& p = path.positions;
        if (time <= p.front().first)
            return p.front().second;
        if (time >= p.back().first)
            return p.back().second;
        const auto next = std::ranges::upper_bound(p, time, {}, [](const auto& e) { return e.first; });
        const auto& [t1, b] = *next;
        const auto& [t0, a] = *(next - 1);
        const double u = double(time - t0) / double(std::max<Ticks>(1, t1 - t0));
        return std::array<double, 2>{a[0] + (b[0] - a[0]) * u, a[1] + (b[1] - a[1]) * u};
    };
    const auto clipTime = [&](Ticks time) { return Ticks(std::llround(double(time - clip.sourceIn) / clip.speed)); };
    const Ticks sourceEnd = clip.sourceIn + Ticks(std::llround(double(clip.duration) * clip.speed));
    std::vector<std::pair<Ticks, std::array<double, 2>>> points{{0, positionAt(clip.sourceIn)}};
    for (const auto& [time, xy] : path.positions) {
        const Ticks at = clipTime(time);
        if (at > 0 && at < clip.duration)
            points.push_back({at, xy});
    }
    points.push_back({clip.duration, positionAt(sourceEnd)});
    Keyframes xs, ys;
    for (const auto& [at, xy] : points) {
        xs.push_back({at, std::clamp(xy[0], -10.0, 10.0)});
        ys.push_back({at, std::clamp(xy[1], -10.0, 10.0)});
    }
    clip.visual.transform = {.scale = path.scale};
    clip.visual.crop = {};
    clip.keyframes[std::string(property::kX)] = std::move(xs);
    clip.keyframes[std::string(property::kY)] = std::move(ys);
    return true;
}

namespace {

struct Cut {
    std::size_t source = 0;
    Ticks from = 0;    // media time
    Ticks length = 0;  // on the timeline
    double score = 0;
};

// Tracks, music and template parts every made edit has.
struct Frame {
    Project project;
    std::size_t music = 0, main = 0, titles = 0;
    TemplateInfo info;
};

Frame frame(int width, int height, const QString& name)
{
    Frame f;
    f.project.settings.width = width;
    f.project.settings.height = height;
    const auto addTrack = [&](TrackKind kind, const char* trackName) {
        f.project.tracks.push_back(Track{.id = f.project.allocateId(), .kind = kind, .name = trackName});
        return f.project.tracks.size() - 1;
    };
    f.music = addTrack(TrackKind::Audio, "Music");
    f.main = addTrack(TrackKind::Video, "Video 1");
    f.titles = addTrack(TrackKind::Video, "Titles");
    f.info.name = name.toStdString();
    return f;
}

} // namespace

std::optional<Project> buildAutoEdit(const std::vector<AutoEditSource>& sources, const AutoEditStyle& style,
                                     const TemplateShape& shape, double seconds, const QString& title, const QString& name,
                                     QString* error)
{
    if (sources.empty()) {
        if (error)
            *error = QCoreApplication::translate("AutoEdit", "Choose some pictures or videos first.");
        return std::nullopt;
    }
    double tempo = 120;
    const QString musicPath = TemplateLibrary::builtInMusic(style.music, &tempo, error);
    if (musicPath.isEmpty())
        return std::nullopt;
    const double beat = 60.0 / std::max(tempo, 1.0);

    // Every cut there could be: each moment of a video (a whole number of beats long, at most
    // the style's cut and the moment), each picture. When that is not enough for the length
    // asked for, cuts may run to the whole moment and pictures stay twice as long.
    const auto candidates = [&](bool stretch) {
        std::vector<Cut> list;
        for (std::size_t i = 0; i < sources.size(); ++i) {
            const AutoEditSource& source = sources[i];
            if (source.media.still) {
                list.push_back({i, 0, ticksOf(style.photoBeats * (stretch ? 2 : 1) * beat), 0.55});
                continue;
            }
            for (const Moment& moment : source.moments) {
                const double whole = std::floor(double(moment.length()) / double(kTicksPerSecond) / beat);
                const double beats = stretch ? whole : std::min(style.cutBeats, whole);
                if (beats < 2)
                    continue;
                const Ticks length = ticksOf(beats * beat);
                if (moment.start + length > source.media.duration)
                    continue;
                list.push_back({i, moment.start, length, moment.score});
            }
        }
        return list;
    };
    const auto sum = [](const std::vector<Cut>& list) {
        Ticks t = 0;
        for (const Cut& cut : list)
            t += cut.length;
        return t;
    };
    std::vector<Cut> cuts = candidates(false);
    if (sum(cuts) < ticksOf(seconds))
        cuts = candidates(true);
    if (cuts.empty()) {
        if (error)
            *error = QCoreApplication::translate("AutoEdit", "Nothing long enough to cut to the music.");
        return std::nullopt;
    }
    // The best cuts up to the length asked for, then in order.
    std::vector<std::size_t> order(cuts.size());
    for (std::size_t i = 0; i < order.size(); ++i)
        order[i] = i;
    std::ranges::stable_sort(order, [&](std::size_t a, std::size_t b) { return cuts[a].score > cuts[b].score; });
    std::vector<Cut> chosen;
    Ticks total = 0;
    for (std::size_t i : order) {
        if (total >= ticksOf(seconds))
            break;
        chosen.push_back(cuts[i]);
        total += cuts[i].length;
    }
    std::ranges::sort(chosen, [](const Cut& a, const Cut& b) { return a.source != b.source ? a.source < b.source : a.from < b.from; });

    Frame f = frame(shape.width, shape.height, name);
    f.info.id = ("auto." + style.id).toStdString();
    std::vector<Id> mediaIds(sources.size(), kNoId);
    const LookInfo* look = style.look.isEmpty() ? nullptr : findLook(style.look.toStdString());
    Ticks at = 0;
    for (std::size_t n = 0; n < chosen.size(); ++n) {
        const Cut& cut = chosen[n];
        const MediaItem& media = sources[cut.source].media;
        if (mediaIds[cut.source] == kNoId) {
            MediaItem copy = media;
            copy.id = f.project.allocateId();
            mediaIds[cut.source] = copy.id;
            f.project.media.push_back(copy);
        }
        Clip clip{.id = f.project.allocateId(), .mediaId = mediaIds[cut.source], .start = at, .duration = cut.length,
                  .sourceIn = cut.from};
        clip.visual.crop = fillCrop(media.width, media.height, shape.width, shape.height);
        if (look)
            clip.look = ClipLook{look->id, look->title, 0.8, look->grade};
        if (n > 0 && !style.transition.isEmpty())
            clip.transitionIn = ClipTransition{.transitionId = style.transition.toStdString(),
                                               .duration = std::clamp(ticksOf(style.transitionBeats * beat), kMinTransitionDuration,
                                                                      std::min(kMaxTransitionDuration, cut.length / 2))};
        f.info.mediaSlots.push_back({.clipId = clip.id, .label = "Shot " + std::to_string(n + 1), .filled = true});
        at += clip.duration;
        f.project.tracks[f.main].insertClip(std::move(clip));
    }
    // An opening title over the first two bars.
    if (!title.trimmed().isEmpty()) {
        QVariantMap titleStyle = style.title;
        titleStyle.insert(QStringLiteral("text"), title.trimmed());
        Clip clip{.id = f.project.allocateId(), .start = ticksOf(beat * 0.5), .duration = std::min(at - ticksOf(beat * 0.5), ticksOf(beat * 7.5))};
        if (clip.duration > 0) {
            clip.text = textFrom(titleStyle);
            f.info.texts.push_back({.clipId = clip.id, .label = "Title"});
            f.project.tracks[f.titles].insertClip(std::move(clip));
        }
    }
    // The music under all of it, fading out over the last bar.
    auto decoder = AudioDecoder::open(musicPath.toStdString());
    if (decoder) {
        MediaItem music{.id = f.project.allocateId(), .path = musicPath.toStdString(), .duration = decoder->info().duration,
                        .hasVideo = false, .hasAudio = true, .audioSampleRate = decoder->info().sourceSampleRate,
                        .audioChannels = decoder->info().sourceChannels};
        Clip clip{.id = f.project.allocateId(), .mediaId = music.id, .start = 0, .duration = std::min(at, music.duration)};
        clip.gainDb = -4.0f;
        clip.fadeOut = std::min(clip.duration / 2, ticksOf(beat * 4));
        f.info.musicClipId = clip.id;
        f.project.media.push_back(music);
        f.project.tracks[f.music].insertClip(std::move(clip));
    }
    f.project.templateInfo = std::move(f.info);
    return f.project;
}

std::optional<Project> buildShorts(const MediaItem& media, const std::vector<ShortPlan>& shorts,
                                   const std::vector<SubjectSample>& subject, QString* error)
{
    if (shorts.empty()) {
        if (error)
            *error = QCoreApplication::translate("AutoEdit", "No moment was long enough for a short.");
        return std::nullopt;
    }
    const TemplateShape shape = *templateShape("9:16");
    Frame f = frame(shape.width, shape.height, QStringLiteral("Shorts"));
    f.info.id = "auto.shorts";
    MediaItem copy = media;
    copy.id = f.project.allocateId();
    f.project.media.push_back(copy);
    Ticks at = 0;
    for (std::size_t i = 0; i < shorts.size(); ++i) {
        const Moment& moment = shorts[i].moment;
        Clip clip{.id = f.project.allocateId(), .mediaId = copy.id, .start = at, .duration = moment.length(), .sourceIn = moment.start};
        // The subject followed into the tall frame; else the middle, filling it.
        if (!followSubject(clip, copy, subject, shape.width, shape.height, 0.6))
            clip.visual.crop = fillCrop(copy.width, copy.height, shape.width, shape.height);
        f.info.mediaSlots.push_back({.clipId = clip.id, .label = "Short " + std::to_string(i + 1), .filled = true});
        // Each short a range to export on its own.
        const QString note = shorts[i].title.isEmpty() ? QCoreApplication::translate("AutoEdit", "Short %1").arg(i + 1)
                                                       : QCoreApplication::translate("AutoEdit", "Short %1: %2").arg(i + 1).arg(shorts[i].title);
        f.project.markers.push_back(Marker{.id = f.project.allocateId(), .time = at, .duration = clip.duration,
                                           .color = MarkerColor::Purple, .note = note.toStdString()});
        at += clip.duration;
        f.project.tracks[f.main].insertClip(std::move(clip));
    }
    f.project.templateInfo = std::move(f.info);
    return f.project;
}
