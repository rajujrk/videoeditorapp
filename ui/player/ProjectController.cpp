#include "ProjectController.h"

#include "Language.h"
#include "model/ImageSequence.h"
#include "model/RenderCost.h"
#include "VideoView.h"
#include "audio/AudioOutput.h"
#include "audio/AudioRecorder.h"
#include "commands/EditCommands.h"
#include "commands/StructureCommands.h"
#include "decode/ImageFile.h"
#include "decode/AudioDecoder.h"
#include "decode/VideoDecoder.h"
#include "io/Interchange.h"
#include "io/LutFile.h"
#include "io/MediaRelink.h"
#include "io/ProjectSerializer.h"
#include "model/TimelineQuery.h"
#include "gpu/ColorGrader.h"
#include "gpu/EffectLibrary.h"
#include "OverlayTransitions.h"
#include "video/OfxHost.h"
#include "Stickers.h"
#include "audio/AudioEffects.h"
#include "decode/LottieAnimation.h"
#include "gpu/LayerPlacement.h"
#include "decode/Thumbnail.h"
#include "EffectInfo.h"
#include "Exporter.h"
#include "LookLibrary.h"
#include "MicrophoneAccess.h"
#include "StillImage.h"
#include "TitleRenderer.h"
#include "playback/TimelinePlayback.h"
#include "speech/SpeechEnhancer.h"
#include "analysis/PeopleVision.h"
#include "analysis/ContentVision.h"
#include "analysis/MotionAnalysis.h"
#include "analysis/Tracking.h"
#include "encode/VideoProcessing.h"
#include "encode/WavWriter.h"
#include "cloud/CloudFeatures.h"
#include "cloud/StickerCutout.h"
#include "commands/TemplateCommands.h"
#include "commands/DrawingCommands.h"
#include "model/Paths.h"
#include "model/Template.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <QBuffer>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <QProcess>
#include <QJsonDocument>
#include <QGuiApplication>
#include <QJsonObject>
#include <QJsonArray>
#include <QCoreApplication>
#include <QLocale>
#include <QRegularExpression>
#include <QPointer>
#include <numbers>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>
#include <QtGlobal>

#include <array>
#include <cerrno>
#include <csignal>
#include <set>
#include <unistd.h>

#include "StillRenderer.h"
#include "model/ClipAnimation.h"
#include "model/LogoReveal.h"
#include "NativeWindow.h"
#include "model/ColourMatch.h"

#include <QCryptographicHash>
#include <QLinearGradient>
#include <QPainter>
#include <QRadialGradient>

#include <algorithm>
#include <cmath>
#include <functional>
#include <random>

using namespace tve;

namespace {

// People settings as the inspector shows them.
const char* const kFaceCovers[] = {"mosaic", "blur", "fill"};

QVariantMap cutoutMap(const PortraitCutout& c)
{
    return {{QStringLiteral("enabled"), c.enabled},
            {QStringLiteral("subject"), !c.subject.empty()},
            {QStringLiteral("invert"), c.invert},
            {QStringLiteral("softness"), c.softness},
            {QStringLiteral("shift"), c.shift},
            {QStringLiteral("blur"), c.blur}};
}

QVariantMap faceCoverMap(const FaceMosaic& m)
{
    QVariantList ids;
    for (const FaceTrack& t : m.faces)
        ids << t.id;
    return {{QStringLiteral("style"), QString::fromLatin1(kFaceCovers[int(m.style)])},
            {QStringLiteral("amount"), m.amount},
            {QStringLiteral("expand"), m.expand},
            {QStringLiteral("faceIds"), ids}};
}

} // namespace

namespace {

double seconds(Ticks ticks)
{
    return toSeconds(ticks);
}

Ticks ticks(double seconds)
{
    return fromSeconds(seconds);
}

Id toId(double value)
{
    return value > 0 ? Id(std::llround(value)) : kNoId;
}

QString fileName(const std::string& path)
{
    return QFileInfo(QString::fromStdString(path)).fileName();
}

// What a media item is called: its file's name, or a built-in sticker's title.
// A note of what didn't carry over to or from another editor, in the app's language (the
// core counts them: "Effects (left out) — 3 clips").
QString noteText(const std::string& note)
{
    static const QRegularExpression counted(QStringLiteral("^(.*) — (\\d+) clips?$"));
    const QRegularExpressionMatch match = counted.match(QString::fromStdString(note));
    if (!match.hasMatch())
        return coreText(note);
    const QString what = coreText(match.captured(1).toStdString());
    const int count = match.captured(2).toInt();
    return count == 1 ? QCoreApplication::translate("ProjectController", "%1 — 1 clip").arg(what)
                      : QCoreApplication::translate("ProjectController", "%1 — %2 clips").arg(what).arg(count);
}

QString mediaName(const std::string& path)
{
    const QString title = stickers::titleOf(QString::fromStdString(path));
    return title.isEmpty() ? fileName(path) : title;
}

// An image sequence by its pattern ("shot_####.png"), not its first frame's name.
QString mediaName(const MediaItem& media)
{
    return media.sequence ? QString::fromStdString(tve::sequenceDisplayName(*media.sequence)) : mediaName(media.path);
}

QString mediaColourLabel(const ColourSpace& colour)
{
    if (colour.transfer == Transfer::Pq)
        return QStringLiteral("HDR · PQ");
    if (colour.transfer == Transfer::Hlg)
        return QStringLiteral("HDR · HLG");
    if (colour.primaries == Primaries::DisplayP3)
        return QStringLiteral("Display P3");
    if (colour.primaries == Primaries::Rec2020)
        return QStringLiteral("Rec.2020");
    return {};
}

// A media file's streams and length, or nullopt (with `error` set) if it has neither
// pictures nor sound.
std::optional<MediaItem> probeMedia(const std::string& path, QString* error)
{
    MediaItem item{.path = path};
    if (auto video = VideoDecoder::open(path)) {
        const VideoStreamInfo& info = video->info();
        item.width = info.width;
        item.height = info.height;
        item.frameRate = info.frameRate;
        item.duration = info.duration;
        item.alpha = info.hasAlpha;
        item.colour = info.colour;
        item.spherical = info.spherical;
    } else {
        item.hasVideo = false;
    }
    if (auto sound = AudioDecoder::open(path)) {
        item.hasAudio = true;
        item.audioSampleRate = sound->info().sourceSampleRate;
        item.audioChannels = sound->info().sourceChannels;
        if (!item.hasVideo || item.duration <= 0)
            item.duration = sound->info().duration;
    }
    if (!item.hasVideo && !item.hasAudio) {
        *error = QCoreApplication::translate("ProjectController", "Could not import %1: no video or audio found").arg(fileName(path));
        return std::nullopt;
    }
    if (item.duration <= 0) {
        *error = QStringLiteral("%1 has no known duration").arg(fileName(path));
        return std::nullopt;
    }
    return item;
}

// TVE_AUDIO_INPUT=null records silence in real time without a microphone (tests, CI);
// =none opens no device at all.
AudioRecorder::Backend inputBackend()
{
    const QByteArray mode = qgetenv("TVE_AUDIO_INPUT");
    return mode == "null"   ? AudioRecorder::Backend::Null
         : mode == "none" ? AudioRecorder::Backend::None
                          : AudioRecorder::Backend::System;
}

QString easingName(Easing easing)
{
    switch (easing) {
    case Easing::Linear:
        break;
    case Easing::EaseIn:
        return QStringLiteral("easeIn");
    case Easing::EaseOut:
        return QStringLiteral("easeOut");
    case Easing::EaseInOut:
        return QStringLiteral("easeInOut");
    case Easing::Hold:
        return QStringLiteral("hold");
    case Easing::Curve:
        return QStringLiteral("curve");
    }
    return QStringLiteral("linear");
}

const char* const kBlendNames[] = {"normal", "multiply", "screen", "overlay", "add", "darken", "lighten"};

QVariantMap visualMap(const ClipVisual& v)
{
    return {
        {QStringLiteral("x"), v.transform.x},
        {QStringLiteral("y"), v.transform.y},
        {QStringLiteral("scale"), v.transform.scale},
        {QStringLiteral("rotation"), v.transform.rotation},
        {QStringLiteral("cropLeft"), v.crop.left},
        {QStringLiteral("cropTop"), v.crop.top},
        {QStringLiteral("cropRight"), v.crop.right},
        {QStringLiteral("cropBottom"), v.crop.bottom},
        {QStringLiteral("opacity"), v.opacity},
        {QStringLiteral("blend"), QString::fromLatin1(kBlendNames[int(v.blend)])},
        {QStringLiteral("yaw"), v.sphere.yaw},
        {QStringLiteral("pitch"), v.sphere.pitch},
        {QStringLiteral("roll"), v.sphere.roll},
        {QStringLiteral("fov"), v.sphere.fov},
        {QStringLiteral("anchorX"), v.transform.anchorX},
        {QStringLiteral("anchorY"), v.transform.anchorY},
        {QStringLiteral("pinTopLeftX"), v.pin.topLeftX},
        {QStringLiteral("pinTopLeftY"), v.pin.topLeftY},
        {QStringLiteral("pinTopRightX"), v.pin.topRightX},
        {QStringLiteral("pinTopRightY"), v.pin.topRightY},
        {QStringLiteral("pinBottomRightX"), v.pin.bottomRightX},
        {QStringLiteral("pinBottomRightY"), v.pin.bottomRightY},
        {QStringLiteral("pinBottomLeftX"), v.pin.bottomLeftX},
        {QStringLiteral("pinBottomLeftY"), v.pin.bottomLeftY},
        {QStringLiteral("pinned"), v.pin.active()},
    };
}

// A colour clip's fill, for QML.
QVariantMap fillMap(const ColourFill& fill)
{
    const auto list = [](const std::array<double, 4>& c) { return QVariantList{c[0], c[1], c[2], c[3]}; };
    return {{QStringLiteral("kind"), fill.kind == ColourFillKind::Solid    ? QStringLiteral("solid")
                                     : fill.kind == ColourFillKind::Linear ? QStringLiteral("linear")
                                                                           : QStringLiteral("radial")},
            {QStringLiteral("from"), list(fill.from)},
            {QStringLiteral("to"), list(fill.to)},
            {QStringLiteral("angle"), fill.angle}};
}

QVariantList effectsList(const std::vector<ClipEffect>& effects)
{
    QVariantList list;
    for (const ClipEffect& effect : effects) {
        QVariantMap values;
        for (const auto& [param, value] : effect.values) {
            QVariantList components;
            for (float c : value)
                components << double(c);
            values[QString::fromStdString(param)] = components.size() == 1 ? components.front() : QVariant(components);
        }
        list << QVariantMap{
            {QStringLiteral("effectId"), QString::fromStdString(effect.effectId)},
            {QStringLiteral("enabled"), effect.enabled},
            {QStringLiteral("values"), values},
            {QStringLiteral("plugin"), isPluginEffect(effect.effectId)},
        };
    }
    return list;
}

std::vector<float> componentsOf(const QVariant& value)
{
    std::vector<float> components;
    if (value.typeId() == QMetaType::QVariantList || value.canConvert<QVariantList>()) {
        const QVariantList list = value.toList();
        if (!list.isEmpty() && value.typeId() != QMetaType::QString) {
            for (const QVariant& v : list)
                components.push_back(v.toFloat());
            return components;
        }
    }
    components.push_back(value.toFloat());
    return components;
}

QVariantList valueList(const std::vector<float>& value)
{
    QVariantList list;
    for (float c : value)
        list << double(c);
    return list;
}

const char* alignmentName(TransitionAlignment alignment)
{
    switch (alignment) {
    case TransitionAlignment::EndsAtCut:
        return "endsAtCut";
    case TransitionAlignment::StartsAtCut:
        return "startsAtCut";
    case TransitionAlignment::Center:
        break;
    }
    return "center";
}

// The transition into `clip` as it plays (empty when it has none or it is inactive):
// {clipId, transitionId, title, start, end, cut, duration, alignment}, times in seconds.
QVariantMap transitionMap(const Track& track, const Clip& clip)
{
    const auto window = transitionInto(track, clip);
    if (!window)
        return {};
    const LoadedEffect* loaded = EffectLibrary::builtIn().findTransition(window->spec->transitionId);
    return {
        {QStringLiteral("clipId"), double(clip.id)},
        {QStringLiteral("transitionId"), QString::fromStdString(window->spec->transitionId)},
        {QStringLiteral("title"), loaded ? catalogueText(loaded->manifest.displayTitle())
                                         : QCoreApplication::translate("ProjectController", "Unknown transition")},
        {QStringLiteral("start"), seconds(window->start)},
        {QStringLiteral("end"), seconds(window->end)},
        {QStringLiteral("cut"), seconds(window->cut)},
        {QStringLiteral("duration"), seconds(window->end - window->start)},
        {QStringLiteral("alignment"), QString::fromLatin1(alignmentName(window->spec->alignment))},
    };
}

// Autosave sessions of documents open in this process (another process's are told apart
// by whether its pid is alive).
std::set<QString>& liveSessions()
{
    static std::set<QString> sessions;
    return sessions;
}

QString newSessionId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

bool processAlive(qint64 pid)
{
    return pid > 0 && (::kill(pid_t(pid), 0) == 0 || errno == EPERM);
}

constexpr int kAutosaveDelayMs = 2000;       // after edits settle
constexpr qint64 kAutosaveMaxDelayMs = 30000; // even while edits keep coming

// Default track layout for a new project, bottom to top: audio, main video, overlay.
// --- Drawings, as QML sees them ---

const char* const kPathKinds[] = {"brush", "line", "arrow", "rectangle", "ellipse"};

QVariantList colorValue(const ColorRgba& c)
{
    return {c.r, c.g, c.b, c.a};
}

ColorRgba colorOf(const QVariant& value, ColorRgba fallback)
{
    const QVariantList list = value.toList();
    if (list.size() < 3)
        return fallback;
    return {std::clamp(list[0].toDouble(), 0.0, 1.0), std::clamp(list[1].toDouble(), 0.0, 1.0),
            std::clamp(list[2].toDouble(), 0.0, 1.0), list.size() > 3 ? std::clamp(list[3].toDouble(), 0.0, 1.0) : 1.0};
}

QVariantMap pathMap(const DrawnPath& path)
{
    QVariantList points;
    for (const auto& [x, y] : path.points)
        points << QVariant(QVariantList{x, y});
    return {{QStringLiteral("kind"), QLatin1String(kPathKinds[int(path.kind)])},
            {QStringLiteral("points"), points},
            {QStringLiteral("stroke"), colorValue(path.stroke)},
            {QStringLiteral("width"), path.width},
            {QStringLiteral("fill"), colorValue(path.fill)}};
}

// `base` with the keys present in `changes` applied.
DrawnPath pathFrom(const QVariantMap& changes, DrawnPath path = {})
{
    if (changes.contains(QStringLiteral("kind"))) {
        const QString kind = changes.value(QStringLiteral("kind")).toString();
        for (int i = 0; i < 5; ++i)
            if (kind == QLatin1String(kPathKinds[i]))
                path.kind = PathKind(i);
    }
    if (changes.contains(QStringLiteral("points"))) {
        path.points.clear();
        for (const QVariant& point : changes.value(QStringLiteral("points")).toList()) {
            const QVariantList xy = point.toList();
            if (xy.size() == 2)
                path.points.push_back({xy[0].toDouble(), xy[1].toDouble()});
        }
    }
    if (changes.contains(QStringLiteral("stroke")))
        path.stroke = colorOf(changes.value(QStringLiteral("stroke")), path.stroke);
    if (changes.contains(QStringLiteral("width")))
        path.width = changes.value(QStringLiteral("width")).toDouble();
    if (changes.contains(QStringLiteral("fill")))
        path.fill = colorOf(changes.value(QStringLiteral("fill")), path.fill);
    return path;
}

QVariantMap drawingMap(const ClipDrawing& drawing)
{
    QVariantList paths;
    for (const DrawnPath& path : drawing.paths)
        paths << pathMap(path);
    return {{QStringLiteral("paths"), paths},
            {QStringLiteral("drawIn"), drawing.drawIn},
            {QStringLiteral("drawOut"), drawing.drawOut},
            {QStringLiteral("oneByOne"), drawing.oneByOne}};
}

Project emptyProject(int width, int height)
{
    Project project;
    project.settings.width = width;
    project.settings.height = height;
    auto addTrack = [&](TrackKind kind, const char* name) {
        project.tracks.push_back(Track{.id = project.allocateId(), .kind = kind, .name = name});
    };
    addTrack(TrackKind::Audio, "Audio 1");
    addTrack(TrackKind::Video, "Video 1");
    addTrack(TrackKind::Video, "Video 2");
    return project;
}

// --- Animated channels ---
// A channel is one value a clip can animate: a property ("x", "volume", …) or a parameter
// of an effect in its stack, named "effect:<stack index>:<parameter id>", or of one in its
// audio stack, "audioEffect:<stack index>:<parameter id>".

struct EffectChannel {
    std::size_t index = 0;
    std::string param;
    bool audio = false;
};

std::optional<EffectChannel> effectChannel(std::string_view id)
{
    constexpr std::string_view prefix = "effect:";
    constexpr std::string_view audioPrefix = "audioEffect:";
    const bool audio = id.starts_with(audioPrefix);
    if (!audio && !id.starts_with(prefix))
        return std::nullopt;
    const std::string_view rest = id.substr(audio ? audioPrefix.size() : prefix.size());
    const auto colon = rest.find(':');
    if (colon == std::string_view::npos || colon == 0 || colon + 1 == rest.size())
        return std::nullopt;
    std::size_t index = 0;
    for (char c : rest.substr(0, colon)) {
        if (c < '0' || c > '9')
            return std::nullopt;
        index = index * 10 + std::size_t(c - '0');
    }
    return EffectChannel{index, std::string(rest.substr(colon + 1)), audio};
}

std::string effectChannelId(std::size_t index, const std::string& param, bool audio = false)
{
    return (audio ? "audioEffect:" : "effect:") + std::to_string(index) + ":" + param;
}

// The stack a channel's effect is in.
template <typename C>
auto& stackOf(C& clip, const EffectChannel& channel)
{
    return channel.audio ? clip.audioEffects : clip.effects;
}

// The OpenFX plug-ins' descriptions, by id (filled when they're found).
std::map<std::string, OfxPluginInfo>& videoPluginInfos();

// The manifest entry of an effect channel's parameter, if it can be animated (any of a
// plug-in's: 0..1).
const EffectParamSpec* animatableParam(const Clip& clip, const EffectChannel& channel)
{
    const auto& stack = stackOf(clip, channel);
    if (channel.index >= stack.size())
        return nullptr;
    const std::string& effectId = stack[channel.index].effectId;
    if (channel.audio && isPluginEffect(effectId)) {
        static const EffectParamSpec plugin = [] {
            EffectParamSpec spec;
            spec.type = ParamType::Float;
            spec.min = 0.0;
            spec.max = 1.0;
            spec.keyframable = true;
            return spec;
        }();
        return &plugin;
    }
    if (!channel.audio && isOfxEffect(effectId)) {
        // An OpenFX plug-in's number (or one number of a 2D/3D one), in its shown range.
        static std::map<std::string, EffectParamSpec> specs;
        const auto info = videoPluginInfos().find(effectId);
        if (info == videoPluginInfos().end())
            return nullptr;
        const std::string name = channel.param.substr(0, channel.param.find('#'));
        for (const OfxParameterInfo& p : info->second.parameters) {
            if (p.name != name || !p.animates || !(p.type.starts_with("double") || p.type.starts_with("int")))
                continue;
            EffectParamSpec& spec = specs[effectId + "/" + channel.param];
            spec.type = p.type.starts_with("int") ? ParamType::Int : ParamType::Float;
            spec.min = p.min;
            spec.max = p.max;
            spec.keyframable = true;
            spec.defaultValue = {float(p.defaults.empty() ? 0.0 : p.defaults[0])};
            return &spec;
        }
        return nullptr;
    }
    const EffectManifest* manifest = channel.audio ? AudioEffectLibrary::builtIn().find(effectId) : nullptr;
    if (!channel.audio)
        if (const LoadedEffect* effect = EffectLibrary::builtIn().find(effectId))
            manifest = &effect->manifest;
    const EffectParamSpec* spec = manifest ? manifest->findParam(channel.param) : nullptr;
    if (!spec || !spec->keyframable || (spec->type != ParamType::Float && spec->type != ParamType::Int))
        return nullptr;
    return spec;
}

// The range a channel's keyframes must stay in; nullopt if the clip cannot animate it.
std::optional<std::pair<double, double>> channelRange(const Clip& clip, const std::string& id)
{
    if (const AnimatableProperty* p = findAnimatable(id))
        return std::pair{p->min, p->max};
    if (const auto channel = effectChannel(id)) {
        if (const EffectParamSpec* spec = animatableParam(clip, *channel))
            return std::pair{spec->min, spec->max};
    }
    return std::nullopt;
}

const Keyframes* channelKeys(const Clip& clip, const std::string& id)
{
    if (const auto channel = effectChannel(id)) {
        const auto& stack = stackOf(clip, *channel);
        if (channel->index >= stack.size())
            return nullptr;
        const auto& keys = stack[channel->index].keyframes;
        const auto it = keys.find(channel->param);
        return it != keys.end() && !it->second.empty() ? &it->second : nullptr;
    }
    return clip.keyframesOf(id);
}

Keyframes& ensureChannel(Clip& clip, const std::string& id)
{
    if (const auto channel = effectChannel(id))
        return stackOf(clip, *channel)[channel->index].keyframes[channel->param];
    return clip.keyframes[id];
}

// Calls f(id, keyframes) for every animated channel.
template <typename C, typename F>
void forEachChannel(C& clip, F f)
{
    for (auto& [id, keys] : clip.keyframes) {
        if (!keys.empty())
            f(id, keys);
    }
    for (std::size_t i = 0; i < clip.effects.size(); ++i) {
        for (auto& [param, keys] : clip.effects[i].keyframes) {
            if (!keys.empty())
                f(effectChannelId(i, param), keys);
        }
    }
    for (std::size_t i = 0; i < clip.audioEffects.size(); ++i) {
        for (auto& [param, keys] : clip.audioEffects[i].keyframes) {
            if (!keys.empty())
                f(effectChannelId(i, param, true), keys);
        }
    }
}

bool isAnimated(const Clip& clip)
{
    bool any = false;
    forEachChannel(clip, [&](const std::string&, const Keyframes&) { any = true; });
    return any;
}

// Drops channels left without keyframes.
void pruneChannels(Clip& clip)
{
    std::erase_if(clip.keyframes, [](const auto& entry) { return entry.second.empty(); });
    for (ClipEffect& effect : clip.effects)
        std::erase_if(effect.keyframes, [](const auto& entry) { return entry.second.empty(); });
    for (ClipEffect& effect : clip.audioEffects)
        std::erase_if(effect.keyframes, [](const auto& entry) { return entry.second.empty(); });
}

double* visualField(ClipVisual& v, std::string_view id)
{
    if (id == property::kX) return &v.transform.x;
    if (id == property::kY) return &v.transform.y;
    if (id == property::kScale) return &v.transform.scale;
    if (id == property::kRotation) return &v.transform.rotation;
    if (id == property::kOpacity) return &v.opacity;
    if (id == property::kCropLeft) return &v.crop.left;
    if (id == property::kCropTop) return &v.crop.top;
    if (id == property::kCropRight) return &v.crop.right;
    if (id == property::kCropBottom) return &v.crop.bottom;
    if (id == property::kYaw) return &v.sphere.yaw;
    if (id == property::kPitch) return &v.sphere.pitch;
    if (id == property::kRoll) return &v.sphere.roll;
    if (id == property::kFov) return &v.sphere.fov;
    if (id == property::kAnchorX) return &v.transform.anchorX;
    if (id == property::kAnchorY) return &v.transform.anchorY;
    if (id == property::kPinTopLeftX) return &v.pin.topLeftX;
    if (id == property::kPinTopLeftY) return &v.pin.topLeftY;
    if (id == property::kPinTopRightX) return &v.pin.topRightX;
    if (id == property::kPinTopRightY) return &v.pin.topRightY;
    if (id == property::kPinBottomRightX) return &v.pin.bottomRightX;
    if (id == property::kPinBottomRightY) return &v.pin.bottomRightY;
    if (id == property::kPinBottomLeftX) return &v.pin.bottomLeftX;
    if (id == property::kPinBottomLeftY) return &v.pin.bottomLeftY;
    return nullptr;
}

// The channel's value at a timeline time, animated or not.
double channelValue(const Clip& clip, const std::string& id, Ticks time)
{
    if (id == property::kVolume)
        return clip.gainDbAt(time);
    if (const auto channel = effectChannel(id)) {
        if (channel->index >= stackOf(clip, *channel).size())
            return 0.0;
        const ClipEffect effect = (channel->audio ? clip.audioEffectsAt(time) : clip.effectsAt(time))[channel->index];
        if (const auto it = effect.values.find(channel->param); it != effect.values.end() && !it->second.empty())
            return it->second.front();
        const EffectParamSpec* spec = animatableParam(clip, *channel);
        return spec && !spec->defaultValue.empty() ? spec->defaultValue.front() : 0.0;
    }
    ClipVisual v = clip.visualAt(time);
    const double* field = visualField(v, id);
    return field ? *field : 0.0;
}

// Sets the value a channel has when it is not animated.
void setChannelStatic(Clip& clip, const std::string& id, double value)
{
    if (id == property::kVolume) {
        clip.gainDb = float(value);
    } else if (const auto channel = effectChannel(id)) {
        if (channel->index < stackOf(clip, *channel).size())
            stackOf(clip, *channel)[channel->index].values[channel->param] = {float(value)};
    } else if (double* field = visualField(clip.visual, id)) {
        *field = value;
    }
}

QVariantList rampList(const SpeedRamp& ramp)
{
    QVariantList list;
    for (const SpeedPoint& p : ramp)
        list << QVariantMap{{QStringLiteral("at"), p.at}, {QStringLiteral("speed"), p.speed}};
    return list;
}

QVariantMap wheelMap(const ColorWheel& w)
{
    return {{QStringLiteral("x"), w.x}, {QStringLiteral("y"), w.y}, {QStringLiteral("master"), w.master}};
}

QVariantList curveList(const ColorCurve& curve)
{
    QVariantList list;
    for (const CurvePoint& p : curve)
        list << QVariantMap{{QStringLiteral("x"), p.x}, {QStringLiteral("y"), p.y}};
    return list;
}

QVariantList hslList(const ColorGrade& g)
{
    QVariantList list;
    for (const HslBand& band : g.hsl)
        list << QVariantMap{{QStringLiteral("hue"), band.hue},
                            {QStringLiteral("saturation"), band.saturation},
                            {QStringLiteral("luminance"), band.luminance}};
    return list;
}

// A LUT's name: a built-in one's title, else its file name.
QString lutName(const std::string& path)
{
    if (path.empty())
        return {};
    for (const LookInfo& look : builtInLooks()) {
        if (look.grade.lutPath == path)
            return catalogueText(look.title);
    }
    return fileName(path);
}

const char* const kMaskShapes[] = {"rectangle", "ellipse", "line", "star", "heart", "pen"};
const char* const kMatteModes[] = {"none", "alpha", "invertedAlpha", "luma", "invertedLuma"};

QVariantMap maskMap(const ClipMask& m)
{
    QVariantList points;
    for (const auto& p : m.points)
        points << QVariant(QVariantList{p[0], p[1]});
    return {
        {QStringLiteral("shape"), QString::fromLatin1(kMaskShapes[int(m.shape)])},
        {QStringLiteral("x"), m.x},
        {QStringLiteral("y"), m.y},
        {QStringLiteral("width"), m.width},
        {QStringLiteral("height"), m.height},
        {QStringLiteral("rotation"), m.rotation},
        {QStringLiteral("roundness"), m.roundness},
        {QStringLiteral("feather"), m.feather},
        {QStringLiteral("invert"), m.invert},
        {QStringLiteral("points"), points},
        {QStringLiteral("target"), m.target == MaskTarget::Effects ? QStringLiteral("effects")
                                   : m.target == MaskTarget::Colour ? QStringLiteral("colour")
                                                                    : QStringLiteral("picture")},
    };
}

QVariantMap chromaKeyMap(const ChromaKey& k)
{
    return {
        {QStringLiteral("enabled"), k.enabled},
        {QStringLiteral("color"), QVariantList{k.color[0], k.color[1], k.color[2]}},
        {QStringLiteral("tolerance"), k.tolerance},
        {QStringLiteral("softness"), k.softness},
        {QStringLiteral("spill"), k.spill},
    };
}

// `mask` with the fields present in `changes`.
ClipMask maskWith(ClipMask m, const QVariantMap& changes)
{
    const auto number = [&](const char* key, double& target) {
        if (const auto it = changes.constFind(QLatin1String(key)); it != changes.constEnd())
            target = it->toDouble();
    };
    number("x", m.x);
    number("y", m.y);
    number("width", m.width);
    number("height", m.height);
    number("rotation", m.rotation);
    number("roundness", m.roundness);
    number("feather", m.feather);
    if (const auto it = changes.constFind(QStringLiteral("invert")); it != changes.constEnd())
        m.invert = it->toBool();
    if (const auto it = changes.constFind(QStringLiteral("target")); it != changes.constEnd()) {
        const QString target = it->toString();
        m.target = target == QLatin1String("effects") ? MaskTarget::Effects
                 : target == QLatin1String("colour")  ? MaskTarget::Colour
                                                      : MaskTarget::Picture;
    }
    if (const auto it = changes.constFind(QStringLiteral("points")); it != changes.constEnd()) {
        m.points.clear();
        for (const QVariant& point : it->toList()) {
            const QVariantList xy = point.toList();
            if (xy.size() == 2)
                m.points.push_back({xy[0].toDouble(), xy[1].toDouble()});
        }
    }
    return m;
}

ChromaKey chromaKeyWith(ChromaKey k, const QVariantMap& changes)
{
    if (const auto it = changes.constFind(QStringLiteral("enabled")); it != changes.constEnd())
        k.enabled = it->toBool();
    if (const auto it = changes.constFind(QStringLiteral("color")); it != changes.constEnd()) {
        const QVariantList rgb = it->value<QVariantList>();
        if (rgb.size() >= 3)
            k.color = {rgb[0].toDouble(), rgb[1].toDouble(), rgb[2].toDouble()};
    }
    for (auto [key, target] : {std::pair{"tolerance", &k.tolerance}, std::pair{"softness", &k.softness},
                               std::pair{"spill", &k.spill}}) {
        if (const auto it = changes.constFind(QLatin1String(key)); it != changes.constEnd())
            *target = it->toDouble();
    }
    return k;
}

QVariantMap gradeMap(const ColorGrade& g)
{
    return {
        {QStringLiteral("exposure"), g.exposure},
        {QStringLiteral("contrast"), g.contrast},
        {QStringLiteral("saturation"), g.saturation},
        {QStringLiteral("temperature"), g.temperature},
        {QStringLiteral("tint"), g.tint},
        {QStringLiteral("lift"), wheelMap(g.lift)},
        {QStringLiteral("gamma"), wheelMap(g.gamma)},
        {QStringLiteral("gain"), wheelMap(g.gain)},
        {QStringLiteral("offset"), wheelMap(g.offset)},
        {QStringLiteral("black"), wheelMap(g.zones[0])},
        {QStringLiteral("dark"), wheelMap(g.zones[1])},
        {QStringLiteral("shadow"), wheelMap(g.zones[2])},
        {QStringLiteral("light"), wheelMap(g.zones[3])},
        {QStringLiteral("highlight"), wheelMap(g.zones[4])},
        {QStringLiteral("specular"), wheelMap(g.zones[5])},
        {QStringLiteral("master"), curveList(g.master)},
        {QStringLiteral("red"), curveList(g.red)},
        {QStringLiteral("green"), curveList(g.green)},
        {QStringLiteral("blue"), curveList(g.blue)},
        {QStringLiteral("hsl"), hslList(g)},
        {QStringLiteral("hueVsSat"), curveList(g.hueVsSat)},
        {QStringLiteral("hueVsHue"), curveList(g.hueVsHue)},
        {QStringLiteral("lumaVsSat"), curveList(g.lumaVsSat)},
        {QStringLiteral("lutPath"), QString::fromStdString(g.lutPath)},
        {QStringLiteral("lutName"), lutName(g.lutPath)},
        {QStringLiteral("lutMissing"), !g.lutPath.empty() && !QFile::exists(QString::fromStdString(g.lutPath))},
        {QStringLiteral("lutIntensity"), g.lutIntensity},
        {QStringLiteral("protectSkin"), g.protectSkin},
        {QStringLiteral("neutral"), g.isNeutral()},
    };
}

// Times of a clip's keyframes inside it (any channel), seconds from its start, unique.
QVariantList keyframeTimes(const Clip& clip)
{
    std::set<Ticks> times;
    forEachChannel(clip, [&](const std::string&, const Keyframes& keys) {
        for (const Keyframe& k : keys) {
            if (k.time >= 0 && k.time <= clip.duration)
                times.insert(k.time);
        }
    });
    QVariantList list;
    for (Ticks t : times)
        list << seconds(t);
    return list;
}

} // namespace

ProjectController::ProjectController(QObject* parent)
    : QObject(parent), m_playback(std::make_shared<TimelinePlayback>())
{
    // TVE_AUDIO=null renders sound without a device (tests, CI); =none disables output.
    const QByteArray mode = qgetenv("TVE_AUDIO");
    const auto backend = mode == "null" ? AudioOutput::Backend::Null
                       : mode == "none" ? AudioOutput::Backend::None
                                        : AudioOutput::Backend::System;
    m_audioOutput = std::make_unique<AudioOutput>(backend);
    m_audioOutput->setSource(m_playback->audio(), &m_playback->clock());
    m_soundLibrary = new SoundLibrary(this);
    m_cloud = new CloudServices(this);
    m_copilot = new Copilot(this);
    m_voiceCloning = new VoiceCloning(this);
    m_templates = new TemplateLibrary(this);
    m_store = new PackStore(this);
    m_pluginHost = new AudioPluginHost(this);
    connect(m_pluginHost, &AudioPluginHost::edited, this, &ProjectController::pluginEdited);
    // Names shown from here follow the app's language.
    connect(Language::instance(), &Language::changed, this, [this] {
        emit languageChanged();
        emit selectedClipInfoChanged();
        emit stickersChanged();
        emit m_soundLibrary->changed();
    });
    // What search can reach follows the media bin and the transcripts.
    connect(this, &ProjectController::projectChanged, this, &ProjectController::searchIndexChanged);
    connect(this, &ProjectController::transcriptChanged, this, &ProjectController::searchIndexChanged);
    // Plug-ins being looked at follow the project (undo, removal).
    connect(this, &ProjectController::projectChanged, this,
            [this] { m_pluginHost->sync([this](const QString& key) { return audioEffectAt(key); }); });
    connect(m_store, &PackStore::contentChanged, this, [this](const QStringList& kinds) {
        if (kinds.contains(QLatin1String("sounds")))
            m_soundLibrary->refresh();
        if (kinds.contains(QLatin1String("templates")))
            m_templates->refresh();
        if (kinds.contains(QLatin1String("stickers"))) {
            stickers::reload();
            emit stickersChanged();
        }
    });
    m_imports = new MediaImports(this);
    connect(m_imports, &MediaImports::fetched, this, [this](const QStringList& paths, const QStringList& errors) {
        QList<QUrl> files;
        for (const QString& path : paths)
            files << QUrl::fromLocalFile(path);
        if (!files.isEmpty())
            importMedia(files);
        if (!errors.isEmpty())
            reportError(tr("Some items could not be imported: %1").arg(errors.join(QStringLiteral("; "))));
    });

    m_playheadTimer.setInterval(16);
    connect(&m_playheadTimer, &QTimer::timeout, this, &ProjectController::updatePlayhead);
    // Shuttling: the playhead moved through frames at the rate, about 30 times a second.
    m_shuttleTimer.setInterval(33);
    connect(&m_shuttleTimer, &QTimer::timeout, this, [this] {
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        const double elapsed = double(now - m_shuttleLast) / 1000.0;
        m_shuttleLast = now;
        const double end = ::seconds(m_project.duration());
        const double next = std::clamp(m_playhead + m_shuttleRate * elapsed, 0.0, end);
        seek(next);
        if (next <= 0.0 || next >= end)
            pause(); // at an end
    });
    connect(&m_speechModels, &SpeechModels::changed, this, &ProjectController::speechModelsChanged);
    connect(&m_speechModels, &SpeechModels::finished, this, [this](const QString& id, bool ok, const QString& error) {
        if (!ok && !error.isEmpty())
            reportError(error);
        emit speechModelsChanged();
        emit modelDownloadFinished(id, ok, error);
    });
    m_playheadTimer.start();

    const QByteArray autosaveOverride = qgetenv("TVE_AUTOSAVE_DIR");
    m_autosaveDir = !autosaveOverride.isEmpty()
                        ? QString::fromLocal8Bit(autosaveOverride)
                        : QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/Autosave");
    m_sessionId = newSessionId();
    liveSessions().insert(m_sessionId);
    connect(&m_proxies, &ProxyManager::changed, this, [this](const QString&, bool ready) {
        emit proxiesChanged();
        if (ready && m_useProxies)
            updatePlayback(); // switch to it where it is on screen
    });
    m_autosaveTimer.setSingleShot(true);
    m_autosaveTimer.setInterval(kAutosaveDelayMs);
    connect(&m_autosaveTimer, &QTimer::timeout, this, &ProjectController::autosaveNow);

    resetProject(emptyProject(1920, 1080), {});
}

ProjectController::~ProjectController()
{
    // A running export is cancelled (even while paused); its queued notifications die with
    // this object.
    {
        std::lock_guard lock(m_pauseMutex);
        m_cancelExport = true;
    }
    m_pauseChanged.notify_all();
    if (m_exportThread.joinable())
        m_exportThread.join();
    m_cancelAnalysis = true;
    if (m_analysisThread.joinable())
        m_analysisThread.join();
    m_cancelRender = true;
    if (m_renderThread.joinable())
        m_renderThread.join();
    // Quitting with unsaved changes keeps them recoverable; otherwise nothing is left behind.
    blockSignals(true); // nothing should react to a half-destroyed controller
    closeVoiceOver();   // a take in progress lands on the timeline first
    if (m_autosaveEnabled && modified())
        autosaveNow();
    else
        removeAutosave();
    liveSessions().erase(m_sessionId);
    m_audioOutput.reset(); // stop the device callback before the playback it reads from
}

QVariantMap ProjectController::audioStats() const
{
    return {
        {QStringLiteral("device"), QString::fromStdString(m_audioOutput->deviceName())},
        {QStringLiteral("running"), m_audioOutput->isRunning()},
        {QStringLiteral("latencyMs"), double(m_audioOutput->latency()) * 1000.0 / double(kTicksPerSecond)},
        {QStringLiteral("underruns"), qint64(m_audioOutput->underruns())},
    };
}

QString ProjectController::audioDevice() const
{
    if (!m_audioOutput->isRunning())
        return QString::fromStdString(m_audioOutput->errorString());
    return QString::fromStdString(m_audioOutput->deviceName());
}

void ProjectController::setView(VideoView* view)
{
    if (view == m_view)
        return;
    m_view = view;
    if (m_view)
        m_view->setFrameSource(m_playback, QStringLiteral("Timeline"));
    emit viewChanged();
}

// --- State exposed to QML ---

QString ProjectController::name() const
{
    return m_filePath.isEmpty() ? tr("Untitled project") : QFileInfo(m_filePath).completeBaseName();
}

double ProjectController::frameRate() const
{
    return double(m_project.settings.frameRate.num) / m_project.settings.frameRate.den;
}

bool ProjectController::setSphericalProject(bool on)
{
    if (on == m_project.settings.spherical)
        return true;
    ProjectSettings settings = m_project.settings;
    settings.spherical = on;
    if (on) {
        // The canvas of the 360° media (the first on the timeline, else in the library).
        const MediaItem* sphere = nullptr;
        for (const Track& track : m_project.tracks)
            for (const Clip& clip : track.clips)
                if (const MediaItem* media = m_project.findMedia(clip.mediaId); !sphere && media && media->spherical)
                    sphere = media;
        for (const MediaItem& media : m_project.media)
            if (!sphere && media.spherical)
                sphere = &media;
        settings.width = sphere ? sphere->width : 3840;
        settings.height = sphere ? sphere->height : 1920;
    } else {
        // A flat 16:9 picture, as sharp as the sphere allows: 4K from a sphere about as tall.
        settings.height = settings.height >= 1800 ? 2160 : settings.height >= 1000 ? 1080 : 720;
        settings.width = settings.height * 16 / 9;
    }
    m_canvasChosen = true;
    return apply(std::make_unique<SetProjectSettingsCommand>(settings, on ? TVE_TR("core", "Make 360° Project") : TVE_TR("core", "Make Flat Project")));
}

void ProjectController::setProjectColour(const QString& colour)
{
    const ProjectColour wanted = projectColourFrom(colour.toStdString());
    if (wanted == m_project.settings.colour)
        return;
    ProjectSettings settings = m_project.settings;
    settings.colour = wanted;
    apply(std::make_unique<SetProjectSettingsCommand>(settings));
}

double ProjectController::duration() const
{
    return seconds(m_project.duration());
}

namespace {

// A multicam clip's angles: [{index, name, and its camera's file: mediaId, path, duration}].
QVariantList angleListOf(const Project& project, const Clip& clip)
{
    QVariantList list;
    for (std::size_t i = 0; i < clip.angles.size(); ++i) {
        const Sequence* angle = project.findSequence(clip.angles[i]);
        const MediaItem* media = nullptr;
        if (angle)
            for (const Track& track : angle->tracks)
                if (track.kind == TrackKind::Video && !track.clips.empty() && !media)
                    media = project.findMedia(track.clips.front().mediaId);
        list << QVariantMap{{QStringLiteral("index"), int(i)},
                            {QStringLiteral("name"), angle ? QString::fromStdString(angle->name) : QString()},
                            {QStringLiteral("mediaId"), media ? double(media->id) : 0.0},
                            {QStringLiteral("path"), media ? QString::fromStdString(media->path) : QString()},
                            {QStringLiteral("duration"), media ? toSeconds(media->duration) : 0.0}};
    }
    return list;
}

} // namespace

QVariantList ProjectController::tracks() const
{
    const Id main = mainTrackId();
    std::set<Id> missing;
    for (const MediaItem* item : tve::missingMedia(m_project))
        missing.insert(item->id);
    QVariantList list;
    for (auto track = m_project.tracks.rbegin(); track != m_project.tracks.rend(); ++track) {
        QVariantList clips;
        for (const Clip& clip : track->clips) {
            const MediaItem* media = m_project.findMedia(clip.mediaId);
            const Sequence* sequence = clip.compound() ? m_project.findSequence(clip.sequenceId) : nullptr;
            const QString label = clip.text      ? QString::fromStdString(clip.text->text).section(QLatin1Char('\n'), 0, 0)
                                  : clip.adjustment ? tr("Adjustment layer")
                                  : clip.drawing ? tr("Drawing")
                                  : sequence       ? QString::fromStdString(sequence->name)
                                  : media          ? mediaName(*media)
                                                   : tr("Missing media");
            const bool sequenceSound = sequence && std::ranges::any_of(sequence->tracks, [&](const Track& t) {
                return std::ranges::any_of(t.clips, [&](const Clip& c) {
                    const MediaItem* m = m_project.findMedia(c.mediaId);
                    return c.compound() || (m && m->hasAudio);
                });
            });
            clips << QVariantMap{
                {QStringLiteral("id"), double(clip.id)},
                {QStringLiteral("mediaId"), double(clip.mediaId)},
                {QStringLiteral("label"), label},
                {QStringLiteral("isTitle"), clip.text.has_value()},
                {QStringLiteral("isDrawing"), clip.drawing.has_value()},
                {QStringLiteral("isAdjustment"), clip.adjustment},
                {QStringLiteral("isCompound"), sequence != nullptr},
                {QStringLiteral("isMulticam"), clip.multicam()},
                {QStringLiteral("angle"), clip.angle()},
                {QStringLiteral("angles"), clip.multicam() ? angleListOf(m_project, clip) : QVariantList()},
                {QStringLiteral("missing"), media && missing.contains(media->id)},
                {QStringLiteral("groupId"), double(clip.groupId)},
                {QStringLiteral("isStill"), media && media->still},
                {QStringLiteral("lookTitle"), clip.look ? QString::fromStdString(clip.look->title) : QString()},
                {QStringLiteral("start"), seconds(clip.start)},
                {QStringLiteral("duration"), seconds(clip.duration)},
                {QStringLiteral("sourceIn"), seconds(clip.sourceIn)},
                {QStringLiteral("mediaDuration"), sequence ? seconds(sequence->duration())
                                                  : media  ? seconds(media->duration)
                                                           : 0.0},
                {QStringLiteral("path"), media ? QString::fromStdString(media->path) : QString()},
                // What it sounds like: its cleaned sound when enhanced (and that file is there).
                {QStringLiteral("soundPath"),
                 clip.enhancedAudio && QFileInfo::exists(QString::fromStdString(clip.enhancedAudio->path))
                     ? QString::fromStdString(clip.enhancedAudio->path)
                     : media ? QString::fromStdString(media->path)
                             : QString()},
                {QStringLiteral("hasAudio"), sequenceSound || (media && media->hasAudio)},
                {QStringLiteral("audioEnabled"), clip.audioEnabled},
                {QStringLiteral("gainDb"), double(clip.gainDb)},
                {QStringLiteral("fadeIn"), seconds(clip.fadeIn)},
                {QStringLiteral("fadeOut"), seconds(clip.fadeOut)},
                {QStringLiteral("speed"), clip.speed},
                {QStringLiteral("reversed"), clip.reversed},
                {QStringLiteral("freeze"), clip.freeze},
                {QStringLiteral("speedRamp"), rampList(clip.speedRamp)},
                {QStringLiteral("keyframeTimes"), keyframeTimes(clip)},
                {QStringLiteral("mediaWidth"), media ? media->width : 0},
                {QStringLiteral("mediaHeight"), media ? media->height : 0},
                {QStringLiteral("transition"), transitionMap(*track, clip)},
            };
        }
        list << QVariantMap{
            {QStringLiteral("id"), double(track->id)},
            {QStringLiteral("name"), QString::fromStdString(track->name)},
            {QStringLiteral("kind"), track->kind == TrackKind::Video ? QStringLiteral("video")
                                     : track->kind == TrackKind::Audio ? QStringLiteral("audio")
                                                                       : QStringLiteral("title")},
            {QStringLiteral("main"), track->id == main},
            {QStringLiteral("locked"), track->locked},
            {QStringLiteral("hidden"), track->hidden},
            {QStringLiteral("muted"), track->muted},
            {QStringLiteral("gainDb"), double(track->gainDb)},
            {QStringLiteral("pan"), double(track->pan)},
            {QStringLiteral("solo"), track->solo},
            {QStringLiteral("captions"), track->captions},
            {QStringLiteral("audioEffects"), effectsList(track->audioEffects)},
            {QStringLiteral("clips"), clips},
        };
    }
    return list;
}

QVariantList ProjectController::media() const
{
    std::set<Id> missing;
    for (const MediaItem* item : tve::missingMedia(m_project))
        missing.insert(item->id);
    QVariantList list;
    for (const MediaItem& item : m_project.media) {
        list << QVariantMap{
            {QStringLiteral("id"), double(item.id)},
            {QStringLiteral("name"), mediaName(item)},
            {QStringLiteral("path"), QString::fromStdString(item.path)},
            {QStringLiteral("duration"), seconds(item.duration)},
            {QStringLiteral("width"), item.width},
            {QStringLiteral("height"), item.height},
            {QStringLiteral("frameRate"), double(item.frameRate.num) / item.frameRate.den},
            {QStringLiteral("hasVideo"), item.hasVideo},
            {QStringLiteral("hasAudio"), item.hasAudio},
            {QStringLiteral("still"), item.still},
            {QStringLiteral("sticker"), item.lottie},
            {QStringLiteral("alpha"), item.alpha},
            // An image sequence's frame count (0: not one).
            {QStringLiteral("sequenceFrames"), item.sequence ? double(item.sequence->length()) : 0.0},
            // "HDR · PQ", "HDR · HLG", "Display P3", "Rec.2020" or "" (SDR Rec.709).
            {QStringLiteral("colour"), mediaColourLabel(item.colour)},
            {QStringLiteral("hdr"), isHdr(item.colour.transfer)},
            {QStringLiteral("spherical"), item.spherical},
            {QStringLiteral("missing"), missing.contains(item.id)},
            {QStringLiteral("placeholder"), item.placeholder},
            {QStringLiteral("colourFill"), item.colourFill ? QVariant(fillMap(*item.colourFill)) : QVariant()},
            {QStringLiteral("generated"),
             item.generated ? QVariant(QVariantMap{{QStringLiteral("provider"), QString::fromStdString(item.generated->provider)},
                                                   {QStringLiteral("model"), QString::fromStdString(item.generated->model)},
                                                   {QStringLiteral("kind"), QString::fromStdString(item.generated->kind)},
                                                   {QStringLiteral("prompt"), QString::fromStdString(item.generated->prompt)},
                                                   {QStringLiteral("reference"), QString::fromStdString(item.generated->reference)},
                                                   {QStringLiteral("source"), QString::fromStdString(item.generated->source)},
                                                   {QStringLiteral("created"), item.generated->created * 1000.0}})
                            : QVariant()},
        };
    }
    return list;
}

QString ProjectController::undoText() const
{
    return coreText(m_stack->undoLabel());
}

QString ProjectController::redoText() const
{
    return coreText(m_stack->redoLabel());
}

bool ProjectController::playing() const
{
    return m_playback->clock().playing();
}

void ProjectController::setSelectedClip(double clipId)
{
    const Id id = toId(clipId);
    // A grouped clip selects its whole group.
    std::vector<Id> selection;
    if (id != kNoId) {
        const auto location = m_project.locateClip(id);
        if (location && location->clip->groupId != kNoId)
            selection = groupMembers(m_project, location->clip->groupId);
        else
            selection = {id};
    }
    if (id == m_selectedClip && (id == kNoId || m_selectedTransition == kNoId) && selection == m_selectedClips)
        return;
    m_selectedClip = id;
    m_selectedClips = std::move(selection);
    if (id != kNoId)
        m_selectedTransition = kNoId;
    // Mask editing and key picking belong to the clip they started on.
    if (m_activeMask != -1 || m_pickingKeyColor) {
        m_activeMask = -1;
        m_pickingKeyColor = false;
        emit editingChanged();
    }
    emit selectionChanged();
    emit selectedClipInfoChanged();
}

void ProjectController::setActiveMask(int index)
{
    if (index == m_activeMask)
        return;
    m_activeMask = index;
    emit editingChanged();
}

void ProjectController::setPickingKeyColor(bool picking)
{
    if (picking == m_pickingKeyColor)
        return;
    m_pickingKeyColor = picking;
    emit editingChanged();
}

void ProjectController::setSelectedTransition(double clipId)
{
    const Id id = toId(clipId);
    if (id == m_selectedTransition && (id == kNoId || m_selectedClip == kNoId))
        return;
    m_selectedTransition = id;
    if (id != kNoId) {
        m_selectedClip = kNoId;
        m_selectedClips.clear();
    }
    emit selectionChanged();
    emit selectedClipInfoChanged();
}

QVariantList ProjectController::availableTransitions() const
{
    // Everyday ones first: by the first category in this order, then as loaded.
    static const std::vector<std::string> kOrder = {"basic", "slide", "wipe", "shape", "zoom",    "blur",
                                                    "stylize", "3d",  "distort", "luma", "light"};
    const auto rank = [](const LoadedEffect& t) {
        const auto& categories = t.manifest.categories;
        const auto it = categories.empty() ? kOrder.end() : std::ranges::find(kOrder, categories.front());
        return std::distance(kOrder.begin(), it);
    };
    std::vector<const LoadedEffect*> sorted;
    for (const LoadedEffect& transition : EffectLibrary::builtIn().transitions())
        sorted.push_back(&transition);
    std::ranges::stable_sort(sorted, {}, [&](const LoadedEffect* t) { return rank(*t); });
    QVariantList list;
    for (const LoadedEffect* transition : sorted)
        list << describeEffect(*transition);
    return list;
}

QVariantList ProjectController::overlayTransitions() const
{
    QVariantList list;
    for (const overlays::Kind& kind : overlays::kinds())
        list << QVariantMap{{QStringLiteral("id"), QString::fromStdString(kind.id)},
                            {QStringLiteral("title"), catalogueText(kind.title)},
                            {QStringLiteral("blend"), QString::fromLatin1(kBlendNames[int(kind.blend)])}};
    return list;
}

QVariantMap ProjectController::selectedTransitionInfo() const
{
    auto location = const_cast<Project&>(m_project).locateClip(m_selectedTransition);
    if (!location || !location->clip->transitionIn)
        return {};
    QVariantMap info = transitionMap(*location->track, *location->clip);
    if (info.isEmpty())
        return {};
    const ClipTransition& spec = *location->clip->transitionIn;
    QVariantMap values;
    for (const auto& [id, value] : spec.values)
        values[QString::fromStdString(id)] = value.size() == 1 ? QVariant(double(value[0])) : QVariant(valueList(value));
    info[QStringLiteral("values")] = values;
    info[QStringLiteral("requestedDuration")] = seconds(spec.duration);
    if (const LoadedEffect* loaded = EffectLibrary::builtIn().findTransition(spec.transitionId))
        info[QStringLiteral("params")] = describeEffect(*loaded).value(QStringLiteral("params"));
    return info;
}

// --- Lifecycle ---

void ProjectController::resetProject(Project project, const QString& filePath)
{
    pause(); // also ends a take in progress, in the project it was recorded for
    // Unsaved work being replaced stays recoverable under its own session; the new
    // document gets a new one.
    if (m_stack && modified() && m_autosaveEnabled) {
        autosaveNow();
        liveSessions().erase(m_sessionId);
        m_sessionId = newSessionId();
        liveSessions().insert(m_sessionId);
        emit recoveryChanged();
    } else if (m_stack) {
        removeAutosave();
    }
    const bool hadTabs = !m_openLevels.empty();
    m_openLevels.clear(); // (open compound clips belonged to the project being replaced)
    m_project = std::move(project);
    m_stack = std::make_unique<CommandStack>(m_project);
    if (hadTabs)
        emit timelineTabsChanged();
    if (m_copilot)
        m_copilot->startOver(); // (its conversation was about the other project)
    m_filePath = filePath;
    m_selectedClip = kNoId;
    m_selectedClips.clear();
    m_canvasChosen = !filePath.isEmpty();
    m_playback->seek(0);
    publish();
    emit selectionChanged();
}

void ProjectController::newProject(int width, int height, const QString&)
{
    resetProject(emptyProject(std::max(width, 16), std::max(height, 16)), {});
    m_canvasChosen = true;
}

bool ProjectController::open(const QUrl& file)
{
    const QString path = file.isLocalFile() ? file.toLocalFile() : file.toString();
    if (interchangeFormatOf(path.toStdString()))
        return openTimeline(file);
    try {
        resetProject(loadProject(path.toStdString()), path);
        requestAutoProxies();
        return true;
    } catch (const std::exception& e) {
        reportError(tr("Could not open %1: %2").arg(QFileInfo(path).fileName(), coreText(e.what())));
        return false;
    }
}

bool ProjectController::openTimeline(const QUrl& file)
{
    QString path = file.isLocalFile() ? file.toLocalFile() : file.toString();
    const QString shown = QFileInfo(path).fileName();
    const auto format = interchangeFormatOf(path.toStdString());
    if (!format || *format == InterchangeFormat::Edl) {
        reportError(tr("%1 isn't a timeline this app can open (OpenTimelineIO or Final Cut Pro XML)").arg(shown));
        return false;
    }
    if (QFileInfo(path).isDir())
        path += QStringLiteral("/Info.fcpxml"); // a .fcpxmld bundle
    QFile in(path);
    if (!in.open(QIODevice::ReadOnly) || in.size() > 64 * 1024 * 1024) {
        reportError(tr("Cannot read %1").arg(shown));
        return false;
    }
    ImportedTimeline imported;
    try {
        imported = importTimeline(in.readAll().toStdString(), *format, QFileInfo(path).absolutePath().toStdString());
    } catch (const std::exception& e) {
        reportError(tr("Could not open %1: %2").arg(shown, coreText(e.what())));
        return false;
    }
    Project& project = imported.project;
    // What the files are, where they are (the rest stay, to relink).
    for (MediaItem& media : project.media) {
        if (!QFileInfo::exists(QString::fromStdString(media.path)))
            continue;
        QString error;
        if (auto probed = describeMedia(media.path, &error)) {
            probed->id = media.id;
            probed->duration = std::max(probed->duration, probed->still ? media.duration : Ticks(0));
            media = *probed;
        }
    }
    if (!imported.canvasStated) {
        const auto video = std::ranges::find_if(project.media, [](const MediaItem& m) {
            return m.hasVideo && !m.still && m.width > 0 && m.height > 0;
        });
        if (video != project.media.end()) {
            project.settings.width = video->width - video->width % 2;
            project.settings.height = video->height - video->height % 2;
        }
    }
    resetProject(std::move(project), {});
    m_canvasChosen = true;
    requestAutoProxies();
    QStringList notes;
    for (const std::string& note : imported.notes)
        notes << noteText(note);
    emit timelineOpened(notes, shown);
    return true;
}

QVariantMap ProjectController::exportTimeline(const QUrl& file, const QString& format)
{
    if (!m_openLevels.empty())
        closeCompoundClip(0); // (the whole project: back to the main timeline first)
    const QString path = file.isLocalFile() ? file.toLocalFile() : file.toString();
    std::optional<InterchangeFormat> kind = format == QLatin1String("otio")     ? std::optional(InterchangeFormat::Otio)
                                          : format == QLatin1String("fcpxml") ? std::optional(InterchangeFormat::Fcpxml)
                                          : format == QLatin1String("edl")    ? std::optional(InterchangeFormat::Edl)
                                                                              : interchangeFormatOf(path.toStdString());
    if (!kind) {
        reportError(tr("Choose OpenTimelineIO (.otio), Final Cut Pro XML (.fcpxml) or an EDL (.edl)"));
        return {{QStringLiteral("ok"), false}};
    }
    if (m_project.duration() == 0) {
        reportError(tr("Add something to the timeline to export"));
        return {{QStringLiteral("ok"), false}};
    }
    const ExportedTimeline out = tve::exportTimeline(m_project, *kind, name().toStdString());
    QSaveFile save(path);
    if (!save.open(QIODevice::WriteOnly) || save.write(QByteArray::fromStdString(out.text)) < 0 || !save.commit()) {
        reportError(tr("Cannot write %1").arg(path));
        return {{QStringLiteral("ok"), false}};
    }
    QStringList notes;
    for (const std::string& note : out.notes)
        notes << noteText(note);
    return {{QStringLiteral("ok"), true}, {QStringLiteral("notes"), notes}};
}

bool ProjectController::save(const QUrl& file)
{
    QString path = file.isEmpty() ? m_filePath : (file.isLocalFile() ? file.toLocalFile() : file.toString());
    if (path.isEmpty()) {
        reportError(tr("Choose where to save the project"));
        return false;
    }
    if (!path.endsWith(QLatin1String(".tve")))
        path += QLatin1String(".tve");
    try {
        saveProject(rootProject(), path.toStdString());
    } catch (const std::exception& e) {
        reportError(tr("Could not save: %1").arg(coreText(e.what())));
        return false;
    }
    m_filePath = path;
    m_stack->setClean();
    // Saved inside an open compound clip: the file has its edits, which reach the histories
    // it was opened from only when it closes.
    for (OpenLevel& level : m_openLevels) {
        level.savedInside = true;
        level.stack->setUnsaved();
    }
    removeAutosave(); // the file has it all now
    emit projectChanged();
    return true;
}

// --- Media ---

std::optional<MediaItem> ProjectController::describeMedia(const std::string& path, QString* error) const
{
    std::optional<MediaItem> item;
    if (isLottieFile(path)) {
        // A sticker: plays in a loop for as long as its clips last (5 seconds when placed).
        const auto animation = stickers::open(QString::fromStdString(path));
        if (!animation) {
            *error = QStringLiteral("%1 is not a Lottie animation this app can play").arg(fileName(path));
            return std::nullopt;
        }
        item = MediaItem{.path = path, .duration = kStillDuration, .hasVideo = true, .hasAudio = false,
                         .width = animation->width(), .height = animation->height(), .still = true, .alpha = true,
                         .lottie = true};
    } else if (still::isStillImage(QString::fromStdString(path))) {
        // A photo or graphic: one picture, 5 seconds when first placed.
        const auto size = still::size(QString::fromStdString(path));
        item = MediaItem{.path = path, .duration = kStillDuration, .hasVideo = true, .hasAudio = false,
                         .width = size ? size->width() : 0, .height = size ? size->height() : 0, .still = true};
    } else {
        item = probeMedia(path, error);
    }
    if (item)
        item->fingerprint = fingerprintFile(path).value_or(std::string());
    return item;
}

QVariantList ProjectController::missingMedia() const
{
    QVariantList list;
    for (const MediaItem* item : tve::missingMedia(m_project)) {
        const QFileInfo file(QString::fromStdString(item->path));
        list << QVariantMap{{QStringLiteral("id"), double(item->id)},
                            {QStringLiteral("name"), mediaName(*item)},
                            {QStringLiteral("path"), file.filePath()},
                            {QStringLiteral("folder"), file.path()}};
    }
    return list;
}

bool ProjectController::relinkMedia(double mediaId, const QUrl& file, bool force)
{
    m_relinkDifferent = false;
    const MediaItem* media = m_project.findMedia(toId(mediaId));
    if (!media) {
        reportError(tr("Media not found"));
        return false;
    }
    const std::string path = (file.isLocalFile() ? file.toLocalFile() : file.toString()).toStdString();
    QString error;
    std::optional<MediaItem> item;
    if (media->sequence) {
        // An image sequence: the run of numbered pictures the chosen one is in, at the rate
        // it had (any frame of it can be chosen).
        const auto sequence = tve::findImageSequence(path);
        if (!sequence) {
            reportError(tr("%1 has no other numbered pictures beside it in its folder")
                            .arg(QFileInfo(QString::fromStdString(path)).fileName()));
            return false;
        }
        item = *media;
        item->path = tve::sequenceFrameFile(std::filesystem::path(path).parent_path().string(), *sequence, sequence->first);
        item->sequence = sequence;
        item->duration = sequence->length() * tve::frameDuration(media->frameRate);
        item->fingerprint = fingerprintFile(item->path).value_or(std::string());
    } else {
        item = describeMedia(path, &error);
    }
    if (!item) {
        reportError(error);
        return false;
    }
    // The clips need what the old file had: pictures for pictures, sound for sound.
    if ((media->hasVideo && !item->hasVideo) || (media->still != item->still) || (!media->hasVideo && !item->hasAudio)) {
        reportError(QStringLiteral("%1 is a different kind of file from %2").arg(fileName(path), fileName(media->path)));
        return false;
    }
    if (!force && !media->fingerprint.empty() && item->fingerprint != media->fingerprint) {
        m_relinkDifferent = true;
        reportError(QStringLiteral("%1 is not the same file as the one imported").arg(fileName(path)));
        return false;
    }
    item->id = media->id;
    if (!apply(std::make_unique<RelinkMediaCommand>(std::vector{std::move(*item)})))
        return false;
    requestAutoProxies();
    return true;
}

int ProjectController::relinkFromFolder(const QUrl& folder)
{
    const QString directory = folder.isLocalFile() ? folder.toLocalFile() : folder.toString();
    const auto missing = tve::missingMedia(m_project);
    if (missing.empty())
        return 0;
    std::vector<MediaItem> relinked;
    for (const auto& [id, path] : findMissingMedia(missing, directory.toStdString())) {
        QString error;
        if (auto item = describeMedia(path, &error)) {
            item->id = id;
            relinked.push_back(std::move(*item));
        }
    }
    if (relinked.empty()) {
        reportError(tr("None of the missing files are in %1").arg(QFileInfo(directory).fileName()));
        return 0;
    }
    const int count = int(relinked.size());
    if (!apply(std::make_unique<RelinkMediaCommand>(std::move(relinked))))
        return 0;
    requestAutoProxies();
    return count;
}

int ProjectController::importMedia(const QList<QUrl>& files)
{
    int imported = 0;
    for (const QUrl& url : files) {
        const std::string path = (url.isLocalFile() ? url.toLocalFile() : url.toString()).toStdString();
        if (std::ranges::any_of(m_project.media, [&](const MediaItem& m) { return m.path == path; })) {
            ++imported;
            continue;
        }
        QString error;
        std::optional<MediaItem> item = describeMedia(path, &error);
        if (!item) {
            reportError(error);
            continue;
        }
        if (apply(std::make_unique<ImportMediaCommand>(std::move(*item))))
            ++imported;
    }
    requestAutoProxies();
    return imported;
}

QVariantMap ProjectController::findImageSequence(const QUrl& frame) const
{
    const std::string path = (frame.isLocalFile() ? frame.toLocalFile() : frame.toString()).toStdString();
    const auto sequence = tve::findImageSequence(path);
    if (!sequence)
        return {{QStringLiteral("found"), false},
                {QStringLiteral("error"), tr("%1 has no other numbered pictures beside it in its folder")
                                              .arg(QFileInfo(QString::fromStdString(path)).fileName())}};
    const std::string folder = std::filesystem::path(path).parent_path().string();
    const std::string first = tve::sequenceFrameFile(folder, *sequence, sequence->first);
    std::int64_t missing = 0;
    for (std::int64_t n = sequence->first; n <= sequence->last; ++n) {
        std::error_code error;
        if (!std::filesystem::is_regular_file(tve::sequenceFrameFile(folder, *sequence, n), error))
            ++missing;
    }
    const auto info = tve::imagefile::probe(first);
    if (!info)
        return {{QStringLiteral("found"), false},
                {QStringLiteral("error"), tr("%1 is not a picture this app can read")
                                              .arg(QFileInfo(QString::fromStdString(first)).fileName())}};
    return {{QStringLiteral("found"), true},
            {QStringLiteral("name"), QString::fromStdString(tve::sequenceDisplayName(*sequence))},
            {QStringLiteral("firstFile"), QFileInfo(QString::fromStdString(first)).fileName()},
            {QStringLiteral("lastFile"),
             QFileInfo(QString::fromStdString(tve::sequenceFrameFile(folder, *sequence, sequence->last))).fileName()},
            {QStringLiteral("frames"), double(sequence->length())},
            {QStringLiteral("missing"), double(missing)},
            {QStringLiteral("width"), info->width},
            {QStringLiteral("height"), info->height},
            {QStringLiteral("alpha"), info->alpha}};
}

double ProjectController::importImageSequence(const QUrl& frame, int rateNum, int rateDen)
{
    const std::string chosen = (frame.isLocalFile() ? frame.toLocalFile() : frame.toString()).toStdString();
    const auto sequence = tve::findImageSequence(chosen);
    if (!sequence) {
        reportError(tr("%1 has no other numbered pictures beside it in its folder")
                        .arg(QFileInfo(QString::fromStdString(chosen)).fileName()));
        return -1;
    }
    if (rateNum <= 0 || rateDen <= 0 || rateNum / rateDen > 240) {
        reportError(tr("Choose a frame rate from 1 to 240 frames a second"));
        return -1;
    }
    const std::string folder = std::filesystem::path(chosen).parent_path().string();
    const std::string first = tve::sequenceFrameFile(folder, *sequence, sequence->first);
    const tve::FrameRate rate{rateNum, rateDen};
    // Imported already at this rate: that one.
    for (const MediaItem& m : m_project.media)
        if (m.path == first && m.sequence == sequence && m.frameRate == rate)
            return double(m.id);
    // (Its numbers are the files found, so the first is a file there.)
    const auto info = tve::imagefile::probe(first);
    if (!info) {
        reportError(tr("%1 is not a picture this app can read").arg(QFileInfo(QString::fromStdString(first)).fileName()));
        return -1;
    }
    MediaItem item{.path = first,
                   .duration = sequence->length() * tve::frameDuration(rate),
                   .hasVideo = true,
                   .hasAudio = false,
                   // (Even sizes for opaque ones, as the decoder makes them: NV12 halves them.)
                   .width = info->alpha ? info->width : std::max(2, info->width & ~1),
                   .height = info->alpha ? info->height : std::max(2, info->height & ~1),
                   .frameRate = rate,
                   .alpha = info->alpha};
    item.sequence = sequence;
    item.fingerprint = fingerprintFile(item.path).value_or(std::string());
    tve::registerImageSequence(item.path, *sequence, rate); // before the library asks for its thumbnail
    if (!apply(std::make_unique<ImportMediaCommand>(std::move(item))))
        return -1;
    requestAutoProxies();
    return double(m_project.media.back().id);
}

bool ProjectController::addClip(double mediaId, double trackId, double start)
{
    const MediaItem* media = m_project.findMedia(toId(mediaId));
    if (!media) {
        reportError(tr("Media not found"));
        return false;
    }
    Id target = toId(trackId);
    if (trackId < 0) {
        target = mainTrackId();
        if (!media->hasVideo) { // sound-only files go on the first audio track
            for (const Track& t : m_project.tracks) {
                if (t.kind == TrackKind::Audio) {
                    target = t.id;
                    break;
                }
            }
        }
    }
    const Track* track = m_project.findTrack(target);
    if (!track) {
        reportError(tr("Track not found"));
        return false;
    }
    // An untouched project takes its canvas from the first video clip.
    const bool projectEmpty = std::ranges::all_of(m_project.tracks, [](const Track& t) { return t.clips.empty(); });
    // (A photo's size is no guide to the video's, so stills leave the canvas alone.)
    if (!m_canvasChosen && projectEmpty && media->hasVideo && !media->still && media->width > 0 && media->height > 0) {
        ProjectSettings settings = m_project.settings;
        settings.width = media->width;
        settings.height = media->height;
        settings.frameRate = media->frameRate;
        settings.spherical = media->spherical; // 360° footage first: a 360° project
        apply(std::make_unique<SetProjectSettingsCommand>(settings));
        m_canvasChosen = true;
    }
    const Ticks at = start < 0 ? (track->clips.empty() ? 0 : track->clips.back().end()) : ticks(start);
    auto command = std::make_unique<AddClipCommand>(target, media->id, std::max<Ticks>(at, 0), media->duration, 0);
    auto* raw = command.get();
    if (!apply(std::move(command)))
        return false;
    setSelectedClip(double(raw->clipId()));
    return true;
}

bool ProjectController::importAndAppend(const QUrl& file)
{
    if (importMedia({file}) == 0)
        return false;
    const std::string path = (file.isLocalFile() ? file.toLocalFile() : file.toString()).toStdString();
    auto it = std::ranges::find(m_project.media, path, &MediaItem::path);
    return it != m_project.media.end() && addClip(double(it->id));
}

// --- Edits ---

bool ProjectController::moveClip(double clipId, double trackId, double start)
{
    const Id id = toId(clipId);
    const Ticks to = std::max<Ticks>(ticks(start), 0);
    const auto location = m_project.locateClip(id);
    const std::vector<Id> members = location ? groupMembers(m_project, location->clip->groupId) : std::vector<Id>{};
    if (members.size() < 2)
        return apply(std::make_unique<MoveClipCommand>(id, toId(trackId), to));

    // A group moves together, every clip by the same time (none before zero); the dragged
    // one can change track, the others keep theirs. Clips go in the order that keeps them
    // from running into each other on a shared track.
    struct Move {
        Id clip;
        Id track;
        Ticks start;
    };
    std::vector<Move> moves;
    Ticks earliest = location->clip->start;
    for (Id member : members) {
        const auto found = m_project.locateClip(member);
        moves.push_back({member, member == id ? toId(trackId) : found->track->id, found->clip->start});
        earliest = std::min(earliest, found->clip->start);
    }
    const Ticks delta = std::max(to - location->clip->start, -earliest);
    std::ranges::sort(moves, [&](const Move& a, const Move& b) { return delta > 0 ? a.start > b.start : a.start < b.start; });
    std::vector<std::unique_ptr<Command>> commands;
    for (const Move& move : moves)
        commands.push_back(std::make_unique<MoveClipCommand>(move.clip, move.track, move.start + delta));
    return apply(std::make_unique<CompositeCommand>(std::move(commands), TVE_TR("core", "Move Group")));
}

bool ProjectController::trimClip(double clipId, bool startEdge, double time)
{
    using Edge = TrimClipCommand::Edge;
    return apply(std::make_unique<TrimClipCommand>(toId(clipId), startEdge ? Edge::Start : Edge::End, ticks(time)));
}

std::optional<Id> ProjectController::clipAtPlayhead() const
{
    const Ticks at = ticks(m_playhead);
    // Prefer the selected clip when the playhead is inside it.
    for (const Track& track : m_project.tracks) {
        if (const Clip* clip = track.findClip(m_selectedClip); clip && clip->start < at && at < clip->end())
            return clip->id;
    }
    if (auto visible = visibleVideoAt(m_project, at); visible && visible->clip->start < at)
        return visible->clip->id;
    return std::nullopt;
}

bool ProjectController::splitAtPlayhead()
{
    const auto clip = clipAtPlayhead();
    if (!clip) {
        reportError(tr("Put the playhead inside a clip to split it"));
        return false;
    }
    return apply(std::make_unique<SplitClipCommand>(*clip, ticks(m_playhead)));
}

bool ProjectController::deleteClip(double clipId)
{
    const Id id = toId(clipId);
    Project& project = m_project;
    auto location = project.locateClip(id);
    if (!location) {
        reportError(tr("Select a clip to delete"));
        return false;
    }
    const bool ripple = location->track->id == mainTrackId();
    const std::vector<Id> members = groupMembers(project, location->clip->groupId);
    if (members.size() > 1) {
        // The whole group, latest first so closing gaps on the main track moves none still to go.
        std::vector<std::pair<Ticks, Id>> order;
        for (Id member : members)
            order.emplace_back(project.locateClip(member)->clip->start, member);
        std::ranges::sort(order, std::greater{});
        std::vector<std::unique_ptr<Command>> commands;
        for (const auto& [start, member] : order) {
            const bool onMain = project.locateClip(member)->track->id == mainTrackId();
            commands.push_back(std::make_unique<DeleteClipCommand>(member, onMain));
        }
        if (!apply(std::make_unique<CompositeCommand>(std::move(commands), TVE_TR("core", "Delete Group"))))
            return false;
        setSelectedClip(0);
        return true;
    }
    if (!apply(std::make_unique<DeleteClipCommand>(id, ripple)))
        return false;
    if (m_selectedClip == id)
        setSelectedClip(0);
    return true;
}

void ProjectController::undo()
{
    if (!m_stack->canUndo())
        return;
    m_stack->undo();
    publish();
}

void ProjectController::redo()
{
    if (!m_stack->canRedo())
        return;
    m_stack->redo();
    publish();
}

bool ProjectController::apply(std::unique_ptr<Command> command)
{
    if (auto error = m_stack->push(std::move(command))) {
        reportError(QString::fromStdString(*error));
        return false;
    }
    publish();
    return true;
}

void ProjectController::publish()
{
    updatePlayback();
    m_trackModel.update(tracks());
    emit projectChanged();
    emit selectedClipInfoChanged();
    emit transcriptChanged();
    scheduleAutosave();
}

// --- Proxies ---

namespace {
// Videos whose short side is above this get proxies automatically.
constexpr int kAutoProxyAbove = 1080;
}

std::shared_ptr<const Project> ProjectController::playbackSnapshot() const
{
    // Decoders are given only a path: a sequence's first frame is told to play the run
    // (model/ImageSequence.h), whether imported now, loaded or brought back by undo.
    for (const MediaItem& media : m_project.media)
        if (media.sequence)
            tve::registerImageSequence(media.path, *media.sequence, media.frameRate);
    auto snapshot = std::make_shared<Project>(m_project);
    if (m_useProxies) {
        for (MediaItem& media : snapshot->media) {
            if (media.hasVideo)
                media.proxyPath = m_proxies.readyProxy(QString::fromStdString(media.path)).toStdString();
        }
    }
    return snapshot;
}

// --- Render files ---

namespace {

// An effect's cost per 4K picture, from its passes (see RenderCost): blurs and glows sample
// widely, most others read each pixel once.
double effectLoad(const tve::ClipEffect& effect)
{
    if (isOfxEffect(effect.effectId))
        return 0.15;
    const tve::LoadedEffect* loaded = tve::EffectLibrary::builtIn().find(effect.effectId);
    if (!loaded)
        return tve::defaultEffectWeight(effect);
    const auto& m = loaded->manifest;
    const bool wide = std::ranges::find(m.categories, std::string("blur")) != m.categories.end()
                   || m.id.find("glow") != std::string::npos;
    return double(std::max<std::size_t>(1, m.passes.size())) * (wide ? 0.11 : 0.01);
}

} // namespace

void ProjectController::updatePlayback()
{
    auto snapshot = playbackSnapshot();
    // The render files made from the project as it is now (an edit inside a stretch gives it
    // another key); the heavy stretches as it plays (proxies count at their size).
    m_rendered = RenderCache().validFor(m_project);
    // (TVE_REALTIME_LOAD: another limit, for tests.)
    bool overridden = false;
    const double limit = qEnvironmentVariable("TVE_REALTIME_LOAD").toDouble(&overridden);
    m_heavy = tve::heavyStretches(*snapshot, overridden && limit > 0.0 ? limit : tve::kRealTimeLoad, effectLoad);
    m_playback->setProject(snapshot, m_rendered);

    // The bar: rendered stretches, and the heavy ones' parts outside them.
    QVariantList stretches;
    std::vector<std::pair<tve::Ticks, tve::Ticks>> marks;
    for (const tve::RenderedStretch& r : m_rendered)
        marks.push_back({r.from, r.to});
    for (const tve::PlaybackLoad& heavy : m_heavy) {
        tve::Ticks at = heavy.from;
        for (const tve::RenderedStretch& r : m_rendered) {
            if (r.to <= at || r.from >= heavy.to)
                continue;
            if (r.from > at)
                stretches << QVariantMap{{QStringLiteral("from"), tve::toSeconds(at)},
                                         {QStringLiteral("to"), tve::toSeconds(r.from)},
                                         {QStringLiteral("rendered"), false}};
            at = std::max(at, r.to);
        }
        if (at < heavy.to)
            stretches << QVariantMap{{QStringLiteral("from"), tve::toSeconds(at)},
                                     {QStringLiteral("to"), tve::toSeconds(heavy.to)},
                                     {QStringLiteral("rendered"), false}};
    }
    for (const tve::RenderedStretch& r : m_rendered)
        stretches << QVariantMap{{QStringLiteral("from"), tve::toSeconds(r.from)},
                                 {QStringLiteral("to"), tve::toSeconds(r.to)},
                                 {QStringLiteral("rendered"), true}};
    std::sort(stretches.begin(), stretches.end(), [](const QVariant& a, const QVariant& b) {
        return a.toMap().value(QStringLiteral("from")).toDouble() < b.toMap().value(QStringLiteral("from")).toDouble();
    });
    if (stretches != m_renderStretches) {
        m_renderStretches = stretches;
        emit renderChanged();
    }
}

bool ProjectController::needsRender() const
{
    for (const QVariant& stretch : m_renderStretches)
        if (!stretch.toMap().value(QStringLiteral("rendered")).toBool())
            return true;
    return false;
}

namespace {

// [from, to) on the frame grid, in pieces of at most RenderCache::kMaxPiece, without what
// `rendered` already covers.
std::vector<std::pair<tve::Ticks, tve::Ticks>> piecesOf(const std::vector<std::pair<tve::Ticks, tve::Ticks>>& spans,
                                                        const std::vector<tve::RenderedStretch>& rendered,
                                                        tve::Ticks frame, tve::Ticks end)
{
    std::vector<std::pair<tve::Ticks, tve::Ticks>> pieces;
    for (auto [from, to] : spans) {
        from = from / frame * frame;
        to = std::min(end, (to + frame - 1) / frame * frame);
        for (tve::Ticks at = from; at < to;) {
            const auto covering = std::ranges::find_if(rendered, [&](const auto& r) { return r.from <= at && at < r.to; });
            if (covering != rendered.end()) {
                at = covering->to;
                continue;
            }
            tve::Ticks next = std::min(to, at + RenderCache::kMaxPiece);
            for (const auto& r : rendered)
                if (r.from > at && r.from < next)
                    next = r.from;
            pieces.push_back({at, next});
            at = next;
        }
    }
    return pieces;
}

} // namespace

bool ProjectController::renderHeavy()
{
    std::vector<std::pair<tve::Ticks, tve::Ticks>> spans;
    for (const tve::PlaybackLoad& heavy : m_heavy)
        spans.push_back({heavy.from, heavy.to});
    return renderPieces(piecesOf(spans, m_rendered, frameTicks(), m_project.duration()));
}

bool ProjectController::renderAll()
{
    // What shows: every stretch with something in it (gaps are black already).
    std::vector<std::pair<tve::Ticks, tve::Ticks>> spans;
    for (const tve::PlaybackLoad& stretch : tve::playbackLoad(m_project)) {
        if (stretch.load <= 0.0)
            continue;
        if (!spans.empty() && spans.back().second == stretch.from)
            spans.back().second = stretch.to;
        else
            spans.push_back({stretch.from, stretch.to});
    }
    return renderPieces(piecesOf(spans, m_rendered, frameTicks(), m_project.duration()));
}

bool ProjectController::renderPieces(std::vector<std::pair<tve::Ticks, tve::Ticks>> pieces)
{
    if (m_renderThread.joinable()) {
        reportError(tr("Already rendering"));
        return false;
    }
    if (pieces.empty()) {
        reportError(tr("Nothing to render: every stretch plays already"));
        return false;
    }
    // From the project as it is (its media, not proxies); each piece's file is keyed by it.
    auto project = std::make_shared<const Project>(m_project);
    tve::Ticks total = 0;
    for (const auto& [from, to] : pieces)
        total += to - from;
    m_cancelRender = false;
    m_renderProgress = 0.0;
    m_renderThread = std::thread([this, project, pieces = std::move(pieces), total] {
        QString error;
        tve::Ticks done = 0;
        double reported = -1.0;
        for (const auto& [from, to] : pieces) {
            const bool ok = RenderCache::render(project, from, to, [&](double part) {
                const double progress = double(done + tve::Ticks(part * double(to - from))) / double(total);
                if (progress - reported >= 0.01) {
                    reported = progress;
                    QMetaObject::invokeMethod(this, [this, progress] {
                        m_renderProgress = progress;
                        emit renderProgressChanged();
                    }, Qt::QueuedConnection);
                }
                return !m_cancelRender.load();
            }, &error);
            if (!ok)
                break;
            done += to - from;
            // Played from its file at once.
            QMetaObject::invokeMethod(this, [this] { updatePlayback(); }, Qt::QueuedConnection);
        }
        QMetaObject::invokeMethod(this, [this, error] {
            if (m_renderThread.joinable())
                m_renderThread.join();
            m_renderProgress = 0.0;
            if (!error.isEmpty())
                reportError(tr("Rendering failed: %1").arg(error));
            updatePlayback();
            emit renderChanged();
            emit renderProgressChanged();
        }, Qt::QueuedConnection);
    });
    emit renderChanged();
    return true;
}

void ProjectController::cancelRender()
{
    m_cancelRender = true;
}

void ProjectController::deleteRenderFiles()
{
    cancelRender();
    if (m_renderThread.joinable())
        m_renderThread.join();
    RenderCache::clear();
    updatePlayback();
    emit renderChanged();
}

void ProjectController::requestAutoProxies()
{
    if (!m_autoProxies)
        return;
    for (const MediaItem& media : m_project.media) {
        // (A proxy would lose transparency, so pictures with it play as they are.)
        if (media.hasVideo && !media.still && !media.alpha && std::min(media.width, media.height) > kAutoProxyAbove)
            m_proxies.request(QString::fromStdString(media.path));
    }
}

void ProjectController::setUseProxies(bool use)
{
    if (use == m_useProxies)
        return;
    m_useProxies = use;
    updatePlayback();
    emit proxiesChanged();
}

void ProjectController::setAutoProxies(bool automatic)
{
    if (automatic == m_autoProxies)
        return;
    m_autoProxies = automatic;
    requestAutoProxies();
    emit proxiesChanged();
}

QVariantMap ProjectController::proxyStates() const
{
    QStringList paths;
    for (const MediaItem& media : m_project.media) {
        if (media.hasVideo)
            paths << QString::fromStdString(media.path);
    }
    return m_proxies.states(paths);
}

bool ProjectController::createProxy(double mediaId)
{
    const MediaItem* media = m_project.findMedia(toId(mediaId));
    if (!media || !media->hasVideo || media->still) {
        reportError(tr("Only videos have proxies"));
        return false;
    }
    m_proxies.request(QString::fromStdString(media->path));
    return true;
}

bool ProjectController::deleteProxy(double mediaId)
{
    const MediaItem* media = m_project.findMedia(toId(mediaId));
    if (!media)
        return false;
    m_proxies.remove(QString::fromStdString(media->path));
    updatePlayback();
    return true;
}

// --- Autosave and recovery ---

QString ProjectController::autosavePath(const QString& id, const char* suffix) const
{
    return m_autosaveDir + QLatin1Char('/') + id + QLatin1String(suffix);
}

void ProjectController::setAutosaveEnabled(bool enabled)
{
    if (enabled == m_autosaveEnabled)
        return;
    m_autosaveEnabled = enabled;
    if (!enabled) {
        m_autosaveTimer.stop();
        removeAutosave();
    } else {
        scheduleAutosave();
    }
    emit recoveryChanged();
}

void ProjectController::setAutosaveDirectory(const QString& directory)
{
    if (directory == m_autosaveDir)
        return;
    removeAutosave();
    m_autosaveDir = directory;
    scheduleAutosave();
    emit recoveryChanged();
}

void ProjectController::scheduleAutosave()
{
    if (!m_stack || !m_autosaveEnabled)
        return;
    if (!modified()) {
        m_autosaveTimer.stop();
        removeAutosave();
        return;
    }
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (m_unsavedSince == 0)
        m_unsavedSince = now;
    if (now - m_unsavedSince >= kAutosaveMaxDelayMs)
        autosaveNow();
    else
        m_autosaveTimer.start(); // restarts: waits for edits to settle
}

void ProjectController::autosaveNow()
{
    m_autosaveTimer.stop();
    if (!m_autosaveEnabled || !modified()) {
        removeAutosave();
        return;
    }
    if (!QDir().mkpath(m_autosaveDir)) {
        reportError(tr("Could not autosave to %1").arg(m_autosaveDir));
        return;
    }
    try {
        saveProject(rootProject(), autosavePath(m_sessionId, ".tve").toStdString());
    } catch (const std::exception& e) {
        reportError(tr("Could not autosave: %1").arg(coreText(e.what())));
        return;
    }
    int clips = 0;
    for (const Track& track : m_project.tracks)
        clips += int(track.clips.size());
    const QJsonObject meta{
        {QStringLiteral("sessionId"), m_sessionId},
        {QStringLiteral("pid"), qint64(QCoreApplication::applicationPid())},
        {QStringLiteral("name"), name()},
        {QStringLiteral("originalPath"), m_filePath},
        {QStringLiteral("savedAt"), QDateTime::currentMSecsSinceEpoch()},
        {QStringLiteral("clipCount"), clips},
    };
    QSaveFile file(autosavePath(m_sessionId, ".json"));
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(meta).toJson());
        file.commit();
    }
    m_unsavedSince = 0;
    m_lastAutosave = QDateTime::currentMSecsSinceEpoch();
    emit recoveryChanged();
}

void ProjectController::removeAutosave()
{
    m_unsavedSince = 0;
    if (m_lastAutosave != 0) {
        m_lastAutosave = 0;
        emit recoveryChanged();
    }
    if (m_sessionId.isEmpty() || m_autosaveDir.isEmpty())
        return;
    QFile::remove(autosavePath(m_sessionId, ".json"));
    QFile::remove(autosavePath(m_sessionId, ".tve"));
}

QVariantList ProjectController::recoverableProjects() const
{
    struct Entry {
        qint64 savedAt;
        QVariantMap map;
    };
    std::vector<Entry> entries;
    const QDir dir(m_autosaveDir);
    for (const QString& fileName : dir.entryList({QStringLiteral("*.json")}, QDir::Files)) {
        QFile file(dir.filePath(fileName));
        if (!file.open(QIODevice::ReadOnly))
            continue;
        const QJsonObject meta = QJsonDocument::fromJson(file.readAll()).object();
        const QString id = meta.value(QStringLiteral("sessionId")).toString();
        const qint64 pid = meta.value(QStringLiteral("pid")).toInteger();
        // Not work still open here or in another running copy of the app.
        if (id.isEmpty() || liveSessions().contains(id) || !QFileInfo::exists(autosavePath(id, ".tve")))
            continue;
        if (pid != QCoreApplication::applicationPid() && processAlive(pid))
            continue;
        const qint64 savedAt = meta.value(QStringLiteral("savedAt")).toInteger();
        entries.push_back({savedAt,
                           {
                               {QStringLiteral("id"), id},
                               {QStringLiteral("name"), meta.value(QStringLiteral("name")).toString()},
                               {QStringLiteral("originalPath"), meta.value(QStringLiteral("originalPath")).toString()},
                               {QStringLiteral("savedAt"), double(savedAt)},
                               {QStringLiteral("clipCount"), meta.value(QStringLiteral("clipCount")).toInt()},
                           }});
    }
    std::ranges::sort(entries, std::greater<>{}, &Entry::savedAt);
    QVariantList list;
    for (const Entry& entry : entries)
        list << entry.map;
    return list;
}

bool ProjectController::recoverProject(const QString& id)
{
    QVariantMap found;
    for (const QVariant& entry : recoverableProjects()) {
        if (entry.toMap().value(QStringLiteral("id")).toString() == id)
            found = entry.toMap();
    }
    if (found.isEmpty()) {
        reportError(tr("That unsaved work is no longer there"));
        return false;
    }
    Project recovered;
    try {
        recovered = loadProject(autosavePath(id, ".tve").toStdString());
    } catch (const std::exception& e) {
        reportError(tr("Could not recover: %1").arg(coreText(e.what())));
        return false;
    }
    resetProject(std::move(recovered), found.value(QStringLiteral("originalPath")).toString());
    m_canvasChosen = true;
    m_stack->setUnsaved(); // it differs from any saved file until saved
    // The work now lives in this session: autosave it here, then drop the old copy.
    autosaveNow();
    QFile::remove(autosavePath(id, ".json"));
    QFile::remove(autosavePath(id, ".tve"));
    emit projectChanged();
    emit recoveryChanged();
    return true;
}

void ProjectController::discardRecovery(const QString& id)
{
    if (id.isEmpty() || liveSessions().contains(id))
        return;
    QFile::remove(autosavePath(id, ".json"));
    QFile::remove(autosavePath(id, ".tve"));
    emit recoveryChanged();
}

void ProjectController::reportError(const QString& message)
{
    m_lastError = message;
    emit errorOccurred(message);
}

// --- Audio ---

bool ProjectController::setClipAudio(double clipId, double gainDb, double fadeIn, double fadeOut)
{
    ClipAudio audio{float(std::clamp(gainDb, double(kMinGainDb), double(kMaxGainDb))), ticks(std::max(fadeIn, 0.0)),
                    ticks(std::max(fadeOut, 0.0))};
    // Animated volume: the gain becomes a keyframe at the playhead; fades stay static.
    if (const Clip* clip = findClip(toId(clipId)); clip && clip->keyframesOf(property::kVolume)) {
        const float now = clip->gainDbAt(ticks(m_playhead));
        audio.gainDb = clip->gainDb;
        if (std::abs(float(gainDb) - now) > 1e-4f) {
            std::map<std::string, Keyframes> changes;
            changes.emplace(std::string(property::kVolume), withKey(*clip, property::kVolume, gainDb));
            if (audio.fadeIn == clip->fadeIn && audio.fadeOut == clip->fadeOut)
                return apply(std::make_unique<SetClipKeyframesCommand>(clip->id, std::move(changes), TVE_TR("core", "Change Audio")));
            // Rare: both at once (two steps).
            if (!apply(std::make_unique<SetClipKeyframesCommand>(clip->id, std::move(changes), TVE_TR("core", "Change Audio"))))
                return false;
        }
    }
    return apply(std::make_unique<SetClipAudioCommand>(toId(clipId), audio));
}

bool ProjectController::detachAudio(double clipId)
{
    auto location = m_project.locateClip(toId(clipId));
    if (!location) {
        reportError(tr("Select a clip to detach its audio"));
        return false;
    }
    const Clip& clip = *location->clip;
    for (const Track& track : m_project.tracks) {
        if (track.kind == TrackKind::Audio && !track.locked && !track.overlaps(clip.start, clip.duration)) {
            auto command = std::make_unique<DetachAudioCommand>(clip.id, track.id);
            auto* raw = command.get();
            if (!apply(std::move(command)))
                return false;
            setSelectedClip(double(raw->audioClipId()));
            return true;
        }
    }
    reportError(tr("No audio track is free where this clip is"));
    return false;
}

bool ProjectController::setTrackFlag(double trackId, const QString& flag, bool value)
{
    using Flag = SetTrackFlagCommand::Flag;
    const Flag which = flag == QLatin1String("locked") ? Flag::Locked
                     : flag == QLatin1String("hidden") ? Flag::Hidden : Flag::Muted;
    return apply(std::make_unique<SetTrackFlagCommand>(toId(trackId), which, value));
}

QVariantMap ProjectController::selectedClipInfo() const
{
    for (const Track& track : m_project.tracks) {
        const Clip* clip = track.findClip(m_selectedClip);
        if (!clip)
            continue;
        const MediaItem* media = m_project.findMedia(clip->mediaId);
        const Sequence* sequence = clip->compound() ? m_project.findSequence(clip->sequenceId) : nullptr;
        const Ticks now = ticks(m_playhead);
        const Ticks local = keyTime(*clip);
        QVariantMap keyframes;
        QVariantMap states;
        QString easingAtPlayhead;
        QVariantList curveAtPlayhead;
        forEachChannel(*clip, [&](const std::string& id, const Keyframes& keys) {
            QVariantList list;
            bool here = false;
            for (const Keyframe& k : keys) {
                list << QVariantMap{{QStringLiteral("time"), seconds(k.time)},
                                    {QStringLiteral("value"), k.value},
                                    {QStringLiteral("easing"), easingName(k.easing)}};
                if (std::llabs(k.time - local) <= frameTolerance()) {
                    here = true;
                    if (easingAtPlayhead.isEmpty()) {
                        easingAtPlayhead = easingName(k.easing);
                        const EasingCurve c = k.easing == Easing::Curve ? k.curve : curveFor(k.easing);
                        curveAtPlayhead = {c.x1, c.y1, c.x2, c.y2};
                    }
                }
            }
            const QString name = QString::fromStdString(id);
            keyframes.insert(name, list);
            states.insert(name, here ? QStringLiteral("key") : QStringLiteral("animated"));
        });
        return {
            {QStringLiteral("id"), double(clip->id)},
            {QStringLiteral("label"), clip->text         ? tr("Title")
                                      : clip->drawing    ? tr("Drawing")
                                      : clip->adjustment ? tr("Adjustment layer")
                                      : sequence         ? QString::fromStdString(sequence->name)
                                      : media            ? mediaName(*media)
                                                         : QString()},
            {QStringLiteral("isTitle"), clip->text.has_value()},
            {QStringLiteral("spherical"), media && media->spherical && !clip->generated()},
            {QStringLiteral("isCompound"), sequence != nullptr},
            {QStringLiteral("isMulticam"), clip->multicam()},
            {QStringLiteral("motionBlur"), clip->motionBlur},
            {QStringLiteral("colourFill"), media && media->colourFill ? QVariant(fillMap(*media->colourFill)) : QVariant()},
            {QStringLiteral("angle"), clip->angle()},
            {QStringLiteral("angles"), angleListOf(m_project, *clip)},
            {QStringLiteral("groupId"), double(clip->groupId)},
            {QStringLiteral("text"), clip->text ? textMap(*clip->text) : QVariantMap()},
            {QStringLiteral("isDrawing"), clip->drawing.has_value()},
            {QStringLiteral("drawing"), clip->drawing ? drawingMap(*clip->drawing) : QVariantMap()},
            {QStringLiteral("trackKind"), track.kind == TrackKind::Audio ? QStringLiteral("audio") : QStringLiteral("video")},
            {QStringLiteral("locked"), track.locked},
            {QStringLiteral("start"), seconds(clip->start)},
            {QStringLiteral("duration"), seconds(clip->duration)},
            {QStringLiteral("hasAudio"), sequence || (media && media->hasAudio)},
            {QStringLiteral("audioEnabled"), clip->audioEnabled},
            {QStringLiteral("gainDb"), double(clip->gainDbAt(now))},
            {QStringLiteral("fadeIn"), seconds(clip->fadeIn)},
            {QStringLiteral("fadeOut"), seconds(clip->fadeOut)},
            {QStringLiteral("visual"), visualMap(clip->visualAt(now))},
            {QStringLiteral("keyframes"), keyframes},
            {QStringLiteral("keyframeStates"), states},
            {QStringLiteral("keyframeEasing"), easingAtPlayhead},
            {QStringLiteral("keyframeCurve"), curveAtPlayhead},
            {QStringLiteral("localTime"), seconds(local)},
            {QStringLiteral("effects"), effectsList(clip->effectsAt(now))}, // values at the playhead
            {QStringLiteral("color"), gradeMap(clip->color)},
            {QStringLiteral("masks"), [&] {
                 QVariantList list;
                 for (const ClipMask& m : clip->masks)
                     list << maskMap(m);
                 return list;
             }()},
            {QStringLiteral("chromaKey"), chromaKeyMap(clip->chromaKey)},
            {QStringLiteral("matte"), QString::fromLatin1(kMatteModes[int(clip->matte)])},
            {QStringLiteral("isAdjustment"), clip->adjustment},
            {QStringLiteral("isStill"), media && media->still},
            {QStringLiteral("isSticker"), media && media->lottie},
            {QStringLiteral("look"), clip->look ? QVariant(QVariantMap{{QStringLiteral("id"), QString::fromStdString(clip->look->id)},
                                                                       {QStringLiteral("title"), catalogueText(clip->look->title)},
                                                                       {QStringLiteral("intensity"), clip->look->intensity}})
                                                : QVariant()},
            {QStringLiteral("speed"), clip->speed},
            {QStringLiteral("reversed"), clip->reversed},
            {QStringLiteral("freeze"), clip->freeze},
            {QStringLiteral("speedRamp"), rampList(clip->speedRamp)},
            {QStringLiteral("keepPitch"), clip->keepPitch},
            // Sound the clip plays itself (not detached, not a title or a silent file).
            {QStringLiteral("hasSound"), !clip->generated() && (sequence || (media && media->hasAudio))
                                             && (track.kind == TrackKind::Audio || clip->audioEnabled)},
            {QStringLiteral("audioEffects"), effectsList(clip->audioEffects)},
            {QStringLiteral("cutout"), cutoutMap(clip->cutout)},
            {QStringLiteral("beauty"), QVariantMap{{QStringLiteral("enabled"), clip->beauty.enabled},
                                                   {QStringLiteral("smooth"), clip->beauty.smooth},
                                                   {QStringLiteral("brighten"), clip->beauty.brighten},
                                                   {QStringLiteral("teeth"), clip->beauty.teeth}}},
            {QStringLiteral("stabilization"), QVariantMap{{QStringLiteral("enabled"), clip->stabilization.enabled},
                                                          {QStringLiteral("smoothness"), clip->stabilization.smoothness},
                                                          {QStringLiteral("lock"), clip->stabilization.lock},
                                                          {QStringLiteral("zoom"), clip->stabilization.zoom}}},
            {QStringLiteral("deflicker"), QVariantMap{{QStringLiteral("enabled"), clip->deflicker.enabled},
                                                      {QStringLiteral("strength"), clip->deflicker.strength}}},
            {QStringLiteral("denoise"), clip->denoise},
            {QStringLiteral("cursor"), clip->cursor.hasLog()
                 ? QVariant(QVariantMap{{QStringLiteral("rings"), clip->cursor.rings},
                                        {QStringLiteral("highlight"), clip->cursor.highlight},
                                        {QStringLiteral("spotlight"), clip->cursor.spotlight},
                                        {QStringLiteral("size"), clip->cursor.size},
                                        {QStringLiteral("clicks"), int(clip->cursor.clicks.size())}})
                 : QVariant()},
            {QStringLiteral("processed"), clip->processedVideo
                 ? QVariant(QVariantMap{{QStringLiteral("steps"), [&] {
                                             QStringList steps;
                                             for (const std::string& s : clip->processedVideo->steps)
                                                 steps << QString::fromStdString(s);
                                             return steps;
                                         }()},
                                        {QStringLiteral("missing"),
                                         !QFileInfo::exists(QString::fromStdString(clip->processedVideo->path))}})
                 : QVariant()},
            {QStringLiteral("motionTracks"), [&] {
                 QVariantList list;
                 for (const MotionTrack& t : clip->motionTracks) {
                     const auto timelineAt = [&](Ticks m) {
                         return seconds(clip->start + Ticks(std::llround(double(m - clip->sourceIn) / clip->speed)));
                     };
                     list << QVariantMap{{QStringLiteral("id"), t.id},
                                         {QStringLiteral("surface"), t.surface},
                                         {QStringLiteral("start"), t.samples.empty() ? 0.0 : timelineAt(t.samples.front().time)},
                                         {QStringLiteral("end"), t.samples.empty() ? 0.0 : timelineAt(t.samples.back().time)}};
                 }
                 return list;
             }()},
            {QStringLiteral("faceCover"), faceCoverMap(clip->faceMosaic)},
            // People can be found in it (a clip of a video file).
            {QStringLiteral("canFindPeople"), !clip->generated() && !sequence && media && media->hasVideo && !media->still
                                                  && !media->lottie},
            {QStringLiteral("enhanced"), clip->enhancedAudio
                 ? QVariant(QVariantMap{{QStringLiteral("limitDb"), clip->enhancedAudio->limitDb},
                                        {QStringLiteral("missing"),
                                         !QFileInfo::exists(QString::fromStdString(clip->enhancedAudio->path))}})
                 : QVariant()},
            {QStringLiteral("loudness"), [&]() -> QVariant {
                 const auto found = m_loudness.find(clip->id);
                 if (found == m_loudness.end())
                     return {};
                 return QVariantMap{{QStringLiteral("measured"), found->second.first},
                                    {QStringLiteral("target"), found->second.second}};
             }()},
        };
    }
    return {};
}

// --- Picture ---

const Clip* ProjectController::findClip(Id clipId) const
{
    for (const Track& track : m_project.tracks) {
        if (const Clip* clip = track.findClip(clipId))
            return clip;
    }
    return nullptr;
}

std::optional<ClipVisual> ProjectController::visualFrom(Id clipId, const QVariantMap& values) const
{
    const Clip* clip = findClip(clipId);
    if (!clip)
        return std::nullopt;
    ClipVisual v = clip->visualAt(ticks(m_playhead));
    auto take = [&](const char* key, double& target) {
        const auto it = values.constFind(QLatin1String(key));
        if (it != values.constEnd())
            target = it->toDouble();
    };
    take("x", v.transform.x);
    take("y", v.transform.y);
    take("scale", v.transform.scale);
    take("rotation", v.transform.rotation);
    take("cropLeft", v.crop.left);
    take("cropTop", v.crop.top);
    take("cropRight", v.crop.right);
    take("cropBottom", v.crop.bottom);
    take("opacity", v.opacity);
    take("yaw", v.sphere.yaw);
    take("pitch", v.sphere.pitch);
    take("roll", v.sphere.roll);
    take("fov", v.sphere.fov);
    take("anchorX", v.transform.anchorX);
    take("anchorY", v.transform.anchorY);
    take("pinTopLeftX", v.pin.topLeftX);
    take("pinTopLeftY", v.pin.topLeftY);
    take("pinTopRightX", v.pin.topRightX);
    take("pinTopRightY", v.pin.topRightY);
    take("pinBottomRightX", v.pin.bottomRightX);
    take("pinBottomRightY", v.pin.bottomRightY);
    take("pinBottomLeftX", v.pin.bottomLeftX);
    take("pinBottomLeftY", v.pin.bottomLeftY);
    if (const auto it = values.constFind(QStringLiteral("blend")); it != values.constEnd()) {
        const QString name = it->toString();
        for (int i = 0; i < int(std::size(kBlendNames)); ++i) {
            if (name == QLatin1String(kBlendNames[i]))
                v.blend = BlendMode(i);
        }
    }
    return v;
}

bool ProjectController::setClipVisual(double clipId, const QVariantMap& visual)
{
    const auto v = visualFrom(toId(clipId), visual);
    const Clip* clip = findClip(toId(clipId));
    if (!v || !clip) {
        reportError(tr("Select a video clip"));
        return false;
    }
    // Animated properties take the new value as a keyframe at the playhead (their unused
    // static value stays); the others change the static picture.
    Clip after = *clip;
    const Ticks now = ticks(m_playhead);
    ClipVisual next = *v;
    for (const AnimatableProperty& p : animatableProperties()) {
        const std::string id(p.id);
        double* value = visualField(next, id);
        if (!value)
            continue;
        if (channelKeys(*clip, id)) {
            if (*value != channelValue(*clip, id, now))
                setKey(after, id, *value);
        } else {
            setChannelStatic(after, id, *value);
        }
    }
    after.visual.blend = next.blend;
    const bool ok = after.keyframes == clip->keyframes
                        ? apply(std::make_unique<SetClipVisualCommand>(clip->id, after.visual))
                        : commitAnimation(*clip, after, TVE_TR("core", "Change Picture"));
    if (!ok && m_view)
        m_view->clearPreview(toId(clipId)); // the refused value must not stay on screen
    return ok;
}

void ProjectController::previewClipVisual(double clipId, const QVariantMap& visual)
{
    if (const auto v = visualFrom(toId(clipId), visual); v && m_view)
        m_view->setPreviewVisual(toId(clipId), *v);
}

void ProjectController::cancelPreview(double clipId)
{
    if (m_view)
        m_view->clearPreview(toId(clipId));
}

bool ProjectController::resetClipVisual(double clipId)
{
    const Clip* clip = findClip(toId(clipId));
    if (!clip)
        return false;
    // Picture keyframes go too; volume and effect ones stay.
    Clip after = *clip;
    after.visual = ClipVisual{};
    std::erase_if(after.keyframes, [](const auto& entry) {
        const AnimatableProperty* p = findAnimatable(entry.first);
        return p && p->picture;
    });
    if (after.keyframes == clip->keyframes)
        return apply(std::make_unique<SetClipVisualCommand>(clip->id, ClipVisual{}));
    return commitAnimation(*clip, after, TVE_TR("core", "Change Picture"));
}

// --- Colour ---

std::optional<ColorGrade> ProjectController::gradeWith(Id clipId, const QVariantMap& changes) const
{
    const Clip* clip = findClip(clipId);
    if (!clip)
        return std::nullopt;
    ColorGrade g = clip->color;
    const auto number = [&](const char* key, double& target) {
        if (const auto it = changes.constFind(QLatin1String(key)); it != changes.constEnd())
            target = it->toDouble();
    };
    const auto wheel = [&](const char* key, ColorWheel& target) {
        if (const auto it = changes.constFind(QLatin1String(key)); it != changes.constEnd()) {
            const QVariantMap m = it->toMap();
            target.x = m.value(QStringLiteral("x"), target.x).toDouble();
            target.y = m.value(QStringLiteral("y"), target.y).toDouble();
            target.master = m.value(QStringLiteral("master"), target.master).toDouble();
        }
    };
    const auto curve = [&](const char* key, ColorCurve& target) {
        if (const auto it = changes.constFind(QLatin1String(key)); it != changes.constEnd()) {
            target.clear();
            for (const QVariant& point : it->toList()) {
                const QVariantMap m = point.toMap();
                target.push_back({m.value(QStringLiteral("x")).toDouble(), m.value(QStringLiteral("y")).toDouble()});
            }
        }
    };
    number("exposure", g.exposure);
    number("contrast", g.contrast);
    number("saturation", g.saturation);
    number("temperature", g.temperature);
    number("tint", g.tint);
    number("lutIntensity", g.lutIntensity);
    number("protectSkin", g.protectSkin);
    wheel("lift", g.lift);
    wheel("gamma", g.gamma);
    wheel("gain", g.gain);
    wheel("offset", g.offset);
    // HDR wheels, darkest first.
    wheel("black", g.zones[0]);
    wheel("dark", g.zones[1]);
    wheel("shadow", g.zones[2]);
    wheel("light", g.zones[3]);
    wheel("highlight", g.zones[4]);
    wheel("specular", g.zones[5]);
    curve("master", g.master);
    curve("red", g.red);
    curve("green", g.green);
    curve("blue", g.blue);
    curve("hueVsSat", g.hueVsSat);
    curve("hueVsHue", g.hueVsHue);
    curve("lumaVsSat", g.lumaVsSat);
    // hsl: the bands to change, [{band (0–7), hue, saturation, luminance}] (missing values stay).
    if (const auto it = changes.constFind(QStringLiteral("hsl")); it != changes.constEnd()) {
        for (const QVariant& entry : it->toList()) {
            const QVariantMap m = entry.toMap();
            const int band = m.value(QStringLiteral("band"), -1).toInt();
            if (band < 0 || band >= kHslBands)
                continue;
            HslBand& target = g.hsl[std::size_t(band)];
            target.hue = m.value(QStringLiteral("hue"), target.hue).toDouble();
            target.saturation = m.value(QStringLiteral("saturation"), target.saturation).toDouble();
            target.luminance = m.value(QStringLiteral("luminance"), target.luminance).toDouble();
        }
    }
    return g;
}

bool ProjectController::setClipColor(double clipId, const QVariantMap& changes)
{
    const auto grade = gradeWith(toId(clipId), changes);
    if (!grade) {
        reportError(tr("Select a video clip"));
        return false;
    }
    const bool ok = apply(std::make_unique<SetClipColorCommand>(std::vector<Id>{toId(clipId)}, *grade));
    if (!ok && m_view)
        m_view->clearPreview(toId(clipId));
    return ok;
}

void ProjectController::previewClipColor(double clipId, const QVariantMap& changes)
{
    if (const auto grade = gradeWith(toId(clipId), changes); grade && m_view && !validateGrade(*grade))
        m_view->setPreviewGrade(toId(clipId), *grade);
}

bool ProjectController::resetClipColor(double clipId)
{
    return apply(std::make_unique<SetClipColorCommand>(std::vector<Id>{toId(clipId)}, ColorGrade{}, TVE_TR("core", "Reset Color")));
}

bool ProjectController::chooseLut(double clipId, const QUrl& file)
{
    // qrc:/… names a built-in LUT (stored as :/…).
    const QString path = file.isLocalFile()                   ? file.toLocalFile()
                       : file.scheme() == QLatin1String("qrc") ? QLatin1Char(':') + file.path()
                                                               : file.toString();
    try {
        readLut(path.toStdString());
    } catch (const std::exception& e) {
        reportError(tr("Could not use %1: %2").arg(QFileInfo(path).fileName(), coreText(e.what())));
        return false;
    }
    const Clip* clip = findClip(toId(clipId));
    if (!clip)
        return false;
    ColorGrade g = clip->color;
    g.lutPath = path.toStdString();
    g.lutIntensity = 1.0;
    return apply(std::make_unique<SetClipColorCommand>(std::vector<Id>{clip->id}, g, TVE_TR("core", "Apply LUT")));
}

bool ProjectController::removeLut(double clipId)
{
    const Clip* clip = findClip(toId(clipId));
    if (!clip || clip->color.lutPath.empty())
        return false;
    ColorGrade g = clip->color;
    g.lutPath.clear();
    g.lutIntensity = 1.0;
    return apply(std::make_unique<SetClipColorCommand>(std::vector<Id>{clip->id}, g, TVE_TR("core", "Remove LUT")));
}

// --- Masks, keys and layouts ---

namespace {

ClipKeying keyingOf(const Clip& clip)
{
    return {clip.masks, clip.chromaKey, clip.matte};
}

} // namespace

bool ProjectController::addMask(double clipId, const QString& shape, const QVariantList& points)
{
    const Clip* clip = findClip(toId(clipId));
    if (!clip) {
        reportError(tr("Select a clip"));
        return false;
    }
    ClipMask mask;
    bool known = false;
    for (int i = 0; i < int(std::size(kMaskShapes)); ++i) {
        if (shape == QLatin1String(kMaskShapes[i])) {
            mask.shape = MaskShape(i);
            known = true;
        }
    }
    if (!known) {
        reportError(tr("Unknown mask shape %1").arg(shape));
        return false;
    }
    // Each starts in the middle of the picture at a useful size.
    if (mask.shape == MaskShape::Ellipse || mask.shape == MaskShape::Star || mask.shape == MaskShape::Heart)
        mask.width = mask.height = 0.6;
    if (mask.shape == MaskShape::Line)
        mask.feather = 0.05;
    if (mask.shape == MaskShape::Pen) {
        mask = maskWith(mask, {{QStringLiteral("points"), points}});
        if (points.isEmpty())
            mask.points = {{0.3, 0.3}, {0.7, 0.3}, {0.7, 0.7}, {0.3, 0.7}};
    }
    ClipKeying keying = keyingOf(*clip);
    keying.masks.push_back(mask);
    const int index = int(keying.masks.size()) - 1;
    if (!apply(std::make_unique<SetClipKeyingCommand>(clip->id, std::move(keying), TVE_TR("core", "Add Mask"))))
        return false;
    setActiveMask(index); // edit the new one
    return true;
}

bool ProjectController::setMask(double clipId, int index, const QVariantMap& changes)
{
    const Clip* clip = findClip(toId(clipId));
    if (!clip || index < 0 || index >= int(clip->masks.size()))
        return false;
    ClipKeying keying = keyingOf(*clip);
    keying.masks[std::size_t(index)] = maskWith(keying.masks[std::size_t(index)], changes);
    const bool ok = apply(std::make_unique<SetClipKeyingCommand>(clip->id, std::move(keying), TVE_TR("core", "Change Mask")));
    if (!ok && m_view)
        m_view->clearPreview(toId(clipId));
    return ok;
}

void ProjectController::previewMask(double clipId, int index, const QVariantMap& changes)
{
    const Clip* clip = findClip(toId(clipId));
    if (!clip || !m_view || index < 0 || index >= int(clip->masks.size()))
        return;
    auto masks = clip->masks;
    masks[std::size_t(index)] = maskWith(masks[std::size_t(index)], changes);
    if (!validateMasks(masks))
        m_view->setPreviewKeying(clip->id, clip->chromaKey, masks);
}

bool ProjectController::removeMask(double clipId, int index)
{
    const Clip* clip = findClip(toId(clipId));
    if (!clip || index < 0 || index >= int(clip->masks.size()))
        return false;
    ClipKeying keying = keyingOf(*clip);
    keying.masks.erase(keying.masks.begin() + index);
    if (!apply(std::make_unique<SetClipKeyingCommand>(clip->id, std::move(keying), TVE_TR("core", "Remove Mask"))))
        return false;
    setActiveMask(-1);
    return true;
}

bool ProjectController::setChromaKey(double clipId, const QVariantMap& changes)
{
    const Clip* clip = findClip(toId(clipId));
    if (!clip)
        return false;
    ClipKeying keying = keyingOf(*clip);
    keying.chromaKey = chromaKeyWith(keying.chromaKey, changes);
    const bool ok = apply(std::make_unique<SetClipKeyingCommand>(clip->id, std::move(keying), TVE_TR("core", "Change Key")));
    if (!ok && m_view)
        m_view->clearPreview(toId(clipId));
    return ok;
}

void ProjectController::previewChromaKey(double clipId, const QVariantMap& changes)
{
    const Clip* clip = findClip(toId(clipId));
    if (!clip || !m_view)
        return;
    const ChromaKey key = chromaKeyWith(clip->chromaKey, changes);
    if (!validateChromaKey(key))
        m_view->setPreviewKeying(clip->id, key, clip->masks);
}

bool ProjectController::setMatte(double clipId, const QString& mode)
{
    const Clip* clip = findClip(toId(clipId));
    if (!clip)
        return false;
    for (int i = 0; i < int(std::size(kMatteModes)); ++i) {
        if (mode == QLatin1String(kMatteModes[i])) {
            ClipKeying keying = keyingOf(*clip);
            keying.matte = MatteMode(i);
            return apply(std::make_unique<SetClipKeyingCommand>(clip->id, std::move(keying), TVE_TR("core", "Change Matte")));
        }
    }
    reportError(tr("Unknown matte %1").arg(mode));
    return false;
}

QVariantList ProjectController::clipColorAt(double clipId, double u, double v)
{
    const Clip* clip = findClip(toId(clipId));
    const MediaItem* media = clip ? m_project.findMedia(clip->mediaId) : nullptr;
    if (!media || !media->hasVideo)
        return {};
    QImage picture;
    if (media->still) {
        picture = still::read(QString::fromStdString(media->path), 512);
    } else {
        const Ticks source = clampedSourceTime(m_project, *clip, ticks(m_playhead));
        if (auto frame = extractThumbnail(media->path, source, 360))
            picture = QImage(frame->pixels.data(), frame->width, frame->height, QImage::Format_RGBA8888).copy();
    }
    if (picture.isNull())
        return {};
    const int x = std::clamp(int(u * picture.width()), 0, picture.width() - 1);
    const int y = std::clamp(int(v * picture.height()), 0, picture.height() - 1);
    // A small average, so noise does not decide the colour.
    double rgb[3] = {0, 0, 0};
    int count = 0;
    for (int dy = -2; dy <= 2; ++dy) {
        for (int dx = -2; dx <= 2; ++dx) {
            const int px = std::clamp(x + dx, 0, picture.width() - 1);
            const int py = std::clamp(y + dy, 0, picture.height() - 1);
            const QColor c = picture.pixelColor(px, py);
            rgb[0] += c.redF();
            rgb[1] += c.greenF();
            rgb[2] += c.blueF();
            ++count;
        }
    }
    return {rgb[0] / count, rgb[1] / count, rgb[2] / count};
}

QVariantMap ProjectController::clipPlacement(double clipId) const
{
    const Clip* clip = findClip(toId(clipId));
    if (!clip || clip->adjustment)
        return {};
    const MediaItem* media = m_project.findMedia(clip->mediaId);
    // A title (and a compound clip's sequence) is drawn at canvas size.
    const bool canvasSized = clip->text || clip->compound();
    const double width = canvasSized ? m_project.settings.width : media ? media->width : 0;
    const double height = canvasSized ? m_project.settings.height : media ? media->height : 0;
    if (width <= 0 || height <= 0)
        return {};
    const LayerPlacement placement = placeLayer(clip->visualAt(ticks(m_playhead)), m_project.settings.width,
                                                m_project.settings.height, width, height);
    QVariantList m, toCanvas;
    for (double v : placement.canvasToLayer)
        m << v;
    for (double v : placement.layerToCanvas)
        toCanvas << v;
    QVariantList crop;
    for (double v : placement.cropRect)
        crop << v;
    return {{QStringLiteral("m"), m}, {QStringLiteral("toCanvas"), toCanvas}, {QStringLiteral("width"), width},
            {QStringLiteral("height"), height}, {QStringLiteral("crop"), crop}};
}

double ProjectController::clipAtCanvas(double u, double v) const
{
    // Top layer first: the first whose visible picture holds the point. Titles, drawings and
    // adjustment layers cover the whole canvas, so they are passed over.
    const auto layers = visibleVideoLayersAt(m_project, ticks(m_playhead));
    for (auto layer = layers.rbegin(); layer != layers.rend(); ++layer) {
        const Clip& clip = *layer->clip;
        if (clip.generated() || layer->track->locked)
            continue;
        const MediaItem* media = m_project.findMedia(clip.mediaId);
        const double w = clip.compound() ? m_project.settings.width : media ? media->width : 0;
        const double h = clip.compound() ? m_project.settings.height : media ? media->height : 0;
        if (w <= 0 || h <= 0)
            continue;
        const LayerPlacement placement = placeLayer(clip.visualAt(ticks(m_playhead)), m_project.settings.width,
                                                    m_project.settings.height, w, h);
        const auto [lu, lv] = placement.apply(u, v);
        const auto& c = placement.cropRect;
        if (placement.inFront(u, v) && lu >= c[0] && lu <= c[2] && lv >= c[1] && lv <= c[3])
            return double(clip.id);
    }
    return 0;
}

namespace {

struct Layout {
    const char* id;
    const char* title;
    std::vector<std::array<double, 4>> regions; // canvas regions, top clip first
    bool inset = false;                        // picture in picture: the top clip, small
};

const std::vector<Layout>& builtInLayouts()
{
    static const std::vector<Layout> layouts{
        {"pip", TVE_TR("core", "Picture in picture"), {{0.62, 0.06, 0.95, 0.39}}, true},
        {"side-by-side", TVE_TR("core", "Side by side"), {{0, 0, 0.5, 1}, {0.5, 0, 1, 1}}},
        {"top-bottom", TVE_TR("core", "Top and bottom"), {{0, 0, 1, 0.5}, {0, 0.5, 1, 1}}},
        {"three-columns", TVE_TR("core", "Three columns"), {{0, 0, 1.0 / 3, 1}, {1.0 / 3, 0, 2.0 / 3, 1}, {2.0 / 3, 0, 1, 1}}},
        {"one-and-two", TVE_TR("core", "One and two"), {{0, 0, 2.0 / 3, 1}, {2.0 / 3, 0, 1, 0.5}, {2.0 / 3, 0.5, 1, 1}}},
        {"grid", TVE_TR("core", "Grid of four"), {{0, 0, 0.5, 0.5}, {0.5, 0, 1, 0.5}, {0, 0.5, 0.5, 1}, {0.5, 0.5, 1, 1}}},
    };
    return layouts;
}

// A visual that makes a picture of `w` × `h` fill canvas region `r` (cropped to its shape).
ClipVisual fillRegion(ClipVisual v, double w, double h, double canvasW, double canvasH, const std::array<double, 4>& r)
{
    const double regionW = (r[2] - r[0]) * canvasW;
    const double regionH = (r[3] - r[1]) * canvasH;
    v.crop = {};
    if (w / h > regionW / regionH) {
        const double keep = (h * regionW / regionH) / w;
        v.crop.left = v.crop.right = (1.0 - keep) / 2.0;
    } else {
        const double keep = (w * regionH / regionW) / h;
        v.crop.top = v.crop.bottom = (1.0 - keep) / 2.0;
    }
    const double visibleW = w * (1.0 - v.crop.left - v.crop.right);
    const double visibleH = h * (1.0 - v.crop.top - v.crop.bottom);
    const double fit = std::min(canvasW / visibleW, canvasH / visibleH);
    v.transform.scale = regionW / (visibleW * fit);
    v.transform.x = (r[0] + r[2]) / 2.0 - 0.5;
    v.transform.y = (r[1] + r[3]) / 2.0 - 0.5;
    v.transform.rotation = 0.0;
    return v;
}

} // namespace

QVariantList ProjectController::layouts() const
{
    QVariantList list;
    for (const Layout& layout : builtInLayouts()) {
        QVariantList regions;
        for (const auto& r : layout.regions)
            regions << QVariant(QVariantList{r[0], r[1], r[2], r[3]});
        list << QVariantMap{{QStringLiteral("id"), QString::fromLatin1(layout.id)},
                            {QStringLiteral("title"), coreText(layout.title)},
                            {QStringLiteral("regions"), regions}};
    }
    return list;
}

bool ProjectController::applyLayout(const QString& layoutId)
{
    const auto& all = builtInLayouts();
    const auto layout = std::ranges::find_if(all, [&](const Layout& l) { return layoutId == QLatin1String(l.id); });
    if (layout == all.end()) {
        reportError(tr("Unknown layout %1").arg(layoutId));
        return false;
    }
    // The pictures at the playhead, top track first (titles and adjustment layers stay).
    const Ticks now = ticks(m_playhead);
    std::vector<const Clip*> pictures;
    const auto layers = visibleVideoLayersAt(m_project, now);
    for (auto it = layers.rbegin(); it != layers.rend(); ++it) {
        if (!it->clip->generated())
            pictures.push_back(it->clip);
    }
    const std::size_t needed = layout->inset ? 1 : layout->regions.size();
    if (pictures.size() < needed) {
        reportError(QStringLiteral("%1 needs %2 clips on separate tracks at the playhead")
                        .arg(QString::fromLatin1(layout->title))
                        .arg(needed));
        return false;
    }
    const double canvasW = m_project.settings.width;
    const double canvasH = m_project.settings.height;
    std::vector<std::unique_ptr<Command>> commands;
    for (std::size_t i = 0; i < needed; ++i) {
        const Clip& clip = *pictures[i];
        const MediaItem* media = m_project.findMedia(clip.mediaId);
        if (!media || media->width <= 0 || media->height <= 0)
            continue;
        ClipVisual visual = clip.visual;
        if (layout->inset) {
            // Whole, small, in the region's corner.
            const auto& r = layout->regions[0];
            visual.crop = {};
            const double fit = std::min(canvasW / media->width, canvasH / media->height);
            visual.transform.scale = std::min((r[2] - r[0]) * canvasW / (media->width * fit),
                                              (r[3] - r[1]) * canvasH / (media->height * fit));
            visual.transform.x = (r[0] + r[2]) / 2.0 - 0.5;
            visual.transform.y = (r[1] + r[3]) / 2.0 - 0.5;
            visual.transform.rotation = 0.0;
        } else {
            visual = fillRegion(visual, media->width, media->height, canvasW, canvasH, layout->regions[i]);
        }
        commands.push_back(std::make_unique<SetClipVisualCommand>(clip.id, visual));
    }
    return apply(std::make_unique<CompositeCommand>(std::move(commands), TVE_TR("core", "Apply Layout")));
}

// --- Looks and adjustment layers ---

QVariantList ProjectController::looks() const
{
    QVariantList list;
    for (const LookInfo& look : builtInLooks())
        list << QVariantMap{{QStringLiteral("id"), QString::fromStdString(look.id)},
                            {QStringLiteral("title"), catalogueText(look.title)},
                            {QStringLiteral("category"), catalogueText(look.category)}};
    return list;
}

QVariantList ProjectController::lutLibrary() const
{
    QVariantList list;
    for (const LookInfo& look : builtInLooks()) {
        if (!look.grade.lutPath.empty())
            list << QVariantMap{{QStringLiteral("name"), catalogueText(look.title)},
                                {QStringLiteral("path"), QString::fromStdString(look.grade.lutPath)}};
    }
    return list;
}

bool ProjectController::applyLook(double clipId, const QString& lookId)
{
    const LookInfo* look = findLook(lookId.toStdString());
    if (!look) {
        reportError(tr("Unknown look %1").arg(lookId));
        return false;
    }
    const Clip* clip = findClip(toId(clipId));
    if (!clip) {
        reportError(tr("Select a video clip"));
        return false;
    }
    return apply(std::make_unique<SetClipLookCommand>(clip->id, ClipLook{look->id, look->title, 1.0, look->grade},
                                                      TVE_TR("core", "Apply Look")));
}

bool ProjectController::setLookIntensity(double clipId, double intensity)
{
    const Clip* clip = findClip(toId(clipId));
    if (!clip || !clip->look)
        return false;
    ClipLook look = *clip->look;
    look.intensity = std::clamp(intensity, 0.0, 1.0);
    return apply(std::make_unique<SetClipLookCommand>(clip->id, look, TVE_TR("core", "Change Look")));
}

void ProjectController::previewLookIntensity(double clipId, double intensity)
{
    const Clip* clip = findClip(toId(clipId));
    if (!clip || !clip->look || !m_view)
        return;
    ClipLook look = *clip->look;
    look.intensity = std::clamp(intensity, 0.0, 1.0);
    m_view->setPreviewLook(clip->id, look.effective());
}

bool ProjectController::removeLook(double clipId)
{
    const Clip* clip = findClip(toId(clipId));
    if (!clip || !clip->look)
        return false;
    return apply(std::make_unique<SetClipLookCommand>(clip->id, std::nullopt, TVE_TR("core", "Remove Look")));
}

bool ProjectController::addAdjustmentLayer(double trackId, double start)
{
    constexpr Ticks kLength = 5 * kTicksPerSecond;
    const Ticks at = start < 0 ? ticks(m_playhead) : ticks(start);
    Id track = toId(trackId);
    if (trackId < 0) {
        // The lowest free video track above the main one; else a new track on top.
        const auto main = m_project.trackIndex(mainTrackId());
        for (std::size_t i = main ? *main + 1 : 0; i < m_project.tracks.size() && track == kNoId; ++i) {
            const Track& candidate = m_project.tracks[i];
            if (candidate.kind != TrackKind::Audio && !candidate.locked && !candidate.hidden
                && !candidate.overlaps(at, kLength))
                track = candidate.id;
        }
    }
    auto command = std::make_unique<AddAdjustmentCommand>(track, at, kLength);
    auto* raw = command.get();
    if (!apply(std::move(command)))
        return false;
    setSelectedClip(double(raw->clipId()));
    return true;
}

bool ProjectController::copyGrade(double clipId)
{
    const Clip* clip = findClip(toId(clipId));
    if (!clip)
        return false;
    m_copiedGrade = clip->color;
    emit colorClipboardChanged();
    return true;
}

bool ProjectController::pasteGrade(double clipId)
{
    if (!m_copiedGrade) {
        reportError(tr("Copy a clip's grade first"));
        return false;
    }
    return apply(std::make_unique<SetClipColorCommand>(std::vector<Id>{toId(clipId)}, *m_copiedGrade, TVE_TR("core", "Paste Grade")));
}

QString ProjectController::colorPresetDirectory() const
{
    const QByteArray override = qgetenv("TVE_COLOR_PRESET_DIR");
    return !override.isEmpty() ? QString::fromLocal8Bit(override)
                               : QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                                     + QStringLiteral("/Color Presets");
}

QString ProjectController::colorPresetPath(const QString& name) const
{
    QString safe = name.trimmed();
    for (QChar& c : safe) {
        if (QStringLiteral("/\\:*?\"<>|").contains(c))
            c = QLatin1Char('-');
    }
    return QDir(colorPresetDirectory()).filePath(safe + QStringLiteral(".json"));
}

QStringList ProjectController::colorPresets() const
{
    QStringList names;
    const QDir dir(colorPresetDirectory());
    for (const QFileInfo& info : dir.entryInfoList({QStringLiteral("*.json")}, QDir::Files, QDir::Name | QDir::IgnoreCase)) {
        QFile file(info.absoluteFilePath());
        if (!file.open(QIODevice::ReadOnly))
            continue;
        const QJsonObject object = QJsonDocument::fromJson(file.readAll()).object();
        const QString name = object.value(QStringLiteral("name")).toString();
        if (!name.isEmpty() && object.contains(QStringLiteral("grade")))
            names << name;
    }
    return names;
}

bool ProjectController::saveColorPreset(double clipId, const QString& name)
{
    const Clip* clip = findClip(toId(clipId));
    if (!clip)
        return false;
    if (name.trimmed().isEmpty()) {
        reportError(tr("Name the preset"));
        return false;
    }
    if (!QDir().mkpath(colorPresetDirectory())) {
        reportError(tr("Could not create %1").arg(colorPresetDirectory()));
        return false;
    }
    const nlohmann::json json{{"name", name.trimmed().toStdString()}, {"grade", gradeToJson(clip->color)}};
    QSaveFile file(colorPresetPath(name));
    if (!file.open(QIODevice::WriteOnly) || file.write(QByteArray::fromStdString(json.dump(2))) < 0 || !file.commit()) {
        reportError(tr("Could not save the preset %1").arg(name));
        return false;
    }
    emit colorPresetsChanged();
    return true;
}

bool ProjectController::applyColorPreset(double clipId, const QString& name)
{
    QFile file(colorPresetPath(name));
    if (!file.open(QIODevice::ReadOnly)) {
        reportError(tr("No colour preset named %1").arg(name));
        return false;
    }
    ColorGrade grade;
    try {
        grade = gradeFromJson(nlohmann::json::parse(file.readAll().toStdString()).at("grade"));
    } catch (const std::exception&) {
        reportError(tr("The preset %1 is damaged").arg(name));
        return false;
    }
    return apply(std::make_unique<SetClipColorCommand>(std::vector<Id>{toId(clipId)}, grade, TVE_TR("core", "Apply Color Preset")));
}

QVariantList ProjectController::sampleColorCurve(const QVariantList& points, int count) const
{
    ColorCurve curve;
    for (const QVariant& point : points) {
        const QVariantMap m = point.toMap();
        curve.push_back({m.value(QStringLiteral("x")).toDouble(), m.value(QStringLiteral("y")).toDouble()});
    }
    QVariantList samples;
    count = std::clamp(count, 2, 1024);
    for (int i = 0; i < count; ++i)
        samples << evaluateColorCurve(curve, double(i) / double(count - 1));
    return samples;
}

bool ProjectController::deleteColorPreset(const QString& name)
{
    if (!QFile::remove(colorPresetPath(name)))
        return false;
    emit colorPresetsChanged();
    return true;
}

// --- Keyframes ---

Ticks ProjectController::keyTime(const Clip& clip) const
{
    const Ticks frame = frameTicks();
    const Ticks snapped = (ticks(m_playhead) + frame / 2) / frame * frame;
    return std::clamp<Ticks>(snapped - clip.start, 0, clip.duration);
}

void ProjectController::setKey(Clip& clip, const std::string& id, double value) const
{
    const Ticks at = keyTime(clip);
    if (const auto range = channelRange(clip, id))
        value = std::clamp(value, range->first, range->second);
    Keyframes& keys = ensureChannel(clip, id);
    for (Keyframe& k : keys) {
        if (std::llabs(k.time - at) <= frameTolerance()) {
            k.value = value;
            return;
        }
    }
    keys.insert(std::ranges::upper_bound(keys, at, {}, &Keyframe::time), Keyframe{at, value});
}

Keyframes ProjectController::withKey(const Clip& clip, std::string_view id, double value) const
{
    Clip copy = clip;
    setKey(copy, std::string(id), value);
    return *channelKeys(copy, std::string(id));
}

bool ProjectController::commitAnimation(const Clip& before, Clip after, const char* label)
{
    pruneChannels(after);
    std::map<std::string, Keyframes> changes;
    for (const auto& [id, keys] : after.keyframes) {
        const Keyframes* old = before.keyframesOf(id);
        if (!old || *old != keys)
            changes.emplace(id, keys);
    }
    for (const auto& [id, keys] : before.keyframes) {
        if (!keys.empty() && !after.keyframes.contains(id))
            changes.emplace(id, Keyframes{});
    }
    std::optional<ClipVisual> visual;
    if (after.visual != before.visual)
        visual = validateVisual(after.visual) ? before.visual : after.visual; // edges keyed apart may not fit
    std::optional<float> gain;
    if (after.gainDb != before.gainDb)
        gain = std::clamp(after.gainDb, kMinGainDb, kMaxGainDb);
    const bool effectsChanged = after.effects != before.effects;
    const bool audioEffectsChanged = after.audioEffects != before.audioEffects;
    if (changes.empty() && !visual && !gain && !effectsChanged && !audioEffectsChanged)
        return false;
    auto command = std::make_unique<SetClipKeyframesCommand>(before.id, std::move(changes), label, visual, gain);
    if (effectsChanged)
        command->setEffects(std::move(after.effects));
    if (audioEffectsChanged)
        command->setAudioEffects(std::move(after.audioEffects));
    return apply(std::move(command));
}

bool ProjectController::addKeyframes(double clipId, const QStringList& properties)
{
    const Clip* clip = findClip(toId(clipId));
    if (!clip) {
        reportError(tr("Select a clip to animate"));
        return false;
    }
    const auto location = m_project.locateClip(clip->id);
    std::vector<std::string> ids;
    for (const QString& p : properties)
        ids.push_back(p.toStdString());
    if (ids.empty()) {
        if (location->track->kind == TrackKind::Video)
            ids = {"x", "y", "scale", "rotation", "opacity"};
        else
            ids = {"volume"};
    }
    const Ticks at = clip->start + keyTime(*clip);
    Clip after = *clip;
    for (const std::string& id : ids) {
        if (!channelRange(*clip, id)) {
            reportError(QStringLiteral("%1 cannot be animated").arg(QString::fromStdString(id)));
            return false;
        }
        setKey(after, id, channelValue(*clip, id, at));
    }
    return commitAnimation(*clip, after, TVE_TR("core", "Add Keyframe"));
}

bool ProjectController::removeKeyframes(double clipId, const QStringList& properties)
{
    const Clip* clip = findClip(toId(clipId));
    if (!clip)
        return false;
    const Ticks at = keyTime(*clip);
    Clip after = *clip;
    bool removed = false;
    std::vector<std::string> emptied;
    forEachChannel(after, [&](const std::string& id, Keyframes& keys) {
        if (!properties.isEmpty() && !properties.contains(QString::fromStdString(id)))
            return;
        const std::size_t before = keys.size();
        std::erase_if(keys, [&](const Keyframe& k) { return std::llabs(k.time - at) <= frameTolerance(); });
        removed = removed || keys.size() != before;
        if (keys.empty())
            emptied.push_back(id);
    });
    if (!removed) {
        reportError(tr("No keyframe at the playhead"));
        return false;
    }
    // A channel whose last keyframe goes keeps the value it had there.
    for (const std::string& id : emptied)
        setChannelStatic(after, id, channelValue(*clip, id, clip->start + at));
    return commitAnimation(*clip, after, TVE_TR("core", "Remove Keyframe"));
}

bool ProjectController::clearKeyframes(double clipId, const QStringList& properties)
{
    const Clip* clip = findClip(toId(clipId));
    if (!clip)
        return false;
    // Every keyframe of these channels goes; each keeps its value at the playhead.
    const Ticks now = clip->start + keyTime(*clip);
    Clip after = *clip;
    std::vector<std::string> cleared;
    forEachChannel(after, [&](const std::string& id, Keyframes& keys) {
        if (properties.isEmpty() || properties.contains(QString::fromStdString(id))) {
            keys.clear();
            cleared.push_back(id);
        }
    });
    for (const std::string& id : cleared)
        setChannelStatic(after, id, channelValue(*clip, id, now));
    if (cleared.empty())
        return false;
    return commitAnimation(*clip, after, TVE_TR("core", "Clear Keyframes"));
}

namespace {

std::optional<Easing> easingFromName(const QString& name)
{
    for (Easing e : {Easing::Linear, Easing::EaseIn, Easing::EaseOut, Easing::EaseInOut, Easing::Hold, Easing::Curve}) {
        if (easingName(e) == name)
            return e;
    }
    return std::nullopt;
}

} // namespace

bool ProjectController::setKeyframeEasing(double clipId, const QString& easing)
{
    const auto chosen = easingFromName(easing);
    if (!chosen) {
        reportError(tr("Unknown easing %1").arg(easing));
        return false;
    }
    const Clip* clip = findClip(toId(clipId));
    if (!clip)
        return false;
    const Ticks at = keyTime(*clip);
    Clip after = *clip;
    forEachChannel(after, [&](const std::string&, Keyframes& keys) {
        for (Keyframe& k : keys) {
            if (std::llabs(k.time - at) > frameTolerance() || k.easing == *chosen)
                continue;
            // A custom curve starts from the shape of the easing it replaces.
            if (*chosen == Easing::Curve && k.easing != Easing::Hold)
                k.curve = curveFor(k.easing);
            k.easing = *chosen;
        }
    });
    return commitAnimation(*clip, after, TVE_TR("core", "Change Keyframe"));
}

bool ProjectController::setKeyframeCurve(double clipId, double x1, double y1, double x2, double y2)
{
    const Clip* clip = findClip(toId(clipId));
    if (!clip)
        return false;
    const EasingCurve curve{std::clamp(x1, 0.0, 1.0), std::clamp(y1, kMinCurveY, kMaxCurveY), std::clamp(x2, 0.0, 1.0),
                            std::clamp(y2, kMinCurveY, kMaxCurveY)};
    const Ticks at = keyTime(*clip);
    Clip after = *clip;
    forEachChannel(after, [&](const std::string&, Keyframes& keys) {
        for (Keyframe& k : keys) {
            if (std::llabs(k.time - at) <= frameTolerance()) {
                k.easing = Easing::Curve;
                k.curve = curve;
            }
        }
    });
    if (!commitAnimation(*clip, after, TVE_TR("core", "Change Keyframe"))) {
        reportError(tr("No keyframe at the playhead"));
        return false;
    }
    return true;
}

bool ProjectController::moveKeyframes(double clipId, double from, double to)
{
    const Clip* clip = findClip(toId(clipId));
    if (!clip)
        return false;
    const Ticks frame = frameTicks();
    const Ticks source = ticks(from);
    // Keyframes land on the frame grid of the timeline.
    const Ticks target = std::clamp<Ticks>((clip->start + ticks(to) + frame / 2) / frame * frame - clip->start, 0,
                                           clip->duration);
    Clip after = *clip;
    bool taken = false;
    forEachChannel(after, [&](const std::string&, Keyframes& keys) {
        auto it = std::ranges::find_if(keys, [&](const Keyframe& k) { return std::llabs(k.time - source) <= frame / 2; });
        if (it == keys.end() || it->time == target)
            return;
        if (std::ranges::any_of(keys, [&](const Keyframe& k) { return &k != &*it && k.time == target; })) {
            taken = true;
            return;
        }
        it->time = target;
        std::ranges::sort(keys, {}, &Keyframe::time);
    });
    if (taken) {
        reportError(tr("Another keyframe is already there"));
        return false;
    }
    const Ticks landed = clip->start + target; // `clip` does not outlive the edit
    if (!commitAnimation(*clip, after, TVE_TR("core", "Move Keyframe")))
        return false;
    seek(::seconds(landed));
    return true;
}

bool ProjectController::seekKeyframe(double clipId, int direction)
{
    const Clip* clip = findClip(toId(clipId));
    if (!clip || direction == 0)
        return false;
    const Ticks now = ticks(m_playhead) - clip->start;
    std::optional<Ticks> best;
    forEachChannel(*clip, [&](const std::string&, const Keyframes& keys) {
        for (const Keyframe& k : keys) {
            if (k.time < 0 || k.time > clip->duration)
                continue;
            const bool ahead = direction > 0 ? k.time > now + frameTolerance() : k.time < now - frameTolerance();
            if (ahead && (!best || (direction > 0 ? k.time < *best : k.time > *best)))
                best = k.time;
        }
    });
    if (!best)
        return false;
    seek(::seconds(clip->start + *best));
    return true;
}

// --- Speed ---

bool ProjectController::setClipSpeed(double clipId, double speed, bool reversed)
{
    // Turning a ramped clip around keeps its ramp; a new speed replaces it.
    if (const Clip* clip = findClip(toId(clipId)); clip && !clip->speedRamp.empty() && speed == clip->speed)
        return apply(std::make_unique<SetClipSpeedRampCommand>(clip->id, clip->speedRamp, reversed));
    return apply(std::make_unique<SetClipSpeedCommand>(toId(clipId), std::clamp(speed, kMinSpeed, kMaxSpeed), reversed));
}

QVariantList ProjectController::speedRampPresets() const
{
    QVariantList list;
    for (const SpeedRampPreset& preset : tve::speedRampPresets())
        list << QVariantMap{{QStringLiteral("id"), QString::fromStdString(preset.id)},
                            {QStringLiteral("title"), coreText(preset.title)},
                            {QStringLiteral("points"), rampList(preset.ramp)}};
    return list;
}

bool ProjectController::setClipSpeedRamp(double clipId, const QVariantList& points)
{
    SpeedRamp ramp;
    for (const QVariant& point : points) {
        const QVariantMap map = point.toMap();
        ramp.push_back({map.value(QStringLiteral("at")).toDouble(), map.value(QStringLiteral("speed")).toDouble()});
    }
    if (ramp.empty()) {
        reportError(tr("A speed ramp needs points"));
        return false;
    }
    return apply(std::make_unique<SetClipSpeedRampCommand>(toId(clipId), std::move(ramp)));
}

bool ProjectController::applySpeedRampPreset(double clipId, const QString& presetId)
{
    for (const SpeedRampPreset& preset : tve::speedRampPresets()) {
        if (QString::fromStdString(preset.id) == presetId)
            return apply(std::make_unique<SetClipSpeedRampCommand>(toId(clipId), preset.ramp));
    }
    reportError(tr("Unknown speed ramp %1").arg(presetId));
    return false;
}

bool ProjectController::removeSpeedRamp(double clipId)
{
    return apply(std::make_unique<SetClipSpeedRampCommand>(toId(clipId), SpeedRamp{}));
}

double ProjectController::clipSourceTime(double clipId, double offset) const
{
    const Clip* clip = findClip(toId(clipId));
    if (!clip)
        return 0.0;
    return ::seconds(clip->sourceTimeAt(clip->start + std::clamp(ticks(offset), Ticks(0), clip->duration - 1)));
}

bool ProjectController::freezeFrame(double holdSeconds)
{
    const Ticks at = ticks(m_playhead);
    // The selected clip if the playhead is on it, else the visible clip there.
    Id target = kNoId;
    if (const Clip* selected = findClip(m_selectedClip);
        selected && !selected->text && selected->start <= at && at < selected->end()) {
        target = selected->id;
    } else {
        // The topmost video clip there (titles have no frames to freeze).
        const auto layers = visibleVideoLayersAt(m_project, at);
        for (auto layer = layers.rbegin(); layer != layers.rend() && target == kNoId; ++layer) {
            if (!layer->clip->text)
                target = layer->clip->id;
        }
    }
    if (target == kNoId) {
        reportError(tr("Put the playhead on a video clip to freeze a frame"));
        return false;
    }
    auto command = std::make_unique<FreezeFrameCommand>(target, at, ticks(std::max(holdSeconds, 0.1)));
    auto* raw = command.get();
    if (!apply(std::move(command)))
        return false;
    setSelectedClip(double(raw->freezeClipId()));
    return true;
}

// --- Titles ---

QStringList ProjectController::titleFonts() const
{
    return titleFontFamilies();
}

bool ProjectController::addTitle(const QVariantMap& style, double trackId, double start)
{
    // Five seconds unless the style asks for longer (credits roll for a while).
    const double length = style.value(QStringLiteral("duration"), 5.0).toDouble();
    const Ticks kTitleLength = ticks(std::clamp(length, 0.5, 600.0));
    const Ticks at = start < 0 ? ticks(m_playhead) : ticks(start);
    Id track = toId(trackId);
    if (trackId < 0) {
        // The lowest free picture track above the main one; else a new track on top.
        const auto main = m_project.trackIndex(mainTrackId());
        for (std::size_t i = main ? *main + 1 : 0; i < m_project.tracks.size() && track == kNoId; ++i) {
            const Track& candidate = m_project.tracks[i];
            if (candidate.kind != TrackKind::Audio && !candidate.locked && !candidate.hidden
                && !candidate.overlaps(at, kTitleLength))
                track = candidate.id;
        }
    }
    ClipVisual visual;
    visual.transform.x = style.value(QStringLiteral("x")).toDouble();
    visual.transform.y = style.value(QStringLiteral("y")).toDouble();
    auto command = std::make_unique<AddTitleCommand>(track, at, kTitleLength, textFrom(style), visual);
    auto* raw = command.get();
    if (!apply(std::move(command)))
        return false;
    const Id added = raw->clipId();
    setSelectedClip(double(added));
    // One that moves with the music, or a visualiser, listens to the music under it, if there
    // is some.
    if (const Clip* clip = findClip(added); clip && !analyzing() && musicUnder(*clip) != kNoId) {
        if (clip->text->visualiser && clip->text->visualiser->frames.empty())
            measureVisualiser(double(added));
        else if (clip->text->react.kind != TextReactKind::None)
            reactToMusic(double(added));
    }
    return true;
}

bool ProjectController::setClipText(double clipId, const QVariantMap& changes)
{
    const Clip* clip = findClip(toId(clipId));
    if (!clip || !clip->text)
        return false;
    ClipText text = textFrom(changes, *clip->text);
    if (text == *clip->text)
        return true;
    return apply(std::make_unique<SetClipTextCommand>(clip->id, std::move(text)));
}

// --- Title presets, music, charts and logo reveals ---

QString ProjectController::titlePresetDirectory() const
{
    const QByteArray override = qgetenv("TVE_TITLE_PRESET_DIR");
    return !override.isEmpty() ? QString::fromLocal8Bit(override)
                               : QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                                     + QStringLiteral("/Title Presets");
}

QString ProjectController::titlePresetPath(const QString& name) const
{
    QString safe = name.trimmed();
    for (QChar& c : safe) {
        if (QStringLiteral("/\\:*?\"<>|").contains(c))
            c = QLatin1Char('-');
    }
    return QDir(titlePresetDirectory()).filePath(safe + QStringLiteral(".json"));
}

QVariantList ProjectController::titlePresets() const
{
    QVariantList list;
    const QDir dir(titlePresetDirectory());
    for (const QFileInfo& info : dir.entryInfoList({QStringLiteral("*.json")}, QDir::Files, QDir::Name | QDir::IgnoreCase)) {
        QFile file(info.absoluteFilePath());
        if (!file.open(QIODevice::ReadOnly))
            continue;
        const QJsonObject object = QJsonDocument::fromJson(file.readAll()).object();
        const QString name = object.value(QStringLiteral("name")).toString();
        const QJsonObject style = object.value(QStringLiteral("title")).toObject();
        if (!name.isEmpty() && !style.isEmpty())
            list << QVariantMap{{QStringLiteral("name"), name}, {QStringLiteral("style"), style.toVariantMap()}};
    }
    return list;
}

bool ProjectController::saveTitlePreset(double clipId, const QString& name)
{
    const Clip* clip = findClip(toId(clipId));
    if (!clip || !clip->text) {
        reportError(tr("Select a title to save as a preset"));
        return false;
    }
    if (name.trimmed().isEmpty()) {
        reportError(tr("Name the preset"));
        return false;
    }
    if (!QDir().mkpath(titlePresetDirectory())) {
        reportError(tr("Could not create %1").arg(titlePresetDirectory()));
        return false;
    }
    // Its look and text (a caption's words and the measured music stay with the clip).
    QVariantMap style = textMap(*clip->text);
    style.remove(QStringLiteral("wordCount"));
    QVariantMap react = style.value(QStringLiteral("react")).toMap();
    react.remove(QStringLiteral("measured"));
    style.insert(QStringLiteral("react"), react);
    if (QVariantMap chart = style.value(QStringLiteral("chart")).toMap(); !chart.isEmpty()) {
        chart.remove(QStringLiteral("data"));
        style.insert(QStringLiteral("chart"), chart);
    } else {
        style.remove(QStringLiteral("chart"));
    }
    style.insert(QStringLiteral("x"), clip->visual.transform.x);
    style.insert(QStringLiteral("y"), clip->visual.transform.y);
    style.insert(QStringLiteral("duration"), toSeconds(clip->duration));
    const QJsonObject object{{QStringLiteral("name"), name.trimmed()}, {QStringLiteral("title"), QJsonObject::fromVariantMap(style)}};
    QSaveFile file(titlePresetPath(name));
    if (!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(object).toJson()) < 0 || !file.commit()) {
        reportError(tr("Could not save the preset %1").arg(name));
        return false;
    }
    emit titlePresetsChanged();
    return true;
}

bool ProjectController::applyTitlePreset(double clipId, const QString& name)
{
    const Clip* clip = findClip(toId(clipId));
    if (!clip || !clip->text) {
        reportError(tr("Select a title to restyle"));
        return false;
    }
    QFile file(titlePresetPath(name));
    if (!file.open(QIODevice::ReadOnly)) {
        reportError(tr("No title preset named %1").arg(name));
        return false;
    }
    QVariantMap style = QJsonDocument::fromJson(file.readAll()).object().value(QStringLiteral("title")).toObject().toVariantMap();
    if (style.isEmpty()) {
        reportError(tr("The preset %1 is damaged").arg(name));
        return false;
    }
    // The clip keeps its own words, path and chart values.
    for (const char* key : {"text", "path", "x", "y", "duration"})
        style.remove(QLatin1String(key));
    if (QVariantMap chart = style.value(QStringLiteral("chart")).toMap(); !chart.isEmpty() && clip->text->chart) {
        chart.remove(QStringLiteral("points"));
        style.insert(QStringLiteral("chart"), chart);
    } else if (!chart.isEmpty()) {
        style.remove(QStringLiteral("chart")); // not on a title without values
    } else {
        style.insert(QStringLiteral("chart"), QVariant());
    }
    ClipText text = textFrom(style, *clip->text);
    if (text == *clip->text)
        return true;
    auto command = std::make_unique<SetClipTextCommand>(clip->id, std::move(text));
    return apply(std::move(command));
}

bool ProjectController::deleteTitlePreset(const QString& name)
{
    if (!QFile::remove(titlePresetPath(name)))
        return false;
    emit titlePresetsChanged();
    return true;
}

Id ProjectController::musicUnder(const Clip& title) const
{
    Id found = kNoId;
    Ticks best = 0;
    bool bestOnAudio = false;
    for (const Track& track : m_project.tracks) {
        if (track.muted)
            continue;
        for (const Clip& clip : track.clips) {
            const MediaItem* media = m_project.findMedia(clip.mediaId);
            if (!media || !media->hasAudio || !clip.audioEnabled || clip.id == title.id)
                continue;
            const Ticks overlap = std::min(clip.end(), title.end()) - std::max(clip.start, title.start);
            const bool onAudio = track.kind == TrackKind::Audio;
            if (overlap > 0 && (std::pair{onAudio, overlap} > std::pair{bestOnAudio, best})) {
                best = overlap;
                bestOnAudio = onAudio;
                found = clip.id;
            }
        }
    }
    return found;
}

bool ProjectController::reactToMusic(double clipId, const QVariantMap& options)
{
    const Clip* title = findClip(toId(clipId));
    if (!title || !title->text) {
        reportError(tr("Select a title to move with the music"));
        return false;
    }
    ClipText text = textFrom(QVariantMap{{QStringLiteral("react"), options}}, *title->text);
    if (text.react.kind == TextReactKind::None) {
        text.react.levels.clear();
        if (text == *title->text)
            return true;
        return apply(std::make_unique<SetClipTextCommand>(title->id, std::move(text)));
    }
    if (analyzing()) {
        reportError(tr("An analysis is already running"));
        return false;
    }
    // The music: the clip asked for, or the one with sound under the title the longest (on an
    // audio track before a video track's own sound).
    Id musicId = toId(options.value(QStringLiteral("musicClipId"), -1).toDouble());
    if (!options.contains(QStringLiteral("musicClipId"))) {
        musicId = musicUnder(*title);
        if (musicId == kNoId) {
            reportError(tr("Put music under the title first"));
            return false;
        }
    }
    const Clip* music = analysableClip(musicId, true, false);
    if (!music)
        return false;
    const Id titleId = title->id;
    withFeatures({music->mediaId}, false, tr("Listening to the music"), [this, titleId, musicId, react = text.react] {
        const Clip* title = findClip(titleId);
        const Clip* music = findClip(musicId);
        const MediaItem* media = music ? m_project.findMedia(music->mediaId) : nullptr;
        if (!title || !title->text || !media) {
            emit analysisFinished(false, {});
            return;
        }
        // Each moment of the title, in the music's media time; silence outside the clip.
        const auto count = std::size_t(std::ceil(toSeconds(title->duration) * TextReact::kRate));
        ClipText text = *title->text;
        text.react = react;
        text.react.levels = tve::musicLevels(m_audioFeatures.at(fingerprintOf(*media)),
                                             title->start - music->start + music->sourceIn,
                                             std::min(count, TextReact::kMaxLevels), TextReact::kRate,
                                             {music->sourceIn, music->sourceIn + music->duration});
        const bool ok = apply(std::make_unique<SetClipTextCommand>(titleId, std::move(text), TVE_TR("core", "Move With the Music")));
        emit analysisFinished(ok, {});
    });
    return true;
}

bool ProjectController::loadChartData(double clipId, const QUrl& file)
{
    const Clip* clip = findClip(toId(clipId));
    if (!clip || !clip->text) {
        reportError(tr("Select a title to chart"));
        return false;
    }
    QFile data(file.isLocalFile() ? file.toLocalFile() : file.toString());
    if (data.size() > 1024 * 1024 || !data.open(QIODevice::ReadOnly | QIODevice::Text)) {
        reportError(tr("Could not read %1").arg(QFileInfo(data.fileName()).fileName()));
        return false;
    }
    const std::vector<ChartPoint> points = parseChartData(QString::fromUtf8(data.readAll()));
    if (points.empty()) {
        reportError(tr("%1 has no values to chart: put a label and a number on each line")
                        .arg(QFileInfo(data.fileName()).fileName()));
        return false;
    }
    ClipText text = *clip->text;
    TextChart chart = text.chart.value_or(TextChart{});
    chart.points = points;
    text.chart = std::move(chart);
    return apply(std::make_unique<SetClipTextCommand>(clip->id, std::move(text)));
}

bool ProjectController::measureSpectrum(Id musicId, Ticks start, Ticks duration,
                                        std::function<void(std::vector<std::uint8_t>)> then)
{
    if (analyzing()) {
        reportError(tr("An analysis is already running"));
        return false;
    }
    const Clip* music = analysableClip(musicId, true, false);
    if (!music)
        return false;
    const MediaItem* media = m_project.findMedia(music->mediaId);
    const auto count = std::size_t(std::ceil(toSeconds(duration) * TextVisualiser::kRate));
    const Ticks mediaStart = start - music->start + music->sourceIn;
    const tve::TimeRange within{music->sourceIn, music->sourceIn + music->duration};
    m_cancelAnalysis = false;
    m_analysisProgress = 0;
    m_analysisTask = tr("Listening to the music");
    m_analysisThread = std::thread([this, path = media->path, mediaStart, count, within, then = std::move(then)]() mutable {
        double reported = -1.0;
        std::string error;
        auto levels = tve::analyzeSpectrum(path, mediaStart, count, TextVisualiser::kRate, TextVisualiser::kBands, within,
                                           [&](double done) {
                                               if (done - reported >= 0.01) {
                                                   reported = done;
                                                   QMetaObject::invokeMethod(this, [this, done] {
                                                       m_analysisProgress = done;
                                                       emit analysisChanged();
                                                   }, Qt::QueuedConnection);
                                               }
                                               return !m_cancelAnalysis.load();
                                           },
                                           &error);
        QMetaObject::invokeMethod(this, [this, levels = std::move(levels), error, then = std::move(then)]() mutable {
            if (m_analysisThread.joinable())
                m_analysisThread.join();
            m_analysisProgress = 0;
            m_analysisTask.clear();
            emit analysisChanged();
            if (levels) {
                then(std::move(*levels));
                return;
            }
            const QString message = error.empty() ? QString() : coreText(error);
            if (!message.isEmpty())
                reportError(tr("Analysis failed: %1").arg(message));
            emit analysisFinished(false, message);
        }, Qt::QueuedConnection);
    });
    emit analysisChanged();
    return true;
}

bool ProjectController::addVisualiser(const QVariantMap& style)
{
    // The music at the playhead: on an audio track first, else a video clip's own sound.
    const Ticks at = ticks(m_playhead);
    const Clip* music = nullptr;
    bool onAudio = false;
    for (const Track& track : m_project.tracks) {
        if (track.muted)
            continue;
        for (const Clip& clip : track.clips) {
            const bool audioTrack = track.kind == TrackKind::Audio;
            if (clip.start <= at && at < clip.end() && hasOwnSound(clip, track.kind) && (!music || (audioTrack && !onAudio))) {
                music = &clip;
                onAudio = audioTrack;
            }
        }
    }
    if (!music) {
        reportError(tr("Put the playhead on music first"));
        return false;
    }
    const Id musicId = music->id;
    const Ticks start = music->start;
    const Ticks duration = std::min(music->duration, ticks(TextVisualiser::kMaxSeconds));
    ClipText text = textFrom(style);
    if (!text.visualiser)
        text.visualiser = TextVisualiser{};
    return measureSpectrum(musicId, start, duration, [this, text, start, duration](std::vector<std::uint8_t> levels) mutable {
        text.visualiser->frames = std::move(levels);
        // The lowest free picture track above the main one (a new one on top if none is).
        Id track = kNoId;
        const auto main = m_project.trackIndex(mainTrackId());
        for (std::size_t i = main ? *main + 1 : 0; i < m_project.tracks.size() && track == kNoId; ++i) {
            const Track& candidate = m_project.tracks[i];
            if (candidate.kind != TrackKind::Audio && !candidate.locked && !candidate.hidden && !candidate.overlaps(start, duration))
                track = candidate.id;
        }
        auto command = std::make_unique<AddTitleCommand>(track, start, duration, std::move(text));
        auto* raw = command.get();
        const bool ok = apply(std::move(command));
        if (ok)
            setSelectedClip(double(raw->clipId()));
        emit analysisFinished(ok, {});
    });
}

bool ProjectController::measureVisualiser(double clipId)
{
    const Clip* title = findClip(toId(clipId));
    if (!title || !title->text || !title->text->visualiser) {
        reportError(tr("Select an audio visualiser"));
        return false;
    }
    const Id musicId = musicUnder(*title);
    if (musicId == kNoId) {
        reportError(tr("Put music under the title first"));
        return false;
    }
    const Id id = title->id;
    return measureSpectrum(musicId, title->start, std::min(title->duration, ticks(TextVisualiser::kMaxSeconds)),
                           [this, id](std::vector<std::uint8_t> levels) {
                               const Clip* title = findClip(id);
                               if (!title || !title->text || !title->text->visualiser) {
                                   emit analysisFinished(false, {});
                                   return;
                               }
                               ClipText text = *title->text;
                               text.visualiser->frames = std::move(levels);
                               const bool ok = apply(std::make_unique<SetClipTextCommand>(id, std::move(text),
                                                                                          TVE_TR("core", "Measure the Music")));
                               emit analysisFinished(ok, {});
                           });
}

QVariantList ProjectController::logoReveals() const
{
    QVariantList list;
    for (const LogoRevealPreset& preset : logoRevealPresets())
        list << QVariantMap{{QStringLiteral("id"), QString::fromLatin1(preset.id)}, {QStringLiteral("title"), coreText(preset.title)}};
    return list;
}

bool ProjectController::addLogoReveal(const QString& revealId, const QUrl& logo, const QString& tagline)
{
    const LogoRevealPreset* preset = findLogoReveal(revealId.toStdString());
    if (!preset) {
        reportError(tr("Unknown logo reveal %1").arg(revealId));
        return false;
    }
    const QString path = logo.isLocalFile() ? logo.toLocalFile() : logo.toString();
    Project after = m_project;
    // The picture, once.
    Id mediaId = kNoId;
    if (const auto same = std::ranges::find(after.media, path.toStdString(), &MediaItem::path); same != after.media.end()) {
        mediaId = same->id;
    } else {
        QString error;
        std::optional<MediaItem> item = describeMedia(path.toStdString(), &error);
        if (!item) {
            reportError(error.isEmpty() ? tr("Could not open %1").arg(QFileInfo(path).fileName()) : error);
            return false;
        }
        ImportMediaCommand import(std::move(*item));
        if (auto failed = import.redo(after)) {
            reportError(coreText(*failed));
            return false;
        }
        mediaId = import.mediaId();
    }
    const MediaItem* media = after.findMedia(mediaId);
    if (!media->hasVideo) {
        reportError(tr("Choose a picture of the logo"));
        return false;
    }
    const Ticks at = ticks(m_playhead);
    const Ticks length = ticks(kLogoRevealLength);
    const Ticks delay = ticks(preset->taglineDelay);
    // Two free picture tracks above the main one (new ones on top when there are none).
    const bool hasTagline = !tagline.trimmed().isEmpty();
    std::vector<Id> free;
    const auto main = after.trackIndex(mainTrackId());
    for (std::size_t i = main ? *main + 1 : 0; i < after.tracks.size(); ++i) {
        const Track& t = after.tracks[i];
        if (t.kind != TrackKind::Audio && !t.locked && !t.hidden && !t.overlaps(at, length))
            free.push_back(t.id);
    }
    while (free.size() < (hasTagline ? 2u : 1u)) {
        InsertTrackCommand track(TrackKind::Video,
                                 "Video " + std::to_string(std::ranges::count(after.tracks, TrackKind::Video, &Track::kind) + 1),
                                 after.tracks.size());
        if (auto failed = track.redo(after)) {
            reportError(coreText(*failed));
            return false;
        }
        free.push_back(track.trackId());
    }
    ClipVisual visual;
    // At most kLogoRevealScale of the canvas's height and kLogoRevealWidth of its width,
    // whatever the picture's size.
    const double fit = std::min(double(after.settings.width) / std::max(1, media->width),
                                double(after.settings.height) / std::max(1, media->height));
    const double shownHigh = double(media->height) * fit / double(std::max(1, after.settings.height));
    const double shownWide = double(media->width) * fit / double(std::max(1, after.settings.width));
    visual.transform.scale = std::clamp(std::min(kLogoRevealScale / std::max(shownHigh, 0.01),
                                                 kLogoRevealWidth / std::max(shownWide, 0.01)), 0.05, 4.0);
    visual.transform.y = hasTagline ? kLogoRevealY : 0.0;
    AddClipCommand add(free[0], mediaId, at, length, 0, visual, TVE_TR("core", "Add Logo Reveal"));
    if (auto failed = add.redo(after)) {
        reportError(coreText(*failed));
        return false;
    }
    Clip* logoClip = after.locateClip(add.clipId())->clip;
    for (const auto& [id, seconds] : {std::pair{preset->logoIn, preset->inSeconds}, std::pair{preset->logoOut, preset->outSeconds}}) {
        if (auto failed = applyClipAnimation(*logoClip, id, ticks(seconds))) {
            reportError(coreText(*failed));
            return false;
        }
    }
    logoClip->motionBlur = preset->motionBlur;
    if (hasTagline) {
        ClipText text;
        text.text = tagline.trimmed().toStdString();
        text.size = kLogoTaglineSize;
        text.letterSpacing = 0.12;
        text.animationIn = {.preset = preset->taglineIn, .duration = preset->taglineSeconds, .unit = preset->taglineUnit,
                            .stagger = preset->taglineStagger};
        text.animationOut = {.preset = "fade", .duration = preset->outSeconds, .unit = TextUnit::Whole, .stagger = 0.0};
        ClipVisual under;
        under.transform.y = kLogoTaglineY;
        AddTitleCommand title(free[1], at + delay, length - delay, std::move(text), under);
        if (auto failed = title.redo(after)) {
            reportError(coreText(*failed));
            return false;
        }
        // Moved and deleted together.
        const Id group = after.allocateId();
        after.locateClip(add.clipId())->clip->groupId = group;
        after.locateClip(title.clipId())->clip->groupId = group;
    }
    m_project.setNextId(after.nextId());
    if (!apply(std::make_unique<ApplyEditsCommand>(m_project, std::move(after), TVE_TR("core", "Add Logo Reveal"))))
        return false;
    setSelectedClip(double(add.clipId()));
    return true;
}

// --- Transitions ---

std::optional<Id> ProjectController::cutNear(const Track& track, Ticks time, Ticks tolerance) const
{
    std::optional<Id> best;
    Ticks bestDistance = tolerance;
    for (std::size_t i = 1; i < track.clips.size(); ++i) {
        const Clip& clip = track.clips[i];
        if (track.clips[i - 1].end() != clip.start)
            continue;
        const Ticks distance = std::abs(clip.start - time);
        if (distance <= bestDistance) {
            bestDistance = distance;
            best = clip.id;
        }
    }
    return best;
}

bool ProjectController::addTransition(const QString& transitionId, double clipId)
{
    if (!EffectLibrary::builtIn().findTransition(transitionId.toStdString())) {
        reportError(tr("Unknown transition %1").arg(transitionId));
        return false;
    }
    Id target = toId(clipId);
    if (clipId < 0) {
        // The cut nearest the playhead, on the selected clip's track or else the main track.
        const Track* track = nullptr;
        if (auto location = m_project.locateClip(m_selectedClip))
            track = location->track;
        if (!track)
            track = m_project.findTrack(mainTrackId());
        const auto cut = track ? cutNear(*track, ticks(m_playhead), std::numeric_limits<Ticks>::max()) : std::nullopt;
        if (!cut) {
            reportError(tr("Put two clips side by side to add a transition between them"));
            return false;
        }
        target = *cut;
    }
    const Clip* clip = findClip(target);
    if (!clip) {
        reportError(tr("Drop the transition on a cut between two clips"));
        return false;
    }
    ClipTransition transition{transitionId.toStdString()};
    if (clip->transitionIn) {
        transition.duration = clip->transitionIn->duration;
        transition.alignment = clip->transitionIn->alignment;
    }
    if (!apply(std::make_unique<SetClipTransitionCommand>(target, std::move(transition))))
        return false;
    setSelectedTransition(double(target));
    return true;
}

bool ProjectController::addTransitionAt(const QString& transitionId, double trackId, double time, double tolerance)
{
    const Track* track = m_project.findTrack(toId(trackId));
    const auto cut = track ? cutNear(*track, ticks(time), ticks(tolerance)) : std::nullopt;
    if (!cut) {
        reportError(tr("Drop the transition on a cut between two clips"));
        return false;
    }
    return addTransition(transitionId, double(*cut));
}

bool ProjectController::addOverlayTransition(const QString& overlayId, double seconds)
{
    const overlays::Kind* kind = overlays::find(overlayId.toStdString());
    if (!kind) {
        reportError(tr("Unknown overlay %1").arg(overlayId));
        return false;
    }
    QString error;
    const QString path = overlays::movie(kind->id, &error);
    if (path.isEmpty()) {
        reportError(error);
        return false;
    }
    auto item = probeMedia(path.toStdString(), &error);
    if (!item) {
        reportError(error);
        return false;
    }
    item->fingerprint = fingerprintFile(path.toStdString()).value_or(std::string());
    // Centred on the cut (or the playhead).
    Ticks at = seconds < 0 ? ticks(m_playhead) : ticks(seconds);
    if (const Track* main = m_project.findTrack(mainTrackId()))
        if (const auto cut = cutNear(*main, at, std::numeric_limits<Ticks>::max()))
            at = findClip(*cut)->start;
    const Ticks length = item->duration;
    const Ticks start = std::max<Ticks>(0, at - length / 2);
    Id track = kNoId;
    const auto main = m_project.trackIndex(mainTrackId());
    for (std::size_t i = main ? *main + 1 : 0; i < m_project.tracks.size() && track == kNoId; ++i) {
        const Track& candidate = m_project.tracks[i];
        if (candidate.kind == TrackKind::Video && !candidate.locked && !candidate.hidden && !candidate.overlaps(start, length))
            track = candidate.id;
    }
    ClipVisual visual;
    visual.blend = kind->blend;
    auto add = std::make_unique<AddOverlayClipCommand>(std::move(*item), track, start, length, visual, "Overlays", TVE_TR("core", "Add Overlay"));
    auto* raw = add.get();
    if (!apply(std::move(add)))
        return false;
    setSelectedClip(double(raw->clipId()));
    return true;
}

bool ProjectController::setTransition(double clipId, const QVariantMap& changes)
{
    const Clip* clip = findClip(toId(clipId));
    if (!clip || !clip->transitionIn)
        return false;
    ClipTransition transition = *clip->transitionIn;
    if (changes.contains(QStringLiteral("duration")))
        transition.duration = ticks(changes.value(QStringLiteral("duration")).toDouble());
    if (changes.contains(QStringLiteral("alignment"))) {
        const QString alignment = changes.value(QStringLiteral("alignment")).toString();
        if (alignment == QLatin1String("center"))
            transition.alignment = TransitionAlignment::Center;
        else if (alignment == QLatin1String("endsAtCut"))
            transition.alignment = TransitionAlignment::EndsAtCut;
        else if (alignment == QLatin1String("startsAtCut"))
            transition.alignment = TransitionAlignment::StartsAtCut;
        else
            return false;
    }
    const QVariantMap values = changes.value(QStringLiteral("values")).toMap();
    for (auto it = values.begin(); it != values.end(); ++it)
        transition.values[it.key().toStdString()] = componentsOf(it.value());
    if (transition == *clip->transitionIn)
        return true;
    return apply(std::make_unique<SetClipTransitionCommand>(clip->id, std::move(transition)));
}

bool ProjectController::removeTransition(double clipId)
{
    const Clip* clip = findClip(toId(clipId));
    if (!clip || !clip->transitionIn)
        return false;
    if (!apply(std::make_unique<SetClipTransitionCommand>(clip->id, std::nullopt)))
        return false;
    if (m_selectedTransition == clip->id)
        setSelectedTransition(0);
    return true;
}

// --- Per-clip effects ---

bool ProjectController::addClipEffect(double clipId, const QString& effectId)
{
    const Clip* clip = findClip(toId(clipId));
    if (!clip) {
        reportError(tr("Select a video clip to add an effect to"));
        return false;
    }
    const bool plugin = isOfxEffect(effectId.toStdString());
    if (!plugin && !EffectLibrary::builtIn().find(effectId.toStdString())) {
        reportError(tr("Unknown effect %1").arg(effectId));
        return false;
    }
    auto effects = clip->effects;
    effects.push_back(ClipEffect{effectId.toStdString(), true, {}});
    return apply(std::make_unique<SetClipEffectsCommand>(clip->id, std::move(effects), plugin ? TVE_TR("core", "Add Plug-in") : TVE_TR("core", "Add Effect")));
}

bool ProjectController::removeClipEffect(double clipId, int index)
{
    const Clip* clip = findClip(toId(clipId));
    if (!clip || index < 0 || index >= int(clip->effects.size()))
        return false;
    auto effects = clip->effects;
    effects.erase(effects.begin() + index);
    return apply(std::make_unique<SetClipEffectsCommand>(clip->id, std::move(effects), TVE_TR("core", "Remove Effect")));
}

bool ProjectController::setClipEffectEnabled(double clipId, int index, bool enabled)
{
    const Clip* clip = findClip(toId(clipId));
    if (!clip || index < 0 || index >= int(clip->effects.size()))
        return false;
    auto effects = clip->effects;
    effects[std::size_t(index)].enabled = enabled;
    return apply(std::make_unique<SetClipEffectsCommand>(clip->id, std::move(effects),
                                                         enabled ? TVE_TR("core", "Enable Effect") : TVE_TR("core", "Disable Effect")));
}

std::optional<std::vector<ClipEffect>> ProjectController::effectsWithParam(Id clipId, int index,
                                                                           const QString& paramId,
                                                                           const QVariant& value) const
{
    const Clip* clip = findClip(clipId);
    if (!clip || index < 0 || index >= int(clip->effects.size()))
        return std::nullopt;
    auto effects = clip->effects;
    effects[std::size_t(index)].values[paramId.toStdString()] = componentsOf(value);
    return effects;
}

bool ProjectController::setClipEffectParam(double clipId, int index, const QString& paramId, const QVariant& value)
{
    // An animated parameter takes the value as a keyframe at the playhead.
    const Clip* clip = findClip(toId(clipId));
    const std::string channel = effectChannelId(std::size_t(std::max(index, 0)), paramId.toStdString());
    if (clip && index >= 0 && channelKeys(*clip, channel)) {
        const auto components = componentsOf(value);
        if (components.empty())
            return false;
        Clip after = *clip;
        setKey(after, channel, components.front());
        return commitAnimation(*clip, after, TVE_TR("core", "Change Effect"));
    }
    auto effects = effectsWithParam(toId(clipId), index, paramId, value);
    if (!effects)
        return false;
    return apply(std::make_unique<SetClipEffectsCommand>(toId(clipId), std::move(*effects), TVE_TR("core", "Change Effect")));
}

void ProjectController::previewClipEffectParam(double clipId, int index, const QString& paramId, const QVariant& value)
{
    // Frames carry the stack as it is at their time; preview it as it is at the playhead.
    const Clip* clip = findClip(toId(clipId));
    if (!clip || !m_view || index < 0 || index >= int(clip->effects.size()))
        return;
    auto effects = clip->effectsAt(ticks(m_playhead));
    effects[std::size_t(index)].values[paramId.toStdString()] = componentsOf(value);
    m_view->setPreviewEffects(clip->id, std::move(effects));
}

// --- Export ---

namespace {

struct ExportFormat {
    const char* id;
    const char* label;
    Container container;
    VideoCodec codec;
    ProResProfile proRes;
    AudioCodec audio;
};

constexpr ExportFormat kExportFormats[] = {
    {"mp4-h264", QT_TRANSLATE_NOOP("ProjectController", "MP4 · H.264 (plays everywhere)"), Container::Mp4, VideoCodec::H264, ProResProfile::Hq, AudioCodec::Aac},
    {"mp4-hevc", QT_TRANSLATE_NOOP("ProjectController", "MP4 · HEVC (smaller files)"), Container::Mp4, VideoCodec::Hevc, ProResProfile::Hq, AudioCodec::Aac},
    {"mov-h264", QT_TRANSLATE_NOOP("ProjectController", "MOV · H.264"), Container::Mov, VideoCodec::H264, ProResProfile::Hq, AudioCodec::Aac},
    {"mov-prores-proxy", QT_TRANSLATE_NOOP("ProjectController", "MOV · ProRes Proxy"), Container::Mov, VideoCodec::ProRes, ProResProfile::Proxy, AudioCodec::Pcm24},
    {"mov-prores-lt", QT_TRANSLATE_NOOP("ProjectController", "MOV · ProRes LT"), Container::Mov, VideoCodec::ProRes, ProResProfile::Lt, AudioCodec::Pcm24},
    {"mov-prores-422", QT_TRANSLATE_NOOP("ProjectController", "MOV · ProRes 422"), Container::Mov, VideoCodec::ProRes, ProResProfile::Standard, AudioCodec::Pcm24},
    {"mov-prores-hq", QT_TRANSLATE_NOOP("ProjectController", "MOV · ProRes 422 HQ"), Container::Mov, VideoCodec::ProRes, ProResProfile::Hq, AudioCodec::Pcm24},
    {"mov-prores-4444", QT_TRANSLATE_NOOP("ProjectController", "MOV · ProRes 4444"), Container::Mov, VideoCodec::ProRes, ProResProfile::P4444, AudioCodec::Pcm24},
    {"gif", QT_TRANSLATE_NOOP("ProjectController", "GIF · animated, no sound"), Container::Gif, VideoCodec::Gif, ProResProfile::Hq, AudioCodec::Aac},
    {"m4a", QT_TRANSLATE_NOOP("ProjectController", "M4A · AAC, sound only"), Container::M4a, VideoCodec::H264, ProResProfile::Hq, AudioCodec::Aac},
    {"wav-16", QT_TRANSLATE_NOOP("ProjectController", "WAV · 16-bit, sound only"), Container::Wav, VideoCodec::H264, ProResProfile::Hq, AudioCodec::Pcm16},
    {"wav-24", QT_TRANSLATE_NOOP("ProjectController", "WAV · 24-bit, sound only"), Container::Wav, VideoCodec::H264, ProResProfile::Hq, AudioCodec::Pcm24},
    {"wav-32f", QT_TRANSLATE_NOOP("ProjectController", "WAV · 32-bit float, sound only"), Container::Wav, VideoCodec::H264, ProResProfile::Hq, AudioCodec::Pcm32Float},
    {"flac", QT_TRANSLATE_NOOP("ProjectController", "FLAC · lossless, sound only"), Container::Flac, VideoCodec::H264, ProResProfile::Hq, AudioCodec::Flac},
    {"mp3", QT_TRANSLATE_NOOP("ProjectController", "MP3 · 320 kbit/s, sound only"), Container::Mp3, VideoCodec::H264, ProResProfile::Hq, AudioCodec::Mp3},
};

const ExportFormat* findExportFormat(const QString& id)
{
    for (const ExportFormat& format : kExportFormats)
        if (id == QLatin1String(format.id))
            return &format;
    return nullptr;
}

const QVariantList& presetList()
{
    static const QVariantList presets = [] {
        QFile file(QStringLiteral(":/export_presets.json"));
        if (!file.open(QIODevice::ReadOnly))
            return QVariantList();
        return QJsonDocument::fromJson(file.readAll()).object().value(QStringLiteral("presets")).toArray().toVariantList();
    }();
    return presets;
}

// Common rates as exact fractions (29.97 is 30000/1001).
FrameRate frameRateFrom(double fps)
{
    for (const int whole : {24, 30, 60}) {
        if (std::abs(fps - whole * 1000.0 / 1001.0) < 0.01)
            return {whole * 1000, 1001};
    }
    return {std::max(1, int(std::lround(fps))), 1};
}

} // namespace

QVariantList ProjectController::exportPresets() const
{
    // Their title and description in the app's language (catalogue names).
    QVariantList list;
    for (const QVariant& preset : presetList()) {
        QVariantMap map = preset.toMap();
        map[QStringLiteral("title")] = catalogueText(map.value(QStringLiteral("title")).toString());
        map[QStringLiteral("description")] = catalogueText(map.value(QStringLiteral("description")).toString());
        list << map;
    }
    return list;
}

QVariantList ProjectController::exportFormats() const
{
    QVariantList list;
    for (const ExportFormat& format : kExportFormats) {
        MediaSettings settings;
        settings.container = format.container;
        list << QVariantMap{
            {QStringLiteral("id"), QString::fromLatin1(format.id)},
            {QStringLiteral("label"), tr(format.label)},
            {QStringLiteral("extension"), QString::fromLatin1(fileExtension(format.container))},
            {QStringLiteral("video"), settings.hasVideo()},
            {QStringLiteral("sound"), format.container != Container::Gif},
            // (A bitrate for pictures: sound-only files have none to choose.)
            {QStringLiteral("quality"), settings.hasVideo() && (format.codec == VideoCodec::H264 || format.codec == VideoCodec::Hevc)},
        };
    }
    // A key photo and a 3-second movie around the playhead (MOV · H.264), as Photos pairs them.
    list << QVariantMap{{QStringLiteral("id"), QStringLiteral("live-photo")},
                        {QStringLiteral("label"), tr("Live Photo · a key photo and 3 seconds around it")},
                        {QStringLiteral("extension"), QStringLiteral("mov")},
                        {QStringLiteral("video"), true},
                        {QStringLiteral("sound"), true},
                        {QStringLiteral("quality"), false},
                        {QStringLiteral("livePhoto"), true}};
    return list;
}

std::optional<Exporter::Options> ProjectController::exportOptions(const QVariantMap& given, QString* error) const
{
    const auto refuse = [&](const QString& why) -> std::optional<Exporter::Options> {
        if (error)
            *error = why;
        return std::nullopt;
    };
    // A preset's values, then what was asked for.
    QVariantMap options;
    if (const QString preset = given.value(QStringLiteral("preset")).toString(); !preset.isEmpty()) {
        const auto found = std::ranges::find_if(presetList(), [&](const QVariant& p) {
            return p.toMap().value(QStringLiteral("id")).toString() == preset;
        });
        if (found == presetList().end())
            return refuse(tr("Unknown export preset: %1").arg(preset));
        options = found->toMap();
    }
    for (auto it = given.begin(); it != given.end(); ++it)
        options.insert(it.key(), it.value());

    QString formatId = options.value(QStringLiteral("format")).toString();
    // A Live Photo's movie is a MOV · H.264, at most 1080 on its short side.
    const bool livePhoto = formatId == QLatin1String("live-photo");
    if (livePhoto) {
        formatId = QStringLiteral("mov-h264");
        if (options.value(QStringLiteral("width")).toInt() <= 0 && options.value(QStringLiteral("shortSide")).toInt() <= 0
            && std::min(m_project.settings.width, m_project.settings.height) > 1080)
            options.insert(QStringLiteral("shortSide"), 1080);
    }
    if (formatId.isEmpty())
        formatId = options.value(QStringLiteral("codec")).toString() == QLatin1String("hevc") ? QStringLiteral("mp4-hevc")
                                                                                              : QStringLiteral("mp4-h264");
    const ExportFormat* format = findExportFormat(formatId);
    if (!format)
        return refuse(tr("Unknown export format: %1").arg(formatId));

    Exporter::Options result;
    MediaSettings& settings = result.settings;
    settings.container = format->container;
    settings.codec = format->codec;
    settings.proRes = format->proRes;
    settings.audioCodec = format->audio;
    settings.audioBitrate = format->container == Container::M4a ? 256'000 : format->container == Container::Mp3 ? 320'000 : 192'000;
    if (options.contains(QStringLiteral("audio")) && settings.hasVideo())
        settings.audio = options.value(QStringLiteral("audio")).toBool();
    // An HDR project's HEVC stays HDR (unless asked for SDR); every other format is SDR.
    if (isHdr(m_project.settings.colour) && settings.codec == VideoCodec::Hevc
        && options.value(QStringLiteral("hdr"), true).toBool())
        settings.colour = m_project.settings.colour;
    // A 360° project's MP4 says so; other files hold the equirectangular picture unmarked.
    settings.spherical = m_project.settings.spherical && settings.container == Container::Mp4;
    const bool gif = format->codec == VideoCodec::Gif && settings.hasVideo();

    // Size: exact, or a short side in the canvas's shape, or the canvas.
    const int canvasW = m_project.settings.width;
    const int canvasH = m_project.settings.height;
    const auto even = [&](int v) { return gif ? std::max(1, v) : std::max(2, v - v % 2); };
    const int width = options.value(QStringLiteral("width")).toInt();
    const int height = options.value(QStringLiteral("height")).toInt();
    int shortSide = options.value(QStringLiteral("shortSide")).toInt();
    if (gif && shortSide <= 0 && (width <= 0 || height <= 0))
        shortSide = 360;
    if (width > 0 && height > 0) {
        settings.width = even(width);
        settings.height = even(height);
    } else if (shortSide > 0) {
        const double scale = double(shortSide) / std::min(canvasW, canvasH);
        settings.width = even(int(std::lround(canvasW * scale)));
        settings.height = even(int(std::lround(canvasH * scale)));
    } else {
        settings.width = even(canvasW);
        settings.height = even(canvasH);
    }
    result.fill = options.value(QStringLiteral("fill")).toBool();

    const double fps = options.value(QStringLiteral("frameRate")).toDouble();
    settings.frameRate = fps > 0 ? frameRateFrom(fps) : gif ? FrameRate{15, 1} : m_project.settings.frameRate;
    const QString quality = options.value(QStringLiteral("quality")).toString();
    const double factor = quality == QLatin1String("high") ? 1.5 : quality == QLatin1String("small") ? 0.5 : 1.0;
    const double bitrate = options.value(QStringLiteral("videoBitrate")).toDouble();
    // H.264 and HEVC take a bitrate; ProRes and GIF set their own.
    if (settings.codec == VideoCodec::H264 || settings.codec == VideoCodec::Hevc)
        settings.videoBitrate = bitrate > 0 ? std::int64_t(bitrate) : std::int64_t(double(defaultVideoBitrate(settings)) * factor);
    if (auto invalid = validateSettings(settings))
        return refuse(QString::fromStdString(*invalid));

    if (options.value(QStringLiteral("normalizeLoudness")).toBool())
        result.loudnessTarget = options.value(QStringLiteral("loudnessTarget"), -14.0).toDouble();

    // Range
    const Ticks end = m_project.duration();
    const QString range = options.value(QStringLiteral("range"), QStringLiteral("all")).toString();
    if (range == QLatin1String("marks") && m_markIn >= 0 && m_markOut > m_markIn) {
        result.from = ticks(m_markIn);
        result.to = ticks(m_markOut);
    } else if (range == QLatin1String("custom")) {
        result.from = std::max<Ticks>(0, ticks(options.value(QStringLiteral("from")).toDouble()));
        result.to = std::min(end, ticks(options.value(QStringLiteral("to")).toDouble()));
    } else if (range == QLatin1String("selection")) {
        bool any = false;
        for (const QVariant& id : selectedClips()) {
            const Clip* clip = findClip(toId(id.toDouble()));
            result.from = any ? std::min(result.from, clip->start) : clip->start;
            result.to = any ? std::max(result.to, clip->end()) : clip->end();
            any = true;
        }
        if (!any)
            return refuse(tr("Select the clips to export"));
    } else if (range == QLatin1String("marker")) {
        const auto marker = std::ranges::find(m_project.markers, toId(options.value(QStringLiteral("markerId")).toDouble()),
                                              &tve::Marker::id);
        if (marker == m_project.markers.end() || marker->duration <= 0)
            return refuse(tr("Pick a marker with a length (a range) to export"));
        result.from = marker->time;
        result.to = std::min(end, marker->time + marker->duration);
    } else if (range == QLatin1String("all")) {
        result.from = 0;
        result.to = end;
    } else {
        return refuse(tr("Unknown export range: %1").arg(range));
    }
    if (end == 0)
        return refuse(tr("Add something to the timeline to export"));
    // A Live Photo: 3 seconds with the key photo (keyTime, in seconds; the playhead when not
    // given) in the middle, moved inside the timeline at its ends.
    if (livePhoto) {
        const Ticks frame = frameDuration(settings.frameRate);
        const Ticks key = std::clamp<Ticks>(options.contains(QStringLiteral("keyTime"))
                                                ? ticks(options.value(QStringLiteral("keyTime")).toDouble())
                                                : ticks(m_playhead),
                                            0, std::max<Ticks>(0, end - frame));
        const Ticks length = std::min<Ticks>(3 * kTicksPerSecond, end);
        result.from = std::clamp<Ticks>(key - length / 2, 0, end - length);
        result.to = result.from + length;
        result.livePhotoKey = key;
        result.addToPhotos = options.value(QStringLiteral("addToPhotos")).toBool();
    }
    if (result.to <= result.from)
        return refuse(tr("There is nothing to export in that range"));

    // A file size (megabytes, as Finder counts them): the pictures get what is left of it over
    // the range's length once the sound and the file's own overhead (3 %) are taken out, as
    // their average and as a ceiling for every second, so the file stays under it.
    const double targetMegabytes = options.value(QStringLiteral("targetSize")).toDouble();
    if (targetMegabytes > 0 && settings.hasVideo() && (settings.codec == VideoCodec::H264 || settings.codec == VideoCodec::Hevc)) {
        const double length = toSeconds(result.to - result.from);
        const double budget = targetMegabytes * 8e6 * 0.97 / length; // bits a second
        // A small file: lighter sound leaves more for the pictures.
        if (settings.hasAudio() && settings.audioCodec == AudioCodec::Aac && budget < 2'000'000)
            settings.audioBitrate = 96'000;
        const double video = budget - double(audioBitrate(settings));
        constexpr double kLeast = 150'000; // below this the pictures fall apart
        if (video < kLeast) {
            const double needed = (kLeast + double(audioBitrate(settings))) * length / 8e6 / 0.97;
            return refuse(tr("%1 MB is too small for %2 seconds of video: make it at least %3 MB")
                              .arg(QLocale().toString(targetMegabytes, 'g', 4))
                              .arg(QLocale().toString(length, 'f', 1))
                              .arg(QLocale().toString(std::ceil(needed * 10) / 10, 'f', 1)));
        }
        settings.videoBitrate = settings.maxVideoBitrate = std::int64_t(video);
        result.targetBytes = std::int64_t(targetMegabytes * 1e6);
    }
    return result;
}

double ProjectController::exportBitrate(const QVariantMap& options) const
{
    const auto planned = exportOptions(options);
    if (!planned)
        return 0;
    const MediaSettings& settings = planned->settings;
    const std::int64_t video = !settings.hasVideo() ? 0
                             : settings.videoBitrate > 0 ? settings.videoBitrate
                                                         : defaultVideoBitrate(settings);
    return double(video + audioBitrate(settings));
}

QVariantMap ProjectController::exportRange(const QVariantMap& options) const
{
    const auto planned = exportOptions(options);
    if (!planned)
        return {};
    return {{QStringLiteral("from"), seconds(planned->from)},
            {QStringLiteral("to"), seconds(planned->to)},
            {QStringLiteral("duration"), seconds(planned->to - planned->from)}};
}

bool ProjectController::exportVideo(const QUrl& file, const QVariantMap& options)
{
    if (!m_openLevels.empty())
        closeCompoundClip(0); // (the whole project: back to the main timeline first)
    const QString path = file.isLocalFile() ? file.toLocalFile() : file.toString();
    if (path.isEmpty()) {
        reportError(tr("Choose where to save the video"));
        return false;
    }
    const bool queued = std::ranges::any_of(m_exportJobs, [&](const ExportJob& job) {
        return job.path == path && (job.state == QLatin1String("waiting") || job.state == QLatin1String("running"));
    });
    if (queued) {
        reportError(tr("That file is already being exported"));
        return false;
    }
    QString error;
    auto planned = exportOptions(options, &error);
    if (!planned) {
        reportError(error);
        return false;
    }
    pause();
    ExportJob job;
    job.id = m_nextExportJob++;
    job.path = path;
    for (const ExportFormat& format : kExportFormats) {
        if (format.container == planned->settings.container && format.codec == planned->settings.codec
            && format.audio == planned->settings.audioCodec
            && (format.codec != VideoCodec::ProRes || format.proRes == planned->settings.proRes))
            job.format = tr(format.label).section(QStringLiteral(" ("), 0, 0);
    }
    if (planned->livePhotoKey)
        job.format = tr("Live Photo");
    job.options = std::move(*planned);
    job.snapshot = std::make_shared<const Project>(m_project);
    m_exportJobs.push_back(std::move(job));
    emit exportChanged();
    startNextExport();
    return true;
}

int ProjectController::exportAlso(const QUrl& file, const QVariantMap& options, const QStringList& formats)
{
    const QFileInfo main(file.isLocalFile() ? file.toLocalFile() : file.toString());
    const QString base = main.dir().filePath(main.completeBaseName());
    // Names taken: the main file's, then each one given out here.
    std::set<QString> used{main.absoluteFilePath()};
    for (const ExportJob& job : m_exportJobs)
        if (job.state == QLatin1String("waiting") || job.state == QLatin1String("running"))
            used.insert(QFileInfo(job.path).absoluteFilePath());
    int count = 0;
    for (const QString& id : formats) {
        const ExportFormat* format = findExportFormat(id);
        if (!format || id == options.value(QStringLiteral("format")).toString())
            continue;
        const QString extension = QString::fromLatin1(fileExtension(format->container));
        // "Holiday.mp4", or (that taken) "Holiday - HEVC.mp4", by the format's name.
        QString path = base + QLatin1Char('.') + extension;
        if (used.contains(QFileInfo(path).absoluteFilePath())) {
            QString tag = QString::fromUtf8(format->label).section(QStringLiteral(" · "), 1);
            tag = tag.section(QStringLiteral(" ("), 0, 0).section(QLatin1Char(','), 0, 0).trimmed();
            path = base + QStringLiteral(" - ") + tag + QLatin1Char('.') + extension;
        }
        for (int n = 2; used.contains(QFileInfo(path).absoluteFilePath()); ++n)
            path = base + QStringLiteral(" %1.").arg(n) + extension;
        used.insert(QFileInfo(path).absoluteFilePath());
        QVariantMap each = options;
        each.insert(QStringLiteral("format"), id);
        each.remove(QStringLiteral("preset"));
        // A GIF keeps its own size and rate (small, 15 fps) unless it was the one chosen.
        if (format->codec == VideoCodec::Gif) {
            each.remove(QStringLiteral("width"));
            each.remove(QStringLiteral("height"));
            each.remove(QStringLiteral("shortSide"));
            each.remove(QStringLiteral("frameRate"));
        }
        if (exportVideo(QUrl::fromLocalFile(path), each))
            ++count;
    }
    return count;
}

int ProjectController::exportMarkerRanges(const QUrl& folder, const QVariantMap& options)
{
    if (!m_openLevels.empty())
        closeCompoundClip(0); // (the whole project: back to the main timeline first)
    const QString directory = folder.isLocalFile() ? folder.toLocalFile() : folder.toString();
    QString error;
    const auto planned = exportOptions(options, &error);
    if (!planned) {
        reportError(error);
        return 0;
    }
    const QString extension = QString::fromLatin1(fileExtension(planned->settings.container));
    const QString base = name().isEmpty() ? QStringLiteral("Untitled") : name();
    int count = 0;
    std::set<QString> used;
    for (const tve::Marker& marker : m_project.markers) {
        if (marker.duration <= 0 || marker.time >= m_project.duration())
            continue;
        QString title = QString::fromStdString(marker.note).trimmed();
        title.replace(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|\\n]")), QStringLiteral("-"));
        if (title.isEmpty())
            title = QStringLiteral("Range %1").arg(count + 1);
        QString fileName = base + QStringLiteral(" - ") + title;
        for (int n = 2; used.contains(fileName); ++n)
            fileName = base + QStringLiteral(" - ") + title + QStringLiteral(" %1").arg(n);
        used.insert(fileName);
        QVariantMap each = options;
        each.insert(QStringLiteral("range"), QStringLiteral("marker"));
        each.insert(QStringLiteral("markerId"), double(marker.id));
        if (exportVideo(QUrl::fromLocalFile(QDir(directory).filePath(fileName + QLatin1Char('.') + extension)), each))
            ++count;
    }
    if (count == 0 && used.empty())
        reportError(tr("Give markers a length (a range) to export them one by one"));
    return count;
}

ProjectController::ExportJob* ProjectController::findExportJob(Id id)
{
    const auto found = std::ranges::find(m_exportJobs, id, &ExportJob::id);
    return found == m_exportJobs.end() ? nullptr : &*found;
}

void ProjectController::startNextExport()
{
    if (m_exportThread.joinable())
        return;
    const auto next = std::ranges::find(m_exportJobs, QStringLiteral("waiting"), &ExportJob::state);
    if (next == m_exportJobs.end())
        return;
    next->state = QStringLiteral("running");
    m_runningExport = next->id;
    m_cancelExport = false;
    m_exportProgress = 0;
    m_exportThread = std::thread([this, id = next->id, snapshot = next->snapshot, path = next->path,
                                  options = next->options] {
        // Progress goes to the UI thread at most every 1 %. While paused, the export waits
        // here between frames.
        double reported = 0;
        const Exporter::Result result = Exporter::run(snapshot, path, options, [&](double progress) {
            if (progress - reported >= 0.01 || progress >= 1.0) {
                reported = progress;
                QMetaObject::invokeMethod(this, [this, id, progress] {
                    m_exportProgress = progress;
                    if (ExportJob* job = findExportJob(id))
                        job->progress = progress;
                    emit exportChanged();
                }, Qt::QueuedConnection);
            }
            std::unique_lock lock(m_pauseMutex);
            m_pauseChanged.wait(lock, [&] { return !m_exportPausedFlag || m_cancelExport.load(); });
            return !m_cancelExport.load();
        });
        QMetaObject::invokeMethod(this, [this, id, result, path] {
            if (m_exportThread.joinable())
                m_exportThread.join();
            m_runningExport = kNoId;
            {
                std::lock_guard lock(m_pauseMutex); // a pause ends with its job
                m_exportPausedFlag = false;
            }
            if (ExportJob* job = findExportJob(id)) {
                job->state = result.ok ? QStringLiteral("done") : result.error.isEmpty() ? QStringLiteral("cancelled")
                                                                                         : QStringLiteral("failed");
                job->error = result.error;
                job->snapshot.reset();
                if (result.ok)
                    job->progress = 1.0;
            }
            emit exportChanged();
            emit exportFinished(result.ok, result.ok ? path : result.error, double(id));
            startNextExport();
        }, Qt::QueuedConnection);
    });
    emit exportChanged();
}

bool ProjectController::exportPaused() const
{
    std::lock_guard lock(m_pauseMutex);
    return m_exportPausedFlag;
}

QVariantList ProjectController::exportJobs() const
{
    QVariantList list;
    for (const ExportJob& job : m_exportJobs) {
        list << QVariantMap{
            {QStringLiteral("id"), double(job.id)},
            {QStringLiteral("name"), QFileInfo(job.path).fileName()},
            {QStringLiteral("path"), job.path},
            {QStringLiteral("format"), job.format},
            {QStringLiteral("state"), job.state},
            {QStringLiteral("progress"), job.progress},
            {QStringLiteral("error"), job.error},
            {QStringLiteral("duration"), seconds(job.options.to - job.options.from)},
        };
    }
    return list;
}

void ProjectController::cancelExport(double jobId)
{
    const Id id = jobId > 0 ? toId(jobId) : m_runningExport;
    ExportJob* job = findExportJob(id);
    if (!job)
        return;
    if (job->state == QLatin1String("waiting")) {
        job->state = QStringLiteral("cancelled");
        job->snapshot.reset();
        emit exportChanged();
        return;
    }
    if (id != m_runningExport)
        return;
    {
        std::lock_guard lock(m_pauseMutex);
        m_cancelExport = true;
    }
    m_pauseChanged.notify_all();
}

void ProjectController::pauseExport()
{
    if (!exporting())
        return;
    {
        std::lock_guard lock(m_pauseMutex);
        m_exportPausedFlag = true;
    }
    emit exportChanged();
}

void ProjectController::resumeExport()
{
    {
        std::lock_guard lock(m_pauseMutex);
        if (!m_exportPausedFlag)
            return;
        m_exportPausedFlag = false;
    }
    m_pauseChanged.notify_all();
    emit exportChanged();
}

bool ProjectController::removeExportJob(double jobId)
{
    const Id id = toId(jobId);
    if (id == m_runningExport)
        return false;
    const auto removed = std::erase_if(m_exportJobs, [&](const ExportJob& job) { return job.id == id; });
    if (removed)
        emit exportChanged();
    return removed > 0;
}

void ProjectController::clearFinishedExports()
{
    const auto removed = std::erase_if(m_exportJobs, [](const ExportJob& job) {
        return job.state == QLatin1String("done") || job.state == QLatin1String("failed")
            || job.state == QLatin1String("cancelled");
    });
    if (removed)
        emit exportChanged();
}

// --- Transport ---

void ProjectController::play()
{
    if (m_shuttleRate != 0) {
        m_shuttleRate = 0;
        m_shuttleTimer.stop();
    }
    m_playUntil = -1;
    if (m_project.duration() == 0)
        return;
    if (m_playback->clock().now() >= m_project.duration())
        m_playback->seek(0); // replay from the start
    m_playback->play();
    updatePlayhead();
}

void ProjectController::pause()
{
    m_playUntil = -1;
    if (m_shuttleRate != 0) {
        m_shuttleRate = 0;
        m_shuttleTimer.stop();
        emit playingChanged();
    }
    if (recording()) {
        stopRecording();
        return;
    }
    m_playback->pause();
    updatePlayhead();
}

void ProjectController::togglePlay()
{
    playing() ? pause() : play();
}

void ProjectController::seek(double seconds)
{
    if (recording())
        return; // the take is tied to the time it started at
    const Ticks clamped = std::clamp<Ticks>(ticks(seconds), 0, m_project.duration());
    m_playback->seek(clamped);
    updatePlayhead();
}

void ProjectController::stepFrames(int frames)
{
    pause();
    // Snap to the frame grid, then step.
    const Ticks frame = frameTicks();
    const Ticks current = (m_playback->clock().now() / frame) * frame;
    seek(::seconds(current + frames * frame));
}

void ProjectController::updatePlayhead()
{
    Ticks now = m_playback->clock().now();
    const Ticks end = m_project.duration();
    if (m_playback->clock().playing() && now >= end && !recording()) { // takes run past the end
        m_playback->pause();
        m_playback->seek(end);
        now = end;
    }
    // Playing the marked range: it stops at Out.
    if (m_playUntil >= 0 && m_playback->clock().playing() && now >= ticks(m_playUntil)) {
        m_playback->pause();
        m_playback->seek(ticks(m_playUntil));
        now = ticks(m_playUntil);
        m_playUntil = -1;
    }
    const double value = seconds(now);
    if (value != m_playhead) {
        m_playhead = value;
        emit playheadChanged();
        // The inspector shows animated values as they are at the playhead.
        if (const Clip* clip = findClip(m_selectedClip); clip && isAnimated(*clip))
            emit selectedClipInfoChanged();
    }
    updateMixLevels(now);
    const double left = m_audioOutput->peakLeft();
    const double right = m_audioOutput->peakRight();
    if (left != m_levelLeft || right != m_levelRight) {
        m_levelLeft = left;
        m_levelRight = right;
        emit levelsChanged();
    }
    updateInput();
    const bool isPlaying = m_playback->clock().playing();
    if (isPlaying != m_wasPlaying) {
        m_wasPlaying = isPlaying;
        emit playingChanged();
    }
}

// --- Voice-over ---

bool ProjectController::recording() const
{
    return m_recorder && m_recorder->recording();
}

double ProjectController::recordingStart() const
{
    return m_recordingStart ? seconds(*m_recordingStart) : 0.0;
}

QString ProjectController::inputDevice() const
{
    return m_recorder ? QString::fromStdString(m_recorder->deviceName()) : m_inputDevice;
}

void ProjectController::setInputDevice(const QString& name)
{
    if (recording() || name == inputDevice())
        return;
    m_inputDevice = name;
    if (m_recorder) {
        m_recorder.reset();
        openVoiceOver();
    }
    emit voiceOverChanged();
}

QString ProjectController::microphoneAccessName() const
{
    if (inputBackend() != AudioRecorder::Backend::System)
        return QStringLiteral("granted");
    switch (microphoneAccess()) {
    case MicrophoneAccess::Granted:
        return QStringLiteral("granted");
    case MicrophoneAccess::Undetermined:
        return QStringLiteral("undetermined");
    case MicrophoneAccess::Denied:
        break;
    }
    return QStringLiteral("denied");
}

void ProjectController::setMuteWhileRecording(bool mute)
{
    if (mute == m_muteWhileRecording)
        return;
    m_muteWhileRecording = mute;
    if (recording())
        m_audioOutput->setMuted(mute);
    emit voiceOverChanged();
}

QString ProjectController::recordingsDirectory() const
{
    const QByteArray override = qgetenv("TVE_RECORDING_DIR");
    if (!override.isEmpty())
        return QString::fromLocal8Bit(override);
    if (!m_filePath.isEmpty()) {
        const QFileInfo project(m_filePath);
        return project.absolutePath() + QLatin1Char('/') + project.completeBaseName() + QStringLiteral(" Voice-overs");
    }
    return QStandardPaths::writableLocation(QStandardPaths::MoviesLocation) + QStringLiteral("/TechieVideoEditor Voice-overs");
}

QString ProjectController::takePath() const
{
    const QDir folder(recordingsDirectory());
    if (!folder.mkpath(QStringLiteral(".")))
        return {};
    for (int n = 1;; ++n) {
        const QString path = folder.filePath(QStringLiteral("Voice-over %1.wav").arg(n));
        if (!QFileInfo::exists(path))
            return path;
    }
}

bool ProjectController::openVoiceOver()
{
    if (m_recorder)
        return true;
    const AudioRecorder::Backend backend = inputBackend();
    if (backend == AudioRecorder::Backend::System) {
        const MicrophoneAccess access = microphoneAccess();
        if (access == MicrophoneAccess::Denied) {
            emit voiceOverChanged();
            return false;
        }
        if (access == MicrophoneAccess::Undetermined) {
            // macOS asks the person; carry on when they answer, unless closed meanwhile.
            if (!m_requestingAccess) {
                m_requestingAccess = true;
                requestMicrophoneAccess(this, [this](bool granted) {
                    const bool wanted = m_requestingAccess;
                    m_requestingAccess = false;
                    if (granted && wanted)
                        openVoiceOver();
                    emit voiceOverChanged();
                });
            }
            return false;
        }
    }
    auto recorder = std::make_unique<AudioRecorder>(backend, m_inputDevice.toStdString());
    if (!recorder->isOpen()) {
        reportError(tr("Could not open the microphone: %1").arg(coreText(recorder->errorString())));
        return false;
    }
    m_inputDevices.clear();
    for (const std::string& name : AudioRecorder::inputDevices(backend))
        m_inputDevices << QString::fromStdString(name);
    m_recorder = std::move(recorder);
    emit voiceOverChanged();
    return true;
}

void ProjectController::closeVoiceOver()
{
    m_requestingAccess = false;
    if (!m_recorder)
        return;
    if (recording())
        stopRecording();
    m_recorder.reset();
    m_inputLevel = 0;
    m_recordingTime = 0;
    emit inputLevelChanged();
    emit voiceOverChanged();
}

bool ProjectController::startRecording()
{
    if (!openVoiceOver() || recording())
        return false;
    const QString path = takePath();
    if (path.isEmpty()) {
        reportError(tr("Could not create the folder %1").arg(recordingsDirectory()));
        return false;
    }
    std::string error;
    if (!m_recorder->start(path.toStdString(), &error)) {
        reportError(tr("Could not start recording: %1").arg(coreText(error)));
        return false;
    }
    // The take begins at the playhead and the timeline plays along (past its end if need be).
    m_recordingStart = m_playback->clock().now();
    m_recordingTime = 0;
    m_audioOutput->setMuted(m_muteWhileRecording);
    m_playback->play();
    updatePlayhead();
    emit voiceOverChanged();
    return true;
}

bool ProjectController::stopRecording()
{
    if (!recording())
        return false;
    std::string error;
    const auto take = m_recorder->stop(&error);
    m_playback->pause();
    m_audioOutput->setMuted(false);
    const Ticks start = *m_recordingStart;
    m_recordingStart.reset();
    updatePlayhead();
    emit voiceOverChanged();
    if (!take) {
        reportError(tr("Recording failed: %1").arg(coreText(error)));
        return false;
    }
    QString probeError;
    auto item = probeMedia(take->path, &probeError);
    if (item)
        item->fingerprint = fingerprintFile(take->path).value_or(std::string());
    if (!item) {
        reportError(probeError);
        return false;
    }
    // The topmost audio track with room for the whole take, else a new one.
    Id target = kNoId;
    for (auto track = m_project.tracks.rbegin(); track != m_project.tracks.rend(); ++track) {
        if (track->kind == TrackKind::Audio && !track->locked && !track->muted
            && !track->overlaps(start, item->duration)) {
            target = track->id;
            break;
        }
    }
    auto command = std::make_unique<AddVoiceOverCommand>(std::move(*item), target, start);
    auto* raw = command.get();
    if (!apply(std::move(command)))
        return false;
    setSelectedClip(double(raw->clipId()));
    return true;
}

// --- Sound library ---

bool ProjectController::addSound(const QString& soundId)
{
    QString error;
    const auto file = m_soundLibrary->fileFor(soundId, &error);
    if (!file) {
        reportError(error);
        return false;
    }
    const std::string path = file->path.toStdString();
    std::optional<MediaItem> item;
    if (auto known = std::ranges::find(m_project.media, path, &MediaItem::path); known != m_project.media.end())
        item = *known;
    else if ((item = probeMedia(path, &error)))
        item->fingerprint = fingerprintFile(path).value_or(std::string());
    if (!item) {
        reportError(error);
        return false;
    }
    const bool music = file->kind == QLatin1String("music");
    const Ticks start = ticks(m_playhead);
    Id target = kNoId;
    for (auto track = m_project.tracks.rbegin(); track != m_project.tracks.rend(); ++track) {
        if (track->kind == TrackKind::Audio && !track->locked && !track->muted
            && !track->overlaps(start, item->duration)) {
            target = track->id;
            break;
        }
    }
    auto command = std::make_unique<AddVoiceOverCommand>(std::move(*item), target, start,
                                                         music ? TVE_TR("core", "Add Music") : TVE_TR("core", "Add Sound Effect"),
                                                         music ? "Music" : "Sound effects");
    auto* raw = command.get();
    if (!apply(std::move(command)))
        return false;
    setSelectedClip(double(raw->clipId()));
    return true;
}

// --- Imports from Photos, Music and devices ---

QString ProjectController::importsDirectory() const
{
    const QByteArray override = qgetenv("TVE_IMPORT_DIR");
    if (!override.isEmpty())
        return QString::fromLocal8Bit(override);
    if (!m_filePath.isEmpty()) {
        const QFileInfo project(m_filePath);
        return project.absolutePath() + QLatin1Char('/') + project.completeBaseName() + QStringLiteral(" Imports");
    }
    return QStandardPaths::writableLocation(QStandardPaths::MoviesLocation) + QStringLiteral("/TechieVideoEditor Imports");
}

bool ProjectController::importFrom(const QString& source, const QStringList& itemIds)
{
    if (!m_imports->fetch(source, itemIds, importsDirectory())) {
        reportError(m_imports->fetching() ? tr("An import is already under way")
                                          : tr("Nothing to import"));
        return false;
    }
    return true;
}

// --- AI: words and voices ---

QStringList ProjectController::translationLanguages() const
{
    return cloudfeatures::languages();
}

QString ProjectController::voicesDirectory() const
{
    const QByteArray override = qgetenv("TVE_VOICE_DIR");
    if (!override.isEmpty())
        return QString::fromLocal8Bit(override);
    if (!m_filePath.isEmpty()) {
        const QFileInfo project(m_filePath);
        return project.absolutePath() + QLatin1Char('/') + project.completeBaseName() + QStringLiteral(" Voices");
    }
    return QStandardPaths::writableLocation(QStandardPaths::MoviesLocation) + QStringLiteral("/TechieVideoEditor Voices");
}

bool ProjectController::writeCopy(const QString& kind, const QString& tone, const QString& notes)
{
    // What is said: the transcript, else the captions.
    QStringList said;
    for (const PlacedWord& w : placedWords(m_project, m_transcriptVariant))
        said << QString::fromStdString(w.text);
    if (said.isEmpty())
        for (const CaptionBlock& caption : captionBlocks())
            said << QString::fromStdString(caption.text).replace(QLatin1Char('\n'), QLatin1Char(' '));
    if (said.isEmpty()) {
        reportError(tr("Transcribe the video first (Transcript tab): the writing works from what is said"));
        return false;
    }
    const TextRequest request = cloudfeatures::copyRequest(kind, said.join(QLatin1Char(' ')), tone, notes, m_cloud->claudeModel());
    QPointer<ProjectController> self(this);
    const bool started = m_cloud->generate(QStringLiteral("write"), request, [self](const TextResult& result) {
        if (!self)
            return;
        CloudServices* cloud = self->m_cloud;
        // The model's name, when it is one offered (a fallback may have answered).
        QString model = result.usage.model;
        for (const QVariant& offered : cloud->claudeModels())
            if (offered.toMap().value(QStringLiteral("id")).toString() == model)
                model = offered.toMap().value(QStringLiteral("label")).toString();
        cloud->endJob(result.ok ? tr("Written by %1").arg(model) : result.error);
        emit cloud->jobFinished(QStringLiteral("write"), result.ok, result.cancelled ? QString() : result.error);
        if (!result.ok && !result.cancelled)
            self->reportError(result.error);
    });
    if (!started)
        reportError(tr("Another AI job is running"));
    return started;
}

bool ProjectController::translateCaptions(const QString& language)
{
    const std::vector<CaptionBlock> captions = captionBlocks();
    if (captions.empty()) {
        reportError(tr("There are no captions to translate"));
        return false;
    }
    if (m_cloud->busy()) {
        reportError(tr("Another AI job is running"));
        return false;
    }
    // Batches, one after another; the captions change only when all are in (and only if the
    // caption track has not changed meanwhile).
    struct Translation {
        std::vector<CaptionBlock> captions;
        QMap<int, QString> texts;
        int next = 0;
        QString language;
    };
    auto state = std::make_shared<Translation>(Translation{captions, {}, 0, language});
    QPointer<ProjectController> self(this);
    auto step = std::make_shared<std::function<void()>>();
    *step = [self, state, step] {
        if (!self)
            return;
        CloudServices* cloud = self->m_cloud;
        const int total = int(state->captions.size());
        const int from = state->next;
        const int to = std::min(total, from + cloudfeatures::kCaptionsPerRequest);
        QList<QPair<int, QString>> batch;
        QList<int> indices;
        for (int i = from; i < to; ++i) {
            batch.push_back({i, QString::fromStdString(state->captions[std::size_t(i)].text)});
            indices << i;
        }
        cloud->setProgress(double(from) / total, tr("Translating captions %1–%2 of %3…").arg(from + 1).arg(to).arg(total));
        const auto done = [self, state, step, indices, to, total](const TextResult& result) {
            if (!self)
                return;
            CloudServices* cloud = self->m_cloud;
            const auto fail = [&](const QString& error) {
                cloud->endJob(error);
                emit cloud->jobFinished(QStringLiteral("translate"), false, error);
                if (!error.isEmpty())
                    self->reportError(error);
                *step = nullptr; // (breaks the cycle)
            };
            if (!result.ok) {
                fail(result.cancelled ? QString() : result.error);
                return;
            }
            const auto texts = cloudfeatures::readTranslation(result.text, indices);
            if (!texts) {
                fail(tr("The translation came back incomplete; try again"));
                return;
            }
            state->texts.insert(*texts);
            state->next = to;
            if (to < total) {
                (*step)();
                return;
            }
            // All in: the same captions at the same times, in the new language.
            std::vector<CaptionBlock> now = self->captionBlocks();
            const bool unchanged = now.size() == state->captions.size()
                                && std::ranges::equal(now, state->captions, [](const CaptionBlock& a, const CaptionBlock& b) {
                                       return a.start == b.start && a.end == b.end && a.text == b.text;
                                   });
            if (!unchanged) {
                fail(tr("The captions changed while they were being translated; try again"));
                return;
            }
            for (std::size_t i = 0; i < now.size(); ++i) {
                now[i].text = state->texts.value(int(i)).toStdString();
                now[i].words.clear(); // the words' times belong to the original language
            }
            auto [style, visual] = self->captionLook();
            const bool applied = self->apply(std::make_unique<SetCaptionsCommand>(
                std::move(now), "Translate Captions to " + state->language.toStdString(), style, visual));
            cloud->endJob(applied ? tr("Translated into %1").arg(state->language) : QString());
            emit cloud->jobFinished(QStringLiteral("translate"), applied, QString());
            *step = nullptr;
        };
        if (!cloud->generate(QStringLiteral("translate"),
                             cloudfeatures::translateRequest(batch, state->language, cloud->claudeModel()), done)) {
            cloud->endJob(QString());
            *step = nullptr;
        }
    };
    (*step)();
    return true;
}

bool ProjectController::speakText(const QString& text, const QString& voiceId, const QVariantMap& options)
{
    const QString words = text.trimmed();
    if (words.isEmpty()) {
        reportError(tr("Type something to say first"));
        return false;
    }
    if (m_cloud->busy()) {
        reportError(tr("Another AI job is running"));
        return false;
    }
    const double speed = std::clamp(options.value(QStringLiteral("speed"), 1.0).toDouble(), 0.5, 2.0);
    const bool preview = options.value(QStringLiteral("preview")).toBool();
    const bool captions = options.value(QStringLiteral("captions"), true).toBool();
    const QString key = voiceId + QLatin1Char('\n') + QString::number(speed, 'f', 2) + QLatin1Char('\n') + words;
    const auto use = [this, preview, captions](const SpeechResult& speech) {
        if (preview) {
            if (!m_soundLibrary->preview(QStringLiteral("file:") + speech.path))
                reportError(m_soundLibrary->lastError());
            return true;
        }
        return placeVoice(speech, captions);
    };
    if (m_lastVoice && m_lastVoice->key == key && QFileInfo::exists(m_lastVoice->result.path))
        return use(m_lastVoice->result);

    const QDir folder(voicesDirectory());
    QString base;
    for (int n = 1;; ++n) {
        base = folder.filePath(QStringLiteral("Voice %1").arg(n));
        if (!QFileInfo::exists(base + QStringLiteral(".wav")) && !QFileInfo::exists(base + QStringLiteral(".mp3")))
            break;
    }
    QPointer<ProjectController> self(this);
    const bool started = m_cloud->speak(words, voiceId, speed, base, [self, key, use](const SpeechResult& speech) {
        if (!self)
            return;
        CloudServices* cloud = self->m_cloud;
        cloud->endJob(speech.ok ? QString() : speech.error);
        if (speech.ok) {
            self->m_lastVoice = SpokenVoice{key, speech};
            use(speech);
        } else if (!speech.cancelled) {
            self->reportError(speech.error);
        }
        emit cloud->jobFinished(QStringLiteral("speak"), speech.ok, speech.cancelled ? QString() : speech.error);
    });
    return started;
}

bool ProjectController::placeVoice(const SpeechResult& speech, bool captions)
{
    QString probeError;
    auto item = probeMedia(speech.path.toStdString(), &probeError);
    if (!item) {
        reportError(probeError);
        return false;
    }
    item->fingerprint = fingerprintFile(speech.path.toStdString()).value_or(std::string());
    const Ticks start = ticks(m_playhead);
    Id target = kNoId;
    for (auto track = m_project.tracks.rbegin(); track != m_project.tracks.rend(); ++track) {
        if (track->kind == TrackKind::Audio && !track->locked && !track->muted && !track->overlaps(start, item->duration)) {
            target = track->id;
            break;
        }
    }
    const Ticks length = item->duration;
    auto add = std::make_unique<AddVoiceOverCommand>(std::move(*item), target, start, TVE_TR("core", "Add Voice"), "Voice");
    auto* raw = add.get();
    std::vector<std::unique_ptr<Command>> commands;
    commands.push_back(std::move(add));
    if (captions) {
        // The voice's captions replace any over the same time.
        std::vector<CaptionBlock> all;
        for (CaptionBlock& c : captionBlocks())
            if (c.end <= start || c.start >= start + length)
                all.push_back(std::move(c));
        for (CaptionBlock& c : cloudfeatures::speechCaptions(speech.words, start))
            all.push_back(std::move(c));
        auto [style, visual] = captionLook();
        commands.push_back(std::make_unique<SetCaptionsCommand>(std::move(all), TVE_TR("core", "Add Voice"), style, visual));
    }
    if (!apply(std::make_unique<CompositeCommand>(std::move(commands), TVE_TR("core", "Add Voice"))))
        return false;
    setSelectedClip(double(raw->clipId()));
    return true;
}

// --- Drawings ---

double ProjectController::addDrawing(const QVariantMap& path)
{
    const Ticks at = ticks(m_playhead);
    const Ticks length = 5 * kTicksPerSecond;
    // The lowest free picture track above the main one; else a new one on top.
    Id track = kNoId;
    const auto main = m_project.trackIndex(mainTrackId());
    for (std::size_t i = main ? *main + 1 : 0; i < m_project.tracks.size() && track == kNoId; ++i) {
        const Track& candidate = m_project.tracks[i];
        if (candidate.kind == TrackKind::Video && !candidate.locked && !candidate.hidden && !candidate.overlaps(at, length))
            track = candidate.id;
    }
    ClipDrawing drawing;
    drawing.paths.push_back(pathFrom(path));
    auto command = std::make_unique<AddDrawingCommand>(track, at, length, std::move(drawing));
    auto* raw = command.get();
    if (!apply(std::move(command)))
        return 0;
    setSelectedClip(double(raw->clipId()));
    return double(raw->clipId());
}

bool ProjectController::addDrawnPath(double clipId, const QVariantMap& path)
{
    const Clip* clip = findClip(toId(clipId));
    if (!clip || !clip->drawing) {
        reportError(tr("Select a drawing"));
        return false;
    }
    ClipDrawing drawing = *clip->drawing;
    drawing.paths.push_back(pathFrom(path));
    return apply(std::make_unique<SetClipDrawingCommand>(clip->id, std::move(drawing), TVE_TR("core", "Draw")));
}

bool ProjectController::setDrawnPath(double clipId, int index, const QVariantMap& changes)
{
    const Clip* clip = findClip(toId(clipId));
    if (!clip || !clip->drawing || index < 0 || std::size_t(index) >= clip->drawing->paths.size())
        return false;
    ClipDrawing drawing = *clip->drawing;
    drawing.paths[std::size_t(index)] = pathFrom(changes, drawing.paths[std::size_t(index)]);
    return apply(std::make_unique<SetClipDrawingCommand>(clip->id, std::move(drawing), TVE_TR("core", "Change Path")));
}

bool ProjectController::removeDrawnPath(double clipId, int index)
{
    const Clip* clip = findClip(toId(clipId));
    if (!clip || !clip->drawing || index < 0 || std::size_t(index) >= clip->drawing->paths.size())
        return false;
    ClipDrawing drawing = *clip->drawing;
    drawing.paths.erase(drawing.paths.begin() + index);
    return apply(std::make_unique<SetClipDrawingCommand>(clip->id, std::move(drawing), TVE_TR("core", "Remove Path")));
}

bool ProjectController::setDrawing(double clipId, const QVariantMap& options)
{
    const Clip* clip = findClip(toId(clipId));
    if (!clip || !clip->drawing)
        return false;
    ClipDrawing drawing = *clip->drawing;
    drawing.drawIn = options.value(QStringLiteral("drawIn"), drawing.drawIn).toDouble();
    drawing.drawOut = options.value(QStringLiteral("drawOut"), drawing.drawOut).toDouble();
    drawing.oneByOne = options.value(QStringLiteral("oneByOne"), drawing.oneByOne).toBool();
    // Never longer than the clip together.
    const double seconds = toSeconds(clip->duration);
    if (drawing.drawIn + drawing.drawOut > seconds) {
        reportError(tr("Drawing on and off take longer than the clip"));
        return false;
    }
    return apply(std::make_unique<SetClipDrawingCommand>(clip->id, std::move(drawing), TVE_TR("core", "Animate Drawing")));
}

QVariantList ProjectController::pathsToFollow(double clipId) const
{
    QVariantList list;
    const Clip* clip = findClip(toId(clipId));
    if (!clip)
        return list;
    for (const Track& track : m_project.tracks)
        for (const Clip& other : track.clips) {
            if (!other.drawing || other.id == clip->id || other.end() <= clip->start || other.start >= clip->end())
                continue;
            for (std::size_t i = 0; i < other.drawing->paths.size(); ++i) {
                QVariantList line;
                for (const auto& [x, y] : pathLine(other.drawing->paths[i]))
                    line << QVariant(QVariantList{x, y});
                list << QVariantMap{{QStringLiteral("clipId"), double(other.id)},
                                    {QStringLiteral("index"), int(i)},
                                    {QStringLiteral("label"), tr("Path %1 (%2)").arg(i + 1)
                                                                  .arg(QLatin1String(kPathKinds[int(other.drawing->paths[i].kind)]))},
                                    {QStringLiteral("line"), line}};
            }
        }
    return list;
}

bool ProjectController::followPath(double clipId, double drawingClipId, int pathIndex, const QVariantMap& options)
{
    const Clip* clip = findClip(toId(clipId));
    const Clip* drawing = findClip(toId(drawingClipId));
    if (!clip || !drawing || !drawing->drawing || pathIndex < 0 || std::size_t(pathIndex) >= drawing->drawing->paths.size()) {
        reportError(tr("Choose a drawn path to follow"));
        return false;
    }
    const bool turn = options.value(QStringLiteral("turn")).toBool();
    const double aspect = double(m_project.settings.width) / std::max(1, m_project.settings.height);
    const FollowKeyframes keys = followLine(pathLine(drawing->drawing->paths[std::size_t(pathIndex)]), aspect, clip->duration, turn);
    if (keys.x.empty()) {
        reportError(tr("That path is too short to follow"));
        return false;
    }
    std::map<std::string, Keyframes> changes{{std::string(tve::property::kX), keys.x}, {std::string(tve::property::kY), keys.y}};
    if (turn)
        changes.emplace(std::string(tve::property::kRotation), keys.rotation);
    return apply(std::make_unique<SetClipKeyframesCommand>(clip->id, std::move(changes), TVE_TR("core", "Follow Path")));
}

// --- Templates ---

QVariantMap ProjectController::templateInfo() const
{
    if (!m_project.templateInfo)
        return {};
    const TemplateInfo& info = *m_project.templateInfo;
    const auto clipMedia = [this](Id clipId) -> std::pair<const Clip*, const MediaItem*> {
        const Clip* clip = findClip(clipId);
        return {clip, clip ? m_project.findMedia(clip->mediaId) : nullptr};
    };
    QVariantList slotList;
    for (std::size_t i = 0; i < info.mediaSlots.size(); ++i) {
        const TemplateSlot& slot = info.mediaSlots[i];
        const auto [clip, media] = clipMedia(slot.clipId);
        if (!clip)
            continue;
        slotList << QVariantMap{{QStringLiteral("index"), int(i)},
                                {QStringLiteral("label"), QString::fromStdString(slot.label)},
                                {QStringLiteral("kind"), QString::fromStdString(slot.kind)},
                                {QStringLiteral("filled"), slot.filled},
                                {QStringLiteral("clipId"), double(slot.clipId)},
                                {QStringLiteral("start"), seconds(clip->start)},
                                {QStringLiteral("duration"), seconds(clip->duration)},
                                {QStringLiteral("path"), media ? QString::fromStdString(media->path) : QString()},
                                {QStringLiteral("name"), media ? mediaName(*media) : QString()},
                                {QStringLiteral("still"), media && media->still},
                                {QStringLiteral("sourceIn"), seconds(clip->sourceIn)},
                                {QStringLiteral("speed"), clip->speed},
                                {QStringLiteral("mediaDuration"), media ? seconds(media->duration) : 0.0}};
    }
    QVariantList textList;
    for (std::size_t i = 0; i < info.texts.size(); ++i) {
        const Clip* clip = findClip(info.texts[i].clipId);
        if (!clip || !clip->text)
            continue;
        textList << QVariantMap{{QStringLiteral("index"), int(i)},
                                {QStringLiteral("label"), QString::fromStdString(info.texts[i].label)},
                                {QStringLiteral("clipId"), double(info.texts[i].clipId)},
                                {QStringLiteral("text"), QString::fromStdString(clip->text->text)}};
    }
    QVariantMap music;
    if (const auto [clip, media] = clipMedia(info.musicClipId); clip && media)
        music = {{QStringLiteral("clipId"), double(clip->id)},
                 {QStringLiteral("path"), QString::fromStdString(media->path)},
                 {QStringLiteral("name"), mediaName(*media)},
                 {QStringLiteral("sourceIn"), seconds(clip->sourceIn)},
                 {QStringLiteral("duration"), seconds(clip->duration)},
                 {QStringLiteral("mediaDuration"), seconds(media->duration)}};
    return {{QStringLiteral("id"), QString::fromStdString(info.id)},
            {QStringLiteral("name"), QString::fromStdString(info.name)},
            {QStringLiteral("slots"), slotList},
            {QStringLiteral("texts"), textList},
            {QStringLiteral("music"), music}};
}

bool ProjectController::newFromTemplate(const QString& templateId, const QString& shape)
{
    QString error;
    auto project = TemplateLibrary::build(templateId, shape, &error);
    if (!project) {
        reportError(error);
        return false;
    }
    resetProject(std::move(*project), {});
    m_canvasChosen = true;
    return true;
}

bool ProjectController::fillTemplateSlot(int index, double mediaId, double fromSeconds)
{
    if (!m_project.templateInfo || index < 0 || std::size_t(index) >= m_project.templateInfo->mediaSlots.size()) {
        reportError(tr("No such slot"));
        return false;
    }
    const TemplateSlot& slot = m_project.templateInfo->mediaSlots[std::size_t(index)];
    const Clip* clip = findClip(slot.clipId);
    const MediaItem* media = m_project.findMedia(toId(mediaId));
    if (!clip || !media || !media->hasVideo) {
        reportError(tr("Choose a picture or a video for the slot"));
        return false;
    }
    if ((slot.kind == "video" && media->still) || (slot.kind == "photo" && !media->still)) {
        reportError(slot.kind == "video" ? tr("This slot takes a video") : tr("This slot takes a photo"));
        return false;
    }
    // From that far in (as far as leaves the slot full), slowed if it is still too short.
    Ticks from = 0;
    double speed = 1.0;
    if (!media->still) {
        from = std::clamp(ticks(fromSeconds), Ticks(0), std::max<Ticks>(0, media->duration - clip->duration));
        speed = fillSpeed(media->duration - from, clip->duration);
    }
    const ClipCrop crop = fillCrop(media->width, media->height, m_project.settings.width, m_project.settings.height);
    // A short follows its subject: the path again from the new start, where it is known.
    if (m_project.templateInfo->id == "auto.shorts" && !media->still) {
        const auto subject = m_subjects.find(fingerprintOf(*media));
        Clip moved = *clip;
        moved.mediaId = media->id;
        moved.sourceIn = from;
        moved.speed = speed;
        moved.keyframes.clear();
        if (subject != m_subjects.end()
            && followSubject(moved, *media, subject->second, m_project.settings.width, m_project.settings.height, 0.6)) {
            std::vector<std::unique_ptr<Command>> commands;
            commands.push_back(std::make_unique<ReplaceClipSourceCommand>(clip->id, media->id, from, speed, std::nullopt,
                                                                          std::size_t(index), TVE_TR("core", "Fill Slot")));
            std::map<std::string, Keyframes> changes{{std::string(tve::property::kX), moved.keyframes[std::string(tve::property::kX)]},
                                                     {std::string(tve::property::kY), moved.keyframes[std::string(tve::property::kY)]}};
            commands.push_back(std::make_unique<SetClipKeyframesCommand>(clip->id, std::move(changes), TVE_TR("core", "Fill Slot"), moved.visual));
            return apply(std::make_unique<CompositeCommand>(std::move(commands), TVE_TR("core", "Fill Slot")));
        }
    }
    return apply(std::make_unique<ReplaceClipSourceCommand>(clip->id, media->id, from, speed, crop, std::size_t(index), TVE_TR("core", "Fill Slot")));
}

bool ProjectController::setTemplateSlotStart(int index, double fromSeconds)
{
    if (!m_project.templateInfo || index < 0 || std::size_t(index) >= m_project.templateInfo->mediaSlots.size())
        return false;
    const Clip* clip = findClip(m_project.templateInfo->mediaSlots[std::size_t(index)].clipId);
    return clip && fillTemplateSlot(index, double(clip->mediaId), fromSeconds);
}

bool ProjectController::setTemplateText(int index, const QString& text)
{
    if (!m_project.templateInfo || index < 0 || std::size_t(index) >= m_project.templateInfo->texts.size())
        return false;
    return setClipText(double(m_project.templateInfo->texts[std::size_t(index)].clipId), {{QStringLiteral("text"), text}});
}

bool ProjectController::setTemplateMusic(double mediaId)
{
    if (!m_project.templateInfo || !findClip(m_project.templateInfo->musicClipId)) {
        reportError(tr("This project has no template music"));
        return false;
    }
    const MediaItem* media = m_project.findMedia(toId(mediaId));
    if (!media || !media->hasAudio) {
        reportError(tr("Choose something with sound"));
        return false;
    }
    const Clip* clip = findClip(m_project.templateInfo->musicClipId);
    if (media->duration < clip->duration) {
        reportError(QStringLiteral("%1 is shorter than the video").arg(mediaName(*media)));
        return false;
    }
    return apply(std::make_unique<ReplaceClipSourceCommand>(clip->id, media->id, 0, 1.0, std::nullopt, std::nullopt, TVE_TR("core", "Replace Music")));
}

bool ProjectController::setTemplateMusicStart(double fromSeconds)
{
    if (!m_project.templateInfo)
        return false;
    const Clip* clip = findClip(m_project.templateInfo->musicClipId);
    const MediaItem* media = clip ? m_project.findMedia(clip->mediaId) : nullptr;
    if (!media)
        return false;
    const Ticks from = std::clamp(ticks(fromSeconds), Ticks(0), std::max<Ticks>(0, media->duration - clip->duration));
    return apply(std::make_unique<ReplaceClipSourceCommand>(clip->id, media->id, from, 1.0, std::nullopt, std::nullopt,
                                                            TVE_TR("core", "Move Music")));
}

QVariantList ProjectController::templateCandidates() const
{
    QVariantList list;
    for (auto track = m_project.tracks.rbegin(); track != m_project.tracks.rend(); ++track)
        for (const Clip& clip : track->clips) {
            const MediaItem* media = m_project.findMedia(clip.mediaId);
            QString kind;
            if (clip.text)
                kind = QStringLiteral("title");
            else if (media && media->hasVideo)
                kind = QStringLiteral("picture");
            else if (media && media->hasAudio)
                kind = QStringLiteral("sound");
            if (kind.isEmpty())
                continue;
            list << QVariantMap{{QStringLiteral("clipId"), double(clip.id)},
                                {QStringLiteral("kind"), kind},
                                {QStringLiteral("label"), clip.text ? QString::fromStdString(clip.text->text).section(QLatin1Char('\n'), 0, 0)
                                                                    : mediaName(*media)},
                                {QStringLiteral("track"), QString::fromStdString(track->name)},
                                {QStringLiteral("start"), seconds(clip.start)},
                                {QStringLiteral("duration"), seconds(clip.duration)}};
        }
    return list;
}

QString ProjectController::saveAsTemplate(const QString& name, const QString& category, const QVariantList& slotClips,
                                          const QVariantList& textClips, double musicClip)
{
    if (!m_openLevels.empty())
        closeCompoundClip(0); // (the whole project: back to the main timeline first)
    std::vector<Id> slotIds, textIds;
    for (const QVariant& id : slotClips)
        slotIds.push_back(toId(id.toDouble()));
    for (const QVariant& id : textClips)
        textIds.push_back(toId(id.toDouble()));
    QString error;
    const QString id = m_templates->save(m_project, name, category, slotIds, textIds,
                                         musicClip > 0 ? toId(musicClip) : kNoId, &error);
    if (id.isEmpty())
        reportError(error);
    return id;
}

// --- Made for you: instant videos, highlight montages, shorts ---

namespace {
QString analysisDirectory(); // (below, with the rest of the analysis)
} // namespace

std::vector<TimedWord> ProjectController::mediaWords(const MediaItem& media, std::string* variant) const
{
    std::vector<TimedWord> words;
    const std::string prefix = fingerprintOf(media) + "|";
    // The variant shown in the Transcript tab first, else any.
    auto found = m_transcripts.find(prefix + m_transcriptVariant);
    if (found == m_transcripts.end())
        found = std::ranges::find_if(m_transcripts, [&](const auto& entry) { return entry.first.starts_with(prefix); });
    if (found == m_transcripts.end())
        return words;
    if (variant)
        *variant = found->first.substr(prefix.size());
    for (const tve::TranscriptWord& word : found->second.words)
        words.push_back({word.start, word.end, word.text});
    return words;
}

bool ProjectController::hasTranscript(double mediaId) const
{
    const MediaItem* media = m_project.findMedia(toId(mediaId));
    return media && !mediaWords(*media).empty();
}

// --- Smart search ---

bool ProjectController::lookable(const MediaItem& media) const
{
    return media.hasVideo && !media.lottie && !media.placeholder && !media.colourFill && !media.sequence
        && QFileInfo::exists(QString::fromStdString(media.path));
}

const tve::ContentIndex* ProjectController::contentOf(const MediaItem& media)
{
    const std::string key = fingerprintOf(media);
    auto found = m_content.find(key);
    if (found == m_content.end()) {
        // (An empty index: not looked at yet; not read from disk again.)
        auto kept = tve::loadContent(tve::AnalysisCache(analysisDirectory().toStdString()), key);
        found = m_content.emplace(key, kept ? std::move(*kept) : tve::ContentIndex{}).first;
    }
    return found->second.frames.empty() ? nullptr : &found->second;
}

const std::vector<TimedWord>& ProjectController::searchWords(const MediaItem& media)
{
    const std::string key = fingerprintOf(media);
    std::vector<TimedWord> words = mediaWords(media);
    if (!words.empty())
        return m_searchWords.insert_or_assign(key, std::move(words)).first->second;
    auto found = m_searchWords.find(key);
    if (found == m_searchWords.end()) {
        std::vector<TimedWord> kept;
        if (media.hasAudio)
            if (auto transcript = tve::loadAnyTranscript(tve::AnalysisCache(analysisDirectory().toStdString()), key))
                for (const tve::TranscriptWord& word : transcript->words)
                    kept.push_back({word.start, word.end, word.text});
        found = m_searchWords.emplace(key, std::move(kept)).first;
    }
    return found->second;
}

QVariantMap ProjectController::searchStatus() const
{
    auto* self = const_cast<ProjectController*>(this); // (reads the caches in)
    int lookableCount = 0, looked = 0, transcribed = 0;
    for (const MediaItem& media : m_project.media) {
        if (self->lookable(media)) {
            ++lookableCount;
            if (self->contentOf(media))
                ++looked;
        }
        if (media.hasAudio && !self->searchWords(media).empty())
            ++transcribed;
    }
    return {{QStringLiteral("lookable"), lookableCount}, {QStringLiteral("looked"), looked},
            {QStringLiteral("transcribed"), transcribed}};
}

bool ProjectController::analyzeForSearch(const QVariantList& mediaIds)
{
    if (analyzing()) {
        reportError(tr("An analysis is already running"));
        return false;
    }
    std::vector<const MediaItem*> chosen;
    if (mediaIds.isEmpty()) {
        for (const MediaItem& media : m_project.media)
            chosen.push_back(&media);
    } else {
        for (const QVariant& id : mediaIds)
            if (const MediaItem* media = m_project.findMedia(toId(id.toDouble())))
                chosen.push_back(media);
    }
    struct Job {
        std::string path;
        bool still = false;
        std::string key;
    };
    std::vector<Job> jobs;
    for (const MediaItem* media : chosen) {
        if (!lookable(*media) || contentOf(*media))
            continue;
        const std::string key = fingerprintOf(*media);
        if (std::ranges::none_of(jobs, [&](const Job& j) { return j.key == key; }))
            jobs.push_back({media->path, media->still, key});
    }
    if (jobs.empty()) {
        if (chosen.empty() || std::ranges::none_of(chosen, [this](const MediaItem* m) { return lookable(*m); })) {
            reportError(tr("There are no pictures or videos to look at"));
            return false;
        }
        emit searchIndexChanged();
        emit analysisFinished(true, {});
        return true;
    }
    auto found = std::make_shared<std::vector<std::pair<std::string, tve::ContentIndex>>>();
    runInBackground(tr("Looking at the media for search"),
                    [jobs, found, directory = analysisDirectory().toStdString()](const tve::AnalysisProgress& progress) -> std::string {
                        const tve::AnalysisCache cache(directory);
                        const double n = double(jobs.size());
                        for (std::size_t i = 0; i < jobs.size(); ++i) {
                            std::string error;
                            auto index = tve::analyzeContent(jobs[i].path, jobs[i].still,
                                                             [&](double done) { return progress((double(i) + done) / n); }, &error);
                            if (!index) {
                                if (error.empty())
                                    return "cancelled";
                                continue; // (this one cannot be read; the others still can)
                            }
                            tve::saveContent(cache, jobs[i].key, *index);
                            found->emplace_back(jobs[i].key, std::move(*index));
                            if (!progress(double(i + 1) / n))
                                return "cancelled";
                        }
                        return {};
                    },
                    [this, found] {
                        for (auto& [key, index] : *found)
                            m_content.insert_or_assign(key, std::move(index));
                        emit searchIndexChanged();
                        emit analysisFinished(true, {});
                    });
    return true;
}

QVariantList ProjectController::searchMedia(const QString& query, int limit)
{
    std::vector<tve::SearchableMedia> searchable;
    for (const MediaItem& media : m_project.media) {
        const tve::ContentIndex* content = lookable(media) ? contentOf(media) : nullptr;
        const std::vector<TimedWord>* words = media.hasAudio ? &searchWords(media) : nullptr;
        searchable.push_back({media.id, media.duration, media.still, content, words});
    }
    QVariantList hits;
    for (const tve::SearchHit& hit : tve::searchMedia(query.toStdString(), searchable, std::size_t(std::max(1, limit)))) {
        const MediaItem* media = m_project.findMedia(hit.mediaId);
        hits << QVariantMap{{QStringLiteral("mediaId"), double(hit.mediaId)},
                            {QStringLiteral("name"), mediaName(*media)},
                            {QStringLiteral("path"), QString::fromStdString(media->path)},
                            {QStringLiteral("still"), media->still},
                            {QStringLiteral("start"), seconds(hit.start)},
                            {QStringLiteral("end"), seconds(hit.end)},
                            {QStringLiteral("seen"), bool(hit.found & tve::kFoundSeen)},
                            {QStringLiteral("said"), bool(hit.found & tve::kFoundSaid)},
                            {QStringLiteral("shown"), bool(hit.found & tve::kFoundShown)},
                            {QStringLiteral("snippet"), QString::fromStdString(hit.snippet)}};
    }
    return hits;
}

QVariantList ProjectController::mediaLabels(double mediaId, int count)
{
    QVariantList labels;
    const MediaItem* media = m_project.findMedia(toId(mediaId));
    const tve::ContentIndex* content = media && lookable(*media) ? contentOf(*media) : nullptr;
    if (!content)
        return labels;
    for (const tve::LabelShare& label : tve::topLabels(*content, std::size_t(std::max(1, count))))
        labels << QVariantMap{{QStringLiteral("label"), QString::fromStdString(tve::labelText(label.name))},
                              {QStringLiteral("share"), label.share}};
    return labels;
}

// --- Proofreading and prompts (Claude) ---

namespace {

// The name of the model that answered, as offered (a fallback may have answered).
QString answeredBy(CloudServices* cloud, const QString& model)
{
    for (const QVariant& offered : cloud->claudeModels())
        if (offered.toMap().value(QStringLiteral("id")).toString() == model)
            return offered.toMap().value(QStringLiteral("label")).toString();
    return model;
}

} // namespace

bool ProjectController::checkText()
{
    struct Item {
        Id clipId = kNoId;
        bool caption = false;
        QString text;
    };
    // The captions in order, then the other titles by time.
    std::vector<Item> items;
    std::vector<std::pair<Ticks, Item>> titles;
    for (const Track& track : m_project.tracks)
        for (const Clip& clip : track.clips) {
            if (!clip.text || QString::fromStdString(clip.text->text).trimmed().isEmpty())
                continue;
            Item item{clip.id, track.captions, QString::fromStdString(clip.text->text)};
            if (track.captions)
                items.push_back(std::move(item));
            else
                titles.emplace_back(clip.start, std::move(item));
        }
    std::ranges::stable_sort(titles, {}, &std::pair<Ticks, Item>::first);
    for (auto& [start, item] : titles)
        items.push_back(std::move(item));
    if (items.empty()) {
        reportError(tr("There are no titles or captions to check"));
        return false;
    }
    if (m_cloud->busy()) {
        reportError(tr("Another AI job is running"));
        return false;
    }
    m_textFixes.clear();
    emit textFixesChanged();

    // Batches, one after another; the fixes are offered when all are in.
    struct Check {
        std::vector<Item> items;
        QMap<int, cloudfeatures::Correction> fixes;
        int next = 0;
        QString model;
    };
    auto state = std::make_shared<Check>(Check{std::move(items), {}, 0, {}});
    QPointer<ProjectController> self(this);
    auto step = std::make_shared<std::function<void()>>();
    *step = [self, state, step] {
        if (!self)
            return;
        CloudServices* cloud = self->m_cloud;
        const int total = int(state->items.size());
        const int from = state->next;
        const int to = std::min(total, from + cloudfeatures::kTextsPerRequest);
        QList<QPair<int, QString>> batch;
        QMap<int, QString> asked;
        for (int i = from; i < to; ++i) {
            batch.push_back({i, state->items[std::size_t(i)].text});
            asked.insert(i, state->items[std::size_t(i)].text);
        }
        cloud->setProgress(double(from) / total, total > cloudfeatures::kTextsPerRequest
                                                     ? tr("Checking texts %1–%2 of %3…").arg(from + 1).arg(to).arg(total)
                                                     : tr("Checking the text…"));
        const auto done = [self, state, step, asked, to, total](const TextResult& result) {
            if (!self)
                return;
            CloudServices* cloud = self->m_cloud;
            const auto fail = [&](const QString& error) {
                cloud->endJob(error);
                emit cloud->jobFinished(QStringLiteral("proofread"), false, error);
                if (!error.isEmpty())
                    self->reportError(error);
                *step = nullptr; // (breaks the cycle)
            };
            if (!result.ok) {
                fail(result.cancelled ? QString() : result.error);
                return;
            }
            const auto fixes = cloudfeatures::readCorrections(result.text, asked);
            if (!fixes) {
                fail(tr("The check came back unreadable; try again"));
                return;
            }
            state->fixes.insert(*fixes);
            state->model = result.usage.model;
            state->next = to;
            if (to < total) {
                (*step)();
                return;
            }
            QVariantList list;
            for (auto it = state->fixes.cbegin(); it != state->fixes.cend(); ++it) {
                const Item& item = state->items[std::size_t(it.key())];
                list << QVariantMap{{QStringLiteral("index"), list.size()},
                                    {QStringLiteral("clipId"), double(item.clipId)},
                                    {QStringLiteral("kind"), item.caption ? QStringLiteral("caption") : QStringLiteral("title")},
                                    {QStringLiteral("before"), item.text},
                                    {QStringLiteral("after"), it.value().text},
                                    {QStringLiteral("note"), it.value().note}};
            }
            self->m_textFixes = list;
            emit self->textFixesChanged();
            const QString by = answeredBy(cloud, state->model);
            cloud->endJob(list.isEmpty() ? tr("Nothing to fix (checked by %1)").arg(by)
                         : list.size() == 1 ? tr("1 thing to fix (checked by %1)").arg(by)
                                            : tr("%1 things to fix (checked by %2)").arg(list.size()).arg(by));
            emit cloud->jobFinished(QStringLiteral("proofread"), true, QString());
            *step = nullptr;
        };
        if (!cloud->generate(QStringLiteral("proofread"), cloudfeatures::correctionRequest(batch, cloud->claudeModel()), done)) {
            cloud->endJob(QString());
            *step = nullptr;
        }
    };
    (*step)();
    return true;
}

bool ProjectController::applyTextFixes(const QVariantList& indices)
{
    std::set<int> chosen;
    for (const QVariant& index : indices)
        chosen.insert(index.toInt());
    std::vector<std::unique_ptr<Command>> commands;
    QVariantList left;
    for (const QVariant& value : m_textFixes) {
        const QVariantMap fix = value.toMap();
        if (!chosen.contains(fix.value(QStringLiteral("index")).toInt())) {
            left << fix;
            continue;
        }
        const Clip* clip = findClip(toId(fix.value(QStringLiteral("clipId")).toDouble()));
        if (!clip || !clip->text || QString::fromStdString(clip->text->text) != fix.value(QStringLiteral("before")).toString())
            continue; // changed since: dropped
        ClipText text = *clip->text;
        text.text = fix.value(QStringLiteral("after")).toString().toStdString();
        text.words = tve::retimeWords(text.words, text.text);
        commands.push_back(std::make_unique<SetClipTextCommand>(clip->id, std::move(text)));
    }
    if (commands.empty()) {
        m_textFixes = left;
        emit textFixesChanged();
        reportError(tr("The text has changed since it was checked; check it again"));
        return false;
    }
    if (!apply(std::make_unique<CompositeCommand>(std::move(commands), TVE_TR("core", "Correct Text"))))
        return false;
    m_textFixes = left;
    emit textFixesChanged();
    return true;
}

void ProjectController::dismissTextFixes()
{
    if (m_textFixes.isEmpty())
        return;
    m_textFixes.clear();
    emit textFixesChanged();
}

bool ProjectController::promptFromPicture(const QString& purpose, double mediaId)
{
    if (purpose != QLatin1String("picture") && purpose != QLatin1String("sticker") && purpose != QLatin1String("video")) {
        reportError(tr("Unknown kind of prompt"));
        return false;
    }
    if (m_cloud->busy()) {
        reportError(tr("Another AI job is running"));
        return false;
    }
    QImage image;
    if (mediaId >= 0) {
        const MediaItem* media = m_project.findMedia(toId(mediaId));
        if (!media || !media->still || media->lottie) {
            reportError(tr("Choose a picture to describe"));
            return false;
        }
        QImageReader reader(QString::fromStdString(media->path));
        reader.setAutoTransform(true);
        image = reader.read();
    } else {
        if (duration() <= 0) {
            reportError(tr("There is nothing at the playhead to describe"));
            return false;
        }
        StillRenderer renderer;
        if (renderer.isValid())
            image = renderer.render(std::make_shared<const Project>(m_project), ticks(m_playhead),
                                    QSize(m_project.settings.width, m_project.settings.height));
    }
    if (image.isNull()) {
        reportError(tr("Could not read the picture to describe"));
        return false;
    }
    // Claude reads pictures up to about 1568 pixels on the long side; larger only costs more.
    constexpr int kLongSide = 1568;
    if (std::max(image.width(), image.height()) > kLongSide)
        image = image.scaled(kLongSide, kLongSide, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    QByteArray jpeg;
    QBuffer buffer(&jpeg);
    buffer.open(QIODevice::WriteOnly);
    image.convertToFormat(QImage::Format_RGB888).save(&buffer, "JPEG", 85);

    QPointer<ProjectController> self(this);
    const TextRequest request = cloudfeatures::imagePromptRequest(jpeg, QStringLiteral("image/jpeg"), purpose, m_cloud->claudeModel());
    const bool started = m_cloud->generate(QStringLiteral("describe"), request, [self](const TextResult& result) {
        if (!self)
            return;
        CloudServices* cloud = self->m_cloud;
        cloud->endJob(result.ok ? tr("Written by %1").arg(answeredBy(cloud, result.usage.model)) : result.error);
        emit cloud->jobFinished(QStringLiteral("describe"), result.ok, result.cancelled ? QString() : result.error);
        if (!result.ok && !result.cancelled)
            self->reportError(result.error);
    });
    if (!started)
        reportError(tr("Another AI job is running"));
    return started;
}

void ProjectController::withAllFeatures(const std::vector<Id>& mediaIds, const QString& task, std::function<void()> then)
{
    std::vector<Id> heard;
    for (Id id : mediaIds)
        if (const MediaItem* media = m_project.findMedia(id); media && media->hasAudio)
            heard.push_back(id);
    withFeatures(heard, false, task, [this, mediaIds, task, then = std::move(then)] {
        withFeatures(mediaIds, true, task, then);
    });
}

void ProjectController::withSubject(const MediaItem& media, std::function<void()> then)
{
    const std::string key = fingerprintOf(media);
    if (!m_subjects.contains(key))
        if (auto kept = tve::loadSubject(tve::AnalysisCache(analysisDirectory().toStdString()), key))
            m_subjects.emplace(key, std::move(*kept));
    if (m_subjects.contains(key)) {
        then();
        return;
    }
    auto found = std::make_shared<std::vector<SubjectSample>>();
    runInBackground(tr("Following the subject"),
                    [path = media.path, key, found, directory = analysisDirectory().toStdString()](
                        const tve::AnalysisProgress& progress) -> std::string {
                        std::string error;
                        auto samples = tve::findSubject(path, progress, &error);
                        if (!samples)
                            return error.empty() ? std::string("cancelled") : error;
                        tve::saveSubject(tve::AnalysisCache(directory), key, *samples);
                        *found = std::move(*samples);
                        return {};
                    },
                    [this, key, found, then = std::move(then)] {
                        m_subjects.insert_or_assign(key, std::move(*found));
                        then();
                    });
}

void ProjectController::rankMoments(bool rank, const QString& purpose, const QList<cloudfeatures::RankItem>& items,
                                    std::function<void(QList<QPair<int, QString>>)> then)
{
    if (!rank || items.size() < 2 || !m_cloud->hasClaudeKey()) {
        then({});
        return;
    }
    QList<int> indices;
    for (const cloudfeatures::RankItem& item : items)
        indices << item.index;
    QPointer<ProjectController> self(this);
    const bool started = m_cloud->generate(
        QStringLiteral("rank"), cloudfeatures::rankRequest(items, purpose, m_cloud->claudeModel()),
        [self, indices, then](const TextResult& result) {
            if (!self)
                return;
            self->m_cloud->endJob(result.ok ? tr("Ranked by Claude") : result.error);
            // Without Claude's order the edit still goes ahead, in the measured order.
            const auto order = result.ok ? cloudfeatures::readRanking(result.text, indices) : std::nullopt;
            if (!result.ok && !result.cancelled)
                self->reportError(tr("Claude could not rank the moments (%1); using the measured order").arg(result.error));
            then(order.value_or(QList<QPair<int, QString>>{}));
        });
    if (!started)
        then({});
}

void ProjectController::autoEditFailed(const QString& kind, const QString& message)
{
    if (!message.isEmpty())
        reportError(message);
    emit autoEditFinished(kind, false, message);
}

bool ProjectController::makeAutoEdit(const QVariantMap& options)
{
    const QString kind = options.value(QStringLiteral("kind"), QStringLiteral("instant")).toString();
    if (analyzing() || m_cloud->busy()) {
        reportError(tr("Something is already being worked on"));
        return false;
    }
    const AutoEditStyle* style = findAutoEditStyle(options.value(QStringLiteral("style"), QStringLiteral("upbeat")).toString());
    const auto shape = templateShape(options.value(QStringLiteral("shape"), QStringLiteral("16:9")).toString().toStdString());
    if (!style || !shape) {
        reportError(tr("Unknown style or shape"));
        return false;
    }
    std::vector<Id> ids, videos;
    for (const QVariant& value : options.value(QStringLiteral("media")).toList()) {
        const MediaItem* media = m_project.findMedia(toId(value.toDouble()));
        if (!media || !media->hasVideo || media->lottie || media->placeholder)
            continue;
        if (kind == QLatin1String("montage") && media->still)
            continue;
        ids.push_back(media->id);
        if (!media->still)
            videos.push_back(media->id);
    }
    if (ids.empty() || (kind == QLatin1String("montage") && videos.empty())) {
        reportError(kind == QLatin1String("montage") ? tr("Choose one or more videos") : tr("Choose some pictures or videos"));
        return false;
    }
    const double seconds = std::clamp(options.value(QStringLiteral("seconds"), 30.0).toDouble(), 5.0, 300.0);
    const QString title = options.value(QStringLiteral("title")).toString();
    const bool rank = options.value(QStringLiteral("rank")).toBool();
    const TemplateShape canvas = *shape;
    const AutoEditStyle chosenStyle = *style;
    withAllFeatures(videos, tr("Finding the best moments"), [this, kind, ids, chosenStyle, canvas, seconds, title, rank] {
        // Each video's moments, from its sound, pictures and words.
        auto sources = std::make_shared<std::vector<AutoEditSource>>();
        for (Id id : ids) {
            const MediaItem* media = m_project.findMedia(id);
            if (!media)
                continue;
            AutoEditSource source{*media, {}};
            if (!media->still) {
                const std::string key = fingerprintOf(*media);
                const auto audio = m_audioFeatures.find(key);
                const auto video = m_videoFeatures.find(key);
                source.moments = tve::findMoments(audio != m_audioFeatures.end() ? &audio->second : nullptr,
                                                  video != m_videoFeatures.end() ? &video->second : nullptr,
                                                  mediaWords(*media), media->duration, {.minLength = 2.0, .maxLength = 8.0});
            }
            sources->push_back(std::move(source));
        }
        // The strongest moments, for Claude to order.
        QList<cloudfeatures::RankItem> items;
        std::vector<std::pair<std::size_t, std::size_t>> where; // (source, moment)
        for (std::size_t s = 0; s < sources->size(); ++s)
            for (std::size_t m = 0; m < (*sources)[s].moments.size(); ++m)
                where.push_back({s, m});
        std::ranges::stable_sort(where, [&](const auto& a, const auto& b) {
            return (*sources)[a.first].moments[a.second].score > (*sources)[b.first].moments[b.second].score;
        });
        where.resize(std::min<std::size_t>(where.size(), 30));
        for (std::size_t i = 0; i < where.size(); ++i) {
            const Moment& moment = (*sources)[where[i].first].moments[where[i].second];
            items.push_back({int(i), double(moment.start) / kTicksPerSecond, double(moment.end) / kTicksPerSecond,
                             QString::fromStdString(moment.text), QString::fromStdString(moment.why)});
        }
        rankMoments(rank, QStringLiteral("montage"), items, [this, kind, sources, where, chosenStyle, canvas, seconds, title](QList<QPair<int, QString>> order) {
            // Claude's order becomes the scores (the best near 1).
            for (qsizetype r = 0; r < order.size(); ++r) {
                const auto [s, m] = where[std::size_t(order[r].first)];
                (*sources)[s].moments[m].score = 1.0 - double(r) / double(order.size() + 1);
            }
            for (AutoEditSource& source : *sources)
                std::ranges::stable_sort(source.moments, [](const Moment& a, const Moment& b) { return a.score > b.score; });
            QString error;
            const QString name = (kind == QLatin1String("montage") ? QStringLiteral("%1 montage") : QStringLiteral("%1 video")).arg(chosenStyle.name);
            auto project = buildAutoEdit(*sources, chosenStyle, canvas, seconds, title, name, &error);
            if (!project) {
                autoEditFailed(kind, error);
                emit analysisFinished(false, error);
                return;
            }
            resetProject(std::move(*project), {});
            m_canvasChosen = true;
            emit autoEditFinished(kind, true, order.isEmpty() ? QString() : tr("Moments ranked by Claude"));
            emit analysisFinished(true, {});
        });
    });
    return true;
}

bool ProjectController::makeShorts(const QVariantMap& options)
{
    if (analyzing() || m_cloud->busy()) {
        reportError(tr("Something is already being worked on"));
        return false;
    }
    const MediaItem* media = m_project.findMedia(toId(options.value(QStringLiteral("media")).toDouble()));
    const double seconds = std::clamp(options.value(QStringLiteral("seconds"), 30.0).toDouble(), 10.0, 90.0);
    if (!media || !media->hasVideo || media->still || media->lottie) {
        reportError(tr("Choose a video to make shorts from"));
        return false;
    }
    if (double(media->duration) / kTicksPerSecond < seconds * 0.6) {
        reportError(QStringLiteral("%1 is too short for shorts of %2 s").arg(mediaName(*media)).arg(seconds));
        return false;
    }
    const int count = std::clamp(options.value(QStringLiteral("count"), 3).toInt(), 1, 5);
    const bool captions = options.value(QStringLiteral("captions"), true).toBool();
    const bool rank = options.value(QStringLiteral("rank")).toBool();
    const MediaItem source = *media;
    withAllFeatures({source.id}, tr("Finding the best moments"), [this, source, seconds, count, captions, rank] {
        withSubject(source, [this, source, seconds, count, captions, rank] {
            const std::string key = fingerprintOf(source);
            const auto audio = m_audioFeatures.find(key);
            const auto video = m_videoFeatures.find(key);
            std::string variant;
            const std::vector<TimedWord> words = mediaWords(source, &variant);
            auto moments = std::make_shared<std::vector<Moment>>(
                tve::findMoments(audio != m_audioFeatures.end() ? &audio->second : nullptr,
                                 video != m_videoFeatures.end() ? &video->second : nullptr, words, source.duration,
                                 {.minLength = std::max(5.0, seconds * 0.6), .maxLength = seconds * 1.4}));
            moments->resize(std::min<std::size_t>(moments->size(), std::size_t(count) * 3));
            QList<cloudfeatures::RankItem> items;
            for (std::size_t i = 0; i < moments->size(); ++i)
                items.push_back({int(i), double((*moments)[i].start) / kTicksPerSecond, double((*moments)[i].end) / kTicksPerSecond,
                                 QString::fromStdString((*moments)[i].text), QString::fromStdString((*moments)[i].why)});
            rankMoments(rank, QStringLiteral("shorts"), items, [this, source, moments, count, captions, words](QList<QPair<int, QString>> order) {
                std::vector<ShortPlan> plans;
                if (!order.isEmpty())
                    for (const auto& [index, title] : order)
                        plans.push_back({(*moments)[std::size_t(index)], title});
                else
                    for (const Moment& moment : *moments)
                        plans.push_back({moment, {}});
                plans.resize(std::min<std::size_t>(plans.size(), std::size_t(count)));
                const auto found = m_subjects.find(fingerprintOf(source));
                QString error;
                auto project = buildShorts(source, plans, found != m_subjects.end() ? found->second : std::vector<SubjectSample>{}, &error);
                if (!project) {
                    autoEditFailed(QStringLiteral("shorts"), error);
                    emit analysisFinished(false, error);
                    return;
                }
                // Captions from what is said in each short.
                QString note;
                if (captions && !words.empty()) {
                    std::vector<TimedWord> placed;
                    for (const Track& track : project->tracks)
                        for (const Clip& clip : track.clips)
                            if (clip.mediaId != kNoId && !track.clips.empty() && track.kind == TrackKind::Video)
                                for (const TimedWord& word : words)
                                    if (word.start >= clip.sourceIn && word.start < clip.sourceIn + clip.duration)
                                        placed.push_back({word.start - clip.sourceIn + clip.start,
                                                          std::min(word.end, clip.sourceIn + clip.duration) - clip.sourceIn + clip.start,
                                                          word.text});
                    std::ranges::sort(placed, {}, &TimedWord::start);
                    SetCaptionsCommand(tve::layoutCaptions(placed), "Captions").redo(*project);
                } else if (captions) {
                    note = tr("No captions: transcribe the video first (Transcript tab)");
                }
                const int made = int(plans.size());
                resetProject(std::move(*project), {});
                m_canvasChosen = true;
                if (!order.isEmpty())
                    note = note.isEmpty() ? tr("Ranked and titled by Claude") : note;
                emit autoEditFinished(QStringLiteral("shorts"), true,
                                      QStringLiteral("%1 short%2").arg(made).arg(made == 1 ? "" : "s") + (note.isEmpty() ? QString() : QStringLiteral(" · ") + note));
                emit analysisFinished(true, {});
            });
        });
    });
    return true;
}

// --- AI: generated media ---

QString ProjectController::generatedDirectory() const
{
    const QByteArray override = qgetenv("TVE_GENERATED_DIR");
    if (!override.isEmpty())
        return QString::fromLocal8Bit(override);
    if (!m_filePath.isEmpty()) {
        const QFileInfo project(m_filePath);
        return project.absolutePath() + QLatin1Char('/') + project.completeBaseName() + QStringLiteral(" Generated");
    }
    return QStandardPaths::writableLocation(QStandardPaths::MoviesLocation) + QStringLiteral("/TechieVideoEditor Generated");
}

QString ProjectController::generatedBase(const QString& base) const
{
    const QDir folder(generatedDirectory());
    for (int n = 1;; ++n) {
        const QString candidate = folder.filePath(QStringLiteral("%1 %2").arg(base).arg(n));
        bool taken = false;
        for (const char* extension : {".png", ".mp4", ".mp3", ".wav"})
            taken = taken || QFileInfo::exists(candidate + QLatin1String(extension));
        if (!taken)
            return candidate;
    }
}

Id ProjectController::importGenerated(const MediaResult& result, GeneratedBy by, bool alpha)
{
    QString error;
    auto item = describeMedia(result.path.toStdString(), &error);
    if (!item) {
        reportError(error);
        return kNoId;
    }
    by.model = result.model.toStdString();
    by.reference = result.reference.toStdString();
    by.created = double(QDateTime::currentSecsSinceEpoch());
    item->generated = std::move(by);
    item->alpha = item->alpha || alpha;
    auto command = std::make_unique<ImportMediaCommand>(std::move(*item));
    if (!apply(std::move(command)))
        return kNoId;
    const auto found = std::ranges::find(m_project.media, result.path.toStdString(), &MediaItem::path);
    return found != m_project.media.end() ? found->id : kNoId;
}

void ProjectController::endGeneration(const QString& job, const MediaResult& result, bool ok)
{
    m_cloud->endJob(ok ? QString() : result.error);
    if (!ok && !result.cancelled && !result.error.isEmpty())
        reportError(result.error);
    emit m_cloud->jobFinished(job, ok, result.cancelled ? QString() : result.error);
}

namespace {

// Where each still media to work from is, and their names; empty if one is not a picture.
std::optional<std::pair<QStringList, QStringList>> stillInputs(const Project& project, const QVariantList& ids)
{
    QStringList paths, names;
    for (const QVariant& id : ids) {
        const MediaItem* media = project.findMedia(Id(id.toDouble()));
        if (!media || !media->still || media->lottie)
            return std::nullopt;
        paths << QString::fromStdString(media->path);
        names << QFileInfo(paths.back()).fileName();
    }
    return std::pair{paths, names};
}

} // namespace

bool ProjectController::generateImage(const QString& prompt, const QVariantMap& options)
{
    if (prompt.trimmed().isEmpty()) {
        reportError(tr("Describe the picture first"));
        return false;
    }
    const bool sticker = options.value(QStringLiteral("sticker")).toBool();
    const auto inputs = stillInputs(m_project, options.value(QStringLiteral("inputs")).toList());
    if (!inputs) {
        reportError(tr("Only pictures in the media bin can be worked from"));
        return false;
    }
    ImageOptions image;
    image.model = m_cloud->imageModel();
    const QString shape = options.value(QStringLiteral("size"), QStringLiteral("square")).toString();
    image.size = shape == QLatin1String("landscape") ? QStringLiteral("1536x1024")
               : shape == QLatin1String("portrait")  ? QStringLiteral("1024x1536")
                                                     : QStringLiteral("1024x1024");
    image.quality = options.value(QStringLiteral("quality"), QStringLiteral("medium")).toString();
    image.transparent = sticker;
    image.inputs = inputs->first;
    const QString job = sticker ? QStringLiteral("sticker") : QStringLiteral("image");
    const QString base = generatedBase(sticker ? QStringLiteral("Sticker") : QStringLiteral("Picture"));
    QPointer<ProjectController> self(this);
    const QString sources = inputs->second.join(QStringLiteral(", "));
    const bool started = m_cloud->generateMedia(
        job, sticker ? tr("OpenAI is drawing the sticker…") : tr("OpenAI is drawing…"),
        [prompt, image, base](QNetworkAccessManager* network, QObject* parent) {
            return cloudmedia::image(network, prompt, image, base, parent);
        },
        [self, job, sticker, prompt, sources](const MediaResult& result) {
            if (!self)
                return;
            if (!result.ok) {
                self->endGeneration(job, result, false);
                return;
            }
            QString cutError;
            if (sticker && !cutOutSticker(result.path, &cutError))
                self->reportError(cutError);
            const Id media = self->importGenerated(
                result, GeneratedBy{.provider = "openai", .kind = sticker ? "sticker" : "image",
                                    .prompt = prompt.toStdString(), .source = sources.toStdString()},
                sticker);
            const bool ok = media != kNoId && (!sticker || self->placeOverlay(media, 0.4, "Stickers", TVE_TR("core", "Add AI Sticker")));
            self->endGeneration(job, result, ok);
        });
    if (!started)
        reportError(tr("Another AI job is running"));
    return started;
}

bool ProjectController::generateVideo(const QString& prompt, const QVariantMap& options)
{
    VideoOptions video;
    video.mode = options.value(QStringLiteral("mode"), QStringLiteral("text")).toString();
    video.aspect = options.value(QStringLiteral("aspect"), QStringLiteral("16:9")).toString();
    video.resolution = options.value(QStringLiteral("resolution"), QStringLiteral("720p")).toString();
    QString sources;
    if (video.mode == QLatin1String("extend")) {
        const MediaItem* media = m_project.findMedia(Id(options.value(QStringLiteral("extend")).toDouble()));
        if (!media || !media->generated || media->generated->kind != "video" || media->generated->reference.empty()) {
            reportError(tr("Choose a video made here to extend"));
            return false;
        }
        video.previous = QString::fromStdString(media->generated->reference);
        sources = mediaName(*media);
    } else {
        const auto inputs = stillInputs(m_project, options.value(QStringLiteral("images")).toList());
        const qsizetype wanted = video.mode == QLatin1String("image") ? 1 : video.mode == QLatin1String("frames") ? 2 : 0;
        if (!inputs || inputs->first.size() != wanted) {
            reportError(wanted == 2 ? tr("Choose a first and a last picture")
                                    : tr("Choose a picture to start from"));
            return false;
        }
        if (prompt.trimmed().isEmpty() && wanted == 0) {
            reportError(tr("Describe the video first"));
            return false;
        }
        video.images = inputs->first;
        sources = inputs->second.join(QStringLiteral(", "));
    }
    const QString base = generatedBase(QStringLiteral("Video"));
    QPointer<ProjectController> self(this);
    const bool started = m_cloud->generateMedia(
        QStringLiteral("video"), tr("Google is making the video; this can take a few minutes…"),
        [prompt, video, base](QNetworkAccessManager* network, QObject* parent) {
            return cloudmedia::video(network, prompt, video, base, parent);
        },
        [self, prompt, sources, mode = video.mode](const MediaResult& result) {
            if (!self)
                return;
            const QString job = QStringLiteral("video");
            if (!result.ok) {
                self->endGeneration(job, result, false);
                return;
            }
            const Id media = self->importGenerated(result, GeneratedBy{.provider = "google", .kind = "video",
                                                                       .prompt = prompt.toStdString(),
                                                                       .source = (mode == QLatin1String("extend") ? "extends " : "")
                                                                                 + sources.toStdString()});
            self->endGeneration(job, result, media != kNoId);
        });
    if (!started)
        reportError(tr("Another AI job is running"));
    return started;
}

bool ProjectController::generateSound(const QString& prompt, const QVariantMap& options)
{
    if (prompt.trimmed().isEmpty()) {
        reportError(tr("Describe the sound first"));
        return false;
    }
    const bool music = options.value(QStringLiteral("kind"), QStringLiteral("music")).toString() == QLatin1String("music");
    const double length = options.value(QStringLiteral("seconds"), music ? 30.0 : 3.0).toDouble();
    const bool instrumental = options.value(QStringLiteral("instrumental"), true).toBool();
    const bool loop = options.value(QStringLiteral("loop")).toBool();
    const QString job = music ? QStringLiteral("music") : QStringLiteral("sfx");
    const QString base = generatedBase(music ? QStringLiteral("Music") : QStringLiteral("Sound"));
    QPointer<ProjectController> self(this);
    const bool started = m_cloud->generateMedia(
        job, music ? tr("ElevenLabs is composing…") : tr("ElevenLabs is making the sound…"),
        [prompt, music, length, instrumental, loop, base](QNetworkAccessManager* network, QObject* parent) {
            return music ? cloudmedia::music(network, prompt, length, instrumental, base, parent)
                         : cloudmedia::soundEffect(network, prompt, length, loop, base, parent);
        },
        [self, job, music, prompt](const MediaResult& result) {
            if (!self)
                return;
            if (!result.ok) {
                self->endGeneration(job, result, false);
                return;
            }
            // At the playhead, on an audio track with room, like the library's sounds.
            QString error;
            auto item = probeMedia(result.path.toStdString(), &error);
            if (!item) {
                self->reportError(error);
                self->endGeneration(job, result, false);
                return;
            }
            item->fingerprint = fingerprintFile(result.path.toStdString()).value_or(std::string());
            item->generated = GeneratedBy{.provider = "elevenlabs", .model = result.model.toStdString(),
                                          .kind = music ? "music" : "sfx", .prompt = prompt.toStdString(),
                                          .created = double(QDateTime::currentSecsSinceEpoch())};
            const Ticks start = ticks(self->m_playhead);
            Id target = kNoId;
            for (auto track = self->m_project.tracks.rbegin(); track != self->m_project.tracks.rend(); ++track)
                if (track->kind == TrackKind::Audio && !track->locked && !track->muted && !track->overlaps(start, item->duration)) {
                    target = track->id;
                    break;
                }
            auto command = std::make_unique<AddVoiceOverCommand>(std::move(*item), target, start,
                                                                 music ? TVE_TR("core", "Add AI Music") : TVE_TR("core", "Add AI Sound Effect"),
                                                                 music ? "Music" : "Sound effects");
            auto* raw = command.get();
            const bool ok = self->apply(std::move(command));
            if (ok)
                self->setSelectedClip(double(raw->clipId()));
            self->endGeneration(job, result, ok);
        });
    if (!started)
        reportError(tr("Another AI job is running"));
    return started;
}

// --- Screen recording ---

namespace {

bool testScreen()
{
    return qgetenv("TVE_SCREEN_SOURCE") == "test";
}

bool testCamera()
{
    return qgetenv("TVE_CAMERA_SOURCE") == "test";
}

QString accessName(CaptureAccess access)
{
    return access == CaptureAccess::Granted ? QStringLiteral("granted")
         : access == CaptureAccess::Denied  ? QStringLiteral("denied")
                                            : QStringLiteral("undetermined");
}

} // namespace

QString ProjectController::screenAccessName() const
{
    return testScreen() ? QStringLiteral("granted") : accessName(tve::screenAccess());
}

QString ProjectController::cameraAccessName() const
{
    return testCamera() ? QStringLiteral("granted") : accessName(tve::cameraAccess());
}

void ProjectController::requestScreenAccess()
{
    if (!testScreen())
        tve::requestScreenAccess();
    emit screenRecordingChanged();
}

void ProjectController::requestCameraAccess()
{
    if (testCamera())
        return;
    QPointer<ProjectController> self(this);
    tve::requestCameraAccess([self](bool) {
        QMetaObject::invokeMethod(qApp, [self] {
            if (self)
                emit self->screenRecordingChanged();
        }, Qt::QueuedConnection);
    });
}

QVariantList ProjectController::listScreenTargets()
{
    QVariantList list;
    if (testScreen()) {
        list << QVariantMap{{QStringLiteral("kind"), QStringLiteral("display")}, {QStringLiteral("id"), 1},
                            {QStringLiteral("title"), QStringLiteral("Test display (1280 × 720)")},
                            {QStringLiteral("width"), 1280}, {QStringLiteral("height"), 720}};
        return list;
    }
    if (tve::screenAccess() != CaptureAccess::Granted)
        return list;
    for (const ScreenTarget& t : tve::screenTargets())
        list << QVariantMap{{QStringLiteral("kind"), t.kind == ScreenTarget::Kind::Display ? QStringLiteral("display")
                                                                                         : QStringLiteral("window")},
                            {QStringLiteral("id"), double(t.id)},
                            {QStringLiteral("title"), QString::fromStdString(t.title)},
                            {QStringLiteral("x"), t.x},
                            {QStringLiteral("y"), t.y},
                            {QStringLiteral("width"), t.width},
                            {QStringLiteral("height"), t.height}};
    return list;
}

QVariantList ProjectController::listCameras()
{
    QVariantList list;
    if (testCamera()) {
        list << QVariantMap{{QStringLiteral("id"), QStringLiteral("test")}, {QStringLiteral("name"), QStringLiteral("Test camera")}};
        return list;
    }
    for (const CameraDevice& c : tve::cameras())
        list << QVariantMap{{QStringLiteral("id"), QString::fromStdString(c.id)}, {QStringLiteral("name"), QString::fromStdString(c.name)}};
    return list;
}

Teleprompter* ProjectController::teleprompter() const
{
    // One for the app: every editor (and the source monitor's controller) shares the script.
    static QPointer<Teleprompter> shared;
    if (!shared)
        shared = new Teleprompter(qApp);
    return shared;
}

void ProjectController::keepOutOfRecordings(QObject* object, bool keep)
{
    auto* window = qobject_cast<QWindow*>(object);
    if (!window)
        return;
    std::erase_if(m_keptOut, [&](const QPointer<QWindow>& w) { return !w || w == window; });
    if (keep)
        m_keptOut.emplace_back(window);
}

QVariantList ProjectController::recordingExclusions() const
{
    QVariantList numbers;
    for (const QPointer<QWindow>& window : m_keptOut)
        if (window && window->isVisible())
            numbers << double(nativeWindowNumber(window));
    return numbers;
}

bool ProjectController::startScreenRecording(const QVariantMap& options)
{
    if (screenRecording() || recording()) {
        reportError(tr("Something is being recorded already"));
        return false;
    }
    const QVariantMap screen = options.value(QStringLiteral("screen")).toMap();
    const QString cameraId = options.value(QStringLiteral("camera")).toString();
    m_cameraBackground = options.value(QStringLiteral("cameraBackground")).toString();
    if (screen.isEmpty() && cameraId.isEmpty()) {
        reportError(tr("Choose a screen or a camera to record"));
        return false;
    }
    const int rate = std::clamp(options.value(QStringLiteral("frameRate"), 30).toInt(), 10, 60);
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH.mm.ss"));
    const QDir folder(recordingsDirectory());
    std::string error;
    if (!screen.isEmpty()) {
        std::unique_ptr<CaptureSource> source;
        if (testScreen()) {
            source = tve::makeTestSource(1280, 720, rate, options.value(QStringLiteral("systemSound")).toBool());
        } else {
            if (tve::screenAccess() != CaptureAccess::Granted) {
                reportError(tr("Allow screen recording in System Settings first"));
                return false;
            }
            ScreenTarget target;
            target.kind = screen.value(QStringLiteral("kind")).toString() == QLatin1String("window") ? ScreenTarget::Kind::Window
                                                                                                    : ScreenTarget::Kind::Display;
            target.id = std::uint32_t(screen.value(QStringLiteral("id")).toDouble());
            target.x = screen.value(QStringLiteral("x")).toDouble();
            target.y = screen.value(QStringLiteral("y")).toDouble();
            target.width = screen.value(QStringLiteral("width")).toDouble();
            target.height = screen.value(QStringLiteral("height")).toDouble();
            std::optional<std::array<double, 4>> region;
            if (options.contains(QStringLiteral("region"))) {
                const QVariantMap r = options.value(QStringLiteral("region")).toMap();
                region = std::array<double, 4>{r.value(QStringLiteral("x")).toDouble(), r.value(QStringLiteral("y")).toDouble(),
                                               r.value(QStringLiteral("width")).toDouble(), r.value(QStringLiteral("height")).toDouble()};
            }
            std::vector<std::uint32_t> keptOut;
            for (const QVariant& number : recordingExclusions())
                if (number.toDouble() > 0)
                    keptOut.push_back(std::uint32_t(number.toDouble()));
            source = tve::makeScreenSource(target, region, options.value(QStringLiteral("systemSound")).toBool(), rate,
                                           std::move(keptOut));
        }
        m_screenRecorder = std::make_unique<CaptureRecorder>(std::move(source), rate);
        if (!m_screenRecorder->start(folder.filePath(QStringLiteral("Screen %1.mov").arg(stamp)).toStdString(), &error)) {
            m_screenRecorder.reset();
            reportError(tr("The screen recording could not start: %1").arg(coreText(error)));
            return false;
        }
        if (const auto area = m_screenRecorder->source().screenArea()) {
            m_cursorLog = std::make_unique<CursorLog>(*area, testScreen());
            m_cursorLog->start();
        }
    }
    if (!cameraId.isEmpty()) {
        if (!testCamera() && tve::cameraAccess() != CaptureAccess::Granted) {
            stopScreenRecording();
            reportError(tr("Allow the camera in System Settings first"));
            return false;
        }
        auto source = testCamera() ? tve::makeTestSource(640, 360, rate, false) : tve::makeCameraSource(cameraId.toStdString(), rate);
        m_cameraRecorder = std::make_unique<CaptureRecorder>(std::move(source), rate);
        if (!m_cameraRecorder->start(folder.filePath(QStringLiteral("Camera %1.mov").arg(stamp)).toStdString(), &error)) {
            m_cameraRecorder.reset();
            const QString message = tr("The camera recording could not start: %1").arg(coreText(error));
            if (m_screenRecorder)
                stopScreenRecording();
            reportError(message);
            return false;
        }
    }
    if (options.value(QStringLiteral("microphone")).toBool()) {
        m_screenMicrophone = std::make_unique<AudioRecorder>(inputBackend(), m_inputDevice.toStdString());
        if (!m_screenMicrophone->isOpen()
            || !m_screenMicrophone->start(folder.filePath(QStringLiteral("Microphone %1.wav").arg(stamp)).toStdString(), &error))
            m_screenMicrophone.reset(); // recorded without it
    }
    m_screenRecordingStart = ticks(m_playhead);
    m_screenRecordingTime = 0;
    if (!m_screenRecordingClock) {
        m_screenRecordingClock = new QTimer(this);
        m_screenRecordingClock->setInterval(100);
        connect(m_screenRecordingClock, &QTimer::timeout, this, [this] {
            const CaptureRecorder* r = m_screenRecorder ? m_screenRecorder.get() : m_cameraRecorder.get();
            m_screenRecordingTime = r ? seconds(r->elapsed()) : 0.0;
            emit screenRecordingTimeChanged();
        });
    }
    m_screenRecordingClock->start();
    emit screenRecordingChanged();
    return true;
}

bool ProjectController::stopScreenRecording()
{
    if (!screenRecording())
        return false;
    if (m_screenRecordingClock)
        m_screenRecordingClock->stop();
    std::vector<RecordedTake> takes;
    QStringList problems;
    const auto takeOf = [&](const std::string& path, TrackKind kind, const char* name) -> std::optional<RecordedTake> {
        QString why;
        auto item = probeMedia(path, &why);
        if (!item) {
            problems << why;
            return std::nullopt;
        }
        item->fingerprint = fingerprintFile(path).value_or(std::string());
        RecordedTake take;
        take.item = std::move(*item);
        take.kind = kind;
        take.trackName = name;
        return take;
    };
    std::string error;
    CursorEffects cursor;
    if (m_cursorLog)
        cursor = m_cursorLog->stop();
    if (m_screenRecorder) {
        if (const auto t = m_screenRecorder->stop(&error)) {
            if (auto take = takeOf(t->path, TrackKind::Video, "Screen")) {
                take->cursor = cursor;
                takes.push_back(std::move(*take));
            }
        } else {
            problems << coreText(error);
        }
    }
    if (m_cameraRecorder) {
        if (const auto t = m_cameraRecorder->stop(&error)) {
            if (auto take = takeOf(t->path, TrackKind::Video, "Camera")) {
                // Its background as asked: blurred, or removed (the person over the screen).
                if (m_cameraBackground == QLatin1String("blur"))
                    take->cutout = PortraitCutout{.enabled = true, .softness = 0.3, .blur = 0.6};
                else if (m_cameraBackground == QLatin1String("remove"))
                    take->cutout = PortraitCutout{.enabled = true, .softness = 0.3};
                // Over a screen: small, in the bottom right corner.
                if (m_screenRecorder) {
                    take->visual.transform.scale = 0.3;
                    take->visual.transform.x = 0.33;
                    take->visual.transform.y = 0.33;
                }
                takes.push_back(std::move(*take));
            }
        } else {
            problems << coreText(error);
        }
    }
    if (m_screenMicrophone) {
        if (const auto t = m_screenMicrophone->stop(&error))
            if (auto take = takeOf(t->path, TrackKind::Audio, "Microphone"))
                takes.push_back(std::move(*take));
    }
    m_screenRecorder.reset();
    m_cameraRecorder.reset();
    m_cursorLog.reset();
    m_screenMicrophone.reset();
    emit screenRecordingChanged();
    if (takes.empty()) {
        reportError(tr("Nothing was recorded: %1").arg(problems.join(QStringLiteral("; "))));
        return false;
    }
    const bool screen = std::ranges::any_of(takes, [](const RecordedTake& t) { return t.trackName == "Screen"; });
    auto command = std::make_unique<AddRecordingCommand>(std::move(takes), m_screenRecordingStart,
                                                         screen ? TVE_TR("core", "Record Screen") : TVE_TR("core", "Record Camera"));
    auto* raw = command.get();
    if (!apply(std::move(command)))
        return false;
    setSelectedClip(double(raw->clipIds().front()));
    return true;
}

bool ProjectController::setCursorEffects(double clipId, const QVariantMap& effects)
{
    const Clip* clip = findClip(toId(clipId));
    if (!clip || !clip->cursor.hasLog()) {
        reportError(tr("Select a screen recording"));
        return false;
    }
    CursorEffects cursor = clip->cursor;
    cursor.rings = effects.value(QStringLiteral("rings"), cursor.rings).toBool();
    cursor.highlight = effects.value(QStringLiteral("highlight"), cursor.highlight).toBool();
    cursor.spotlight = effects.value(QStringLiteral("spotlight"), cursor.spotlight).toBool();
    cursor.size = effects.value(QStringLiteral("size"), cursor.size).toDouble();
    return apply(std::make_unique<SetClipCursorCommand>(clip->id, std::move(cursor)));
}

bool ProjectController::zoomOnClicks(double clipId, const QVariantMap& options)
{
    const Clip* clip = findClip(toId(clipId));
    if (!clip || !clip->cursor.hasLog() || clip->cursor.clicks.empty()) {
        reportError(tr("Select a screen recording with clicks"));
        return false;
    }
    if (clip->reversed || clip->freeze || !clip->speedRamp.empty()) {
        reportError(tr("Zooming on clicks works on clips playing forwards at a steady speed"));
        return false;
    }
    const double zoom = std::clamp(options.value(QStringLiteral("zoom"), 1.8).toDouble(), 1.2, 3.0);
    const Ticks hold = ticks(std::clamp(options.value(QStringLiteral("hold"), 1.2).toDouble(), 0.3, 5.0));
    const Ticks ease = kTicksPerSecond * 3 / 10;
    const double base = clip->visual.transform.scale;
    // At each click: zoom in on the cursor over `ease`, stay `hold`, zoom out over `ease`
    // (clicks close together share one zoom, moving between them).
    Keyframes xs, ys, scales;
    const auto clipTime = [&](Ticks media) { return Ticks(std::llround(double(media - clip->sourceIn) / clip->speed)); };
    const auto centred = [&](double c) { return std::clamp(-(c - 0.5) * zoom, -(zoom - 1) / 2, (zoom - 1) / 2) * base; };
    Ticks lastOut = std::numeric_limits<Ticks>::min();
    for (const Ticks click : clip->cursor.clicks) {
        const Ticks at = clipTime(click);
        if (at < 0 || at > clip->duration)
            continue;
        const auto where = clip->cursor.at(click);
        const Ticks in = std::max<Ticks>(0, at - ease);
        if (in > lastOut) {
            xs.push_back({in, clip->visual.transform.x, Easing::EaseInOut});
            ys.push_back({in, clip->visual.transform.y, Easing::EaseInOut});
            scales.push_back({in, base, Easing::EaseInOut});
        }
        xs.push_back({at, clip->visual.transform.x + centred(where->x), Easing::Linear});
        ys.push_back({at, clip->visual.transform.y + centred(where->y), Easing::Linear});
        scales.push_back({at, base * zoom, Easing::Linear});
        const Ticks out = std::min(clip->duration, at + hold);
        lastOut = std::min(clip->duration, out + ease);
        // Held, then back out (unless the next click comes first: it continues from here).
        for (Keyframes* k : {&xs, &ys, &scales}) {
            Keyframe held = k->back();
            held.time = out;
            held.easing = Easing::EaseInOut;
            if (held.time > k->back().time)
                k->push_back(held);
        }
        xs.push_back({lastOut, clip->visual.transform.x, Easing::Linear});
        ys.push_back({lastOut, clip->visual.transform.y, Easing::Linear});
        scales.push_back({lastOut, base, Easing::Linear});
    }
    // Overlapping zooms: later keyframes win, earlier ones at or after them go.
    const auto tidy = [](Keyframes& k) {
        Keyframes out;
        for (const Keyframe& f : k) {
            while (!out.empty() && out.back().time >= f.time)
                out.pop_back();
            out.push_back(f);
        }
        k = std::move(out);
    };
    tidy(xs);
    tidy(ys);
    tidy(scales);
    if (xs.empty()) {
        reportError(tr("No clicks within the clip"));
        return false;
    }
    return apply(std::make_unique<SetClipKeyframesCommand>(clip->id,
                                                           std::map<std::string, Keyframes>{{std::string(tve::property::kX), xs},
                                                                                            {std::string(tve::property::kY), ys},
                                                                                            {std::string(tve::property::kScale), scales}},
                                                           TVE_TR("core", "Zoom on Clicks")));
}

bool ProjectController::addClickSounds(double clipId)
{
    const Clip* clip = findClip(toId(clipId));
    if (!clip || clip->cursor.clicks.empty()) {
        reportError(tr("Select a screen recording with clicks"));
        return false;
    }
    // The click: 40 ms of a short, bright tick (made once, kept with the recordings).
    const QString path = QDir(recordingsDirectory()).filePath(QStringLiteral("Click.wav"));
    if (!QFileInfo::exists(path)) {
        QDir().mkpath(recordingsDirectory());
        auto writer = WavWriter::open(path.toStdString(), audio::kSampleRate, 1);
        if (!writer) {
            reportError(tr("The click sound could not be made"));
            return false;
        }
        std::vector<float> tick(std::size_t(audio::kSampleRate) / 25);
        for (std::size_t i = 0; i < tick.size(); ++i) {
            const double t = double(i) / audio::kSampleRate;
            tick[i] = float(0.6 * std::exp(-t * 180.0) * std::sin(2.0 * std::numbers::pi * 2400.0 * t)
                            + 0.3 * std::exp(-t * 400.0) * std::sin(2.0 * std::numbers::pi * 5200.0 * t));
        }
        writer->write(tick.data(), std::int64_t(tick.size()));
        writer->finish();
    }
    QString why;
    auto item = probeMedia(path.toStdString(), &why);
    if (!item) {
        reportError(why);
        return false;
    }
    std::vector<Ticks> times;
    for (const Ticks click : clip->cursor.clicks) {
        const Ticks at = clip->start + Ticks(std::llround(double(click - clip->sourceIn) / clip->speed));
        if (at >= clip->start && at < clip->end())
            times.push_back(at);
    }
    if (times.empty()) {
        reportError(tr("No clicks within the clip"));
        return false;
    }
    return apply(std::make_unique<AddClickSoundsCommand>(std::move(*item), std::move(times)));
}

void ProjectController::openMicrophoneSettings()
{
    QDesktopServices::openUrl(
        QUrl(QStringLiteral("x-apple.systempreferences:com.apple.preference.security?Privacy_Microphone")));
}

void ProjectController::updateInput()
{
    const double level = m_recorder ? double(m_recorder->peak()) : 0.0;
    const double time = recording() ? seconds(m_recorder->recordedDuration()) : m_recordingTime;
    if (level != m_inputLevel || time != m_recordingTime) {
        m_inputLevel = level;
        m_recordingTime = time;
        emit inputLevelChanged();
    }
}

// --- Helpers ---

Id ProjectController::mainTrackId() const
{
    for (const Track& track : m_project.tracks) {
        if (track.kind == TrackKind::Video)
            return track.id; // the lowest video track is the main (magnetic) track
    }
    return kNoId;
}

Ticks ProjectController::frameTicks() const
{
    return frameDuration(m_project.settings.frameRate);
}

double ProjectController::snap(double value, double tolerance, double ignoreClipId) const
{
    const Id ignore = toId(ignoreClipId);
    std::vector<Ticks> points{0, ticks(m_playhead)};
    for (const tve::Marker& marker : m_project.markers) {
        points.push_back(marker.time);
        if (marker.duration > 0)
            points.push_back(marker.time + marker.duration);
    }
    for (const Track& track : m_project.tracks) {
        for (const Clip& clip : track.clips) {
            if (clip.id == ignore)
                continue;
            points.push_back(clip.start);
            points.push_back(clip.end());
        }
    }
    double best = value;
    double bestDistance = tolerance;
    for (Ticks point : points) {
        const double distance = std::abs(seconds(point) - value);
        if (distance <= bestDistance) {
            best = seconds(point);
            bestDistance = distance;
        }
    }
    return best;
}

QString ProjectController::timecode(double value) const
{
    const Ticks t = std::max<Ticks>(ticks(value), 0);
    const Ticks frame = frameTicks();
    const long long totalFrames = (t + frame / 2) / frame;
    const long long fps = std::llround(frameRate());
    const long long ff = totalFrames % fps;
    const long long totalSeconds = totalFrames / fps;
    // In the language's digits.
    const QLocale locale;
    const auto two = [&](long long n) { return locale.toString(n).rightJustified(2, locale.zeroDigit().at(0)); };
    return QStringLiteral("%1:%2:%3:%4").arg(two(totalSeconds / 3600), two((totalSeconds / 60) % 60), two(totalSeconds % 60), two(ff));
}

// --- Mixer and audio effects ---

namespace {

QVariantList levelList(const ChannelLevels& l)
{
    return {double(l.peak[0]), double(l.peak[1]), double(l.rms[0]), double(l.rms[1])};
}

QVariant lufs(double value)
{
    return std::isfinite(value) ? QVariant(value) : QVariant();
}

} // namespace

void ProjectController::updateMixLevels(Ticks now)
{
    QVariantMap levels;
    if (m_playback->clock().playing()) {
        if (const auto mix = m_playback->audio()->levelsAt(now)) {
            QVariantMap tracks;
            for (const auto& [id, l] : mix->tracks)
                tracks.insert(QString::number(id), levelList(l));
            levels = {{QStringLiteral("tracks"), tracks},
                      {QStringLiteral("master"), levelList(mix->master)},
                      {QStringLiteral("momentary"), lufs(mix->momentary)},
                      {QStringLiteral("shortTerm"), lufs(mix->shortTerm)},
                      {QStringLiteral("integrated"), lufs(mix->integrated)}};
        }
    }
    if (levels != m_mixLevels) {
        m_mixLevels = levels;
        emit mixLevelsChanged();
    }
}

QVariantList ProjectController::audioEffects() const
{
    // In the library's order of categories, then by title.
    static const std::vector<std::string> order{"eq", "dynamics", "space", "echo", "modulation", "pitch", "voice", "cleanup"};
    std::vector<const EffectManifest*> sorted;
    for (const EffectManifest& m : AudioEffectLibrary::builtIn().effects())
        sorted.push_back(&m);
    const auto rank = [](const EffectManifest* m) {
        return m->categories.empty() ? int(order.size()) : int(std::ranges::find(order, m->categories.front()) - order.begin());
    };
    std::ranges::stable_sort(sorted, [&](const EffectManifest* a, const EffectManifest* b) {
        return std::pair(rank(a), a->displayTitle()) < std::pair(rank(b), b->displayTitle());
    });
    QVariantList list;
    for (const EffectManifest* m : sorted)
        list << describeEffect(*m);
    return list;
}

bool ProjectController::setTrackMix(double trackId, const QVariantMap& changes)
{
    const Track* track = m_project.findTrack(toId(trackId));
    if (!track)
        return false;
    TrackMix mix{track->gainDb, track->pan, track->solo};
    if (changes.contains(QStringLiteral("gainDb")))
        mix.gainDb = float(changes.value(QStringLiteral("gainDb")).toDouble());
    if (changes.contains(QStringLiteral("pan")))
        mix.pan = float(changes.value(QStringLiteral("pan")).toDouble());
    if (changes.contains(QStringLiteral("solo")))
        mix.solo = changes.value(QStringLiteral("solo")).toBool();
    return apply(std::make_unique<SetTrackMixCommand>(track->id, mix));
}

bool ProjectController::addAudioEffect(double clipId, const QString& effectId)
{
    const Clip* clip = findClip(toId(clipId));
    if (!clip) {
        reportError(tr("Select a clip with sound"));
        return false;
    }
    const bool plugin = isPluginEffect(effectId.toStdString());
    if (!plugin && !AudioEffectLibrary::builtIn().find(effectId.toStdString())) {
        reportError(tr("Unknown audio effect %1").arg(effectId));
        return false;
    }
    auto effects = clip->audioEffects;
    effects.push_back(ClipEffect{.effectId = effectId.toStdString()});
    return apply(std::make_unique<SetClipAudioEffectsCommand>(clip->id, std::move(effects), plugin ? TVE_TR("core", "Add Plug-in") : TVE_TR("core", "Add Audio Effect")));
}

bool ProjectController::removeAudioEffect(double clipId, int index)
{
    const Clip* clip = findClip(toId(clipId));
    if (!clip || index < 0 || index >= int(clip->audioEffects.size()))
        return false;
    auto effects = clip->audioEffects;
    effects.erase(effects.begin() + index);
    return apply(std::make_unique<SetClipAudioEffectsCommand>(clip->id, std::move(effects), TVE_TR("core", "Remove Audio Effect")));
}

bool ProjectController::setAudioEffectEnabled(double clipId, int index, bool enabled)
{
    const Clip* clip = findClip(toId(clipId));
    if (!clip || index < 0 || index >= int(clip->audioEffects.size()))
        return false;
    auto effects = clip->audioEffects;
    effects[std::size_t(index)].enabled = enabled;
    return apply(std::make_unique<SetClipAudioEffectsCommand>(clip->id, std::move(effects),
                                                              enabled ? TVE_TR("core", "Enable Audio Effect") : TVE_TR("core", "Disable Audio Effect")));
}

bool ProjectController::setAudioEffectParam(double clipId, int index, const QString& paramId, const QVariant& value)
{
    const Clip* clip = findClip(toId(clipId));
    if (!clip || index < 0 || index >= int(clip->audioEffects.size()))
        return false;
    const std::string& effectId = clip->audioEffects[std::size_t(index)].effectId;
    const bool plugin = isPluginEffect(effectId);
    const EffectManifest* manifest = plugin ? nullptr : AudioEffectLibrary::builtIn().find(effectId);
    const EffectParamSpec* param = manifest ? manifest->findParam(paramId.toStdString()) : nullptr;
    const auto components = componentsOf(value);
    if ((!plugin && !param) || components.empty()) {
        reportError(tr("Unknown setting %1").arg(paramId));
        return false;
    }
    const float clamped = plugin ? std::clamp(components.front(), 0.0f, 1.0f)
                                 : std::clamp(components.front(), float(param->min), float(param->max));
    // An animated one takes the value as a keyframe at the playhead.
    const std::string channel = effectChannelId(std::size_t(index), paramId.toStdString(), true);
    if (channelKeys(*clip, channel)) {
        Clip after = *clip;
        setKey(after, channel, clamped);
        return commitAnimation(*clip, after, plugin ? TVE_TR("core", "Change Plug-in") : TVE_TR("core", "Change Audio Effect"));
    }
    auto effects = clip->audioEffects;
    effects[std::size_t(index)].values[paramId.toStdString()] = {clamped};
    return apply(std::make_unique<SetClipAudioEffectsCommand>(clip->id, std::move(effects),
                                                              plugin ? TVE_TR("core", "Change Plug-in") : TVE_TR("core", "Change Audio Effect")));
}

// --- Track audio effects and plug-ins ---

const std::vector<ClipEffect>* ProjectController::audioStack(double clipId, double trackId) const
{
    if (clipId > 0) {
        const Clip* clip = const_cast<ProjectController*>(this)->findClip(toId(clipId));
        return clip ? &clip->audioEffects : nullptr;
    }
    const Track* track = m_project.findTrack(toId(trackId));
    return track ? &track->audioEffects : nullptr;
}

bool ProjectController::setAudioStack(double clipId, double trackId, std::vector<ClipEffect> effects, const char* label)
{
    if (clipId > 0)
        return apply(std::make_unique<SetClipAudioEffectsCommand>(toId(clipId), std::move(effects), label));
    return apply(std::make_unique<SetTrackAudioEffectsCommand>(toId(trackId), std::move(effects), label));
}

bool ProjectController::addTrackAudioEffect(double trackId, const QString& effectId)
{
    const auto* stack = audioStack(-1, trackId);
    const bool plugin = isPluginEffect(effectId.toStdString());
    if (!stack || (!plugin && !AudioEffectLibrary::builtIn().find(effectId.toStdString()))) {
        reportError(tr("Unknown audio effect %1").arg(effectId));
        return false;
    }
    auto effects = *stack;
    effects.push_back(ClipEffect{.effectId = effectId.toStdString()});
    return setAudioStack(-1, trackId, std::move(effects), plugin ? TVE_TR("core", "Add Track Plug-in") : TVE_TR("core", "Add Track Effect"));
}

bool ProjectController::removeTrackAudioEffect(double trackId, int index)
{
    const auto* stack = audioStack(-1, trackId);
    if (!stack || index < 0 || index >= int(stack->size()))
        return false;
    auto effects = *stack;
    effects.erase(effects.begin() + index);
    return setAudioStack(-1, trackId, std::move(effects), TVE_TR("core", "Remove Track Effect"));
}

bool ProjectController::setTrackAudioEffectEnabled(double trackId, int index, bool enabled)
{
    const auto* stack = audioStack(-1, trackId);
    if (!stack || index < 0 || index >= int(stack->size()))
        return false;
    auto effects = *stack;
    effects[std::size_t(index)].enabled = enabled;
    return setAudioStack(-1, trackId, std::move(effects), enabled ? TVE_TR("core", "Enable Track Effect") : TVE_TR("core", "Disable Track Effect"));
}

bool ProjectController::setTrackAudioEffectParam(double trackId, int index, const QString& paramId, const QVariant& value)
{
    const auto* stack = audioStack(-1, trackId);
    if (!stack || index < 0 || index >= int(stack->size()))
        return false;
    const std::string& effectId = (*stack)[std::size_t(index)].effectId;
    const bool plugin = isPluginEffect(effectId);
    const EffectManifest* manifest = plugin ? nullptr : AudioEffectLibrary::builtIn().find(effectId);
    const EffectParamSpec* param = manifest ? manifest->findParam(paramId.toStdString()) : nullptr;
    const auto components = componentsOf(value);
    if ((!plugin && !param) || components.empty()) {
        reportError(tr("Unknown setting %1").arg(paramId));
        return false;
    }
    auto effects = *stack;
    effects[std::size_t(index)].values[paramId.toStdString()] = {
        plugin ? std::clamp(components.front(), 0.0f, 1.0f) : std::clamp(components.front(), float(param->min), float(param->max))};
    return setAudioStack(-1, trackId, std::move(effects), plugin ? TVE_TR("core", "Change Plug-in") : TVE_TR("core", "Change Track Effect"));
}

QString ProjectController::pluginKey(double clipId, double trackId, int index)
{
    return clipId > 0 ? QStringLiteral("clip:%1/%2").arg(qint64(clipId)).arg(index)
                      : QStringLiteral("track:%1/%2").arg(qint64(trackId)).arg(index);
}

const ClipEffect* ProjectController::audioEffectAt(const QString& key) const
{
    // "clip:<id>/<index>" or "track:<id>/<index>"
    const qsizetype colon = key.indexOf(QLatin1Char(':'));
    const qsizetype slash = key.indexOf(QLatin1Char('/'));
    if (colon < 0 || slash < colon)
        return nullptr;
    const double id = key.mid(colon + 1, slash - colon - 1).toDouble();
    const int index = key.mid(slash + 1).toInt();
    const bool clip = key.startsWith(QLatin1String("clip:"));
    const auto* stack = audioStack(clip ? id : -1, clip ? -1 : id);
    if (!stack || index < 0 || index >= int(stack->size()) || !isPluginEffect((*stack)[std::size_t(index)].effectId))
        return nullptr;
    return &(*stack)[std::size_t(index)];
}

namespace {

// The plug-ins found, shared by every editor in the app: looked for once (in the background),
// again only when asked. Audio (Audio Units, VST3) and video (OpenFX) apart.
struct PluginList {
    QVariantList plugins;
    bool scanned = false;
    bool scanning = false;
    std::vector<QPointer<ProjectController>> waiting;
};
PluginList& pluginList(bool video = false)
{
    static PluginList audio;
    static PluginList pictures;
    return video ? pictures : audio;
}

// The OpenFX plug-ins' descriptions, by id (for their parameters' ranges).
std::map<std::string, OfxPluginInfo>& videoPluginInfos()
{
    static std::map<std::string, OfxPluginInfo> infos;
    return infos;
}

// An OpenFX plug-in's parameters as the inspector's controls take them: a number each for
// 2D and 3D ones ("<name>#<i>").
QVariantList videoPluginControls(const OfxPluginInfo& info)
{
    QVariantList list;
    static const char* const kAxes[] = {" X", " Y", " Z"};
    for (const OfxParameterInfo& p : info.parameters) {
        const QString name = QString::fromStdString(p.name);
        const QString label = QString::fromStdString(p.label);
        const auto value = [&](int i) { return i < int(p.defaults.size()) ? p.defaults[std::size_t(i)] : 0.0; };
        if (p.type == "rgb" || p.type == "rgba") {
            QVariantList colour;
            for (int i = 0; i < p.components; ++i)
                colour << value(i);
            if (p.components == 3)
                colour << 1.0;
            list << QVariantMap{{QStringLiteral("id"), name}, {QStringLiteral("title"), label},
                                {QStringLiteral("type"), QStringLiteral("color")}, {QStringLiteral("default"), colour},
                                {QStringLiteral("min"), 0.0}, {QStringLiteral("max"), 1.0}};
        } else if (p.type == "bool") {
            list << QVariantMap{{QStringLiteral("id"), name}, {QStringLiteral("title"), label},
                                {QStringLiteral("type"), QStringLiteral("bool")}, {QStringLiteral("default"), value(0)},
                                {QStringLiteral("min"), 0.0}, {QStringLiteral("max"), 1.0}};
        } else if (p.type == "choice") {
            QStringList options;
            for (const std::string& o : p.options)
                options << QString::fromStdString(o);
            list << QVariantMap{{QStringLiteral("id"), name}, {QStringLiteral("title"), label},
                                {QStringLiteral("type"), QStringLiteral("enum")}, {QStringLiteral("values"), options},
                                {QStringLiteral("default"), value(0)}, {QStringLiteral("min"), 0.0},
                                {QStringLiteral("max"), double(std::max<std::size_t>(1, p.options.size()) - 1)}};
        } else {
            const bool integer = p.type.starts_with("int");
            for (int i = 0; i < p.components; ++i)
                list << QVariantMap{{QStringLiteral("id"), p.components == 1 ? name : name + QStringLiteral("#%1").arg(i)},
                                    {QStringLiteral("title"), p.components == 1 ? label : label + QLatin1String(kAxes[std::min(i, 2)])},
                                    {QStringLiteral("type"), integer ? QStringLiteral("int") : QStringLiteral("float")},
                                    {QStringLiteral("min"), p.min}, {QStringLiteral("max"), p.max},
                                    {QStringLiteral("default"), value(i)}, {QStringLiteral("keyframable"), p.animates}};
        }
    }
    return list;
}

} // namespace

QVariantList ProjectController::audioPlugins()
{
    // The first read starts the look (after this read, not during it); an editor made while
    // it's under way hears when it ends too.
    if (!pluginList().scanned && !m_scanningPlugins)
        QTimer::singleShot(0, this, [this] {
            if (!pluginList().scanned)
                rescanAudioPlugins();
        });
    return pluginList().plugins;
}

QVariantList ProjectController::videoPlugins()
{
    return audioPlugins().isEmpty() && !pluginList(true).scanned ? QVariantList() : pluginList(true).plugins;
}

void ProjectController::rescanAudioPlugins()
{
    // Both kinds, in the background: checking a new bundle runs it in a process of its own.
    PluginList& audio = pluginList();
    PluginList& video = pluginList(true);
    audio.waiting.push_back(this);
    m_scanningPlugins = true;
    emit audioPluginsChanged();
    if (audio.scanning)
        return;
    audio.scanning = video.scanning = true;
    std::thread([] {
        QVariantList sounds;
        for (const PluginDescription& p : listPlugins())
            sounds << QVariantMap{{QStringLiteral("id"), QString::fromStdString(p.id)},
                                  {QStringLiteral("name"), QString::fromStdString(p.name)},
                                  {QStringLiteral("vendor"), QString::fromStdString(p.vendor)},
                                  {QStringLiteral("format"), QString::fromStdString(p.format)},
                                  {QStringLiteral("version"), QString::fromStdString(p.version)},
                                  {QStringLiteral("problem"), QString::fromStdString(p.problem)}};
        const std::vector<OfxPluginInfo> found = listOfxPlugins();
        QVariantList pictures;
        for (const OfxPluginInfo& p : found)
            pictures << QVariantMap{{QStringLiteral("id"), QString::fromStdString(p.id)},
                                    {QStringLiteral("name"), QString::fromStdString(p.name)},
                                    {QStringLiteral("group"), QString::fromStdString(p.group)},
                                    {QStringLiteral("format"), QStringLiteral("OpenFX")},
                                    {QStringLiteral("metal"), p.metal},
                                    {QStringLiteral("problem"), QString::fromStdString(p.problem)},
                                    {QStringLiteral("params"), videoPluginControls(p)}};
        QMetaObject::invokeMethod(qApp, [sounds, pictures, found] {
            for (const OfxPluginInfo& p : found)
                videoPluginInfos()[p.id] = p;
            PluginList& audio = pluginList();
            PluginList& video = pluginList(true);
            audio.plugins = sounds;
            video.plugins = pictures;
            audio.scanned = video.scanned = true;
            audio.scanning = video.scanning = false;
            const auto waiting = std::exchange(audio.waiting, {});
            for (const QPointer<ProjectController>& controller : waiting) {
                if (!controller)
                    continue;
                controller->m_scanningPlugins = false;
                emit controller->audioPluginsChanged();
                emit controller->selectedClipInfoChanged();
            }
        });
    }).detach();
}

QVariantList ProjectController::pluginParameters(double clipId, double trackId, int index)
{
    const QString key = pluginKey(clipId, trackId, index);
    const ClipEffect* effect = audioEffectAt(key);
    return effect ? m_pluginHost->parameters(key, *effect) : QVariantList();
}

QString ProjectController::pluginValueText(double clipId, double trackId, int index, const QString& paramId, double value)
{
    const QString key = pluginKey(clipId, trackId, index);
    const ClipEffect* effect = audioEffectAt(key);
    return effect ? m_pluginHost->valueText(key, *effect, paramId, value) : QString();
}

bool ProjectController::openPluginWindow(double clipId, double trackId, int index)
{
    const QString key = pluginKey(clipId, trackId, index);
    const ClipEffect* effect = audioEffectAt(key);
    if (!effect) {
        reportError(tr("That isn't a plug-in"));
        return false;
    }
    QString name = QString::fromStdString(effect->effectId);
    for (const QVariant& p : pluginList().plugins)
        if (p.toMap().value(QStringLiteral("id")).toString() == name)
            name = p.toMap().value(QStringLiteral("name")).toString();
    QString where;
    if (clipId > 0) {
        const QVariantMap info = selectedClipInfo();
        where = info.value(QStringLiteral("id")).toDouble() == clipId ? info.value(QStringLiteral("label")).toString() : QString();
    } else if (const Track* track = m_project.findTrack(toId(trackId))) {
        where = QString::fromStdString(track->name);
    }
    QString error;
    const bool opened = m_pluginHost->openWindow(key, *effect, where.isEmpty() ? name : name + QStringLiteral(" — ") + where, &error);
    if (!opened && !error.isEmpty())
        reportError(error);
    return opened;
}

bool ProjectController::isPluginWindowOpen(double clipId, double trackId, int index) const
{
    return m_pluginHost->isWindowOpen(pluginKey(clipId, trackId, index));
}

void ProjectController::closePluginWindow(double clipId, double trackId, int index)
{
    m_pluginHost->closeWindow(pluginKey(clipId, trackId, index));
}

bool ProjectController::editPluginLive(double clipId, double trackId, int index, const QString& paramId, double value)
{
    const QString key = pluginKey(clipId, trackId, index);
    const ClipEffect* effect = audioEffectAt(key);
    return effect && m_pluginHost->editLive(key, *effect, paramId, value);
}

void ProjectController::pluginEdited(const QString& key, const QString& state)
{
    // Its whole state, in place of single values (its animated ones stay animated).
    const ClipEffect* effect = audioEffectAt(key);
    if (!effect)
        return;
    const bool clip = key.startsWith(QLatin1String("clip:"));
    const double id = key.mid(key.indexOf(QLatin1Char(':')) + 1, key.indexOf(QLatin1Char('/')) - key.indexOf(QLatin1Char(':')) - 1).toDouble();
    const int index = key.mid(key.indexOf(QLatin1Char('/')) + 1).toInt();
    auto effects = *audioStack(clip ? id : -1, clip ? -1 : id);
    ClipEffect& changed = effects[std::size_t(index)];
    changed.state = state.toStdString();
    std::erase_if(changed.values, [&](const auto& entry) { return !changed.keyframes.contains(entry.first); });
    setAudioStack(clip ? id : -1, clip ? -1 : id, std::move(effects), TVE_TR("core", "Adjust Plug-in"));
}

bool ProjectController::setKeepPitch(double clipId, bool keep)
{
    const Clip* clip = findClip(toId(clipId));
    return clip && apply(std::make_unique<SetClipKeepPitchCommand>(clip->id, keep));
}

bool ProjectController::normalizeLoudness(double clipId, double targetLufs)
{
    const auto location = m_project.locateClip(toId(clipId));
    if (!location) {
        reportError(tr("Select a clip with sound"));
        return false;
    }
    const Clip& clip = *location->clip;
    const MediaItem* media = m_project.findMedia(clip.mediaId);
    if (clip.generated() || !media || !media->hasAudio
        || (location->track->kind == TrackKind::Video && !clip.audioEnabled)) {
        reportError(tr("This clip has no sound of its own"));
        return false;
    }
    if (clip.keyframesOf(property::kVolume)) {
        reportError(tr("The volume is animated: remove its keyframes to normalize"));
        return false;
    }
    if (m_measuringLoudness)
        return false;
    // The clip alone at 0 dB: what its own volume then has to make up for.
    auto alone = soloProject(clip);
    const Id id = clip.id;
    const Ticks length = clip.duration;
    m_measuringLoudness = true;
    emit measuringLoudnessChanged();
    QPointer<ProjectController> self(this);
    std::thread([self, alone, id, length, targetLufs] {
        const double measured = measureLoudness(alone, 0, length);
        QMetaObject::invokeMethod(qApp, [self, id, measured, targetLufs] {
            if (!self)
                return;
            self->m_measuringLoudness = false;
            emit self->measuringLoudnessChanged();
            const Clip* clip = self->findClip(id);
            if (!clip)
                return;
            if (!std::isfinite(measured)) {
                self->reportError(tr("This clip is silent"));
                emit self->loudnessMeasured(double(id), std::nan(""));
                return;
            }
            const float gain = std::clamp(float(targetLufs - measured), kMinGainDb, kMaxGainDb);
            ClipAudio audio{gain, clip->fadeIn, clip->fadeOut};
            std::vector<std::unique_ptr<Command>> steps;
            steps.push_back(std::make_unique<SetClipAudioCommand>(clip->id, audio));
            self->m_loudness[id] = {measured, targetLufs};
            if (self->apply(std::make_unique<CompositeCommand>(std::move(steps), TVE_TR("core", "Normalize Loudness"))))
                emit self->loudnessMeasured(double(id), measured);
        }, Qt::QueuedConnection);
    }).detach();
    return true;
}

std::shared_ptr<Project> ProjectController::soloProject(const Clip& clip) const
{
    auto alone = std::make_shared<Project>(m_project);
    Clip copy = clip;
    copy.start = 0;
    copy.gainDb = 0.0f;
    copy.fadeIn = copy.fadeOut = 0;
    copy.transitionIn.reset();
    copy.audioEnabled = true;
    Track track;
    track.id = alone->allocateId();
    track.kind = TrackKind::Audio;
    track.clips = {copy};
    alone->tracks = {track};
    return alone;
}

bool ProjectController::hasOwnSound(const Clip& clip, TrackKind trackKind) const
{
    const MediaItem* media = m_project.findMedia(clip.mediaId);
    return !clip.generated() && media && media->hasAudio && (trackKind == TrackKind::Audio || clip.audioEnabled);
}

int ProjectController::clipsWithSound(const QVariantList& clipIds) const
{
    int count = 0;
    for (const QVariant& id : clipIds) {
        const auto location = const_cast<Project&>(m_project).locateClip(toId(id.toDouble()));
        if (location && hasOwnSound(*location->clip, location->track->kind))
            ++count;
    }
    return count;
}

int ProjectController::changeClipsGain(const QVariantList& clipIds, double deltaDb, bool absolute)
{
    if (!std::isfinite(deltaDb))
        return 0;
    Project after = m_project;
    int changed = 0;
    int animated = 0;
    for (const QVariant& value : clipIds) {
        const auto location = after.locateClip(toId(value.toDouble()));
        if (!location || !hasOwnSound(*location->clip, location->track->kind))
            continue;
        Clip& clip = *location->clip;
        const auto clampGain = [](double db) { return std::clamp(db, double(kMinGainDb), double(kMaxGainDb)); };
        if (auto keys = clip.keyframes.find(std::string(property::kVolume)); keys != clip.keyframes.end() && !keys->second.empty()) {
            if (absolute) {
                ++animated;
                continue;
            }
            for (Keyframe& k : keys->second)
                k.value = clampGain(k.value + deltaDb);
        }
        const float before = clip.gainDb;
        clip.gainDb = float(clampGain(absolute ? deltaDb : before + deltaDb));
        ++changed;
    }
    if (animated > 0)
        reportError(animated == 1 ? tr("A clip with animated volume was left as it was")
                                  : tr("%1 clips with animated volume were left as they were").arg(animated));
    if (changed == 0 || after.tracks == m_project.tracks)
        return 0;
    if (!apply(std::make_unique<ApplyEditsCommand>(m_project, std::move(after), TVE_TR("core", "Change Volume"))))
        return 0;
    return changed;
}

bool ProjectController::normalizeClips(const QVariantList& clipIds, double targetLufs)
{
    if (m_measuringLoudness) {
        reportError(tr("A clip's loudness is already being measured"));
        return false;
    }
    struct Job {
        Id id;
        Ticks length;
        std::shared_ptr<Project> alone;
    };
    std::vector<Job> jobs;
    int left = 0;
    for (const QVariant& value : clipIds) {
        const auto location = m_project.locateClip(toId(value.toDouble()));
        if (!location || !hasOwnSound(*location->clip, location->track->kind))
            continue;
        if (location->clip->keyframesOf(property::kVolume)) {
            ++left;
            continue;
        }
        jobs.push_back({location->clip->id, location->clip->duration, soloProject(*location->clip)});
    }
    if (jobs.empty()) {
        reportError(tr("Select clips with sound to normalize"));
        return false;
    }
    m_measuringLoudness = true;
    emit measuringLoudnessChanged();
    QPointer<ProjectController> self(this);
    std::thread([self, jobs = std::move(jobs), targetLufs, left] {
        std::vector<std::pair<Id, double>> measured;
        for (const Job& job : jobs)
            measured.emplace_back(job.id, measureLoudness(job.alone, 0, job.length));
        QMetaObject::invokeMethod(qApp, [self, measured = std::move(measured), targetLufs, left]() mutable {
            if (!self)
                return;
            self->m_measuringLoudness = false;
            emit self->measuringLoudnessChanged();
            Project after = self->m_project;
            int changed = 0;
            int silent = 0;
            for (const auto& [id, lufs] : measured) {
                const auto location = after.locateClip(id);
                if (!location)
                    continue;
                if (!std::isfinite(lufs)) {
                    ++silent;
                    continue;
                }
                location->clip->gainDb = std::clamp(float(targetLufs - lufs), kMinGainDb, kMaxGainDb);
                self->m_loudness[id] = {lufs, targetLufs};
                ++changed;
            }
            if (changed > 0
                && !self->apply(std::make_unique<ApplyEditsCommand>(self->m_project, std::move(after),
                                                                    TVE_TR("core", "Normalize Loudness"))))
                changed = 0;
            emit self->clipsNormalized(changed, left + silent);
        }, Qt::QueuedConnection);
    }).detach();
    return true;
}

QVariantList ProjectController::textAnimations() const
{
    QVariantList list;
    for (const TextAnimationPreset& preset : textAnimationPresets()) {
        list << QVariantMap{{QStringLiteral("id"), QString::fromLatin1(preset.id.data(), qsizetype(preset.id.size()))},
                            {QStringLiteral("title"), coreText(std::string(preset.title))},
                            {QStringLiteral("loop"), preset.loop}};
    }
    return list;
}

// --- Transitions on every cut ---

std::vector<Id> ProjectController::transitionCuts() const
{
    std::vector<Id> cuts;
    for (const Track& track : m_project.tracks) {
        if (track.kind != TrackKind::Video || track.locked)
            continue;
        for (std::size_t i = 1; i < track.clips.size(); ++i) {
            const Clip& before = track.clips[i - 1];
            const Clip& clip = track.clips[i];
            if (before.end() == clip.start && !before.generated() && !clip.generated())
                cuts.push_back(clip.id);
        }
    }
    return cuts;
}

int ProjectController::applyTransitions(const std::function<std::string(std::size_t)>& choose, double duration,
                                        const char* label)
{
    const std::vector<Id> cuts = transitionCuts();
    if (cuts.empty()) {
        reportError(tr("There are no cuts between clips to put transitions on"));
        return 0;
    }
    std::vector<std::unique_ptr<Command>> steps;
    for (std::size_t i = 0; i < cuts.size(); ++i) {
        const Clip& clip = *findClip(cuts[i]);
        ClipTransition transition{choose(i)};
        if (clip.transitionIn)
            transition.alignment = clip.transitionIn->alignment; // keep where it sat
        if (duration > 0)
            transition.duration = ticks(duration);
        else if (clip.transitionIn)
            transition.duration = clip.transitionIn->duration;
        steps.push_back(std::make_unique<SetClipTransitionCommand>(clip.id, std::move(transition)));
    }
    if (!apply(std::make_unique<CompositeCommand>(std::move(steps), label)))
        return 0;
    return int(cuts.size());
}

int ProjectController::applyTransitionToAll(const QString& transitionId, double duration)
{
    const std::string id = transitionId.toStdString();
    if (!EffectLibrary::builtIn().findTransition(id)) {
        reportError(tr("Unknown transition %1").arg(transitionId));
        return 0;
    }
    return applyTransitions([&](std::size_t) { return id; }, duration, TVE_TR("core", "Apply Transition to All"));
}

int ProjectController::applyRandomTransitions(double duration)
{
    const auto& all = EffectLibrary::builtIn().transitions();
    if (all.empty())
        return 0;
    std::mt19937 random{std::random_device{}()};
    std::uniform_int_distribution<std::size_t> pick(0, all.size() - 1);
    // Neighbours differ by id (a pack's transition can share one with another in the list).
    const bool choice = std::ranges::any_of(all, [&](const auto& t) { return t.manifest.id != all.front().manifest.id; });
    std::string previous;
    return applyTransitions(
        [&](std::size_t) {
            std::size_t chosen = pick(random);
            while (choice && all[chosen].manifest.id == previous)
                chosen = pick(random);
            previous = all[chosen].manifest.id;
            return previous;
        },
        duration, TVE_TR("core", "Random Transitions"));
}

// --- Stickers ---

QVariantList ProjectController::stickers() const
{
    return stickers::catalog();
}

Id ProjectController::stickerMedia(const QString& stickerId)
{
    const QString path = stickers::pathOf(stickerId);
    if (path.isEmpty()) {
        reportError(tr("Unknown sticker %1").arg(stickerId));
        return kNoId;
    }
    // Its media, once (a resource path, not a URL).
    auto media = std::ranges::find(m_project.media, path.toStdString(), &MediaItem::path);
    if (media == m_project.media.end()) {
        const auto animation = stickers::open(path);
        if (!animation
            || !apply(std::make_unique<ImportMediaCommand>(MediaItem{.path = path.toStdString(), .duration = kStillDuration,
                                                                     .hasVideo = true, .hasAudio = false,
                                                                     .width = animation->width(),
                                                                     .height = animation->height(), .still = true,
                                                                     .alpha = true, .lottie = true})))
            return kNoId;
        media = std::ranges::find(m_project.media, path.toStdString(), &MediaItem::path);
    }
    return media->id;
}

bool ProjectController::addSticker(const QString& stickerId)
{
    // A clip at the playhead on the lowest free picture track above the main one (a new track
    // on top if none is free), small, in the middle.
    const Id media = stickerMedia(stickerId);
    return media != kNoId && placeOverlay(media, 0.4, "Stickers", TVE_TR("core", "Add Sticker"));
}

bool ProjectController::placeOverlay(Id mediaId, double scale, const std::string& trackName, const std::string& label)
{
    const MediaItem* media = m_project.findMedia(mediaId);
    if (!media)
        return false;
    const Ticks at = ticks(m_playhead);
    Id track = kNoId;
    const auto main = m_project.trackIndex(mainTrackId());
    for (std::size_t i = main ? *main + 1 : 0; i < m_project.tracks.size() && track == kNoId; ++i) {
        const Track& candidate = m_project.tracks[i];
        if (candidate.kind == TrackKind::Video && !candidate.locked && !candidate.hidden
            && !candidate.overlaps(at, media->duration))
            track = candidate.id;
    }
    if (track == kNoId) {
        auto added = std::make_unique<AddTrackCommand>(TrackKind::Video, trackName);
        auto* raw = added.get();
        if (!apply(std::move(added)))
            return false;
        track = raw->trackId();
    }
    ClipVisual visual;
    visual.transform.scale = scale;
    auto clip = std::make_unique<AddClipCommand>(track, mediaId, at, media->duration, 0, visual, label);
    auto* raw = clip.get();
    if (!apply(std::move(clip)))
        return false;
    setSelectedClip(double(raw->clipId()));
    return true;
}

// --- Markers, groups and compound clips ---

namespace {
constexpr std::array kMarkerColors{"red", "orange", "yellow", "green", "blue", "purple"};
} // namespace

QStringList ProjectController::markerColors() const
{
    QStringList list;
    for (const char* name : kMarkerColors)
        list << QString::fromLatin1(name);
    return list;
}

QVariantList ProjectController::markers() const
{
    QVariantList list;
    for (const tve::Marker& marker : m_project.markers) {
        list << QVariantMap{
            {QStringLiteral("id"), double(marker.id)},
            {QStringLiteral("time"), seconds(marker.time)},
            {QStringLiteral("duration"), seconds(marker.duration)},
            {QStringLiteral("color"), QString::fromLatin1(kMarkerColors[std::size_t(marker.color)])},
            {QStringLiteral("note"), QString::fromStdString(marker.note)},
            {QStringLiteral("chapter"), marker.chapter},
        };
    }
    return list;
}

double ProjectController::addMarker(double at, const QString& note)
{
    if (!m_openLevels.empty()) {
        reportError(tr("Markers go on the main timeline"));
        return 0;
    }
    tve::Marker marker;
    marker.time = at < 0 ? ticks(m_playhead) : ticks(at);
    marker.note = note.toStdString();
    auto command = std::make_unique<AddMarkerCommand>(std::move(marker));
    AddMarkerCommand* raw = command.get();
    if (!apply(std::move(command)))
        return 0;
    return double(raw->markerId());
}

bool ProjectController::setMarker(double markerId, const QVariantMap& changes)
{
    const tve::Marker* found = m_project.findMarker(toId(markerId));
    if (!found) {
        reportError(QStringLiteral("marker not found"));
        return false;
    }
    tve::Marker marker = *found;
    if (changes.contains(QStringLiteral("time")))
        marker.time = ticks(changes.value(QStringLiteral("time")).toDouble());
    if (changes.contains(QStringLiteral("duration")))
        marker.duration = ticks(changes.value(QStringLiteral("duration")).toDouble());
    if (changes.contains(QStringLiteral("note")))
        marker.note = changes.value(QStringLiteral("note")).toString().toStdString();
    if (changes.contains(QStringLiteral("chapter")))
        marker.chapter = changes.value(QStringLiteral("chapter")).toBool();
    if (changes.contains(QStringLiteral("color"))) {
        const QString name = changes.value(QStringLiteral("color")).toString();
        const auto color = std::ranges::find(kMarkerColors, name.toStdString());
        if (color == kMarkerColors.end()) {
            reportError(QStringLiteral("unknown marker colour: ") + name);
            return false;
        }
        marker.color = MarkerColor(color - kMarkerColors.begin());
    }
    if (marker == *found)
        return true;
    return apply(std::make_unique<SetMarkerCommand>(std::move(marker)));
}

bool ProjectController::removeMarker(double markerId)
{
    return apply(std::make_unique<RemoveMarkerCommand>(toId(markerId)));
}

bool ProjectController::seekMarker(int direction)
{
    const Ticks now = ticks(m_playhead);
    const Ticks near = frameTicks() / 2;
    std::optional<Ticks> target;
    for (const tve::Marker& marker : m_project.markers) {
        if (direction > 0 && marker.time > now + near && (!target || marker.time < *target))
            target = marker.time;
        if (direction <= 0 && marker.time < now - near && (!target || marker.time > *target))
            target = marker.time;
    }
    if (!target)
        return false;
    seek(seconds(*target));
    return true;
}

QVariantList ProjectController::selectedClips() const
{
    QVariantList list;
    for (Id id : m_selectedClips) {
        if (findClip(id))
            list << double(id);
    }
    return list;
}

void ProjectController::toggleClipSelection(double clipId)
{
    const Id id = toId(clipId);
    const auto location = m_project.locateClip(id);
    if (!location)
        return;
    std::vector<Id> touched = groupMembers(m_project, location->clip->groupId);
    if (touched.empty())
        touched = {id};
    const bool selected = std::ranges::find(m_selectedClips, id) != m_selectedClips.end();
    if (selected) {
        std::erase_if(m_selectedClips, [&](Id s) { return std::ranges::find(touched, s) != touched.end(); });
        if (std::ranges::find(touched, m_selectedClip) != touched.end())
            m_selectedClip = m_selectedClips.empty() ? kNoId : m_selectedClips.back();
    } else {
        for (Id t : touched) {
            if (std::ranges::find(m_selectedClips, t) == m_selectedClips.end())
                m_selectedClips.push_back(t);
        }
        m_selectedClip = id;
        m_selectedTransition = kNoId;
    }
    if (m_activeMask != -1 || m_pickingKeyColor) {
        m_activeMask = -1;
        m_pickingKeyColor = false;
        emit editingChanged();
    }
    emit selectionChanged();
    emit selectedClipInfoChanged();
}

bool ProjectController::groupSelected()
{
    std::vector<Id> ids;
    for (const QVariant& id : selectedClips())
        ids.push_back(toId(id.toDouble()));
    if (ids.size() < 2) {
        reportError(tr("Select two or more clips (⇧-click) to group them"));
        return false;
    }
    return apply(std::make_unique<GroupClipsCommand>(std::move(ids)));
}

bool ProjectController::ungroupSelected()
{
    const Clip* clip = findClip(m_selectedClip);
    if (!clip || clip->groupId == kNoId) {
        reportError(tr("Select a grouped clip to ungroup"));
        return false;
    }
    if (!apply(std::make_unique<UngroupCommand>(clip->groupId)))
        return false;
    setSelectedClip(double(m_selectedClip));
    return true;
}

double ProjectController::createCompound()
{
    std::vector<Id> ids;
    for (const QVariant& id : selectedClips())
        ids.push_back(toId(id.toDouble()));
    if (ids.empty()) {
        reportError(tr("Select the clips to put in a compound clip"));
        return 0;
    }
    std::size_t number = std::size_t(std::ranges::count_if(m_project.sequences, [](const Sequence& s) {
                             return s.name != kHolderSequenceName;
                         })) + 1;
    const auto taken = [&](const std::string& name) {
        return std::ranges::any_of(m_project.sequences, [&](const Sequence& s) { return s.name == name; });
    };
    while (taken("Compound " + std::to_string(number)))
        ++number;
    auto command = std::make_unique<CreateCompoundCommand>(std::move(ids), "Compound " + std::to_string(number));
    CreateCompoundCommand* raw = command.get();
    if (!apply(std::move(command)))
        return 0;
    setSelectedClip(double(raw->clipId()));
    return double(raw->clipId());
}

bool ProjectController::breakCompound(double clipId)
{
    const Clip* clip = findClip(toId(clipId));
    if (!clip || !clip->compound()) {
        reportError(tr("Select a compound clip to break apart"));
        return false;
    }
    if (!apply(std::make_unique<BreakCompoundCommand>(toId(clipId))))
        return false;
    setSelectedClip(0);
    return true;
}

// --- Analysis ---

namespace {

QString analysisDirectory()
{
    const QString overridden = qEnvironmentVariable("TVE_ANALYSIS_DIR");
    return overridden.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/Analysis")
                                : overridden;
}

QVariantList rangeList(const std::vector<std::pair<Ticks, Ticks>>& ranges)
{
    QVariantList list;
    for (const auto& [start, end] : ranges)
        list << QVariantMap{{QStringLiteral("start"), toSeconds(start)}, {QStringLiteral("end"), toSeconds(end)}};
    return list;
}

} // namespace

std::string ProjectController::fingerprintOf(const MediaItem& media) const
{
    // Older projects have none: the path stands in (the cache is then per path).
    return media.fingerprint.empty() ? "path:" + media.path : media.fingerprint;
}

const Clip* ProjectController::analysableClip(Id clipId, bool needsSound, bool needsPictures)
{
    const Clip* clip = findClip(clipId);
    const MediaItem* media = clip ? m_project.findMedia(clip->mediaId) : nullptr;
    if (!clip || !media) {
        reportError(tr("Select a clip with media to analyse"));
        return nullptr;
    }
    if (needsSound && !media->hasAudio) {
        reportError(QStringLiteral("%1 has no sound").arg(mediaName(*media)));
        return nullptr;
    }
    if (needsPictures && (!media->hasVideo || media->still)) {
        reportError(QStringLiteral("%1 has no moving pictures").arg(mediaName(*media)));
        return nullptr;
    }
    if (clip->speed != 1.0 || clip->reversed || clip->freeze || !clip->speedRamp.empty()) {
        reportError(tr("Analysis works on clips playing at normal speed"));
        return nullptr;
    }
    if (!QFileInfo::exists(QString::fromStdString(media->path))) {
        reportError(QStringLiteral("%1 is missing: relink it first").arg(mediaName(*media)));
        return nullptr;
    }
    return clip;
}

void ProjectController::withFeatures(const std::vector<Id>& mediaIds, bool video, const QString& task,
                                     std::function<void()> then)
{
    struct Job {
        std::string path;
        std::string key;
    };
    std::vector<Job> jobs;
    const tve::AnalysisCache cache(analysisDirectory().toStdString());
    for (Id id : mediaIds) {
        const MediaItem* media = m_project.findMedia(id);
        if (!media)
            continue;
        const std::string key = fingerprintOf(*media);
        if (video ? m_videoFeatures.contains(key) : m_audioFeatures.contains(key))
            continue;
        // On disk from an earlier session?
        if (video) {
            if (auto found = cache.loadVideo(key)) {
                m_videoFeatures.emplace(key, std::move(*found));
                continue;
            }
        } else if (auto found = cache.loadAudio(key)) {
            m_audioFeatures.emplace(key, std::move(*found));
            continue;
        }
        if (std::ranges::none_of(jobs, [&](const Job& j) { return j.key == key; }))
            jobs.push_back({media->path, key});
    }
    if (jobs.empty()) {
        then();
        return;
    }
    m_cancelAnalysis = false;
    m_analysisProgress = 0;
    m_analysisTask = task;
    m_analysisThread = std::thread([this, jobs, video, directory = analysisDirectory().toStdString(),
                                    then = std::move(then)]() mutable {
        const tve::AnalysisCache cache(directory);
        std::vector<std::pair<std::string, tve::AudioFeatures>> audio;
        std::vector<std::pair<std::string, tve::VideoFeatures>> pictures;
        std::string error;
        double reported = -1;
        for (std::size_t i = 0; i < jobs.size() && error.empty(); ++i) {
            const auto progress = [&](double part) {
                const double done = (double(i) + part) / double(jobs.size());
                if (done - reported >= 0.01) {
                    reported = done;
                    QMetaObject::invokeMethod(this, [this, done] {
                        m_analysisProgress = done;
                        emit analysisChanged();
                    }, Qt::QueuedConnection);
                }
                return !m_cancelAnalysis.load();
            };
            if (video) {
                if (auto features = tve::analyzeVideo(jobs[i].path, progress, &error)) {
                    cache.save(jobs[i].key, *features);
                    pictures.emplace_back(jobs[i].key, std::move(*features));
                } else if (error.empty()) {
                    error = "cancelled";
                }
            } else if (auto features = tve::analyzeAudio(jobs[i].path, progress, &error)) {
                cache.save(jobs[i].key, *features);
                audio.emplace_back(jobs[i].key, std::move(*features));
            } else if (error.empty()) {
                error = "cancelled";
            }
        }
        QMetaObject::invokeMethod(this, [this, audio = std::move(audio), pictures = std::move(pictures), error,
                                         then = std::move(then)]() mutable {
            if (m_analysisThread.joinable())
                m_analysisThread.join();
            for (auto& [key, features] : audio)
                m_audioFeatures.insert_or_assign(key, std::move(features));
            for (auto& [key, features] : pictures)
                m_videoFeatures.insert_or_assign(key, std::move(features));
            m_analysisProgress = 0;
            m_analysisTask.clear();
            emit analysisChanged();
            if (error.empty()) {
                then();
            } else {
                const QString message = error == "cancelled" ? QString() : coreText(error);
                if (!message.isEmpty())
                    reportError(tr("Analysis failed: %1").arg(message));
                emit analysisFinished(false, message);
            }
        }, Qt::QueuedConnection);
    });
    emit analysisChanged();
}

void ProjectController::cancelAnalysis()
{
    m_cancelAnalysis = true;
}

void ProjectController::clearAnalysis()
{
    m_silences.clear();
    m_scenes.clear();
    m_beats.clear();
    m_faces.clear();
    m_faceTracks.clear();
    emit analysisChanged();
}

bool ProjectController::findSilences(double clipId, const QVariantMap& options)
{
    if (analyzing()) {
        reportError(tr("An analysis is already running"));
        return false;
    }
    const Clip* clip = analysableClip(toId(clipId), true, false);
    if (!clip)
        return false;
    const Id id = clip->id;
    const Id mediaId = clip->mediaId;
    tve::SilenceOptions settings;
    settings.thresholdDb = options.value(QStringLiteral("thresholdDb"), settings.thresholdDb).toDouble();
    settings.minDuration = options.value(QStringLiteral("minDuration"), settings.minDuration).toDouble();
    settings.padding = options.value(QStringLiteral("padding"), settings.padding).toDouble();
    withFeatures({mediaId}, false, tr("Listening for pauses"), [this, id, settings] {
        const Clip* clip = findClip(id);
        const MediaItem* media = clip ? m_project.findMedia(clip->mediaId) : nullptr;
        if (!media)
            return;
        const tve::AudioFeatures& features = m_audioFeatures.at(fingerprintOf(*media));
        std::vector<std::pair<Ticks, Ticks>> ranges;
        Ticks total = 0;
        const Ticks offset = clip->start - clip->sourceIn;
        for (const tve::TimeRange& r : tve::findSilences(features, settings, clip->sourceIn, clip->sourceIn + clip->duration)) {
            ranges.emplace_back(r.start + offset, r.end + offset);
            total += r.length();
        }
        m_silences = {{QStringLiteral("clipId"), double(id)},
                      {QStringLiteral("ranges"), rangeList(ranges)},
                      {QStringLiteral("total"), toSeconds(total)}};
        emit analysisChanged();
        emit analysisFinished(true, {});
    });
    return true;
}

bool ProjectController::removeSilences()
{
    const auto location = m_project.locateClip(toId(m_silences.value(QStringLiteral("clipId")).toDouble()));
    const QVariantList ranges = m_silences.value(QStringLiteral("ranges")).toList();
    if (!location || ranges.isEmpty()) {
        reportError(tr("Find the pauses first"));
        return false;
    }
    std::vector<std::pair<Ticks, Ticks>> cut;
    for (const QVariant& r : ranges) {
        const QVariantMap range = r.toMap();
        cut.emplace_back(ticks(range.value(QStringLiteral("start")).toDouble()), ticks(range.value(QStringLiteral("end")).toDouble()));
    }
    if (!apply(std::make_unique<RemoveRangesCommand>(location->track->id, std::move(cut), TVE_TR("core", "Remove Pauses"))))
        return false;
    m_silences.clear();
    emit analysisChanged();
    return true;
}

bool ProjectController::findScenes(double clipId, const QVariantMap& options)
{
    if (analyzing()) {
        reportError(tr("An analysis is already running"));
        return false;
    }
    const Clip* clip = analysableClip(toId(clipId), false, true);
    if (!clip)
        return false;
    const Id id = clip->id;
    tve::SceneOptions settings;
    settings.threshold = options.value(QStringLiteral("threshold"), settings.threshold).toDouble();
    settings.minScene = options.value(QStringLiteral("minScene"), settings.minScene).toDouble();
    withFeatures({clip->mediaId}, true, tr("Looking for scene changes"), [this, id, settings] {
        const Clip* clip = findClip(id);
        const MediaItem* media = clip ? m_project.findMedia(clip->mediaId) : nullptr;
        if (!media)
            return;
        QVariantList cuts;
        for (Ticks cut : tve::findSceneCuts(m_videoFeatures.at(fingerprintOf(*media)), settings)) {
            const Ticks at = cut - clip->sourceIn + clip->start;
            if (at > clip->start && at < clip->end())
                cuts << toSeconds(at);
        }
        m_scenes = {{QStringLiteral("clipId"), double(id)}, {QStringLiteral("cuts"), cuts}};
        emit analysisChanged();
        emit analysisFinished(true, {});
    });
    return true;
}

bool ProjectController::splitAtScenes()
{
    const Id id = toId(m_scenes.value(QStringLiteral("clipId")).toDouble());
    QVariantList cuts = m_scenes.value(QStringLiteral("cuts")).toList();
    if (!findClip(id) || cuts.isEmpty()) {
        reportError(tr("Find the scene changes first"));
        return false;
    }
    // Latest first: the clip keeps its id on the left of each split.
    std::vector<std::unique_ptr<Command>> commands;
    for (auto it = cuts.rbegin(); it != cuts.rend(); ++it)
        commands.push_back(std::make_unique<SplitClipCommand>(id, ticks(it->toDouble())));
    if (!apply(std::make_unique<CompositeCommand>(std::move(commands), TVE_TR("core", "Split at Scenes"))))
        return false;
    m_scenes.clear();
    emit analysisChanged();
    return true;
}

bool ProjectController::markScenes()
{
    const QVariantList cuts = m_scenes.value(QStringLiteral("cuts")).toList();
    if (cuts.isEmpty()) {
        reportError(tr("Find the scene changes first"));
        return false;
    }
    std::vector<std::unique_ptr<Command>> commands;
    int n = 1;
    for (const QVariant& cut : cuts) {
        tve::Marker marker;
        marker.time = ticks(cut.toDouble());
        marker.color = MarkerColor::Green;
        marker.note = "Scene " + std::to_string(++n);
        commands.push_back(std::make_unique<AddMarkerCommand>(std::move(marker)));
    }
    if (!apply(std::make_unique<CompositeCommand>(std::move(commands), TVE_TR("core", "Mark Scenes"))))
        return false;
    m_scenes.clear();
    emit analysisChanged();
    return true;
}

bool ProjectController::findBeats(double clipId)
{
    if (analyzing()) {
        reportError(tr("An analysis is already running"));
        return false;
    }
    const Clip* clip = analysableClip(toId(clipId), true, false);
    if (!clip)
        return false;
    const Id id = clip->id;
    withFeatures({clip->mediaId}, false, tr("Finding the beat"), [this, id] {
        const Clip* clip = findClip(id);
        const MediaItem* media = clip ? m_project.findMedia(clip->mediaId) : nullptr;
        if (!media)
            return;
        const tve::Beats found = tve::findBeats(m_audioFeatures.at(fingerprintOf(*media)));
        QVariantList times;
        for (Ticks beat : found.times) {
            const Ticks at = beat - clip->sourceIn + clip->start;
            if (at >= clip->start && at < clip->end())
                times << toSeconds(at);
        }
        m_beats = {{QStringLiteral("clipId"), double(id)}, {QStringLiteral("bpm"), found.bpm}, {QStringLiteral("times"), times}};
        emit analysisChanged();
        emit analysisFinished(true, {});
    });
    return true;
}

bool ProjectController::addBeatMarkers(int every)
{
    const QVariantList times = m_beats.value(QStringLiteral("times")).toList();
    if (times.isEmpty()) {
        reportError(tr("Find the beat first"));
        return false;
    }
    every = std::max(1, every);
    std::vector<std::unique_ptr<Command>> commands;
    for (qsizetype i = 0; i < times.size(); i += every) {
        tve::Marker marker;
        marker.time = ticks(times[i].toDouble());
        marker.color = MarkerColor::Yellow;
        commands.push_back(std::make_unique<AddMarkerCommand>(std::move(marker)));
    }
    return apply(std::make_unique<CompositeCommand>(std::move(commands), TVE_TR("core", "Add Beat Markers")));
}

// --- People ---

namespace {

// The options over a clip's cover (missing ones kept); false if a value is unknown.
bool readCover(const QVariantMap& options, FaceMosaic& mosaic)
{
    if (options.contains(QStringLiteral("style"))) {
        const QString style = options.value(QStringLiteral("style")).toString();
        const auto it = std::ranges::find_if(kFaceCovers, [&](const char* s) { return style == QLatin1String(s); });
        if (it == std::end(kFaceCovers))
            return false;
        mosaic.style = FaceCover(it - std::begin(kFaceCovers));
    }
    mosaic.amount = options.value(QStringLiteral("amount"), mosaic.amount).toDouble();
    mosaic.expand = options.value(QStringLiteral("expand"), mosaic.expand).toDouble();
    return true;
}

} // namespace

const Clip* ProjectController::peopleClip(Id clipId, bool needsFile)
{
    const auto location = m_project.locateClip(clipId);
    const Clip* clip = location ? location->clip : nullptr;
    const MediaItem* media = clip ? m_project.findMedia(clip->mediaId) : nullptr;
    if (!clip || clip->generated() || clip->compound() || !media || !media->hasVideo || media->still || media->lottie) {
        reportError(tr("Select a clip of a video file"));
        return nullptr;
    }
    if (needsFile && !QFileInfo::exists(QString::fromStdString(media->path))) {
        reportError(QStringLiteral("%1 is missing: relink it first").arg(mediaName(*media)));
        return nullptr;
    }
    return clip;
}

bool ProjectController::setPortraitCutout(double clipId, const QVariantMap& settings)
{
    const Clip* clip = peopleClip(toId(clipId), false);
    if (!clip)
        return false;
    PortraitCutout cutout = clip->cutout;
    cutout.enabled = settings.value(QStringLiteral("enabled"), cutout.enabled).toBool();
    cutout.invert = settings.value(QStringLiteral("invert"), cutout.invert).toBool();
    cutout.softness = settings.value(QStringLiteral("softness"), cutout.softness).toDouble();
    cutout.shift = settings.value(QStringLiteral("shift"), cutout.shift).toDouble();
    cutout.blur = settings.value(QStringLiteral("blur"), cutout.blur).toDouble();
    if (settings.contains(QStringLiteral("subject")) && !settings.value(QStringLiteral("subject")).toBool())
        cutout.subject.clear(); // back to people
    const char* label = cutout.enabled == clip->cutout.enabled ? TVE_TR("core", "Change Cutout")
                      : cutout.enabled                          ? TVE_TR("core", "Portrait Cutout")
                                                                 : TVE_TR("core", "Remove Cutout");
    return apply(std::make_unique<SetClipPeopleCommand>(clip->id, cutout, std::nullopt, label));
}

bool ProjectController::setFaceBeauty(double clipId, const QVariantMap& settings)
{
    const Clip* clip = peopleClip(toId(clipId), false);
    if (!clip)
        return false;
    FaceBeauty beauty = clip->beauty;
    beauty.enabled = settings.value(QStringLiteral("enabled"), beauty.enabled).toBool();
    beauty.smooth = settings.value(QStringLiteral("smooth"), beauty.smooth).toDouble();
    beauty.brighten = settings.value(QStringLiteral("brighten"), beauty.brighten).toDouble();
    beauty.teeth = settings.value(QStringLiteral("teeth"), beauty.teeth).toDouble();
    if (beauty == clip->beauty)
        return true;
    const char* label = beauty.enabled == clip->beauty.enabled ? TVE_TR("core", "Change Beauty")
                      : beauty.enabled                          ? TVE_TR("core", "Beauty")
                                                                 : TVE_TR("core", "Remove Beauty");
    return apply(std::make_unique<SetClipPeopleCommand>(clip->id, beauty, label));
}

bool ProjectController::findFaces(double clipId)
{
    if (analyzing()) {
        reportError(tr("An analysis is already running"));
        return false;
    }
    const Clip* clip = peopleClip(toId(clipId), true);
    if (!clip)
        return false;
    const MediaItem& media = *m_project.findMedia(clip->mediaId);
    const std::string key = fingerprintOf(media);
    const Id id = clip->id;
    const auto place = [this, id, key] {
        const Clip* clip = findClip(id);
        const auto frames = m_faceFrames.find(key);
        if (!clip || frames == m_faceFrames.end())
            return;
        // The faces seen while the clip plays, at their times on the timeline.
        const Ticks from = clip->sourceIn;
        const Ticks to = clip->sourceIn + Ticks(std::llround(double(clip->duration) * clip->speed));
        const auto timelineAt = [&](Ticks media) {
            const double along = std::clamp(double(media - from) / double(std::max<Ticks>(1, to - from)), 0.0, 1.0);
            return clip->start + Ticks(std::llround(along * double(clip->duration)));
        };
        m_faceTracks.clear();
        QVariantList tracks;
        for (FaceTrack& track : tve::linkFaces(frames->second)) {
            if (track.end() < from || track.start() > to)
                continue;
            const FaceBox first = track.samples.front().box;
            Ticks start = timelineAt(track.start());
            Ticks end = timelineAt(track.end());
            if (clip->reversed)
                std::tie(start, end) = std::pair(clip->start + clip->end() - end, clip->start + clip->end() - start);
            tracks << QVariantMap{{QStringLiteral("id"), track.id},       {QStringLiteral("start"), seconds(start)},
                                  {QStringLiteral("end"), seconds(end)},   {QStringLiteral("x"), first.x},
                                  {QStringLiteral("y"), first.y}};
            m_faceTracks.push_back(std::move(track));
        }
        m_faces = {{QStringLiteral("clipId"), double(id)}, {QStringLiteral("tracks"), tracks}};
        emit analysisChanged();
        emit analysisFinished(true, {});
    };
    if (!m_faceFrames.contains(key)) {
        if (auto kept = tve::loadFaces(tve::AnalysisCache(analysisDirectory().toStdString()), key))
            m_faceFrames.emplace(key, std::move(*kept));
    }
    if (m_faceFrames.contains(key)) {
        place();
        return true;
    }
    auto found = std::make_shared<std::vector<FaceFrame>>();
    runInBackground(tr("Looking for faces"),
                    [path = media.path, key, found, directory = analysisDirectory().toStdString()](
                        const tve::AnalysisProgress& progress) -> std::string {
                        std::string error;
                        auto frames = tve::findFaces(path, progress, &error);
                        if (!frames)
                            return error.empty() ? std::string("cancelled") : error;
                        tve::saveFaces(tve::AnalysisCache(directory), key, *frames);
                        *found = std::move(*frames);
                        return {};
                    },
                    [this, key, found, place] {
                        m_faceFrames.insert_or_assign(key, std::move(*found));
                        place();
                    });
    return true;
}

bool ProjectController::coverFaces(double clipId, const QVariantList& faceIds, const QVariantMap& options)
{
    const Clip* clip = peopleClip(toId(clipId), false);
    if (!clip)
        return false;
    if (toId(m_faces.value(QStringLiteral("clipId"), -1.0).toDouble()) != clip->id) {
        reportError(tr("Find the faces first"));
        return false;
    }
    FaceMosaic mosaic = clip->faceMosaic;
    if (!readCover(options, mosaic)) {
        reportError(tr("Unknown face cover"));
        return false;
    }
    mosaic.faces.clear();
    for (const FaceTrack& track : m_faceTracks) {
        if (faceIds.isEmpty() || faceIds.contains(QVariant(track.id)) || faceIds.contains(QVariant(double(track.id))))
            mosaic.faces.push_back(track);
    }
    if (mosaic.faces.empty()) {
        reportError(m_faceTracks.empty() ? tr("No faces were found in this clip")
                                         : tr("Choose the faces to cover"));
        return false;
    }
    return apply(std::make_unique<SetClipPeopleCommand>(clip->id, std::nullopt, mosaic, TVE_TR("core", "Cover Faces")));
}

namespace {

// Where a sticker sits on a face, in the face box's widths and heights from its centre, and
// how wide it is, in face widths.
struct FacePlacement {
    double dx, dy, size;
};
std::optional<FacePlacement> facePlacement(const QString& name)
{
    if (name == QLatin1String("head"))
        return FacePlacement{0.0, -0.95, 1.0}; // a crown, a hat: above the forehead
    if (name == QLatin1String("eyes"))
        return FacePlacement{0.0, -0.12, 0.95}; // across the eyes
    if (name == QLatin1String("face"))
        return FacePlacement{0.0, 0.0, 1.35}; // over the whole face
    if (name == QLatin1String("beside"))
        return FacePlacement{0.95, -0.35, 0.6}; // up beside the head
    return std::nullopt;
}

} // namespace

bool ProjectController::stickToFaces(double clipId, const QString& stickerId, const QString& placementName,
                                     const QVariantList& faceIds)
{
    const Clip* source = peopleClip(toId(clipId), false);
    if (!source)
        return false;
    if (toId(m_faces.value(QStringLiteral("clipId"), -1.0).toDouble()) != source->id || m_faceTracks.empty()) {
        reportError(tr("Find the faces first"));
        return false;
    }
    const auto placement = facePlacement(placementName);
    if (!placement) {
        reportError(tr("Unknown place on the face"));
        return false;
    }
    if (source->reversed || source->freeze || !source->speedRamp.empty()) {
        reportError(tr("Stickers follow faces only in clips played forwards at a steady speed"));
        return false;
    }
    std::vector<const FaceTrack*> chosen;
    for (const FaceTrack& track : m_faceTracks)
        if (faceIds.isEmpty() || faceIds.contains(QVariant(track.id)) || faceIds.contains(QVariant(double(track.id))))
            chosen.push_back(&track);
    if (chosen.empty()) {
        reportError(tr("Choose the faces"));
        return false;
    }
    if (!stickers::pathOf(stickerId).size()) {
        reportError(tr("Unknown sticker %1").arg(stickerId));
        return false;
    }
    const Id trackId = m_project.locateClip(source->id)->track->id;
    const MediaItem* video = m_project.findMedia(source->mediaId);
    const double cw = m_project.settings.width, ch = m_project.settings.height;
    const double vw = std::max(1, video->width), vh = std::max(1, video->height);
    const Ticks mediaOut = source->sourceIn + Ticks(std::llround(double(source->duration) * source->speed));
    const auto timelineAt = [&](Ticks media) {
        return source->start + Ticks(std::llround(double(media - source->sourceIn) / source->speed));
    };
    const auto mediaAt = [&](Ticks timeline) {
        return source->sourceIn + Ticks(std::llround(double(timeline - source->start) * source->speed));
    };
    // The sticker's media (imported once, a step of its own the first time).
    const Id stickerMediaId = stickerMedia(stickerId);
    if (stickerMediaId == kNoId)
        return false;
    const MediaItem* sticker = m_project.findMedia(stickerMediaId);
    const double sw = std::max(1, sticker->width), sh = std::max(1, sticker->height);
    const double fittedWidth = sw * std::min(cw / sw, ch / sh); // the sticker at scale 1, in canvas pixels

    std::vector<Clip> clips;
    for (const FaceTrack* face : chosen) {
        const Ticks from = std::max(source->start, timelineAt(std::max(face->start(), source->sourceIn)));
        const Ticks to = std::min(source->end(), timelineAt(std::min(face->end(), mediaOut)));
        if (to - from < kTicksPerSecond / 10)
            continue; // not on screen in this clip (or only for a moment)
        std::vector<Ticks> times{from};
        for (const FaceSample& s : face->samples) {
            const Ticks at = timelineAt(s.time);
            if (at > from && at < to)
                times.push_back(at);
        }
        times.push_back(to);
        Keyframes xs, ys, scales;
        for (const Ticks at : times) {
            const auto box = face->boxAt(mediaAt(at));
            if (!box)
                continue;
            const LayerPlacement shown = placeLayer(source->visualAt(at), cw, ch, vw, vh);
            const auto [u, v] = shown.toCanvas(box->x + placement->dx * box->width, box->y + placement->dy * box->height);
            const auto left = shown.toCanvas(box->x - box->width / 2, box->y);
            const auto right = shown.toCanvas(box->x + box->width / 2, box->y);
            const double faceWidth = std::hypot((right[0] - left[0]) * cw, (right[1] - left[1]) * ch);
            const Ticks t = at - from;
            xs.push_back({t, std::clamp(u - 0.5, -10.0, 10.0)});
            ys.push_back({t, std::clamp(v - 0.5, -10.0, 10.0)});
            scales.push_back({t, std::clamp(placement->size * faceWidth / fittedWidth, 0.01, 20.0)});
        }
        if (xs.empty())
            continue;
        Clip clip{.mediaId = stickerMediaId, .start = from, .duration = to - from};
        clip.visual.transform.x = xs.front().value;
        clip.visual.transform.y = ys.front().value;
        clip.visual.transform.scale = scales.front().value;
        if (xs.size() > 1) {
            clip.keyframes[std::string(tve::property::kX)] = std::move(xs);
            clip.keyframes[std::string(tve::property::kY)] = std::move(ys);
            clip.keyframes[std::string(tve::property::kScale)] = std::move(scales);
        }
        clips.push_back(std::move(clip));
    }
    if (clips.empty()) {
        reportError(tr("Those faces are not on screen in this clip"));
        return false;
    }
    auto command = std::make_unique<AddClipsAboveCommand>(trackId, std::move(clips), "Stickers",
                                                          TVE_TR("core", "Stickers on Faces"));
    auto* raw = command.get();
    if (!apply(std::move(command)))
        return false;
    setSelectedClip(double(raw->clipIds().front()));
    return true;
}

bool ProjectController::trackFaces(double clipId, const QVariantList& faceIds)
{
    const Clip* clip = peopleClip(toId(clipId), false);
    if (!clip)
        return false;
    if (toId(m_faces.value(QStringLiteral("clipId"), -1.0).toDouble()) != clip->id || m_faceTracks.empty()) {
        reportError(tr("Find the faces first"));
        return false;
    }
    std::vector<MotionTrack> tracks = clip->motionTracks;
    int next = 1;
    for (const MotionTrack& t : tracks)
        next = std::max(next, t.id + 1);
    const std::size_t before = tracks.size();
    for (const FaceTrack& face : m_faceTracks) {
        if (!faceIds.isEmpty() && !faceIds.contains(QVariant(face.id)) && !faceIds.contains(QVariant(double(face.id))))
            continue;
        MotionTrack track{.id = next++};
        for (const FaceSample& s : face.samples)
            track.samples.push_back({s.time, s.box.x, s.box.y, s.box.width, s.box.height, 0.0});
        if (track.samples.size() >= 2)
            tracks.push_back(std::move(track));
    }
    if (tracks.size() == before) {
        reportError(tr("Choose the faces"));
        return false;
    }
    return apply(std::make_unique<SetClipTracksCommand>(clip->id, std::move(tracks), TVE_TR("core", "Track Faces")));
}

bool ProjectController::setFaceCover(double clipId, const QVariantMap& options)
{
    const Clip* clip = peopleClip(toId(clipId), false);
    if (!clip)
        return false;
    FaceMosaic mosaic = clip->faceMosaic;
    if (!mosaic.active()) {
        reportError(tr("This clip covers no faces"));
        return false;
    }
    if (!readCover(options, mosaic)) {
        reportError(tr("Unknown face cover"));
        return false;
    }
    return apply(std::make_unique<SetClipPeopleCommand>(clip->id, std::nullopt, mosaic, TVE_TR("core", "Change Face Cover")));
}

bool ProjectController::uncoverFaces(double clipId)
{
    const Clip* clip = peopleClip(toId(clipId), false);
    if (!clip)
        return false;
    if (!clip->faceMosaic.active()) {
        reportError(tr("This clip covers no faces"));
        return false;
    }
    FaceMosaic none = clip->faceMosaic;
    none.faces.clear();
    return apply(std::make_unique<SetClipPeopleCommand>(clip->id, std::nullopt, none, TVE_TR("core", "Uncover Faces")));
}

bool ProjectController::autoReframe(double clipId, const QVariantMap& options)
{
    if (analyzing()) {
        reportError(tr("An analysis is already running"));
        return false;
    }
    const Clip* clip = peopleClip(toId(clipId), true);
    if (!clip)
        return false;
    if (clip->reversed || clip->freeze || !clip->speedRamp.empty()) {
        reportError(tr("Auto reframe works on clips playing forwards at a steady speed"));
        return false;
    }
    const MediaItem& media = *m_project.findMedia(clip->mediaId);
    if (media.width <= 0 || media.height <= 0) {
        reportError(tr("The size of %1 is not known").arg(mediaName(media)));
        return false;
    }
    // The canvas: the shape asked for, its shorter side kept (even sizes, for encoders).
    ProjectSettings canvas = m_project.settings;
    const QString aspect = options.value(QStringLiteral("aspect")).toString();
    if (!aspect.isEmpty()) {
        const QStringList parts = aspect.split(QLatin1Char(':'));
        const double a = parts.size() == 2 ? parts[0].toDouble() : 0.0;
        const double b = parts.size() == 2 ? parts[1].toDouble() : 0.0;
        if (a <= 0.0 || b <= 0.0) {
            reportError(tr("Unknown shape: %1").arg(aspect));
            return false;
        }
        const int shorter = std::min(canvas.width, canvas.height);
        const auto even = [](double v) { return std::max(2, int(std::lround(v / 2.0)) * 2); };
        canvas.width = a <= b ? shorter : even(shorter * a / b);
        canvas.height = a <= b ? even(shorter * b / a) : shorter;
    }
    const double smoothing = std::clamp(options.value(QStringLiteral("smoothing"), 0.6).toDouble(), 0.0, 1.0);
    const std::string key = fingerprintOf(media);
    const Id id = clip->id;
    const auto place = [this, id, key, canvas, smoothing] {
        const Clip* clip = findClip(id);
        const MediaItem* media = clip ? m_project.findMedia(clip->mediaId) : nullptr;
        if (!clip || !media)
            return;
        const tve::ReframePath path = tve::reframe(m_subjects[key], media->width, media->height, canvas.width,
                                                   canvas.height, {.smoothing = smoothing});
        // Media time → time in the clip; the ends get the path's value there.
        const auto clipTime = [&](Ticks media) {
            return Ticks(std::llround(double(media - clip->sourceIn) / clip->speed));
        };
        const auto positionAt = [&](Ticks media) {
            const auto& p = path.positions;
            if (media <= p.front().first)
                return p.front().second;
            if (media >= p.back().first)
                return p.back().second;
            const auto next = std::ranges::upper_bound(p, media, {}, [](const auto& e) { return e.first; });
            const auto& [t1, b] = *next;
            const auto& [t0, a] = *(next - 1);
            const double u = double(media - t0) / double(std::max<Ticks>(1, t1 - t0));
            return std::array<double, 2>{a[0] + (b[0] - a[0]) * u, a[1] + (b[1] - a[1]) * u};
        };
        Keyframes xs, ys;
        if (!path.positions.empty()) {
            const Ticks sourceEnd = clip->sourceIn + Ticks(std::llround(double(clip->duration) * clip->speed));
            std::vector<std::pair<Ticks, std::array<double, 2>>> points{{0, positionAt(clip->sourceIn)}};
            for (const auto& [time, xy] : path.positions) {
                const Ticks at = clipTime(time);
                if (at > 0 && at < clip->duration)
                    points.push_back({at, xy});
            }
            points.push_back({clip->duration, positionAt(sourceEnd)});
            for (const auto& [at, xy] : points) {
                xs.push_back({at, std::clamp(xy[0], -10.0, 10.0)});
                ys.push_back({at, std::clamp(xy[1], -10.0, 10.0)});
            }
        }
        ClipVisual visual = clip->visual;
        visual.transform.scale = path.scale;
        visual.transform.x = 0.0;
        visual.transform.y = 0.0;
        visual.transform.rotation = 0.0;
        visual.crop = {};
        std::map<std::string, Keyframes> changes{{std::string(tve::property::kX), xs},
                                                 {std::string(tve::property::kY), ys},
                                                 {std::string(tve::property::kScale), {}},
                                                 {std::string(tve::property::kRotation), {}}};
        std::vector<std::unique_ptr<Command>> commands;
        if (canvas.width != m_project.settings.width || canvas.height != m_project.settings.height)
            commands.push_back(std::make_unique<SetProjectSettingsCommand>(canvas));
        commands.push_back(std::make_unique<SetClipKeyframesCommand>(id, std::move(changes), TVE_TR("core", "Auto Reframe"), visual));
        if (apply(std::make_unique<CompositeCommand>(std::move(commands), TVE_TR("core", "Auto Reframe")))) {
            m_canvasChosen = true;
            emit analysisFinished(true, {});
        } else {
            emit analysisFinished(false, lastError());
        }
    };
    withSubject(media, place);
    return true;
}

// --- Motion ---

namespace {

// A point of a stabilized clip's picture (fractions) back in its shaky frame, and the other
// way: steady = zoom · (scale · R(angle) · raw + shift), in pixels about the centre.
std::array<double, 2> steadyToRaw(const Stabilization& s, Ticks mediaTime, double w, double h, double x, double y)
{
    if (!s.enabled || s.samples.empty())
        return {x, y};
    const SteadySample c = s.at(mediaTime);
    const double px = (x - 0.5) * w / s.zoom - c.dx * w, py = (y - 0.5) * h / s.zoom - c.dy * h;
    const double co = std::cos(c.angle), si = std::sin(c.angle);
    return {(co * px + si * py) / c.scale / w + 0.5, (-si * px + co * py) / c.scale / h + 0.5};
}

std::array<double, 2> rawToSteady(const Stabilization& s, Ticks mediaTime, double w, double h, double x, double y)
{
    if (!s.enabled || s.samples.empty())
        return {x, y};
    const SteadySample c = s.at(mediaTime);
    const double px = (x - 0.5) * w, py = (y - 0.5) * h;
    const double co = std::cos(c.angle), si = std::sin(c.angle);
    const double qx = c.scale * (co * px - si * py) + c.dx * w, qy = c.scale * (si * px + co * py) + c.dy * h;
    return {qx * s.zoom / w + 0.5, qy * s.zoom / h + 0.5};
}

} // namespace

void ProjectController::withMotion(Id clipId, const QString& task, std::function<void(const MotionAnalysis&)> then)
{
    const Clip* clip = findClip(clipId);
    const MediaItem& media = *m_project.findMedia(clip->mediaId);
    const std::string key = fingerprintOf(media);
    if (!m_motion.contains(key)) {
        if (auto kept = tve::loadMotion(tve::AnalysisCache(analysisDirectory().toStdString()), key))
            m_motion.emplace(key, std::move(*kept));
    }
    if (m_motion.contains(key)) {
        then(m_motion.at(key));
        return;
    }
    auto found = std::make_shared<MotionAnalysis>();
    runInBackground(task,
                    [path = media.path, key, found, directory = analysisDirectory().toStdString()](
                        const tve::AnalysisProgress& progress) -> std::string {
                        std::string error;
                        auto analysis = tve::analyzeMotion(path, progress, &error);
                        if (!analysis)
                            return error.empty() ? std::string("cancelled") : error;
                        tve::saveMotion(tve::AnalysisCache(directory), key, *analysis);
                        *found = std::move(*analysis);
                        return {};
                    },
                    [this, key, found, then] {
                        m_motion.insert_or_assign(key, std::move(*found));
                        then(m_motion.at(key));
                    });
}

bool ProjectController::stabilize(double clipId, const QVariantMap& options)
{
    if (analyzing()) {
        reportError(tr("An analysis is already running"));
        return false;
    }
    const Clip* clip = peopleClip(toId(clipId), true);
    if (!clip)
        return false;
    const double smoothness = std::clamp(options.value(QStringLiteral("smoothness"), clip->stabilization.smoothness).toDouble(), 0.0, 1.0);
    const bool lock = options.value(QStringLiteral("lock"), clip->stabilization.lock).toBool();
    const double maxZoom = std::clamp(options.value(QStringLiteral("maxZoom"), clip->stabilization.maxZoom).toDouble(), 1.0, 2.0);
    const Id id = clip->id;
    withMotion(id, tr("Measuring camera motion"), [this, id, smoothness, lock, maxZoom](const MotionAnalysis& analysis) {
        if (apply(std::make_unique<SetClipMotionCommand>(id, tve::stabilize(analysis, smoothness, lock, maxZoom), std::nullopt,
                                                         std::nullopt, TVE_TR("core", "Stabilize"))))
            emit analysisFinished(true, {});
        else
            emit analysisFinished(false, lastError());
    });
    return true;
}

bool ProjectController::removeStabilization(double clipId)
{
    const Clip* clip = peopleClip(toId(clipId), false);
    if (!clip)
        return false;
    if (!clip->stabilization.enabled) {
        reportError(tr("This clip is not stabilized"));
        return false;
    }
    Stabilization off;
    off.smoothness = clip->stabilization.smoothness;
    off.lock = clip->stabilization.lock;
    off.maxZoom = clip->stabilization.maxZoom;
    return apply(std::make_unique<SetClipMotionCommand>(clip->id, off, std::nullopt, std::nullopt, TVE_TR("core", "Remove Stabilization")));
}

bool ProjectController::removeFlicker(double clipId, const QVariantMap& options)
{
    if (analyzing()) {
        reportError(tr("An analysis is already running"));
        return false;
    }
    const Clip* clip = peopleClip(toId(clipId), true);
    if (!clip)
        return false;
    const double strength = std::clamp(options.value(QStringLiteral("strength"), clip->deflicker.strength).toDouble(), 0.0, 1.0);
    const Id id = clip->id;
    withMotion(id, tr("Measuring brightness"), [this, id, strength](const MotionAnalysis& analysis) {
        if (apply(std::make_unique<SetClipMotionCommand>(id, std::nullopt, tve::deflicker(analysis, strength), std::nullopt,
                                                         TVE_TR("core", "Remove Flicker"))))
            emit analysisFinished(true, {});
        else
            emit analysisFinished(false, lastError());
    });
    return true;
}

bool ProjectController::keepFlicker(double clipId)
{
    const Clip* clip = peopleClip(toId(clipId), false);
    if (!clip)
        return false;
    if (!clip->deflicker.enabled) {
        reportError(tr("This clip's flicker is not removed"));
        return false;
    }
    Deflicker off;
    off.strength = clip->deflicker.strength;
    return apply(std::make_unique<SetClipMotionCommand>(clip->id, std::nullopt, off, std::nullopt, TVE_TR("core", "Keep Flicker")));
}

bool ProjectController::setDenoise(double clipId, double strength)
{
    const Clip* clip = peopleClip(toId(clipId), false);
    if (!clip)
        return false;
    return apply(std::make_unique<SetClipMotionCommand>(clip->id, std::nullopt, std::nullopt, strength,
                                                        clip->denoise > 0.0 && strength > 0.0 ? TVE_TR("core", "Change Denoise")
                                                        : strength > 0.0                      ? TVE_TR("core", "Denoise")
                                                                                              : TVE_TR("core", "Remove Denoise")));
}

void ProjectController::setDrawingMode(bool drawing)
{
    if (drawing == m_drawingMode)
        return;
    m_drawingMode = drawing;
    emit editingChanged();
}

void ProjectController::setPickingTrackArea(bool picking)
{
    if (picking == m_pickingTrackArea)
        return;
    m_pickingTrackArea = picking;
    emit editingChanged();
}

void ProjectController::setPinningCorners(bool pinning)
{
    if (pinning == m_pinningCorners)
        return;
    m_pinningCorners = pinning;
    emit editingChanged();
}

bool ProjectController::setTrackArea(double clipId, const QVariantMap& box)
{
    const Clip* clip = peopleClip(toId(clipId), false);
    if (!clip)
        return false;
    const double w = box.value(QStringLiteral("width")).toDouble(), h = box.value(QStringLiteral("height")).toDouble();
    if (w <= 0.005 || h <= 0.005) {
        reportError(tr("Draw a larger box"));
        return false;
    }
    m_trackArea = box;
    m_trackArea.insert(QStringLiteral("clipId"), double(clip->id));
    m_pickingTrackArea = false;
    emit editingChanged();
    return true;
}

bool ProjectController::trackMotion(double clipId, const QVariantMap& options)
{
    if (analyzing()) {
        reportError(tr("An analysis is already running"));
        return false;
    }
    const Clip* clip = peopleClip(toId(clipId), true);
    if (!clip)
        return false;
    if (clip->reversed || clip->freeze || !clip->speedRamp.empty()) {
        reportError(tr("Tracking works on clips playing forwards at a steady speed"));
        return false;
    }
    if (toId(m_trackArea.value(QStringLiteral("clipId"), -1.0).toDouble()) != clip->id) {
        reportError(tr("Draw a box around what to follow first"));
        return false;
    }
    const MediaItem& media = *m_project.findMedia(clip->mediaId);
    const Ticks now = std::clamp(ticks(m_playhead), clip->start, clip->end() - 1);
    const Ticks from = clip->sourceTimeBeyond(now);
    const Ticks to = clip->sourceIn + Ticks(std::llround(double(clip->duration) * clip->speed));
    const TrackSample box = drawnBox(*clip, from);
    const bool surface = options.value(QStringLiteral("surface")).toBool();
    const Id id = clip->id;
    auto found = std::make_shared<std::vector<TrackSample>>();
    runInBackground(QStringLiteral("Following"),
                    [path = media.path, from, to, box, surface, found](const tve::AnalysisProgress& progress) -> std::string {
                        std::string error;
                        auto samples = tve::trackRegion(path, from, to, box, surface, progress, &error);
                        if (!samples)
                            return error.empty() ? std::string("cancelled") : error;
                        *found = tve::thinTrack(*samples);
                        return {};
                    },
                    [this, id, surface, found] {
                        const Clip* clip = findClip(id);
                        if (!clip)
                            return;
                        std::vector<MotionTrack> tracks = clip->motionTracks;
                        int next = 1;
                        for (const MotionTrack& t : tracks)
                            next = std::max(next, t.id + 1);
                        tracks.push_back({next, surface, std::move(*found)});
                        if (apply(std::make_unique<SetClipTracksCommand>(id, std::move(tracks), TVE_TR("core", "Track Motion")))) {
                            m_trackArea.clear();
                            emit editingChanged();
                            emit analysisFinished(true, {});
                        } else {
                            emit analysisFinished(false, lastError());
                        }
                    });
    return true;
}

TrackSample ProjectController::drawnBox(const Clip& clip, Ticks mediaTime) const
{
    // The box as drawn is in the picture as shown: back into the shaky frame, if steadied.
    const MediaItem& media = *m_project.findMedia(clip.mediaId);
    const double w = std::max(1, media.width), h = std::max(1, media.height);
    const double bx = m_trackArea.value(QStringLiteral("x")).toDouble(), by = m_trackArea.value(QStringLiteral("y")).toDouble();
    const double bw = m_trackArea.value(QStringLiteral("width")).toDouble(), bh = m_trackArea.value(QStringLiteral("height")).toDouble();
    const auto centre = steadyToRaw(clip.stabilization, mediaTime, w, h, bx, by);
    const double zoom = clip.stabilization.enabled ? clip.stabilization.zoom : 1.0;
    return {mediaTime, centre[0], centre[1], bw / zoom, bh / zoom, 0.0};
}

bool ProjectController::cutOutSubject(double clipId)
{
    if (analyzing()) {
        reportError(tr("An analysis is already running"));
        return false;
    }
    const Clip* clip = peopleClip(toId(clipId), true);
    if (!clip)
        return false;
    if (clip->reversed || clip->freeze || !clip->speedRamp.empty()) {
        reportError(tr("Smart cutout works on clips playing forwards at a steady speed"));
        return false;
    }
    if (toId(m_trackArea.value(QStringLiteral("clipId"), -1.0).toDouble()) != clip->id) {
        reportError(tr("Draw a box around the subject first"));
        return false;
    }
    const MediaItem& media = *m_project.findMedia(clip->mediaId);
    const Ticks now = std::clamp(ticks(m_playhead), clip->start, clip->end() - 1);
    const Ticks from = clip->sourceTimeBeyond(now);
    const Ticks to = clip->sourceIn + Ticks(std::llround(double(clip->duration) * clip->speed));
    const TrackSample box = drawnBox(*clip, from);
    const Id id = clip->id;
    auto found = std::make_shared<std::vector<TrackSample>>();
    runInBackground(tr("Following the subject"),
                    [path = media.path, from, to, box, found](const tve::AnalysisProgress& progress) -> std::string {
                        std::string error;
                        auto samples = tve::trackRegion(path, from, to, box, false, progress, &error);
                        if (!samples)
                            return error.empty() ? std::string("cancelled") : error;
                        *found = tve::thinTrack(*samples);
                        return {};
                    },
                    [this, id, found] {
                        const Clip* clip = findClip(id);
                        if (!clip)
                            return;
                        PortraitCutout cutout = clip->cutout;
                        cutout.enabled = true;
                        cutout.invert = false;
                        cutout.subject = std::move(*found);
                        if (apply(std::make_unique<SetClipPeopleCommand>(id, cutout, std::nullopt, TVE_TR("core", "Cut Out Subject")))) {
                            m_trackArea.clear();
                            emit editingChanged();
                            emit analysisFinished(true, {});
                        } else {
                            emit analysisFinished(false, lastError());
                        }
                    });
    return true;
}

bool ProjectController::deleteTrack(double clipId, int trackId)
{
    const Clip* clip = peopleClip(toId(clipId), false);
    if (!clip)
        return false;
    std::vector<MotionTrack> tracks = clip->motionTracks;
    if (std::erase_if(tracks, [&](const MotionTrack& t) { return t.id == trackId; }) == 0) {
        reportError(tr("No such track"));
        return false;
    }
    return apply(std::make_unique<SetClipTracksCommand>(clip->id, std::move(tracks), TVE_TR("core", "Delete Track")));
}

QVariantList ProjectController::tracksToFollow(double clipId) const
{
    QVariantList list;
    const Clip* follower = findClip(toId(clipId));
    if (!follower)
        return list;
    for (const Track& track : m_project.tracks) {
        for (const Clip& clip : track.clips) {
            if (clip.id == follower->id || clip.motionTracks.empty() || clip.end() <= follower->start
                || clip.start >= follower->end())
                continue;
            const MediaItem* media = m_project.findMedia(clip.mediaId);
            for (const MotionTrack& t : clip.motionTracks)
                list << QVariantMap{{QStringLiteral("clipId"), double(clip.id)},
                                    {QStringLiteral("trackId"), t.id},
                                    {QStringLiteral("label"), (media ? mediaName(*media) : QStringLiteral("Clip"))
                                                                  + QStringLiteral(" · track %1 (%2)").arg(t.id).arg(
                                                                      t.surface ? QStringLiteral("surface") : QStringLiteral("object"))}};
        }
    }
    return list;
}

bool ProjectController::followTrack(double clipId, double sourceClipId, int trackId, const QVariantMap& options)
{
    const auto location = m_project.locateClip(toId(clipId));
    const Clip* follower = location ? location->clip : nullptr;
    const Clip* source = findClip(toId(sourceClipId));
    if (!follower || follower->adjustment || !source || source == follower) {
        reportError(tr("Select the clip that should follow"));
        return false;
    }
    const auto track = std::ranges::find(source->motionTracks, trackId, &MotionTrack::id);
    const MediaItem* media = m_project.findMedia(source->mediaId);
    if (track == source->motionTracks.end() || !media || media->width <= 0 || media->height <= 0) {
        reportError(tr("No such track"));
        return false;
    }
    const bool withScale = options.value(QStringLiteral("scale")).toBool();
    const bool withRotation = options.value(QStringLiteral("rotation")).toBool() && track->surface;
    // Each sample on the canvas: through the source clip's stabilization and placement.
    const double cw = m_project.settings.width, ch = m_project.settings.height;
    const double w = media->width, h = media->height;
    struct Point {
        Ticks time; // on the timeline
        double x, y, size, angle;
    };
    std::vector<Point> points;
    for (const TrackSample& s : track->samples) {
        const Ticks at = source->start + Ticks(std::llround(double(s.time - source->sourceIn) / source->speed));
        if (at < source->start || at > source->end())
            continue;
        const auto shown = rawToSteady(source->stabilization, s.time, w, h, s.x, s.y);
        const LayerPlacement placement = placeLayer(source->visualAt(at), cw, ch, w, h);
        const auto [u, v] = placement.toCanvas(shown[0], shown[1]);
        points.push_back({at, u, v, std::sqrt(std::max(1e-9, s.width * s.height)), s.angle});
    }
    const Ticks first = std::max(follower->start, points.empty() ? follower->end() : points.front().time);
    const Ticks last = std::min(follower->end(), points.empty() ? follower->start : points.back().time);
    if (points.empty() || first > last) {
        reportError(tr("This clip and the track are never on screen together"));
        return false;
    }
    // From where the follower is now (at the playhead, within the track), it moves as the track does.
    const Ticks anchor = std::clamp(ticks(m_playhead), first, last);
    const auto pointAt = [&](Ticks t) {
        if (t <= points.front().time)
            return points.front();
        if (t >= points.back().time)
            return points.back();
        const auto next = std::ranges::upper_bound(points, t, {}, &Point::time);
        const Point& q = *next;
        const Point& p = *(next - 1);
        const double k = double(t - p.time) / double(std::max<Ticks>(1, q.time - p.time));
        return Point{t, p.x + (q.x - p.x) * k, p.y + (q.y - p.y) * k, p.size + (q.size - p.size) * k,
                     p.angle + (q.angle - p.angle) * k};
    };
    const Point origin = pointAt(anchor);
    const ClipVisual start = follower->visualAt(anchor);
    Keyframes xs, ys, scales, turns;
    std::vector<Ticks> times;
    for (const Point& p : points)
        if (p.time >= follower->start && p.time <= follower->end())
            times.push_back(p.time);
    if (times.empty() || times.front() > follower->start)
        times.insert(times.begin(), std::max(follower->start, first));
    for (const Ticks t : times) {
        const Point p = pointAt(t);
        const Ticks at = t - follower->start;
        xs.push_back({at, std::clamp(start.transform.x + (p.x - origin.x), -10.0, 10.0)});
        ys.push_back({at, std::clamp(start.transform.y + (p.y - origin.y), -10.0, 10.0)});
        if (withScale)
            scales.push_back({at, std::clamp(start.transform.scale * p.size / origin.size, 0.01, 20.0)});
        if (withRotation)
            turns.push_back({at, std::clamp(start.transform.rotation + (p.angle - origin.angle) * 180.0 / std::numbers::pi,
                                            -3600.0, 3600.0)});
    }
    std::map<std::string, Keyframes> changes{{std::string(tve::property::kX), xs}, {std::string(tve::property::kY), ys}};
    if (withScale)
        changes.emplace(std::string(tve::property::kScale), scales);
    if (withRotation)
        changes.emplace(std::string(tve::property::kRotation), turns);
    return apply(std::make_unique<SetClipKeyframesCommand>(follower->id, std::move(changes), TVE_TR("core", "Follow Track")));
}

// --- Processed pictures ---

namespace {

QString processedDirectory()
{
    const QString overridden = qEnvironmentVariable("TVE_PROCESSED_DIR");
    return overridden.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/Processed")
                                : overridden;
}

} // namespace

QVariantMap ProjectController::upscaleModel() const
{
    const tve::UpscaleModel status = tve::upscaleModelStatus();
    const char* name = status == tve::UpscaleModel::Ready         ? "ready"
                     : status == tve::UpscaleModel::Downloading   ? "downloading"
                     : status == tve::UpscaleModel::NeedsDownload ? "download"
                                                                  : "unsupported";
    return {{QStringLiteral("status"), QString::fromLatin1(name)},
            {QStringLiteral("progress"), status == tve::UpscaleModel::Downloading ? tve::upscaleModelProgress() : 0.0}};
}

bool ProjectController::downloadUpscaleModel()
{
    const tve::UpscaleModel status = tve::upscaleModelStatus();
    if (status == tve::UpscaleModel::Unsupported) {
        reportError(tr("Upscaling is not available on this Mac"));
        return false;
    }
    if (status != tve::UpscaleModel::NeedsDownload)
        return true;
    QPointer<ProjectController> self(this);
    tve::downloadUpscaleModel([self](bool ok, std::string error) {
        QMetaObject::invokeMethod(qApp, [self, ok, error] {
            if (!self)
                return;
            if (self->m_upscaleWatch)
                self->m_upscaleWatch->stop();
            if (!ok)
                self->reportError(tr("The upscaling model could not be downloaded: %1").arg(coreText(error)));
            emit self->upscaleModelChanged();
        }, Qt::QueuedConnection);
    });
    // Progress, now and then, while macOS downloads it.
    if (!m_upscaleWatch) {
        m_upscaleWatch = new QTimer(this);
        m_upscaleWatch->setInterval(500);
        connect(m_upscaleWatch, &QTimer::timeout, this, &ProjectController::upscaleModelChanged);
    }
    m_upscaleWatch->start();
    emit upscaleModelChanged();
    return true;
}

bool ProjectController::processVideo(double clipId, const QVariantMap& options)
{
    if (analyzing()) {
        reportError(tr("An analysis is already running"));
        return false;
    }
    const Clip* clip = peopleClip(toId(clipId), true);
    if (!clip)
        return false;
    const QString kind = options.value(QStringLiteral("kind")).toString();
    const MediaItem& media = *m_project.findMedia(clip->mediaId);
    // The clip's part of the media.
    const Ticks span = Ticks(std::llround(double(clip->duration) * clip->speed));
    const Ticks from = clip->sourceIn;
    const Ticks to = clip->sourceIn + span;
    // Working on what the clip shows now: its processed copy, where that covers the part.
    const bool chained = clip->processedVideo && clip->processedVideo->from <= from && clip->processedVideo->to >= to
                      && QFileInfo::exists(QString::fromStdString(clip->processedVideo->path));
    const std::string input = chained ? clip->processedVideo->path : media.path;
    const Ticks offset = chained ? clip->processedVideo->from : 0;
    std::vector<std::string> steps = chained ? clip->processedVideo->steps : std::vector<std::string>{};

    std::function<bool(const std::string&, const tve::AnalysisProgress&, std::string*)> work;
    QString task;
    if (kind == QLatin1String("upscale")) {
        const int scale = options.value(QStringLiteral("scale"), 2).toInt() >= 4 ? 4 : 2;
        if (tve::upscaleModelStatus() != tve::UpscaleModel::Ready) {
            reportError(tr("Download Apple's upscaling model first"));
            return false;
        }
        steps.push_back("upscale" + std::to_string(scale));
        task = QStringLiteral("Upscaling");
        work = [input, from, to, offset, scale](const std::string& out, const tve::AnalysisProgress& p, std::string* e) {
            return tve::upscaleVideo(input, from - offset, to - offset, scale, out, p, e);
        };
    } else if (kind == QLatin1String("smooth")) {
        const int factor = std::clamp(options.value(QStringLiteral("factor"), 4).toInt(), 2, 8);
        steps.push_back("smooth" + std::to_string(factor));
        task = tr("Making frames between");
        work = [input, from, to, offset, factor](const std::string& out, const tve::AnalysisProgress& p, std::string* e) {
            return tve::smoothVideo(input, from - offset, to - offset, factor, out, p, e);
        };
    } else if (kind == QLatin1String("remove")) {
        if (clip->reversed || clip->freeze || !clip->speedRamp.empty()) {
            reportError(tr("Removing works on clips playing forwards at a steady speed"));
            return false;
        }
        if (toId(m_trackArea.value(QStringLiteral("clipId"), -1.0).toDouble()) != clip->id) {
            reportError(tr("Draw a box around what to remove first"));
            return false;
        }
        const Ticks at = clip->sourceTimeBeyond(std::clamp(ticks(m_playhead), clip->start, clip->end() - 1));
        const TrackSample box = drawnBox(*clip, at);
        const double expand = std::clamp(options.value(QStringLiteral("expand"), 0.15).toDouble(), 0.0, 1.0);
        steps.push_back("remove");
        task = QStringLiteral("Removing");
        // First followed through the clip (on the media), then filled in.
        work = [input, path = media.path, from, to, offset, at, box, expand](const std::string& out,
                                                                             const tve::AnalysisProgress& p, std::string* e) {
            const auto half = [&p](double base) {
                return [&p, base](double v) { return !p || p(base + 0.5 * v); };
            };
            auto object = tve::trackRegion(path, at, to, box, false, half(0.0), e);
            if (!object)
                return false;
            // Media time → the input's own time.
            for (TrackSample& s : *object)
                s.time -= offset;
            return tve::removeObject(input, from - offset, to - offset, *object, expand, out, half(0.5), e);
        };
    } else {
        reportError(tr("Unknown processing: %1").arg(kind));
        return false;
    }
    std::string name = fingerprintOf(media) + "-" + std::to_string(from) + "-" + std::to_string(to);
    for (const std::string& s : steps)
        name += "-" + s;
    std::ranges::replace(name, ':', '_');
    if (kind == QLatin1String("remove")) // a different box each time
        name += "-" + std::to_string(QDateTime::currentMSecsSinceEpoch());
    const std::string output = QDir(processedDirectory()).filePath(QString::fromStdString(name) + QStringLiteral(".mov")).toStdString();
    const Id id = clip->id;
    const ProcessedVideo result{output, from, to, steps};
    const char* label = kind == QLatin1String("upscale") ? TVE_TR("core", "Upscale") : kind == QLatin1String("smooth") ? TVE_TR("core", "Smooth Slow Motion") : TVE_TR("core", "Remove Object");
    const auto place = [this, id, result, label] {
        if (apply(std::make_unique<SetClipProcessedVideoCommand>(id, result, label))) {
            if (result.steps.back() == "remove") {
                m_trackArea.clear();
                emit editingChanged();
            }
            emit analysisFinished(true, {});
        } else {
            emit analysisFinished(false, lastError());
        }
    };
    if (QFileInfo::exists(QString::fromStdString(output))) {
        place();
        return true;
    }
    runInBackground(task,
                    [work, output](const tve::AnalysisProgress& progress) -> std::string {
                        std::string error;
                        if (!work(output, progress, &error))
                            return error.empty() ? std::string("cancelled") : error;
                        return {};
                    },
                    place);
    return true;
}

bool ProjectController::restoreVideo(double clipId)
{
    const Clip* clip = peopleClip(toId(clipId), false);
    if (!clip)
        return false;
    if (!clip->processedVideo) {
        reportError(tr("This clip plays its own pictures already"));
        return false;
    }
    return apply(std::make_unique<SetClipProcessedVideoCommand>(clip->id, std::nullopt, TVE_TR("core", "Original Picture")));
}

// --- Multicam ---

bool ProjectController::syncBySound(const QVariantList& clipIds)
{
    if (analyzing()) {
        reportError(tr("An analysis is already running"));
        return false;
    }
    std::vector<Id> ids;
    for (const QVariant& v : clipIds)
        if (std::ranges::find(ids, toId(v.toDouble())) == ids.end())
            ids.push_back(toId(v.toDouble()));
    if (ids.size() < 2) {
        reportError(tr("Select two or more clips to line up"));
        return false;
    }
    std::vector<Id> mediaIds;
    for (const Id id : ids) {
        const Clip* clip = analysableClip(id, true, false);
        if (!clip)
            return false;
        mediaIds.push_back(clip->mediaId);
    }
    withFeatures(mediaIds, false, tr("Listening to each camera"), [this, ids] {
        const Clip* reference = findClip(ids.front());
        if (!reference)
            return;
        const auto featuresOf = [&](const Clip& clip) -> const AudioFeatures& {
            return m_audioFeatures.at(fingerprintOf(*m_project.findMedia(clip.mediaId)));
        };
        struct Move {
            Id clip;
            Id track;
            Ticks start;
        };
        std::vector<Move> moves;
        for (std::size_t i = 1; i < ids.size(); ++i) {
            const auto location = m_project.locateClip(ids[i]);
            if (!location)
                continue;
            const auto found = tve::findAudioOffset(featuresOf(*reference), featuresOf(*location->clip));
            if (!found || found->confidence < 0.15) {
                reportError(QStringLiteral("%1 and %2 do not share enough sound to line up")
                                .arg(mediaName(*m_project.findMedia(reference->mediaId)),
                                     mediaName(*m_project.findMedia(location->clip->mediaId))));
                emit analysisFinished(false, lastError());
                return;
            }
            // Where the shared sound plays in the reference, this clip plays it too.
            const Ticks start = reference->start - reference->sourceIn + ticks(found->offset) + location->clip->sourceIn;
            moves.push_back({ids[i], location->track->id, start});
        }
        // Nothing before zero: everything later by as much as needed (the reference too).
        Ticks earliest = reference->start;
        for (const Move& m : moves)
            earliest = std::min(earliest, m.start);
        std::vector<std::unique_ptr<Command>> commands;
        if (earliest < 0) {
            const auto location = m_project.locateClip(reference->id);
            commands.push_back(std::make_unique<MoveClipCommand>(reference->id, location->track->id, reference->start - earliest));
        }
        for (const Move& m : moves)
            commands.push_back(std::make_unique<MoveClipCommand>(m.clip, m.track, m.start - std::min<Ticks>(0, earliest)));
        if (apply(std::make_unique<CompositeCommand>(std::move(commands), "Sync by Sound")))
            emit analysisFinished(true, {});
        else
            emit analysisFinished(false, lastError());
    });
    return true;
}

// --- Speech enhancement ---

namespace {

// Cleaned sound is kept (not cached: projects play it), in <app data>/Enhanced.
QString enhancedDirectory()
{
    const QString overridden = qEnvironmentVariable("TVE_ENHANCED_DIR");
    return overridden.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/Enhanced")
                                : overridden;
}

constexpr const char* kEnhanceModel = "deepfilternet3";

} // namespace

QVariantMap ProjectController::enhanceModel() const
{
    const QVariantList list = m_speechModels.list(QStringLiteral("enhance"));
    return list.isEmpty() ? QVariantMap() : list.first().toMap();
}

bool ProjectController::enhanceSpeech(double clipId, const QVariantMap& options)
{
    if (analyzing()) {
        reportError(tr("An analysis is already running"));
        return false;
    }
    const auto location = m_project.locateClip(toId(clipId));
    const Clip* clip = location ? location->clip : nullptr;
    const MediaItem* media = clip ? m_project.findMedia(clip->mediaId) : nullptr;
    if (!clip || clip->generated() || clip->compound() || !media || !media->hasAudio
        || (location->track->kind == TrackKind::Video && !clip->audioEnabled)) {
        reportError(tr("Select a clip with sound to enhance"));
        return false;
    }
    if (!QFileInfo::exists(QString::fromStdString(media->path))) {
        reportError(tr("This clip's media is missing"));
        return false;
    }
    const auto modelPath = m_speechModels.path(QString::fromLatin1(kEnhanceModel));
    if (!modelPath) {
        reportError(tr("Download the speech enhancement model first"));
        return false;
    }
    double limit = options.value(QStringLiteral("limitDb"), 100.0).toDouble();
    limit = limit >= 100.0 ? 100.0 : std::clamp(std::round(limit), 3.0, 99.0);

    // <fingerprint>-dfn3.wav holds it fully cleaned; a limit mixes some of the original back.
    const QDir folder(enhancedDirectory());
    const QString stem = QString::fromStdString(fingerprintOf(*media)).replace(QLatin1Char(':'), QLatin1Char('_'))
                       + QStringLiteral("-dfn3");
    const std::string full = folder.filePath(stem + QStringLiteral(".wav")).toStdString();
    const std::string limited =
        limit >= 100.0 ? full : folder.filePath(stem + QStringLiteral("-%1dB.wav").arg(int(limit))).toStdString();
    const Id id = clip->id;
    const auto place = [this, id, limited, limit] {
        if (apply(std::make_unique<SetClipEnhancedAudioCommand>(id, EnhancedAudio{limited, limit})))
            emit analysisFinished(true, {});
        else
            emit analysisFinished(false, lastError());
    };
    const bool haveFull = QFileInfo::exists(QString::fromStdString(full));
    if (haveFull && QFileInfo::exists(QString::fromStdString(limited))) {
        place();
        return true;
    }
    runInBackground(haveFull ? tr("Changing the strength") : tr("Enhancing speech"),
                    [source = media->path, full, limited, limit, haveFull,
                     model = modelPath->toStdString()](const tve::AnalysisProgress& progress) -> std::string {
                        std::string error;
                        const double share = limit >= 100.0 ? 1.0 : haveFull ? 0.0 : 0.9;
                        if (!haveFull) {
                            const auto enhancer = tve::SpeechEnhancer::load(model, &error);
                            if (!enhancer)
                                return error;
                            if (!enhancer->enhanceFile(source, full, [&](double p) { return progress(p * share); }, &error))
                                return error.empty() ? std::string("cancelled") : error;
                        }
                        if (limited != full
                            && !tve::limitEnhancement(source, full, limit, limited,
                                                      [&](double p) { return progress(share + p * (1.0 - share)); }, &error))
                            return error.empty() ? std::string("cancelled") : error;
                        return {};
                    },
                    place);
    return true;
}

bool ProjectController::removeEnhancement(double clipId)
{
    const Clip* clip = findClip(toId(clipId));
    if (!clip || !clip->enhancedAudio) {
        reportError(tr("This clip's sound is not enhanced"));
        return false;
    }
    return apply(std::make_unique<SetClipEnhancedAudioCommand>(clip->id, std::nullopt));
}

bool ProjectController::duckUnderSpeech(double clipId, const QVariantMap& options)
{
    if (analyzing()) {
        reportError(tr("An analysis is already running"));
        return false;
    }
    const Clip* music = findClip(toId(clipId));
    const MediaItem* musicMedia = music ? m_project.findMedia(music->mediaId) : nullptr;
    if (!music || !musicMedia || !musicMedia->hasAudio) {
        reportError(tr("Select the music to lower under speech"));
        return false;
    }
    // The other clips heard at the same time, at normal speed, with their files.
    struct Voice {
        Id clipId;
        Id mediaId;
    };
    std::vector<Voice> voices;
    for (const Track& track : m_project.tracks) {
        if (track.muted || track.kind == TrackKind::Title)
            continue;
        for (const Clip& clip : track.clips) {
            const MediaItem* media = m_project.findMedia(clip.mediaId);
            if (clip.id == music->id || !media || !media->hasAudio || clip.end() <= music->start || clip.start >= music->end()
                || (track.kind == TrackKind::Video && !clip.audioEnabled) || clip.speed != 1.0 || clip.reversed
                || clip.freeze || !clip.speedRamp.empty() || !QFileInfo::exists(QString::fromStdString(media->path)))
                continue;
            voices.push_back({clip.id, clip.mediaId});
        }
    }
    if (voices.empty()) {
        reportError(tr("No other clip with sound plays under this one"));
        return false;
    }
    const double amount = std::clamp(options.value(QStringLiteral("amountDb"), -12.0).toDouble(), -40.0, -1.0);
    const double fade = std::clamp(options.value(QStringLiteral("fade"), 0.25).toDouble(), 0.05, 2.0);
    tve::SpeechOptions speech;
    speech.thresholdDb = options.value(QStringLiteral("thresholdDb"), speech.thresholdDb).toDouble();
    std::vector<Id> mediaIds;
    for (const Voice& v : voices)
        mediaIds.push_back(v.mediaId);
    const Id musicId = music->id;
    withFeatures(mediaIds, false, tr("Listening for speech"), [this, musicId, voices, amount, fade, speech] {
        const Clip* music = findClip(musicId);
        if (!music)
            return;
        // When anyone speaks, on the music clip's own time (from its start), merged where the
        // fades would meet.
        std::vector<std::pair<Ticks, Ticks>> spans;
        for (const Voice& v : voices) {
            const Clip* clip = findClip(v.clipId);
            const MediaItem* media = clip ? m_project.findMedia(clip->mediaId) : nullptr;
            if (!media)
                continue;
            for (const tve::TimeRange& r : tve::findSpeech(m_audioFeatures.at(fingerprintOf(*media)), speech)) {
                const Ticks from = std::max(r.start, clip->sourceIn) - clip->sourceIn + clip->start;
                const Ticks to = std::min(r.end, clip->sourceIn + clip->duration) - clip->sourceIn + clip->start;
                const Ticks a = std::max(from, music->start) - music->start;
                const Ticks b = std::min(to, music->end()) - music->start;
                if (b > a)
                    spans.emplace_back(a, b);
            }
        }
        if (spans.empty()) {
            reportError(tr("No speech under this clip"));
            emit analysisFinished(false, lastError());
            return;
        }
        std::ranges::sort(spans);
        const Ticks fadeTicks = Ticks(fade * kTicksPerSecond);
        std::vector<std::pair<Ticks, Ticks>> merged;
        for (const auto& span : spans) {
            if (!merged.empty() && span.first - merged.back().second <= 2 * fadeTicks)
                merged.back().second = std::max(merged.back().second, span.second);
            else
                merged.push_back(span);
        }
        // Down `fade` before each span, back up `fade` after it.
        const double base = music->gainDb;
        const double low = std::max<double>(kMinGainDb, base + amount);
        Keyframes keys;
        const auto key = [&](Ticks time, double value) {
            time = std::clamp<Ticks>(time, 0, music->duration);
            if (!keys.empty() && keys.back().time >= time)
                keys.back().value = std::min(keys.back().value, value);
            else
                keys.push_back({time, value, Easing::EaseInOut});
        };
        for (const auto& [from, to] : merged) {
            key(from - fadeTicks, base);
            key(from, low);
            key(to, low);
            key(to + fadeTicks, base);
        }
        Clip after = *music;
        after.keyframes[std::string(property::kVolume)] = keys;
        if (commitAnimation(*music, after, TVE_TR("core", "Auto Ducking")))
            emit analysisFinished(true, {});
        else
            emit analysisFinished(false, lastError());
    });
    return true;
}

// --- Captions ---

QVariantList ProjectController::captionLanguages() const
{
    static const std::pair<const char*, const char*> languages[] = {
        {"auto", TVE_TR("core", "Detect the language")}, {"en", TVE_TR("core", "English")}, {"es", TVE_TR("core", "Spanish")}, {"fr", TVE_TR("core", "French")}, {"de", TVE_TR("core", "German")},
        {"it", TVE_TR("core", "Italian")}, {"pt", TVE_TR("core", "Portuguese")}, {"nl", TVE_TR("core", "Dutch")}, {"sv", TVE_TR("core", "Swedish")}, {"pl", TVE_TR("core", "Polish")}, {"ru", TVE_TR("core", "Russian")},
        {"uk", TVE_TR("core", "Ukrainian")}, {"tr", TVE_TR("core", "Turkish")}, {"ar", TVE_TR("core", "Arabic")}, {"hi", TVE_TR("core", "Hindi")}, {"ta", TVE_TR("core", "Tamil")}, {"id", TVE_TR("core", "Indonesian")},
        {"vi", TVE_TR("core", "Vietnamese")}, {"ja", TVE_TR("core", "Japanese")}, {"ko", TVE_TR("core", "Korean")}, {"zh", TVE_TR("core", "Chinese")}};
    QVariantList list;
    for (const auto& [code, name] : languages)
        list << QVariantMap{{QStringLiteral("code"), QString::fromLatin1(code)}, {QStringLiteral("name"), coreText(name)}};
    return list;
}

int ProjectController::captionCount() const
{
    const auto track = std::ranges::find_if(m_project.tracks, [](const Track& t) { return t.captions; });
    return track == m_project.tracks.end() ? 0 : int(track->clips.size());
}

bool ProjectController::downloadSpeechModel(const QString& modelId)
{
    QString error;
    if (!m_speechModels.download(modelId, &error)) {
        reportError(error);
        return false;
    }
    return true;
}

void ProjectController::cancelModelDownload()
{
    m_speechModels.cancel();
}

void ProjectController::runInBackground(const QString& task, std::function<std::string(const tve::AnalysisProgress&)> work,
                                        std::function<void()> then)
{
    m_cancelAnalysis = false;
    m_analysisProgress = 0;
    m_analysisTask = task;
    m_analysisThread = std::thread([this, work = std::move(work), then = std::move(then)]() mutable {
        double reported = -1;
        const std::string error = work([&](double done) {
            if (done - reported >= 0.01) {
                reported = done;
                QMetaObject::invokeMethod(this, [this, done] {
                    m_analysisProgress = done;
                    emit analysisChanged();
                }, Qt::QueuedConnection);
            }
            return !m_cancelAnalysis.load();
        });
        QMetaObject::invokeMethod(this, [this, error, then = std::move(then)]() mutable {
            if (m_analysisThread.joinable())
                m_analysisThread.join();
            m_analysisProgress = 0;
            m_analysisTask.clear();
            emit analysisChanged();
            if (error.empty()) {
                then();
                return;
            }
            const QString message = error == "cancelled" ? QString() : coreText(error);
            if (!message.isEmpty())
                reportError(message);
            emit analysisFinished(false, message);
        }, Qt::QueuedConnection);
    });
    emit analysisChanged();
}

bool ProjectController::generateCaptions(const QVariantMap& options)
{
    return transcribeClips(options, true);
}

bool ProjectController::transcribe(const QVariantMap& options)
{
    return transcribeClips(options, false);
}

std::vector<ProjectController::PlacedWord> ProjectController::placedWords(const Project& project, const std::string& variant,
                                                                          const std::set<Id>* only) const
{
    std::vector<PlacedWord> words;
    if (variant.empty())
        return words;
    for (const Track& track : project.tracks) {
        if (track.kind == TrackKind::Title)
            continue;
        for (const Clip& clip : track.clips) {
            const MediaItem* media = project.findMedia(clip.mediaId);
            if ((only && !only->contains(clip.id)) || !media || clip.speed != 1.0 || clip.reversed || clip.freeze
                || !clip.speedRamp.empty() || (track.kind == TrackKind::Video && !clip.audioEnabled))
                continue;
            const auto found = m_transcripts.find(fingerprintOf(*media) + "|" + variant);
            if (found == m_transcripts.end())
                continue;
            const Ticks from = clip.sourceIn;
            const Ticks to = clip.sourceIn + clip.duration;
            for (const tve::TranscriptWord& w : found->second.words) {
                if (w.start < from || w.start >= to)
                    continue;
                words.push_back({w.start - from + clip.start, std::min(w.end, to) - from + clip.start, w.text, clip.id, track.id});
            }
        }
    }
    std::ranges::stable_sort(words, {}, &PlacedWord::start);
    return words;
}

std::pair<std::optional<ClipText>, std::optional<ClipVisual>> ProjectController::captionLook() const
{
    const auto track = std::ranges::find_if(m_project.tracks, [](const Track& t) { return t.captions; });
    if (track == m_project.tracks.end() || track->clips.empty() || !track->clips.front().text)
        return {};
    return {track->clips.front().text, track->clips.front().visual};
}

bool ProjectController::transcribeClips(const QVariantMap& options, bool makeCaptions)
{
    if (analyzing()) {
        reportError(tr("An analysis is already running"));
        return false;
    }
    const QString modelId = options.value(QStringLiteral("model"), QStringLiteral("base.en-q5_1")).toString();
    const SpeechModels::Model* model = SpeechModels::find(modelId);
    const auto modelPath = m_speechModels.path(modelId);
    if (!model || !modelPath) {
        reportError(tr("Download the speech model first"));
        return false;
    }
    std::string language = options.value(QStringLiteral("language"), QStringLiteral("auto")).toString().toStdString();
    if (model->englishOnly)
        language = "en";

    // Whose speech: the selected clips with sound, or every clip heard.
    std::set<Id> chosen;
    for (const QVariant& id : selectedClips())
        chosen.insert(toId(id.toDouble()));
    std::set<Id> voices;
    std::vector<Id> mediaIds;
    for (const Track& track : m_project.tracks) {
        if (track.kind == TrackKind::Title || (chosen.empty() && track.muted))
            continue;
        for (const Clip& clip : track.clips) {
            const MediaItem* media = m_project.findMedia(clip.mediaId);
            if ((!chosen.empty() && !chosen.contains(clip.id)) || !media || !media->hasAudio
                || (track.kind == TrackKind::Video && !clip.audioEnabled) || clip.speed != 1.0 || clip.reversed
                || clip.freeze || !clip.speedRamp.empty() || !QFileInfo::exists(QString::fromStdString(media->path)))
                continue;
            voices.insert(clip.id);
            mediaIds.push_back(clip.mediaId);
        }
    }
    if (voices.empty()) {
        reportError(chosen.empty() ? tr("Add clips with sound (at normal speed) to make captions from")
                                   : tr("The selected clips have no sound to make captions from"));
        return false;
    }
    // What still needs transcribing (the rest is remembered, here or on disk).
    const std::string variant = modelId.toStdString() + "." + language;
    const tve::AnalysisCache cache(analysisDirectory().toStdString());
    struct Job {
        std::string path;
        std::string key;
        std::string fingerprint;
    };
    std::vector<Job> jobs;
    for (Id mediaId : mediaIds) {
        const MediaItem& media = *m_project.findMedia(mediaId);
        const std::string fingerprint = fingerprintOf(media);
        const std::string key = fingerprint + "|" + variant;
        if (m_transcripts.contains(key) || std::ranges::any_of(jobs, [&](const Job& j) { return j.key == key; }))
            continue;
        if (auto kept = tve::loadTranscript(cache, fingerprint, variant)) {
            m_transcripts.emplace(key, std::move(*kept));
            continue;
        }
        jobs.push_back({media.path, key, fingerprint});
    }

    auto results = std::make_shared<std::vector<std::pair<std::string, tve::Transcript>>>();
    const auto place = [this, voices, variant, makeCaptions] {
        m_transcriptVariant = variant;
        emit transcriptChanged();
        const auto placed = placedWords(m_project, variant, &voices);
        if (placed.empty()) {
            reportError(tr("No speech found"));
            emit analysisFinished(false, lastError());
            return;
        }
        if (!makeCaptions) {
            emit analysisFinished(true, {});
            return;
        }
        std::vector<tve::TimedWord> words;
        for (const PlacedWord& w : placed)
            words.push_back({w.start, w.end, w.text});
        const auto [style, visual] = captionLook();
        if (apply(std::make_unique<SetCaptionsCommand>(tve::layoutCaptions(words), TVE_TR("core", "Generate Captions"), style, visual)))
            emit analysisFinished(true, {});
        else
            emit analysisFinished(false, lastError());
    };
    if (jobs.empty()) {
        place();
        return true;
    }
    runInBackground(QStringLiteral("Transcribing"),
                    [jobs, results, modelPath = modelPath->toStdString(), language, variant,
                     directory = analysisDirectory().toStdString()](const tve::AnalysisProgress& progress) -> std::string {
                        const tve::AnalysisCache cache(directory);
                        for (std::size_t i = 0; i < jobs.size(); ++i) {
                            std::string error;
                            auto transcript = tve::transcribe(jobs[i].path, modelPath, {.language = language},
                                                              [&](double part) { return progress((double(i) + part) / double(jobs.size())); },
                                                              &error);
                            if (!transcript)
                                return error.empty() ? std::string("cancelled") : error;
                            tve::saveTranscript(cache, jobs[i].fingerprint, variant, *transcript);
                            results->emplace_back(jobs[i].key, std::move(*transcript));
                        }
                        return {};
                    },
                    [this, results, place] {
                        for (auto& [key, transcript] : *results)
                            m_transcripts.insert_or_assign(key, std::move(transcript));
                        place();
                    });
    return true;
}

QVariantList ProjectController::transcript() const
{
    QVariantList list;
    int index = 0;
    for (const PlacedWord& w : placedWords(m_project, m_transcriptVariant)) {
        list << QVariantMap{{QStringLiteral("index"), index++},
                            {QStringLiteral("text"), QString::fromStdString(w.text)},
                            {QStringLiteral("start"), seconds(w.start)},
                            {QStringLiteral("end"), seconds(w.end)},
                            {QStringLiteral("clipId"), double(w.clipId)},
                            {QStringLiteral("filler"), tve::isFillerWord(w.text)}};
    }
    return list;
}

bool ProjectController::cutRanges(const std::map<Id, std::vector<std::pair<Ticks, Ticks>>>& byTrack, const char* label)
{
    if (byTrack.empty())
        return false;
    const auto removals = [&] {
        std::vector<std::unique_ptr<Command>> commands;
        for (const auto& [trackId, ranges] : byTrack)
            commands.push_back(std::make_unique<RemoveRangesCommand>(trackId, ranges, label));
        return commands;
    };
    std::vector<std::unique_ptr<Command>> commands = removals();
    // The captions, made again from the transcript as it will be after the cut.
    if (captionCount() > 0) {
        Project after = m_project;
        for (auto& command : removals()) {
            if (auto error = command->redo(after)) {
                reportError(QString::fromStdString(*error));
                return false;
            }
        }
        std::vector<tve::TimedWord> words;
        for (const PlacedWord& w : placedWords(after, m_transcriptVariant))
            words.push_back({w.start, w.end, w.text});
        const auto [style, visual] = captionLook();
        commands.push_back(std::make_unique<SetCaptionsCommand>(tve::layoutCaptions(words), label, style, visual));
    }
    return apply(std::make_unique<CompositeCommand>(std::move(commands), label));
}

bool ProjectController::deleteWords(const QVariantList& indices)
{
    const auto words = placedWords(m_project, m_transcriptVariant);
    std::vector<int> chosen;
    for (const QVariant& i : indices)
        if (const int index = i.toInt(); index >= 0 && index < int(words.size()))
            chosen.push_back(index);
    std::ranges::sort(chosen);
    chosen.erase(std::unique(chosen.begin(), chosen.end()), chosen.end());
    if (chosen.empty()) {
        reportError(tr("Select the words to delete"));
        return false;
    }
    // Runs of neighbouring words on one track become one range: from the first word's start to
    // the next kept word on that track (or a quarter second past the last word, if sooner).
    const auto nextOnTrack = [&](int i) {
        for (int j = i + 1; j < int(words.size()); ++j)
            if (words[std::size_t(j)].trackId == words[std::size_t(i)].trackId)
                return j;
        return -1;
    };
    std::map<Id, std::vector<std::pair<Ticks, Ticks>>> byTrack;
    for (std::size_t k = 0; k < chosen.size();) {
        const int first = chosen[k];
        int last = first;
        while (k + 1 < chosen.size() && nextOnTrack(last) == chosen[k + 1])
            last = chosen[++k];
        ++k;
        const PlacedWord& a = words[std::size_t(first)];
        const PlacedWord& b = words[std::size_t(last)];
        const int after = nextOnTrack(last);
        const Ticks tail = b.end + kTicksPerSecond / 4;
        const Ticks end = after >= 0 ? std::min(words[std::size_t(after)].start, tail) : b.end;
        byTrack[a.trackId].emplace_back(a.start, std::max(end, b.end));
    }
    return cutRanges(byTrack, TVE_TR("core", "Delete Words"));
}

int ProjectController::removeFillerWords()
{
    QVariantList fillers;
    for (const QVariant& w : transcript())
        if (w.toMap().value(QStringLiteral("filler")).toBool())
            fillers << w.toMap().value(QStringLiteral("index"));
    if (fillers.isEmpty()) {
        reportError(tr("No filler words in the transcript"));
        return 0;
    }
    return deleteWords(fillers) ? int(fillers.size()) : 0;
}

int ProjectController::removeTranscriptPauses(double longerThan, double keep)
{
    const auto words = placedWords(m_project, m_transcriptVariant);
    const Ticks minimum = ticks(longerThan);
    const Ticks margin = ticks(std::max(0.0, keep));
    std::map<Id, std::vector<std::pair<Ticks, Ticks>>> byTrack;
    std::map<Id, const PlacedWord*> previous;
    int count = 0;
    for (const PlacedWord& w : words) {
        if (const auto found = previous.find(w.trackId); found != previous.end()) {
            const Ticks gap = w.start - found->second->end;
            if (gap > minimum && gap > 2 * margin) {
                byTrack[w.trackId].emplace_back(found->second->end + margin, w.start - margin);
                ++count;
            }
        }
        previous[w.trackId] = &w;
    }
    if (count == 0) {
        reportError(tr("No pauses that long between words"));
        return 0;
    }
    return cutRanges(byTrack, TVE_TR("core", "Remove Pauses")) ? count : 0;
}

QString ProjectController::captionMotion() const
{
    static const char* const names[] = {"none", "highlight", "pop", "karaoke", "reveal"};
    const auto [style, visual] = captionLook();
    return style ? QString::fromLatin1(names[int(style->captionMotion)]) : QStringLiteral("none");
}

bool ProjectController::setCaptionStyle(const QString& motion, const QVariant& highlightColor)
{
    const auto track = std::ranges::find_if(m_project.tracks, [](const Track& t) { return t.captions; });
    if (track == m_project.tracks.end() || track->clips.empty()) {
        reportError(tr("Make captions first"));
        return false;
    }
    QVariantMap changes{{QStringLiteral("captionMotion"), motion}};
    if (highlightColor.isValid())
        changes.insert(QStringLiteral("highlightColor"), highlightColor);
    std::vector<std::unique_ptr<Command>> commands;
    for (const Clip& clip : track->clips) {
        if (!clip.text)
            continue;
        const ClipText changed = textFrom(changes, *clip.text);
        if (changed != *clip.text)
            commands.push_back(std::make_unique<SetClipTextCommand>(clip.id, changed));
    }
    if (commands.empty())
        return true;
    return apply(std::make_unique<CompositeCommand>(std::move(commands), TVE_TR("core", "Caption Style")));
}

std::vector<tve::CaptionBlock> ProjectController::captionBlocks() const
{
    std::vector<tve::CaptionBlock> blocks;
    const auto track = std::ranges::find_if(m_project.tracks, [](const Track& t) { return t.captions; });
    if (track == m_project.tracks.end())
        return blocks;
    for (const Clip& clip : track->clips)
        if (clip.text) {
            CaptionBlock block{clip.start, clip.end(), clip.text->text, {}};
            for (const CaptionWord& w : clip.text->words)
                block.words.push_back({clip.start + w.start, clip.start + w.end, w.text});
            blocks.push_back(std::move(block));
        }
    return blocks;
}

bool ProjectController::exportSubtitles(const QUrl& file)
{
    if (!m_openLevels.empty())
        closeCompoundClip(0); // (the whole project: back to the main timeline first)
    const QString path = file.isLocalFile() ? file.toLocalFile() : file.toString();
    const auto blocks = captionBlocks();
    if (blocks.empty()) {
        reportError(tr("There are no captions to export"));
        return false;
    }
    const bool vtt = path.endsWith(QLatin1String(".vtt"), Qt::CaseInsensitive);
    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly)) {
        reportError(tr("Cannot write %1").arg(path));
        return false;
    }
    out.write(QByteArray::fromStdString(vtt ? tve::writeVtt(blocks) : tve::writeSrt(blocks)));
    if (!out.commit()) {
        reportError(tr("Cannot write %1").arg(path));
        return false;
    }
    return true;
}

bool ProjectController::importSubtitles(const QUrl& file)
{
    const QString path = file.isLocalFile() ? file.toLocalFile() : file.toString();
    QFile in(path);
    if (!in.open(QIODevice::ReadOnly) || in.size() > 20 * 1024 * 1024) {
        reportError(tr("Cannot read %1").arg(QFileInfo(path).fileName()));
        return false;
    }
    std::string error;
    const auto blocks = tve::readSubtitles(in.readAll().toStdString(), &error);
    if (!blocks) {
        reportError(QStringLiteral("%1: %2").arg(QFileInfo(path).fileName(), coreText(error)));
        return false;
    }
    return apply(std::make_unique<SetCaptionsCommand>(*blocks, TVE_TR("core", "Import Captions")));
}

// --- Tracks, the clipboard and media (right-click menus) ---

void ProjectController::selectClips(std::vector<Id> ids)
{
    std::erase_if(ids, [&](Id id) { return !findClip(id); });
    m_selectedClips = std::move(ids);
    m_selectedClip = m_selectedClips.empty() ? kNoId : m_selectedClips.front();
    m_selectedTransition = kNoId;
    emit selectionChanged();
    emit selectedClipInfoChanged();
}

std::vector<Id> ProjectController::clipsToCopy() const
{
    std::vector<Id> ids;
    for (Id id : m_selectedClips)
        if (findClip(id))
            ids.push_back(id);
    if (ids.empty() && findClip(m_selectedClip))
        ids.push_back(m_selectedClip);
    return ids;
}

double ProjectController::addTrack(const QString& kind, double besideTrackId, bool above)
{
    const TrackKind trackKind = kind == QLatin1String("audio") ? TrackKind::Audio
                              : kind == QLatin1String("title") ? TrackKind::Title
                                                                : TrackKind::Video;
    std::size_t index = m_project.tracks.size();
    if (const auto beside = m_project.trackIndex(toId(besideTrackId)))
        index = *beside + (above ? 1 : 0);
    const auto count = std::ranges::count(m_project.tracks, trackKind, &Track::kind);
    const char* name = trackKind == TrackKind::Audio ? "Audio" : trackKind == TrackKind::Title ? "Title" : "Video";
    auto command = std::make_unique<InsertTrackCommand>(trackKind, std::string(name) + " " + std::to_string(count + 1), index);
    auto* raw = command.get();
    if (!apply(std::move(command)))
        return 0;
    return double(raw->trackId());
}

bool ProjectController::removeTrack(double trackId)
{
    const Id id = toId(trackId);
    const bool hadSelection = std::ranges::any_of(m_selectedClips, [&](Id clip) {
        const auto location = m_project.locateClip(clip);
        return location && location->track->id == id;
    });
    if (!apply(std::make_unique<RemoveTrackCommand>(id)))
        return false;
    if (hadSelection)
        setSelectedClip(0);
    return true;
}

bool ProjectController::renameTrack(double trackId, const QString& name)
{
    return apply(std::make_unique<RenameTrackCommand>(toId(trackId), name.toStdString()));
}

int ProjectController::removeEmptyTracks()
{
    // Empty, unlocked ones; a video track always stays (the lowest, the main track).
    std::vector<std::unique_ptr<Command>> steps;
    auto videos = std::ranges::count(m_project.tracks, TrackKind::Video, &Track::kind);
    for (auto track = m_project.tracks.rbegin(); track != m_project.tracks.rend(); ++track) {
        if (!track->clips.empty() || track->locked)
            continue;
        if (track->kind == TrackKind::Video) {
            if (videos == 1)
                continue;
            --videos;
        }
        steps.push_back(std::make_unique<RemoveTrackCommand>(track->id));
    }
    if (steps.empty()) {
        reportError(tr("There are no empty tracks to remove"));
        return 0;
    }
    const int removed = int(steps.size());
    if (!apply(std::make_unique<CompositeCommand>(std::move(steps), TVE_TR("core", "Delete Empty Tracks"))))
        return 0;
    return removed;
}

bool ProjectController::copyClips()
{
    const std::vector<Id> ids = clipsToCopy();
    if (ids.empty()) {
        reportError(tr("Select the clips to copy"));
        return false;
    }
    m_clipboard = tve::copyClips(m_project, ids);
    emit clipboardChanged();
    return true;
}

bool ProjectController::cutClips()
{
    const std::vector<Id> ids = clipsToCopy();
    if (ids.empty()) {
        reportError(tr("Select the clips to cut"));
        return false;
    }
    if (std::ranges::any_of(ids, [&](Id id) { return m_project.locateClip(id)->track->locked; })) {
        reportError(tr("A clip to cut is on a locked track"));
        return false;
    }
    m_clipboard = tve::copyClips(m_project, ids);
    emit clipboardChanged();
    // Latest first, so closing gaps on the main track moves none still to go.
    std::vector<std::pair<Ticks, Id>> order;
    for (Id id : ids)
        order.emplace_back(m_project.locateClip(id)->clip->start, id);
    std::ranges::sort(order, std::greater{});
    std::vector<std::unique_ptr<Command>> steps;
    for (const auto& [start, id] : order)
        steps.push_back(std::make_unique<DeleteClipCommand>(id, m_project.locateClip(id)->track->id == mainTrackId()));
    if (!apply(std::make_unique<CompositeCommand>(std::move(steps), TVE_TR("core", "Cut"))))
        return false;
    setSelectedClip(0);
    return true;
}

bool ProjectController::pasteClips(double trackId, double seconds)
{
    if (m_clipboard.empty()) {
        reportError(tr("Copy or cut some clips first"));
        return false;
    }
    Id target = toId(trackId);
    if (trackId < 0) {
        const auto location = m_project.locateClip(m_selectedClip);
        target = location ? location->track->id : mainTrackId();
    }
    const Ticks at = ticks(seconds < 0 ? m_playhead : seconds);
    auto command = std::make_unique<PasteClipsCommand>(m_clipboard, at, target, TVE_TR("core", "Paste"));
    auto* raw = command.get();
    if (!apply(std::move(command)))
        return false;
    selectClips(raw->pastedIds());
    return true;
}

bool ProjectController::duplicateClips()
{
    const std::vector<Id> ids = clipsToCopy();
    if (ids.empty()) {
        reportError(tr("Select the clips to duplicate"));
        return false;
    }
    const std::vector<CopiedClip> copies = tve::copyClips(m_project, ids);
    // Right after them, counted from the track the first lane-0 copy came from.
    Ticks end = 0;
    for (Id id : ids)
        end = std::max(end, m_project.locateClip(id)->clip->end());
    Id target = m_project.locateClip(copies.front().clip.id)->track->id;
    for (const CopiedClip& copy : copies) {
        if (copy.lane == 0 && copy.kind == copies.front().kind) {
            target = m_project.locateClip(copy.clip.id)->track->id;
            break;
        }
    }
    auto command = std::make_unique<PasteClipsCommand>(copies, end, target, TVE_TR("core", "Duplicate"));
    auto* raw = command.get();
    if (!apply(std::move(command)))
        return false;
    selectClips(raw->pastedIds());
    return true;
}

bool ProjectController::rippleDeleteClip(double clipId)
{
    if (!findClip(toId(clipId))) {
        reportError(tr("Select a clip to delete"));
        return false;
    }
    if (!apply(std::make_unique<DeleteClipCommand>(toId(clipId), true)))
        return false;
    setSelectedClip(0);
    return true;
}

int ProjectController::selectAllClips(double trackId)
{
    std::vector<Id> ids;
    for (const Track& track : m_project.tracks) {
        if (trackId >= 0 && track.id != toId(trackId))
            continue;
        for (const Clip& clip : track.clips)
            ids.push_back(clip.id);
    }
    selectClips(ids);
    return int(m_selectedClips.size());
}

int ProjectController::clipsUsingMedia(double mediaId) const
{
    return int(tve::clipsUsingMedia(m_project, toId(mediaId)));
}

bool ProjectController::removeMedia(double mediaId)
{
    if (!apply(std::make_unique<RemoveMediaCommand>(toId(mediaId))))
        return false;
    if (!findClip(m_selectedClip) || std::ranges::any_of(m_selectedClips, [&](Id id) { return !findClip(id); }))
        selectClips(clipsToCopy());
    return true;
}

bool ProjectController::revealMedia(double mediaId)
{
    const MediaItem* media = m_project.findMedia(toId(mediaId));
    if (!media || !QFileInfo::exists(QString::fromStdString(media->path))) {
        reportError(tr("The file is not where it was"));
        return false;
    }
    return QProcess::startDetached(QStringLiteral("/usr/bin/open"), {QStringLiteral("-R"), QString::fromStdString(media->path)});
}

double ProjectController::addMediaAt(double mediaId, double seconds)
{
    const MediaItem* media = m_project.findMedia(toId(mediaId));
    if (!media) {
        reportError(tr("Media not found"));
        return 0;
    }
    const TrackKind kind = media->hasVideo ? TrackKind::Video : TrackKind::Audio;
    const Ticks at = std::max<Ticks>(0, ticks(seconds < 0 ? m_playhead : seconds));
    Project after = m_project;
    Id target = kNoId;
    for (const Track& track : after.tracks) {
        if (track.kind == kind && !track.locked && !track.overlaps(at, media->duration)) {
            target = track.id;
            break;
        }
    }
    if (target == kNoId) {
        // Pictures on a new track on top; sound on a new one at the bottom.
        const auto count = std::ranges::count(after.tracks, kind, &Track::kind);
        InsertTrackCommand track(kind, std::string(kind == TrackKind::Audio ? "Audio " : "Video ") + std::to_string(count + 1),
                                 kind == TrackKind::Audio ? 0 : after.tracks.size());
        if (auto error = track.redo(after)) {
            reportError(coreText(*error));
            return 0;
        }
        target = track.trackId();
    }
    AddClipCommand add(target, media->id, at, media->duration, 0);
    if (auto error = add.redo(after)) {
        reportError(coreText(*error));
        return 0;
    }
    m_project.setNextId(after.nextId()); // (the ids the edit took stay taken)
    if (!apply(std::make_unique<ApplyEditsCommand>(m_project, std::move(after), TVE_TR("core", "Add Clip"))))
        return 0;
    setSelectedClip(double(add.clipId()));
    return double(add.clipId());
}

bool ProjectController::dropMediaAt(double mediaId, double trackId, double seconds)
{
    const MediaItem* media = m_project.findMedia(toId(mediaId));
    const Track* track = m_project.findTrack(toId(trackId));
    if (!media) {
        reportError(tr("Media not found"));
        return false;
    }
    const Ticks at = std::max<Ticks>(0, ticks(seconds));
    // (Pictures go on video tracks, sound on audio tracks.)
    const bool fits = track && (track->kind == TrackKind::Video ? media->hasVideo
                                : track->kind == TrackKind::Audio && media->hasAudio && !media->hasVideo);
    if (fits && !track->locked && !track->overlaps(at, media->duration))
        return addClip(double(media->id), double(track->id), ::seconds(at));
    return addMediaAt(double(media->id), ::seconds(at)) > 0;
}

int ProjectController::dropFilesAt(const QList<QUrl>& files, double trackId, double seconds)
{
    int placed = 0;
    double at = std::max(0.0, seconds);
    for (const QUrl& file : files) {
        if (importMedia({file}) == 0)
            continue;
        const std::string path = (file.isLocalFile() ? file.toLocalFile() : file.toString()).toStdString();
        const auto media = std::ranges::find(m_project.media, path, &MediaItem::path);
        if (media == m_project.media.end())
            continue;
        const double length = ::seconds(media->duration);
        if (dropMediaAt(double(media->id), trackId, at)) {
            ++placed;
            at += length;
        }
    }
    return placed;
}

// --- Shuttle, marks, the blade, gaps, attributes (24c) ---

double ProjectController::shuttleRate() const
{
    return m_shuttleRate != 0 ? m_shuttleRate : (playing() ? 1.0 : 0.0);
}

void ProjectController::shuttle(int direction)
{
    if (direction == 0) {
        pause();
        return;
    }
    const double rate = shuttleRate();
    const double next = direction > 0 ? (rate <= 0 ? 1.0 : std::min(rate * 2, 8.0))
                                      : (rate >= 0 ? -1.0 : std::max(rate * 2, -8.0));
    if (next == 1.0) {
        play(); // (stops any shuttle)
        return;
    }
    m_playUntil = -1;
    m_playback->pause();
    m_shuttleRate = next;
    m_shuttleLast = QDateTime::currentMSecsSinceEpoch();
    m_shuttleTimer.start();
    emit playingChanged();
}

double ProjectController::parseTimecode(const QString& text) const
{
    const QString t = text.trimmed();
    if (t.isEmpty())
        return -1;
    bool ok = false;
    if (!t.contains(QLatin1Char(':')) && !t.contains(QLatin1Char(';')) && t.contains(QLatin1Char('.'))) {
        const double value = t.toDouble(&ok);
        return ok && value >= 0 ? value : -1;
    }
    const QStringList parts = t.split(QRegularExpression(QStringLiteral("[:;]")));
    if (parts.size() > 4)
        return -1;
    std::array<long long, 4> values{0, 0, 0, 0}; // frames, seconds, minutes, hours
    for (qsizetype i = 0; i < parts.size(); ++i) {
        const long long value = parts[parts.size() - 1 - i].toLongLong(&ok);
        if (!ok || value < 0)
            return -1;
        values[std::size_t(i)] = value;
    }
    const double fps = frameRate() > 0 ? frameRate() : 30.0;
    return double((values[3] * 60 + values[2]) * 60 + values[1]) + double(values[0]) / fps;
}

void ProjectController::setMarkIn(double secondsAt)
{
    const double at = std::clamp(secondsAt < 0 ? m_playhead : secondsAt, 0.0, ::seconds(m_project.duration()));
    m_markIn = at;
    if (m_markOut >= 0 && m_markOut <= at)
        m_markOut = -1;
    emit marksChanged();
}

void ProjectController::setMarkOut(double secondsAt)
{
    const double at = std::clamp(secondsAt < 0 ? m_playhead : secondsAt, 0.0, ::seconds(m_project.duration()));
    m_markOut = at;
    if (m_markIn >= 0 && m_markIn >= at)
        m_markIn = -1;
    emit marksChanged();
}

void ProjectController::clearMarks()
{
    if (m_markIn < 0 && m_markOut < 0)
        return;
    m_markIn = m_markOut = -1;
    emit marksChanged();
}

bool ProjectController::playMarkedRange()
{
    const double from = m_markIn >= 0 ? m_markIn : 0.0;
    const double to = m_markOut >= 0 ? m_markOut : ::seconds(m_project.duration());
    if (m_markIn < 0 && m_markOut < 0) {
        reportError(tr("Mark an In or Out point first (I, O)"));
        return false;
    }
    seek(from);
    play();
    m_playUntil = to;
    return true;
}

bool ProjectController::deleteMarkedRange()
{
    if (m_markIn < 0 || m_markOut < 0) {
        reportError(tr("Mark both an In and an Out point to delete the range between them"));
        return false;
    }
    const Ticks from = ticks(m_markIn);
    const Ticks to = ticks(m_markOut);
    std::vector<std::unique_ptr<Command>> steps;
    for (const Track& track : m_project.tracks) {
        const bool touches = std::ranges::any_of(track.clips, [&](const Clip& c) { return c.start < to && c.end() > from; });
        if (!track.locked && (touches || std::ranges::any_of(track.clips, [&](const Clip& c) { return c.start >= to; })))
            steps.push_back(std::make_unique<RemoveRangesCommand>(track.id, std::vector<std::pair<Ticks, Ticks>>{{from, to}}));
    }
    if (steps.empty()) {
        reportError(tr("There is nothing in the marked range"));
        return false;
    }
    if (!apply(std::make_unique<CompositeCommand>(std::move(steps), TVE_TR("core", "Delete the Range"))))
        return false;
    const double in = m_markIn;
    clearMarks();
    setSelectedClip(0);
    seek(in);
    return true;
}

bool ProjectController::splitClipAt(double clipId, double secondsAt)
{
    const Clip* clip = findClip(toId(clipId));
    // On the frame grid, inside the clip.
    const Ticks frame = frameTicks();
    const Ticks at = (ticks(secondsAt) + frame / 2) / frame * frame;
    if (!clip || at <= clip->start || at >= clip->end()) {
        reportError(tr("Click inside a clip to cut it there"));
        return false;
    }
    return apply(std::make_unique<SplitClipCommand>(clip->id, at));
}

int ProjectController::closeGaps(double trackId)
{
    Project after = m_project;
    int closed = 0;
    for (Track& track : after.tracks) {
        if ((trackId >= 0 && track.id != toId(trackId)) || track.locked || track.clips.size() < 2)
            continue;
        // Between clips (a track's lead-in stays: a title or sound may start late on purpose).
        Ticks shift = 0;
        for (std::size_t i = 1; i < track.clips.size(); ++i) {
            Clip& clip = track.clips[i];
            const Ticks previousEnd = track.clips[i - 1].end();
            clip.start -= shift;
            if (clip.start > previousEnd) {
                shift += clip.start - previousEnd;
                clip.start = previousEnd;
                ++closed;
            }
        }
    }
    if (closed == 0) {
        reportError(tr("There are no gaps between clips to close"));
        return 0;
    }
    if (!apply(std::make_unique<ApplyEditsCommand>(m_project, std::move(after), TVE_TR("core", "Close Gaps"))))
        return 0;
    return closed;
}

bool ProjectController::closeGapAt(double trackId, double secondsAt)
{
    Project after = m_project;
    Track* track = after.findTrack(toId(trackId));
    const Ticks at = ticks(secondsAt);
    if (!track || track->locked) {
        reportError(tr("That track cannot be changed"));
        return false;
    }
    // The gap holding `at`: from the end of the clip before (or the start) to the next clip.
    Ticks gapStart = 0;
    auto next = track->clips.end();
    for (auto it = track->clips.begin(); it != track->clips.end(); ++it) {
        if (it->start <= at && at < it->end()) {
            reportError(tr("Right-click the empty space between clips to close it"));
            return false;
        }
        if (it->start > at) {
            next = it;
            break;
        }
        gapStart = it->end();
    }
    if (next == track->clips.end() || next->start <= gapStart) {
        reportError(tr("Right-click the empty space between clips to close it"));
        return false;
    }
    const Ticks gap = next->start - gapStart;
    for (auto it = next; it != track->clips.end(); ++it)
        it->start -= gap;
    return apply(std::make_unique<ApplyEditsCommand>(m_project, std::move(after), TVE_TR("core", "Close Gap")));
}

bool ProjectController::copyAttributes(double clipId)
{
    const Clip* clip = findClip(toId(clipId));
    if (!clip) {
        reportError(tr("Select a clip to copy its attributes"));
        return false;
    }
    m_copiedAttributes = *clip;
    emit clipboardChanged();
    return true;
}

bool ProjectController::pasteAttributes(const QStringList& parts, double clipId)
{
    if (!m_copiedAttributes) {
        reportError(tr("Copy a clip's attributes first"));
        return false;
    }
    std::vector<Id> targets = clipId > 0 ? std::vector<Id>{toId(clipId)} : clipsToCopy();
    std::erase(targets, m_copiedAttributes->id);
    if (targets.empty() || parts.isEmpty()) {
        reportError(tr("Select the clips to paste the attributes onto"));
        return false;
    }
    const Clip& from = *m_copiedAttributes;
    const auto has = [&](const char* part) { return parts.contains(QLatin1String(part)); };
    static const std::vector<std::string_view> visualKeys{property::kX, property::kY, property::kScale, property::kRotation,
                                                          property::kOpacity, property::kCropLeft, property::kCropTop,
                                                          property::kCropRight, property::kCropBottom};
    const auto copyKeys = [](Clip& to, const Clip& source, const std::vector<std::string_view>& ids) {
        for (std::string_view id : ids) {
            const std::string key(id);
            to.keyframes.erase(key);
            if (const auto found = source.keyframes.find(key); found != source.keyframes.end())
                to.keyframes[key] = found->second;
        }
    };
    Project after = m_project;
    int changed = 0;
    for (Id id : targets) {
        auto location = after.locateClip(id);
        if (!location || location->track->locked)
            continue;
        Clip& to = *location->clip;
        const bool picture = location->track->kind != TrackKind::Audio;
        const MediaItem* media = after.findMedia(to.mediaId);
        const bool sound = !to.generated() && (to.compound() || (media && media->hasAudio));
        const Clip before = to;
        if (has("visual") && picture) {
            to.visual = from.visual;
            copyKeys(to, from, visualKeys);
        }
        if (has("color") && picture && !to.text) {
            to.color = from.color;
            to.look = from.look;
        }
        if (has("effects") && picture)
            to.effects = from.effects;
        if (has("keying") && picture) {
            to.chromaKey = from.chromaKey;
            to.masks = from.masks;
        }
        if (has("audio") && sound) {
            to.gainDb = from.gainDb;
            to.fadeIn = std::min(from.fadeIn, to.duration);
            to.fadeOut = std::min(from.fadeOut, to.duration - to.fadeIn);
            to.audioEffects = from.audioEffects;
            to.keepPitch = from.keepPitch;
            copyKeys(to, from, {property::kVolume});
        }
        bool sped = false;
        if (has("speed") && !to.generated() && !(media && media->still)) {
            // As the speed commands do it (the length changes, later clips follow).
            const bool same = to.speed == from.speed && to.reversed == from.reversed && to.speedRamp == from.speedRamp;
            if (!same) {
                if (!from.speedRamp.empty())
                    sped = !SetClipSpeedRampCommand(id, from.speedRamp, from.reversed).redo(after);
                else
                    sped = !SetClipSpeedCommand(id, from.speed, from.reversed).redo(after);
            }
        }
        if (sped || !(*after.locateClip(id)->clip == before))
            ++changed;
    }
    if (changed == 0) {
        reportError(tr("Nothing to paste: the chosen attributes do not apply to these clips"));
        return false;
    }
    m_project.setNextId(std::max(m_project.nextId(), after.nextId()));
    return apply(std::make_unique<ApplyEditsCommand>(m_project, std::move(after), TVE_TR("core", "Paste Attributes")));
}

bool ProjectController::copyTransition(double clipId)
{
    const Clip* clip = findClip(toId(clipId));
    if (!clip || !clip->transitionIn) {
        reportError(tr("There is no transition there to copy"));
        return false;
    }
    m_copiedTransition = clip->transitionIn;
    emit clipboardChanged();
    return true;
}

bool ProjectController::pasteTransition(double clipId)
{
    if (!m_copiedTransition) {
        reportError(tr("Copy a transition first"));
        return false;
    }
    if (!apply(std::make_unique<SetClipTransitionCommand>(toId(clipId), *m_copiedTransition)))
        return false;
    setSelectedTransition(clipId);
    return true;
}

// --- Source monitor, compound clips, multicam (24d) ---

bool ProjectController::modified() const
{
    if (!m_stack->isClean())
        return true;
    return std::ranges::any_of(m_openLevels, [](const OpenLevel& l) { return !l.savedInside && !l.stack->isClean(); });
}

Project ProjectController::rootProject() const
{
    std::vector<OpenedSequence> open;
    for (const OpenLevel& level : m_openLevels)
        open.push_back(level.opened);
    return withSequencesClosed(m_project, open);
}

QVariantList ProjectController::timelineTabs() const
{
    QVariantList tabs{QVariantMap{{QStringLiteral("name"), tr("Main timeline")}, {QStringLiteral("depth"), 0}}};
    for (std::size_t i = 0; i < m_openLevels.size(); ++i)
        tabs << QVariantMap{{QStringLiteral("name"), m_openLevels[i].name}, {QStringLiteral("depth"), int(i + 1)}};
    return tabs;
}

double ProjectController::placeMediaRange(double mediaId, double from, double to, const QString& mode)
{
    const MediaItem* media = m_project.findMedia(toId(mediaId));
    if (!media) {
        reportError(tr("Media not found"));
        return 0;
    }
    Id track = mainTrackId();
    if (!media->hasVideo) {
        const auto audio = std::ranges::find(m_project.tracks, TrackKind::Audio, &Track::kind);
        if (audio == m_project.tracks.end()) {
            reportError(tr("Add an audio track for the sound first"));
            return 0;
        }
        track = audio->id;
    }
    const Ticks in = std::max<Ticks>(0, ticks(from));
    const Ticks out = to <= 0 ? (media->still ? in + ticks(5.0) : media->duration) : ticks(to);
    const PlaceMode how = mode == QLatin1String("overwrite") ? PlaceMode::Overwrite
                        : mode == QLatin1String("append")    ? PlaceMode::Append
                                                             : PlaceMode::Insert;
    Project after = m_project;
    Id made = kNoId;
    if (auto error = placeMedia(after, track, media->id, media->still ? 0 : in, out - in, ticks(m_playhead), how, &made)) {
        reportError(coreText(*error));
        return 0;
    }
    m_project.setNextId(after.nextId());
    const char* label = how == PlaceMode::Overwrite ? TVE_TR("core", "Overwrite Clip")
                      : how == PlaceMode::Append    ? TVE_TR("core", "Append Clip")
                                                    : TVE_TR("core", "Insert Clip");
    if (!apply(std::make_unique<ApplyEditsCommand>(m_project, std::move(after), label)))
        return 0;
    setSelectedClip(double(made));
    return double(made);
}

bool ProjectController::openCompoundClip(double clipId)
{
    const Clip* clip = findClip(toId(clipId));
    if (!clip || !clip->compound()) {
        reportError(tr("Select a compound clip to open"));
        return false;
    }
    pause();
    clearMarks();
    const Sequence* sequence = m_project.findSequence(clip->sequenceId);
    OpenLevel level;
    level.entry = m_project;
    level.playhead = m_playhead;
    level.name = sequence ? QString::fromStdString(sequence->name) : tr("Compound clip");
    // The playhead stays on the same picture: where it was in the clip, in its insides.
    const Ticks inside = std::clamp<Ticks>(clip->sourceTimeBeyond(ticks(m_playhead)), 0, sequence ? sequence->duration() : 0);
    if (auto error = openSequence(m_project, clip->id, &level.opened)) {
        m_project = level.entry;
        reportError(coreText(*error));
        return false;
    }
    level.stack = std::move(m_stack);
    m_stack = std::make_unique<CommandStack>(m_project);
    m_openLevels.push_back(std::move(level));
    m_selectedClip = kNoId;
    m_selectedClips.clear();
    m_selectedTransition = kNoId;
    publish();
    emit selectionChanged();
    emit timelineTabsChanged();
    seek(seconds(inside));
    return true;
}

void ProjectController::closeLevel()
{
    OpenLevel level = std::move(m_openLevels.back());
    m_openLevels.pop_back();
    const bool innerClean = m_stack->isClean();
    Project after = m_project;
    closeSequence(after, level.opened);
    m_stack = std::move(level.stack);
    m_project = std::move(level.entry);
    m_project.setNextId(std::max(m_project.nextId(), after.nextId()));
    if (!(after == m_project)) {
        if (auto error = m_stack->push(std::make_unique<ApplyEditsCommand>(m_project, std::move(after),
                                                                          TVE_TR("core", "Edit Compound Clip"))))
            reportError(coreText(*error));
    }
    if (level.savedInside && innerClean)
        m_stack->setClean(); // the file has it as it is now
    m_playhead = level.playhead;
    m_selectedClip = level.opened.clipId;
    m_selectedClips = {level.opened.clipId};
}

bool ProjectController::closeCompoundClip(int depth)
{
    if (m_openLevels.empty())
        return false;
    const std::size_t to = depth < 0 ? m_openLevels.size() - 1 : std::min<std::size_t>(std::size_t(depth), m_openLevels.size());
    if (to >= m_openLevels.size())
        return false;
    pause();
    clearMarks();
    double playhead = m_playhead;
    while (m_openLevels.size() > to) {
        playhead = m_openLevels.back().playhead;
        closeLevel();
    }
    if (!findClip(m_selectedClip)) {
        m_selectedClip = kNoId;
        m_selectedClips.clear();
    }
    publish();
    emit selectionChanged();
    emit timelineTabsChanged();
    seek(playhead);
    return true;
}

double ProjectController::importProjectAsCompound(const QUrl& file)
{
    const QString path = file.isLocalFile() ? file.toLocalFile() : file.toString();
    Project other;
    try {
        other = loadProject(path.toStdString());
    } catch (const std::exception& e) {
        reportError(tr("Could not open the project: %1").arg(coreText(e.what())));
        return 0;
    }
    Project after = m_project;
    Id made = kNoId;
    if (auto error = tve::importProjectAsCompound(after, other, QFileInfo(path).completeBaseName().toStdString(),
                                                  ticks(m_playhead), &made)) {
        reportError(coreText(*error));
        return 0;
    }
    m_project.setNextId(after.nextId());
    if (!apply(std::make_unique<ApplyEditsCommand>(m_project, std::move(after), TVE_TR("core", "Import Project"))))
        return 0;
    setSelectedClip(double(made));
    return double(made);
}

double ProjectController::makeMulticam(const QVariantList& clipIds)
{
    std::vector<Id> ids;
    for (const QVariant& id : clipIds.isEmpty() ? selectedClips() : clipIds)
        ids.push_back(toId(id.toDouble()));
    Project after = m_project;
    Id made = kNoId;
    if (auto error = tve::makeMulticam(after, ids, &made)) {
        reportError(coreText(*error));
        return 0;
    }
    m_project.setNextId(after.nextId());
    if (!apply(std::make_unique<ApplyEditsCommand>(m_project, std::move(after), TVE_TR("core", "Make Multicam Clip"))))
        return 0;
    setSelectedClip(double(made));
    return double(made);
}

bool ProjectController::setClipAngle(double clipId, int angle)
{
    const Clip* clip = findClip(toId(clipId));
    if (clip && clip->angle() == angle)
        return true;
    Project after = m_project;
    if (auto error = setAngle(after, toId(clipId), angle)) {
        reportError(coreText(*error));
        return false;
    }
    return apply(std::make_unique<ApplyEditsCommand>(m_project, std::move(after), TVE_TR("core", "Switch Angle")));
}

bool ProjectController::cutToAngle(int angle)
{
    const Ticks at = ticks(m_playhead);
    const auto covers = [&](const Clip& c) { return c.multicam() && c.start <= at && at < c.end(); };
    const Clip* target = findClip(m_selectedClip);
    if (!target || !covers(*target)) {
        target = nullptr;
        for (auto track = m_project.tracks.rbegin(); track != m_project.tracks.rend() && !target; ++track)
            for (const Clip& clip : track->clips)
                if (covers(clip)) {
                    target = &clip;
                    break;
                }
    }
    if (!target) {
        reportError(tr("There is no multicam clip at the playhead"));
        return false;
    }
    if (target->angle() == angle)
        return true;
    Project after = m_project;
    Id part = kNoId;
    if (auto error = tve::cutToAngle(after, target->id, at, angle, &part)) {
        reportError(coreText(*error));
        return false;
    }
    m_project.setNextId(after.nextId());
    if (!apply(std::make_unique<ApplyEditsCommand>(m_project, std::move(after), TVE_TR("core", "Cut to Angle"))))
        return false;
    setSelectedClip(double(part));
    return true;
}

// --- Placing and moving pictures, colour clips, snapshots (25a) ---

namespace {

QString colourFolder()
{
    const QString overridden = qEnvironmentVariable("TVE_COLOURS_DIR");
    return overridden.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/Colours")
                                : overridden;
}

QString snapshotFolder()
{
    const QString overridden = qEnvironmentVariable("TVE_SNAPSHOT_DIR");
    return overridden.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/Snapshots")
                                : overridden;
}

std::optional<ColourFill> fillFrom(const QVariantMap& map, const ColourFill& base = {})
{
    ColourFill fill = base;
    const QString kind = map.value(QStringLiteral("kind"), QString()).toString();
    if (kind == QLatin1String("solid"))
        fill.kind = ColourFillKind::Solid;
    else if (kind == QLatin1String("linear"))
        fill.kind = ColourFillKind::Linear;
    else if (kind == QLatin1String("radial"))
        fill.kind = ColourFillKind::Radial;
    else if (!kind.isEmpty())
        return std::nullopt;
    for (const auto& [key, target] : {std::pair{QStringLiteral("from"), &fill.from}, std::pair{QStringLiteral("to"), &fill.to}}) {
        if (!map.contains(key))
            continue;
        const QVariantList list = map.value(key).toList();
        if (list.size() < 3)
            return std::nullopt;
        for (int i = 0; i < 4; ++i)
            (*target)[std::size_t(i)] = std::clamp(i < list.size() ? list[i].toDouble() : 1.0, 0.0, 1.0);
    }
    if (map.contains(QStringLiteral("angle")))
        fill.angle = std::fmod(map.value(QStringLiteral("angle")).toDouble(), 360.0);
    return fill;
}

// Draws the fill at the canvas's size into the colours folder (one file per look and size).
QString drawFill(const ColourFill& fill, int width, int height, QString* error)
{
    const QString key = QStringLiteral("%1|%2|%3|%4|%5x%6")
                            .arg(int(fill.kind))
                            .arg(QStringList{QString::number(fill.from[0]), QString::number(fill.from[1]),
                                             QString::number(fill.from[2]), QString::number(fill.from[3])}
                                     .join(QLatin1Char(',')),
                                 QStringList{QString::number(fill.to[0]), QString::number(fill.to[1]),
                                             QString::number(fill.to[2]), QString::number(fill.to[3])}
                                     .join(QLatin1Char(',')))
                            .arg(fill.angle)
                            .arg(width)
                            .arg(height);
    const QString hash = QString::fromLatin1(QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha1).toHex().left(12));
    const auto hex = [](const std::array<double, 4>& c) {
        return QColor::fromRgbF(float(c[0]), float(c[1]), float(c[2])).name().mid(1).toUpper();
    };
    const QString label = fill.kind == ColourFillKind::Solid ? QStringLiteral("Colour %1").arg(hex(fill.from))
                                                             : QStringLiteral("Gradient %1-%2").arg(hex(fill.from), hex(fill.to));
    // (In a folder of its own, so the file keeps a plain name.)
    QDir folder(colourFolder() + QLatin1Char('/') + hash);
    if (!folder.mkpath(QStringLiteral("."))) {
        *error = QObject::tr("Could not make the folder %1").arg(folder.path());
        return {};
    }
    const QString path = folder.filePath(label + QStringLiteral(".png"));
    if (QFileInfo::exists(path))
        return path;
    QImage image(std::max(width, 16), std::max(height, 16), QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    const auto colour = [](const std::array<double, 4>& c) {
        return QColor::fromRgbF(float(c[0]), float(c[1]), float(c[2]), float(c[3]));
    };
    {
        QPainter painter(&image);
        const QRectF all(0, 0, image.width(), image.height());
        if (fill.kind == ColourFillKind::Solid) {
            painter.fillRect(all, colour(fill.from));
        } else if (fill.kind == ColourFillKind::Linear) {
            const double a = fill.angle * std::numbers::pi / 180.0;
            const double dx = std::cos(a), dy = std::sin(a);
            const double half = (std::abs(dx) * all.width() + std::abs(dy) * all.height()) / 2.0;
            const QPointF centre = all.center();
            QLinearGradient gradient(centre - QPointF(dx, dy) * half, centre + QPointF(dx, dy) * half);
            gradient.setColorAt(0, colour(fill.from));
            gradient.setColorAt(1, colour(fill.to));
            painter.fillRect(all, gradient);
        } else {
            QRadialGradient gradient(all.center(), std::hypot(all.width(), all.height()) / 2.0);
            gradient.setColorAt(0, colour(fill.from));
            gradient.setColorAt(1, colour(fill.to));
            painter.fillRect(all, gradient);
        }
    }
    if (!image.save(path)) {
        *error = QObject::tr("Could not write %1").arg(path);
        return {};
    }
    return path;
}

} // namespace

QVariantList ProjectController::clipAnimations() const
{
    QVariantList list;
    for (const ClipAnimationPreset& preset : clipAnimationPresets())
        list << QVariantMap{{QStringLiteral("id"), QString::fromLatin1(preset.id)},
                            {QStringLiteral("title"), coreText(preset.title)},
                            {QStringLiteral("kind"), preset.kind == ClipAnimationKind::In    ? QStringLiteral("in")
                                                     : preset.kind == ClipAnimationKind::Out ? QStringLiteral("out")
                                                                                             : QStringLiteral("combo")}};
    return list;
}

bool ProjectController::animateClip(double clipId, const QString& preset, double seconds)
{
    const auto location = m_project.locateClip(toId(clipId));
    if (!location || location->track->kind == TrackKind::Audio) {
        reportError(tr("Select a video or picture clip to animate"));
        return false;
    }
    Clip after = *location->clip;
    if (auto error = applyClipAnimation(after, preset.toStdString(), ticks(std::clamp(seconds, 0.1, 10.0)))) {
        reportError(coreText(*error));
        return false;
    }
    return commitAnimation(*location->clip, std::move(after), TVE_TR("core", "Animate Clip"));
}

bool ProjectController::clearClipAnimation(double clipId)
{
    const Clip* clip = findClip(toId(clipId));
    if (!clip)
        return false;
    Clip after = *clip;
    tve::clearClipAnimation(after);
    return commitAnimation(*clip, std::move(after), TVE_TR("core", "Remove Animation"));
}

QVariantList ProjectController::panZoomPresets() const
{
    QVariantList list;
    for (const PanZoomPreset& preset : tve::panZoomPresets()) {
        const PanZoom& f = preset.framing;
        list << QVariantMap{{QStringLiteral("id"), QString::fromLatin1(preset.id)},
                            {QStringLiteral("title"), coreText(preset.title)},
                            {QStringLiteral("from"), QVariantMap{{QStringLiteral("scale"), f.fromScale},
                                                                 {QStringLiteral("x"), f.fromX},
                                                                 {QStringLiteral("y"), f.fromY}}},
                            {QStringLiteral("to"), QVariantMap{{QStringLiteral("scale"), f.toScale},
                                                               {QStringLiteral("x"), f.toX},
                                                               {QStringLiteral("y"), f.toY}}}};
    }
    return list;
}

bool ProjectController::panZoom(double clipId, const QVariant& framing)
{
    const auto location = m_project.locateClip(toId(clipId));
    if (!location || location->track->kind == TrackKind::Audio) {
        reportError(tr("Select a video or picture clip to pan and zoom"));
        return false;
    }
    PanZoom f;
    if (framing.typeId() == QMetaType::QString) {
        const auto& presets = tve::panZoomPresets();
        const auto it = std::ranges::find(presets, framing.toString().toStdString(), [](const PanZoomPreset& p) { return std::string(p.id); });
        if (it == presets.end()) {
            reportError(coreText("there is no such animation"));
            return false;
        }
        f = it->framing;
    } else {
        const QVariantMap m = framing.toMap();
        f.fromScale = m.value(QStringLiteral("fromScale"), f.fromScale).toDouble();
        f.fromX = m.value(QStringLiteral("fromX"), f.fromX).toDouble();
        f.fromY = m.value(QStringLiteral("fromY"), f.fromY).toDouble();
        f.toScale = m.value(QStringLiteral("toScale"), f.toScale).toDouble();
        f.toX = m.value(QStringLiteral("toX"), f.toX).toDouble();
        f.toY = m.value(QStringLiteral("toY"), f.toY).toDouble();
    }
    Clip after = *location->clip;
    if (auto error = applyPanZoom(after, f)) {
        reportError(coreText(*error));
        return false;
    }
    return commitAnimation(*location->clip, std::move(after), TVE_TR("core", "Pan and Zoom"));
}

double ProjectController::addColorClip(const QVariantMap& map)
{
    const auto fill = fillFrom(map);
    if (!fill) {
        reportError(tr("Choose a colour, or two for a gradient"));
        return 0;
    }
    QString error;
    const QString path = drawFill(*fill, m_project.settings.width, m_project.settings.height, &error);
    if (path.isEmpty()) {
        reportError(error);
        return 0;
    }
    Project after = m_project;
    Id mediaId = kNoId;
    if (const auto same = std::ranges::find(after.media, path.toStdString(), &MediaItem::path); same != after.media.end()) {
        mediaId = same->id;
    } else {
        std::optional<MediaItem> item = describeMedia(path.toStdString(), &error);
        if (!item) {
            reportError(error);
            return 0;
        }
        item->colourFill = *fill;
        ImportMediaCommand import(std::move(*item));
        if (auto failed = import.redo(after)) {
            reportError(coreText(*failed));
            return 0;
        }
        mediaId = import.mediaId();
    }
    const MediaItem* media = after.findMedia(mediaId);
    const Ticks at = ticks(m_playhead);
    Id target = kNoId;
    for (const Track& track : after.tracks)
        if (track.kind == TrackKind::Video && !track.locked && !track.overlaps(at, media->duration) && target == kNoId)
            target = track.id;
    if (target == kNoId) {
        InsertTrackCommand track(TrackKind::Video,
                                 "Video " + std::to_string(std::ranges::count(after.tracks, TrackKind::Video, &Track::kind) + 1),
                                 after.tracks.size());
        if (auto failed = track.redo(after)) {
            reportError(coreText(*failed));
            return 0;
        }
        target = track.trackId();
    }
    AddClipCommand add(target, mediaId, at, media->duration, 0);
    if (auto failed = add.redo(after)) {
        reportError(coreText(*failed));
        return 0;
    }
    m_project.setNextId(after.nextId());
    if (!apply(std::make_unique<ApplyEditsCommand>(m_project, std::move(after), TVE_TR("core", "Add Colour Clip"))))
        return 0;
    setSelectedClip(double(add.clipId()));
    return double(add.clipId());
}

bool ProjectController::setColorClip(double clipId, const QVariantMap& map)
{
    const Clip* clip = findClip(toId(clipId));
    const MediaItem* media = clip ? m_project.findMedia(clip->mediaId) : nullptr;
    if (!media || !media->colourFill) {
        reportError(tr("Select a colour clip"));
        return false;
    }
    const auto fill = fillFrom(map, *media->colourFill);
    if (!fill) {
        reportError(tr("Choose a colour, or two for a gradient"));
        return false;
    }
    if (*fill == *media->colourFill)
        return true;
    QString error;
    const QString path = drawFill(*fill, m_project.settings.width, m_project.settings.height, &error);
    if (path.isEmpty()) {
        reportError(error);
        return false;
    }
    Project after = m_project;
    Id mediaId = kNoId;
    if (const auto same = std::ranges::find(after.media, path.toStdString(), &MediaItem::path); same != after.media.end()) {
        mediaId = same->id;
    } else {
        std::optional<MediaItem> item = describeMedia(path.toStdString(), &error);
        if (!item) {
            reportError(error);
            return false;
        }
        item->colourFill = *fill;
        ImportMediaCommand import(std::move(*item));
        if (auto failed = import.redo(after)) {
            reportError(coreText(*failed));
            return false;
        }
        mediaId = import.mediaId();
    }
    after.locateClip(clip->id)->clip->mediaId = mediaId;
    m_project.setNextId(after.nextId());
    return apply(std::make_unique<ApplyEditsCommand>(m_project, std::move(after), TVE_TR("core", "Change Colour")));
}

QString ProjectController::takeSnapshot(const QUrl& file)
{
    if (duration() <= 0) {
        reportError(tr("There is nothing on the timeline to take a snapshot of"));
        return {};
    }
    StillRenderer renderer;
    if (!renderer.isValid()) {
        reportError(tr("Could not take a snapshot: no GPU device"));
        return {};
    }
    const QImage image = renderer.render(std::make_shared<const Project>(m_project), ticks(m_playhead),
                                         QSize(m_project.settings.width, m_project.settings.height));
    if (image.isNull()) {
        reportError(tr("Could not take a snapshot of this frame"));
        return {};
    }
    QString path;
    if (!file.isEmpty()) {
        path = file.isLocalFile() ? file.toLocalFile() : file.toString();
        if (QFileInfo(path).suffix().isEmpty())
            path += QStringLiteral(".png");
    } else {
        QDir folder(snapshotFolder());
        if (!folder.mkpath(QStringLiteral("."))) {
            reportError(tr("Could not make the folder %1").arg(folder.path()));
            return {};
        }
        const QString stem = QStringLiteral("%1 %2").arg(name(), timecode(m_playhead).replace(QLatin1Char(':'), QLatin1Char('.')));
        path = folder.filePath(stem + QStringLiteral(".png"));
        for (int n = 2; QFileInfo::exists(path); ++n)
            path = folder.filePath(QStringLiteral("%1 (%2).png").arg(stem).arg(n));
    }
    if (!image.save(path, nullptr, 95)) {
        reportError(tr("Could not write %1").arg(path));
        return {};
    }
    if (file.isEmpty())
        importMedia({QUrl::fromLocalFile(path)});
    return path;
}

bool ProjectController::setClipMotionBlur(double clipId, double shutter)
{
    const auto location = m_project.locateClip(toId(clipId));
    if (!location || location->track->kind == TrackKind::Audio) {
        reportError(tr("Select a video or picture clip"));
        return false;
    }
    if (location->track->locked) {
        reportError(coreText("track is locked"));
        return false;
    }
    const double value = std::clamp(std::round(shutter * 100.0) / 100.0, 0.0, 1.0);
    if (value == location->clip->motionBlur)
        return true;
    Project after = m_project;
    after.locateClip(toId(clipId))->clip->motionBlur = value;
    return apply(std::make_unique<ApplyEditsCommand>(m_project, std::move(after), TVE_TR("core", "Motion Blur")));
}

// --- Colour tools (25c) ---

std::vector<std::array<float, 3>> ProjectController::pixelsForColour(const Clip& clip, Ticks at) const
{
    const MediaItem* media = m_project.findMedia(clip.mediaId);
    if (!media || !media->hasVideo || clip.generated() || clip.compound())
        return {};
    QImage picture;
    if (media->still) {
        picture = still::read(QString::fromStdString(media->path), 360);
    } else {
        const Ticks inside = std::clamp(at, clip.start, clip.end() - 1);
        if (auto frame = extractThumbnail(media->path, clampedSourceTime(m_project, clip, inside), 360))
            picture = QImage(frame->pixels.data(), frame->width, frame->height, QImage::Format_RGBA8888).copy();
    }
    if (picture.isNull())
        return {};
    picture = picture.convertToFormat(QImage::Format_RGBA8888);
    // Its look, then its grade without the curves the tools set (the master curve stays).
    ColorGrade grade = clip.color;
    grade.red.clear();
    grade.green.clear();
    grade.blue.clear();
    const ColorGrade look = clip.look ? clip.look->effective() : ColorGrade{};
    const auto stage = [](const ColorGrade& g) {
        return std::tuple{gradeCoefficients(g), bakeCurves(g), bakeHueCurves(g), g.isNeutral()};
    };
    const auto steps = {stage(look), stage(grade)};
    std::vector<std::array<float, 3>> pixels;
    const int step = std::max(1, int(std::sqrt(double(picture.width()) * picture.height() / 40000.0)));
    for (int y = 0; y < picture.height(); y += step) {
        const uchar* row = picture.constScanLine(y);
        for (int x = 0; x < picture.width(); x += step) {
            if (row[x * 4 + 3] < 128)
                continue; // (transparent parts are not the picture)
            std::array<float, 3> rgb{row[x * 4] / 255.0f, row[x * 4 + 1] / 255.0f, row[x * 4 + 2] / 255.0f};
            for (const auto& [c, curves, hue, neutral] : steps)
                if (!neutral)
                    rgb = applyGrade(c, curves, rgb, hue);
            pixels.push_back(rgb);
        }
    }
    return pixels;
}

bool ProjectController::autoColour(double clipId)
{
    const Clip* clip = findClip(toId(clipId));
    const auto pixels = clip ? pixelsForColour(*clip, ticks(m_playhead)) : std::vector<std::array<float, 3>>{};
    if (pixels.empty()) {
        reportError(tr("Select a video or picture clip to correct its colour"));
        return false;
    }
    const ColourStats stats = colourStats(pixels);
    const ColorGrade grade = withMatchingCurves(clip->color, stats, autoColourTarget(stats));
    return apply(std::make_unique<SetClipColorCommand>(std::vector<Id>{clip->id}, grade, TVE_TR("core", "Auto Colour")));
}

bool ProjectController::matchColour(double clipId, double referenceClipId)
{
    const Clip* clip = findClip(toId(clipId));
    const Clip* reference = findClip(toId(referenceClipId));
    if (!clip || !reference || clip->id == reference->id) {
        reportError(tr("Choose another clip to match the colour to"));
        return false;
    }
    // Each at the playhead if it is there, else halfway through.
    const Ticks now = ticks(m_playhead);
    const auto when = [&](const Clip& c) { return now >= c.start && now < c.end() ? now : c.start + c.duration / 2; };
    const auto pixels = pixelsForColour(*clip, when(*clip));
    auto target = pixelsForColour(*reference, when(*reference));
    if (pixels.empty() || target.empty()) {
        reportError(tr("Both clips need a picture to match colours"));
        return false;
    }
    // The reference as it looks: its own red, green and blue curves too.
    const ColorGrade own{.red = reference->color.red, .green = reference->color.green, .blue = reference->color.blue};
    if (!own.isNeutral()) {
        const auto c = gradeCoefficients(own);
        const auto curves = bakeCurves(own);
        for (auto& rgb : target)
            rgb = applyGrade(c, curves, rgb);
    }
    const ColorGrade grade = withMatchingCurves(clip->color, colourStats(pixels), colourStats(target));
    return apply(std::make_unique<SetClipColorCommand>(std::vector<Id>{clip->id}, grade, TVE_TR("core", "Match Colour")));
}

namespace {

struct LensPreset {
    const char* id;
    const char* title;
    double amount; // the lens distortion effect's: negative straightens a barrel
    double zoom;
};

// Starting points for common wide lenses (an amount to fine-tune on the effect).
const std::vector<LensPreset>& lensPresetList()
{
    static const std::vector<LensPreset> presets{
        {"action-wide", QT_TRANSLATE_NOOP("ProjectController", "Action camera, wide"), -0.42, 1.0},
        {"action-superwide", QT_TRANSLATE_NOOP("ProjectController", "Action camera, super wide"), -0.58, 1.0},
        {"action-medium", QT_TRANSLATE_NOOP("ProjectController", "Action camera, medium"), -0.28, 1.0},
        {"phone-ultrawide", QT_TRANSLATE_NOOP("ProjectController", "Phone ultra wide (0.5×)"), -0.18, 1.0},
        {"drone-wide", QT_TRANSLATE_NOOP("ProjectController", "Drone, wide"), -0.24, 1.0},
        {"fisheye", QT_TRANSLATE_NOOP("ProjectController", "Fisheye"), -0.85, 1.0},
    };
    return presets;
}

constexpr const char* kLensEffect = "tve.distort.lens";

} // namespace

QVariantList ProjectController::lensPresets() const
{
    QVariantList list;
    for (const LensPreset& preset : lensPresetList())
        list << QVariantMap{{QStringLiteral("id"), QString::fromLatin1(preset.id)},
                            {QStringLiteral("title"), tr(preset.title)},
                            {QStringLiteral("amount"), preset.amount}};
    return list;
}

bool ProjectController::correctLens(double clipId, const QString& presetId)
{
    const auto location = m_project.locateClip(toId(clipId));
    if (!location || location->track->kind == TrackKind::Audio || location->clip->generated()) {
        reportError(tr("Select a video or picture clip"));
        return false;
    }
    std::vector<ClipEffect> effects = location->clip->effects;
    const auto existing = std::ranges::find(effects, std::string(kLensEffect), &ClipEffect::effectId);
    if (presetId == QLatin1String("none")) {
        if (existing == effects.end())
            return true;
        effects.erase(existing);
        return apply(std::make_unique<SetClipEffectsCommand>(location->clip->id, std::move(effects),
                                                              TVE_TR("core", "Lens Correction")));
    }
    const auto& presets = lensPresetList();
    const auto preset = std::ranges::find(presets, presetId.toStdString(), [](const LensPreset& p) { return std::string(p.id); });
    if (preset == presets.end()) {
        reportError(tr("There is no such lens"));
        return false;
    }
    ClipEffect lens{.effectId = kLensEffect};
    if (existing != effects.end())
        lens = *existing;
    lens.values["amount"] = {float(preset->amount)};
    lens.values["zoom"] = {float(preset->zoom)};
    lens.values["fringe"] = {0.0f};
    lens.keyframes.erase("amount");
    if (existing != effects.end())
        *existing = lens;
    else
        effects.insert(effects.begin(), lens); // first: straightened before other effects
    return apply(std::make_unique<SetClipEffectsCommand>(location->clip->id, std::move(effects), TVE_TR("core", "Lens Correction")));
}
