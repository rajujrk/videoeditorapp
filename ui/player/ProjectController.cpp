#include "ProjectController.h"

#include "VideoView.h"
#include "audio/AudioOutput.h"
#include "audio/AudioRecorder.h"
#include "commands/EditCommands.h"
#include "decode/AudioDecoder.h"
#include "decode/VideoDecoder.h"
#include "io/LutFile.h"
#include "io/ProjectSerializer.h"
#include "model/TimelineQuery.h"
#include "gpu/EffectLibrary.h"
#include "EffectInfo.h"
#include "Exporter.h"
#include "MicrophoneAccess.h"
#include "TitleRenderer.h"
#include "playback/TimelinePlayback.h"

#include <nlohmann/json.hpp>

#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>
#include <QtGlobal>

#include <cerrno>
#include <csignal>
#include <set>
#include <unistd.h>

#include <algorithm>
#include <cmath>

using namespace tve;

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
        *error = QStringLiteral("Could not import %1: no video or audio found").arg(fileName(path));
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
    };
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
        {QStringLiteral("title"), loaded ? QString::fromStdString(loaded->manifest.displayTitle())
                                         : QStringLiteral("Unknown transition")},
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
// of an effect in its stack, named "effect:<stack index>:<parameter id>".

struct EffectChannel {
    std::size_t index = 0;
    std::string param;
};

std::optional<EffectChannel> effectChannel(std::string_view id)
{
    constexpr std::string_view prefix = "effect:";
    if (!id.starts_with(prefix))
        return std::nullopt;
    const std::string_view rest = id.substr(prefix.size());
    const auto colon = rest.find(':');
    if (colon == std::string_view::npos || colon == 0 || colon + 1 == rest.size())
        return std::nullopt;
    std::size_t index = 0;
    for (char c : rest.substr(0, colon)) {
        if (c < '0' || c > '9')
            return std::nullopt;
        index = index * 10 + std::size_t(c - '0');
    }
    return EffectChannel{index, std::string(rest.substr(colon + 1))};
}

std::string effectChannelId(std::size_t index, const std::string& param)
{
    return "effect:" + std::to_string(index) + ":" + param;
}

// The manifest entry of an effect channel's parameter, if it can be animated.
const EffectParamSpec* animatableParam(const Clip& clip, const EffectChannel& channel)
{
    if (channel.index >= clip.effects.size())
        return nullptr;
    const LoadedEffect* effect = EffectLibrary::builtIn().find(clip.effects[channel.index].effectId);
    const EffectParamSpec* spec = effect ? effect->manifest.findParam(channel.param) : nullptr;
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
        if (channel->index >= clip.effects.size())
            return nullptr;
        const auto& keys = clip.effects[channel->index].keyframes;
        const auto it = keys.find(channel->param);
        return it != keys.end() && !it->second.empty() ? &it->second : nullptr;
    }
    return clip.keyframesOf(id);
}

Keyframes& ensureChannel(Clip& clip, const std::string& id)
{
    if (const auto channel = effectChannel(id))
        return clip.effects[channel->index].keyframes[channel->param];
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
    return nullptr;
}

// The channel's value at a timeline time, animated or not.
double channelValue(const Clip& clip, const std::string& id, Ticks time)
{
    if (id == property::kVolume)
        return clip.gainDbAt(time);
    if (const auto channel = effectChannel(id)) {
        if (channel->index >= clip.effects.size())
            return 0.0;
        const ClipEffect effect = clip.effectsAt(time)[channel->index];
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
        if (channel->index < clip.effects.size())
            clip.effects[channel->index].values[channel->param] = {float(value)};
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
        {QStringLiteral("master"), curveList(g.master)},
        {QStringLiteral("red"), curveList(g.red)},
        {QStringLiteral("green"), curveList(g.green)},
        {QStringLiteral("blue"), curveList(g.blue)},
        {QStringLiteral("lutPath"), QString::fromStdString(g.lutPath)},
        {QStringLiteral("lutName"), g.lutPath.empty() ? QString() : fileName(g.lutPath)},
        {QStringLiteral("lutMissing"), !g.lutPath.empty() && !QFileInfo::exists(QString::fromStdString(g.lutPath))},
        {QStringLiteral("lutIntensity"), g.lutIntensity},
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

    m_playheadTimer.setInterval(16);
    connect(&m_playheadTimer, &QTimer::timeout, this, &ProjectController::updatePlayhead);
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
            m_playback->setProject(playbackSnapshot()); // switch to it where it is on screen
    });
    m_autosaveTimer.setSingleShot(true);
    m_autosaveTimer.setInterval(kAutosaveDelayMs);
    connect(&m_autosaveTimer, &QTimer::timeout, this, &ProjectController::autosaveNow);

    resetProject(emptyProject(1920, 1080), {});
}

ProjectController::~ProjectController()
{
    // A running export is cancelled; its queued notifications die with this object.
    m_cancelExport = true;
    if (m_exportThread.joinable())
        m_exportThread.join();
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
    return m_filePath.isEmpty() ? QStringLiteral("Untitled project") : QFileInfo(m_filePath).completeBaseName();
}

double ProjectController::frameRate() const
{
    return double(m_project.settings.frameRate.num) / m_project.settings.frameRate.den;
}

double ProjectController::duration() const
{
    return seconds(m_project.duration());
}

QVariantList ProjectController::tracks() const
{
    const Id main = mainTrackId();
    QVariantList list;
    for (auto track = m_project.tracks.rbegin(); track != m_project.tracks.rend(); ++track) {
        QVariantList clips;
        for (const Clip& clip : track->clips) {
            const MediaItem* media = m_project.findMedia(clip.mediaId);
            const QString label = clip.text ? QString::fromStdString(clip.text->text).section(QLatin1Char('\n'), 0, 0)
                                  : media     ? fileName(media->path)
                                              : QStringLiteral("Missing media");
            clips << QVariantMap{
                {QStringLiteral("id"), double(clip.id)},
                {QStringLiteral("mediaId"), double(clip.mediaId)},
                {QStringLiteral("label"), label},
                {QStringLiteral("isTitle"), clip.text.has_value()},
                {QStringLiteral("start"), seconds(clip.start)},
                {QStringLiteral("duration"), seconds(clip.duration)},
                {QStringLiteral("sourceIn"), seconds(clip.sourceIn)},
                {QStringLiteral("mediaDuration"), media ? seconds(media->duration) : 0.0},
                {QStringLiteral("path"), media ? QString::fromStdString(media->path) : QString()},
                {QStringLiteral("hasAudio"), media && media->hasAudio},
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
            {QStringLiteral("clips"), clips},
        };
    }
    return list;
}

QVariantList ProjectController::media() const
{
    QVariantList list;
    for (const MediaItem& item : m_project.media) {
        list << QVariantMap{
            {QStringLiteral("id"), double(item.id)},
            {QStringLiteral("name"), fileName(item.path)},
            {QStringLiteral("path"), QString::fromStdString(item.path)},
            {QStringLiteral("duration"), seconds(item.duration)},
            {QStringLiteral("width"), item.width},
            {QStringLiteral("height"), item.height},
            {QStringLiteral("frameRate"), double(item.frameRate.num) / item.frameRate.den},
            {QStringLiteral("hasVideo"), item.hasVideo},
            {QStringLiteral("hasAudio"), item.hasAudio},
        };
    }
    return list;
}

QString ProjectController::undoText() const
{
    return QString::fromStdString(m_stack->undoLabel());
}

QString ProjectController::redoText() const
{
    return QString::fromStdString(m_stack->redoLabel());
}

bool ProjectController::playing() const
{
    return m_playback->clock().playing();
}

void ProjectController::setSelectedClip(double clipId)
{
    const Id id = toId(clipId);
    if (id == m_selectedClip && (id == kNoId || m_selectedTransition == kNoId))
        return;
    m_selectedClip = id;
    if (id != kNoId)
        m_selectedTransition = kNoId;
    emit selectionChanged();
    emit selectedClipInfoChanged();
}

void ProjectController::setSelectedTransition(double clipId)
{
    const Id id = toId(clipId);
    if (id == m_selectedTransition && (id == kNoId || m_selectedClip == kNoId))
        return;
    m_selectedTransition = id;
    if (id != kNoId)
        m_selectedClip = kNoId;
    emit selectionChanged();
    emit selectedClipInfoChanged();
}

QVariantList ProjectController::availableTransitions() const
{
    // Everyday ones first: by the first category in this order, then as loaded.
    static const std::vector<std::string> kOrder = {"basic", "slide", "wipe", "shape", "zoom", "blur", "stylize", "3d"};
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
    m_project = std::move(project);
    m_stack = std::make_unique<CommandStack>(m_project);
    m_filePath = filePath;
    m_selectedClip = kNoId;
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
    try {
        resetProject(loadProject(path.toStdString()), path);
        requestAutoProxies();
        return true;
    } catch (const std::exception& e) {
        reportError(QStringLiteral("Could not open %1: %2").arg(QFileInfo(path).fileName(), QString::fromUtf8(e.what())));
        return false;
    }
}

bool ProjectController::save(const QUrl& file)
{
    QString path = file.isEmpty() ? m_filePath : (file.isLocalFile() ? file.toLocalFile() : file.toString());
    if (path.isEmpty()) {
        reportError(QStringLiteral("Choose where to save the project"));
        return false;
    }
    if (!path.endsWith(QLatin1String(".tve")))
        path += QLatin1String(".tve");
    try {
        saveProject(m_project, path.toStdString());
    } catch (const std::exception& e) {
        reportError(QStringLiteral("Could not save: %1").arg(QString::fromUtf8(e.what())));
        return false;
    }
    m_filePath = path;
    m_stack->setClean();
    removeAutosave(); // the file has it all now
    emit projectChanged();
    return true;
}

// --- Media ---

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
        auto item = probeMedia(path, &error);
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

bool ProjectController::addClip(double mediaId, double trackId, double start)
{
    const MediaItem* media = m_project.findMedia(toId(mediaId));
    if (!media) {
        reportError(QStringLiteral("Media not found"));
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
        reportError(QStringLiteral("Track not found"));
        return false;
    }
    // An untouched project takes its canvas from the first video clip.
    const bool projectEmpty = std::ranges::all_of(m_project.tracks, [](const Track& t) { return t.clips.empty(); });
    if (!m_canvasChosen && projectEmpty && media->hasVideo && media->width > 0 && media->height > 0) {
        ProjectSettings settings = m_project.settings;
        settings.width = media->width;
        settings.height = media->height;
        settings.frameRate = media->frameRate;
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
    return apply(std::make_unique<MoveClipCommand>(toId(clipId), toId(trackId), std::max<Ticks>(ticks(start), 0)));
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
        reportError(QStringLiteral("Put the playhead inside a clip to split it"));
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
        reportError(QStringLiteral("Select a clip to delete"));
        return false;
    }
    const bool ripple = location->track->id == mainTrackId();
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
    m_playback->setProject(playbackSnapshot());
    m_trackModel.update(tracks());
    emit projectChanged();
    emit selectedClipInfoChanged();
    scheduleAutosave();
}

// --- Proxies ---

namespace {
// Videos whose short side is above this get proxies automatically.
constexpr int kAutoProxyAbove = 1080;
}

std::shared_ptr<const Project> ProjectController::playbackSnapshot() const
{
    auto snapshot = std::make_shared<Project>(m_project);
    if (m_useProxies) {
        for (MediaItem& media : snapshot->media) {
            if (media.hasVideo)
                media.proxyPath = m_proxies.readyProxy(QString::fromStdString(media.path)).toStdString();
        }
    }
    return snapshot;
}

void ProjectController::requestAutoProxies()
{
    if (!m_autoProxies)
        return;
    for (const MediaItem& media : m_project.media) {
        if (media.hasVideo && std::min(media.width, media.height) > kAutoProxyAbove)
            m_proxies.request(QString::fromStdString(media.path));
    }
}

void ProjectController::setUseProxies(bool use)
{
    if (use == m_useProxies)
        return;
    m_useProxies = use;
    m_playback->setProject(playbackSnapshot());
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
    if (!media || !media->hasVideo) {
        reportError(QStringLiteral("Only videos have proxies"));
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
    m_playback->setProject(playbackSnapshot());
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
        reportError(QStringLiteral("Could not autosave to %1").arg(m_autosaveDir));
        return;
    }
    try {
        saveProject(m_project, autosavePath(m_sessionId, ".tve").toStdString());
    } catch (const std::exception& e) {
        reportError(QStringLiteral("Could not autosave: %1").arg(QString::fromUtf8(e.what())));
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
        reportError(QStringLiteral("That unsaved work is no longer there"));
        return false;
    }
    Project recovered;
    try {
        recovered = loadProject(autosavePath(id, ".tve").toStdString());
    } catch (const std::exception& e) {
        reportError(QStringLiteral("Could not recover: %1").arg(QString::fromUtf8(e.what())));
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
                return apply(std::make_unique<SetClipKeyframesCommand>(clip->id, std::move(changes), "Change Audio"));
            // Rare: both at once (two steps).
            if (!apply(std::make_unique<SetClipKeyframesCommand>(clip->id, std::move(changes), "Change Audio")))
                return false;
        }
    }
    return apply(std::make_unique<SetClipAudioCommand>(toId(clipId), audio));
}

bool ProjectController::detachAudio(double clipId)
{
    auto location = m_project.locateClip(toId(clipId));
    if (!location) {
        reportError(QStringLiteral("Select a clip to detach its audio"));
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
    reportError(QStringLiteral("No audio track is free where this clip is"));
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
            {QStringLiteral("label"), clip->text ? QStringLiteral("Title") : media ? fileName(media->path) : QString()},
            {QStringLiteral("isTitle"), clip->text.has_value()},
            {QStringLiteral("text"), clip->text ? textMap(*clip->text) : QVariantMap()},
            {QStringLiteral("trackKind"), track.kind == TrackKind::Audio ? QStringLiteral("audio") : QStringLiteral("video")},
            {QStringLiteral("locked"), track.locked},
            {QStringLiteral("start"), seconds(clip->start)},
            {QStringLiteral("duration"), seconds(clip->duration)},
            {QStringLiteral("hasAudio"), media && media->hasAudio},
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
            {QStringLiteral("speed"), clip->speed},
            {QStringLiteral("reversed"), clip->reversed},
            {QStringLiteral("freeze"), clip->freeze},
            {QStringLiteral("speedRamp"), rampList(clip->speedRamp)},
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
        reportError(QStringLiteral("Select a video clip"));
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
                        : commitAnimation(*clip, after, "Change Picture");
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
    return commitAnimation(*clip, after, "Change Picture");
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
    wheel("lift", g.lift);
    wheel("gamma", g.gamma);
    wheel("gain", g.gain);
    wheel("offset", g.offset);
    curve("master", g.master);
    curve("red", g.red);
    curve("green", g.green);
    curve("blue", g.blue);
    return g;
}

bool ProjectController::setClipColor(double clipId, const QVariantMap& changes)
{
    const auto grade = gradeWith(toId(clipId), changes);
    if (!grade) {
        reportError(QStringLiteral("Select a video clip"));
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
    return apply(std::make_unique<SetClipColorCommand>(std::vector<Id>{toId(clipId)}, ColorGrade{}, "Reset Color"));
}

bool ProjectController::chooseLut(double clipId, const QUrl& file)
{
    const QString path = file.isLocalFile() ? file.toLocalFile() : file.toString();
    try {
        loadLut(path.toStdString());
    } catch (const std::exception& e) {
        reportError(QStringLiteral("Could not use %1: %2").arg(QFileInfo(path).fileName(), QString::fromUtf8(e.what())));
        return false;
    }
    const Clip* clip = findClip(toId(clipId));
    if (!clip)
        return false;
    ColorGrade g = clip->color;
    g.lutPath = path.toStdString();
    g.lutIntensity = 1.0;
    return apply(std::make_unique<SetClipColorCommand>(std::vector<Id>{clip->id}, g, "Apply LUT"));
}

bool ProjectController::removeLut(double clipId)
{
    const Clip* clip = findClip(toId(clipId));
    if (!clip || clip->color.lutPath.empty())
        return false;
    ColorGrade g = clip->color;
    g.lutPath.clear();
    g.lutIntensity = 1.0;
    return apply(std::make_unique<SetClipColorCommand>(std::vector<Id>{clip->id}, g, "Remove LUT"));
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
        reportError(QStringLiteral("Copy a clip's grade first"));
        return false;
    }
    return apply(std::make_unique<SetClipColorCommand>(std::vector<Id>{toId(clipId)}, *m_copiedGrade, "Paste Grade"));
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
        reportError(QStringLiteral("Name the preset"));
        return false;
    }
    if (!QDir().mkpath(colorPresetDirectory())) {
        reportError(QStringLiteral("Could not create %1").arg(colorPresetDirectory()));
        return false;
    }
    const nlohmann::json json{{"name", name.trimmed().toStdString()}, {"grade", gradeToJson(clip->color)}};
    QSaveFile file(colorPresetPath(name));
    if (!file.open(QIODevice::WriteOnly) || file.write(QByteArray::fromStdString(json.dump(2))) < 0 || !file.commit()) {
        reportError(QStringLiteral("Could not save the preset %1").arg(name));
        return false;
    }
    emit colorPresetsChanged();
    return true;
}

bool ProjectController::applyColorPreset(double clipId, const QString& name)
{
    QFile file(colorPresetPath(name));
    if (!file.open(QIODevice::ReadOnly)) {
        reportError(QStringLiteral("No colour preset named %1").arg(name));
        return false;
    }
    ColorGrade grade;
    try {
        grade = gradeFromJson(nlohmann::json::parse(file.readAll().toStdString()).at("grade"));
    } catch (const std::exception&) {
        reportError(QStringLiteral("The preset %1 is damaged").arg(name));
        return false;
    }
    return apply(std::make_unique<SetClipColorCommand>(std::vector<Id>{toId(clipId)}, grade, "Apply Color Preset"));
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
    if (changes.empty() && !visual && !gain && !effectsChanged)
        return false;
    auto command = std::make_unique<SetClipKeyframesCommand>(before.id, std::move(changes), label, visual, gain);
    if (effectsChanged)
        command->setEffects(std::move(after.effects));
    return apply(std::move(command));
}

bool ProjectController::addKeyframes(double clipId, const QStringList& properties)
{
    const Clip* clip = findClip(toId(clipId));
    if (!clip) {
        reportError(QStringLiteral("Select a clip to animate"));
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
    return commitAnimation(*clip, after, "Add Keyframe");
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
        reportError(QStringLiteral("No keyframe at the playhead"));
        return false;
    }
    // A channel whose last keyframe goes keeps the value it had there.
    for (const std::string& id : emptied)
        setChannelStatic(after, id, channelValue(*clip, id, clip->start + at));
    return commitAnimation(*clip, after, "Remove Keyframe");
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
    return commitAnimation(*clip, after, "Clear Keyframes");
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
        reportError(QStringLiteral("Unknown easing %1").arg(easing));
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
    return commitAnimation(*clip, after, "Change Keyframe");
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
    if (!commitAnimation(*clip, after, "Change Keyframe")) {
        reportError(QStringLiteral("No keyframe at the playhead"));
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
        reportError(QStringLiteral("Another keyframe is already there"));
        return false;
    }
    const Ticks landed = clip->start + target; // `clip` does not outlive the edit
    if (!commitAnimation(*clip, after, "Move Keyframe"))
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
                            {QStringLiteral("title"), QString::fromStdString(preset.title)},
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
        reportError(QStringLiteral("A speed ramp needs points"));
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
    reportError(QStringLiteral("Unknown speed ramp %1").arg(presetId));
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
        reportError(QStringLiteral("Put the playhead on a video clip to freeze a frame"));
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
    constexpr Ticks kTitleLength = 5 * kTicksPerSecond;
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
    setSelectedClip(double(raw->clipId()));
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
        reportError(QStringLiteral("Unknown transition %1").arg(transitionId));
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
            reportError(QStringLiteral("Put two clips side by side to add a transition between them"));
            return false;
        }
        target = *cut;
    }
    const Clip* clip = findClip(target);
    if (!clip) {
        reportError(QStringLiteral("Drop the transition on a cut between two clips"));
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
        reportError(QStringLiteral("Drop the transition on a cut between two clips"));
        return false;
    }
    return addTransition(transitionId, double(*cut));
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
        reportError(QStringLiteral("Select a video clip to add an effect to"));
        return false;
    }
    if (!EffectLibrary::builtIn().find(effectId.toStdString())) {
        reportError(QStringLiteral("Unknown effect %1").arg(effectId));
        return false;
    }
    auto effects = clip->effects;
    effects.push_back(ClipEffect{effectId.toStdString(), true, {}});
    return apply(std::make_unique<SetClipEffectsCommand>(clip->id, std::move(effects), "Add Effect"));
}

bool ProjectController::removeClipEffect(double clipId, int index)
{
    const Clip* clip = findClip(toId(clipId));
    if (!clip || index < 0 || index >= int(clip->effects.size()))
        return false;
    auto effects = clip->effects;
    effects.erase(effects.begin() + index);
    return apply(std::make_unique<SetClipEffectsCommand>(clip->id, std::move(effects), "Remove Effect"));
}

bool ProjectController::setClipEffectEnabled(double clipId, int index, bool enabled)
{
    const Clip* clip = findClip(toId(clipId));
    if (!clip || index < 0 || index >= int(clip->effects.size()))
        return false;
    auto effects = clip->effects;
    effects[std::size_t(index)].enabled = enabled;
    return apply(std::make_unique<SetClipEffectsCommand>(clip->id, std::move(effects),
                                                         enabled ? "Enable Effect" : "Disable Effect"));
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
        return commitAnimation(*clip, after, "Change Effect");
    }
    auto effects = effectsWithParam(toId(clipId), index, paramId, value);
    if (!effects)
        return false;
    return apply(std::make_unique<SetClipEffectsCommand>(toId(clipId), std::move(*effects), "Change Effect"));
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

Mp4Settings ProjectController::exportSettings(const QVariantMap& options) const
{
    Mp4Settings settings;
    const auto even = [](int v) { return std::max(2, v - v % 2); };
    const int width = options.value(QStringLiteral("width")).toInt();
    const int height = options.value(QStringLiteral("height")).toInt();
    settings.width = even(width > 0 ? width : m_project.settings.width);
    settings.height = even(height > 0 ? height : m_project.settings.height);
    settings.frameRate = m_project.settings.frameRate;
    settings.codec = options.value(QStringLiteral("codec")).toString() == QLatin1String("hevc") ? VideoCodec::Hevc
                                                                                             : VideoCodec::H264;
    const QString quality = options.value(QStringLiteral("quality")).toString();
    const double factor = quality == QLatin1String("high") ? 1.5 : quality == QLatin1String("small") ? 0.5 : 1.0;
    settings.videoBitrate = std::int64_t(double(defaultVideoBitrate(settings)) * factor);
    return settings;
}

double ProjectController::exportBitrate(const QVariantMap& options) const
{
    const Mp4Settings settings = exportSettings(options);
    return double(settings.videoBitrate + settings.audioBitrate);
}

bool ProjectController::exportVideo(const QUrl& file, const QVariantMap& options)
{
    if (exporting()) {
        reportError(QStringLiteral("An export is already running"));
        return false;
    }
    if (m_project.duration() == 0) {
        reportError(QStringLiteral("Add something to the timeline to export"));
        return false;
    }
    const QString path = file.isLocalFile() ? file.toLocalFile() : file.toString();
    if (path.isEmpty()) {
        reportError(QStringLiteral("Choose where to save the video"));
        return false;
    }
    pause();
    m_cancelExport = false;
    m_exportProgress = 0;
    auto snapshot = std::make_shared<const Project>(m_project);
    const Mp4Settings settings = exportSettings(options);
    m_exportThread = std::thread([this, snapshot, path, settings] {
        // Progress goes to the UI thread at most every 1 %.
        double reported = 0;
        const Exporter::Result result = Exporter::run(snapshot, path, settings, [&](double progress) {
            if (progress - reported >= 0.01 || progress >= 1.0) {
                reported = progress;
                QMetaObject::invokeMethod(this, [this, progress] {
                    m_exportProgress = progress;
                    emit exportChanged();
                }, Qt::QueuedConnection);
            }
            return !m_cancelExport.load();
        });
        QMetaObject::invokeMethod(this, [this, result, path] {
            if (m_exportThread.joinable())
                m_exportThread.join();
            emit exportChanged();
            emit exportFinished(result.ok, result.ok ? path : result.error);
        }, Qt::QueuedConnection);
    });
    emit exportChanged();
    return true;
}

void ProjectController::cancelExport()
{
    m_cancelExport = true;
}

// --- Transport ---

void ProjectController::play()
{
    if (m_project.duration() == 0)
        return;
    if (m_playback->clock().now() >= m_project.duration())
        m_playback->seek(0); // replay from the start
    m_playback->play();
    updatePlayhead();
}

void ProjectController::pause()
{
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
    const double value = seconds(now);
    if (value != m_playhead) {
        m_playhead = value;
        emit playheadChanged();
        // The inspector shows animated values as they are at the playhead.
        if (const Clip* clip = findClip(m_selectedClip); clip && isAnimated(*clip))
            emit selectedClipInfoChanged();
    }
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
        reportError(QStringLiteral("Could not open the microphone: %1").arg(QString::fromStdString(recorder->errorString())));
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
        reportError(QStringLiteral("Could not create the folder %1").arg(recordingsDirectory()));
        return false;
    }
    std::string error;
    if (!m_recorder->start(path.toStdString(), &error)) {
        reportError(QStringLiteral("Could not start recording: %1").arg(QString::fromStdString(error)));
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
        reportError(QStringLiteral("Recording failed: %1").arg(QString::fromStdString(error)));
        return false;
    }
    QString probeError;
    auto item = probeMedia(take->path, &probeError);
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
    return QStringLiteral("%1:%2:%3:%4")
        .arg(totalSeconds / 3600, 2, 10, QLatin1Char('0'))
        .arg((totalSeconds / 60) % 60, 2, 10, QLatin1Char('0'))
        .arg(totalSeconds % 60, 2, 10, QLatin1Char('0'))
        .arg(ff, 2, 10, QLatin1Char('0'));
}
