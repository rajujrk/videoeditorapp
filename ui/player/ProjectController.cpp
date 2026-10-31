#include "ProjectController.h"

#include "VideoView.h"
#include "audio/AudioOutput.h"
#include "audio/AudioRecorder.h"
#include "commands/EditCommands.h"
#include "commands/StructureCommands.h"
#include "decode/AudioDecoder.h"
#include "decode/VideoDecoder.h"
#include "io/LutFile.h"
#include "io/MediaRelink.h"
#include "io/ProjectSerializer.h"
#include "model/TimelineQuery.h"
#include "gpu/ColorGrader.h"
#include "gpu/EffectLibrary.h"
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

#include <nlohmann/json.hpp>

#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QGuiApplication>
#include <QJsonObject>
#include <QJsonArray>
#include <QRegularExpression>
#include <QPointer>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>
#include <QtGlobal>

#include <array>
#include <cerrno>
#include <csignal>
#include <set>
#include <unistd.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <random>

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

// What a media item is called: its file's name, or a built-in sticker's title.
QString mediaName(const std::string& path)
{
    const QString title = stickers::titleOf(QString::fromStdString(path));
    return title.isEmpty() ? fileName(path) : title;
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
            return QString::fromStdString(look.title);
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
            m_playback->setProject(playbackSnapshot()); // switch to it where it is on screen
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
                                  : clip.adjustment ? QStringLiteral("Adjustment layer")
                                  : sequence       ? QString::fromStdString(sequence->name)
                                  : media          ? mediaName(media->path)
                                                   : QStringLiteral("Missing media");
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
                {QStringLiteral("isAdjustment"), clip.adjustment},
                {QStringLiteral("isCompound"), sequence != nullptr},
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
            {QStringLiteral("name"), mediaName(item.path)},
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
            {QStringLiteral("missing"), missing.contains(item.id)},
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
                            {QStringLiteral("name"), file.fileName()},
                            {QStringLiteral("path"), file.filePath()},
                            {QStringLiteral("folder"), file.path()}};
    }
    return list;
}

bool ProjectController::relinkMedia(double mediaId, const QUrl& file, bool force)
{
    const MediaItem* media = m_project.findMedia(toId(mediaId));
    if (!media) {
        reportError(QStringLiteral("Media not found"));
        return false;
    }
    const std::string path = (file.isLocalFile() ? file.toLocalFile() : file.toString()).toStdString();
    QString error;
    std::optional<MediaItem> item = describeMedia(path, &error);
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
        reportError(QStringLiteral("None of the missing files are in %1").arg(QFileInfo(directory).fileName()));
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
    // (A photo's size is no guide to the video's, so stills leave the canvas alone.)
    if (!m_canvasChosen && projectEmpty && media->hasVideo && !media->still && media->width > 0 && media->height > 0) {
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
    return apply(std::make_unique<CompositeCommand>(std::move(commands), "Move Group"));
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
        if (!apply(std::make_unique<CompositeCommand>(std::move(commands), "Delete Group")))
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
    m_playback->setProject(playbackSnapshot());
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
    if (!media || !media->hasVideo || media->still) {
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
            {QStringLiteral("label"), clip->text         ? QStringLiteral("Title")
                                      : clip->adjustment ? QStringLiteral("Adjustment layer")
                                      : sequence         ? QString::fromStdString(sequence->name)
                                      : media            ? mediaName(media->path)
                                                         : QString()},
            {QStringLiteral("isTitle"), clip->text.has_value()},
            {QStringLiteral("isCompound"), sequence != nullptr},
            {QStringLiteral("groupId"), double(clip->groupId)},
            {QStringLiteral("text"), clip->text ? textMap(*clip->text) : QVariantMap()},
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
                                                                       {QStringLiteral("title"), QString::fromStdString(clip->look->title)},
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
    // qrc:/… names a built-in LUT (stored as :/…).
    const QString path = file.isLocalFile()                   ? file.toLocalFile()
                       : file.scheme() == QLatin1String("qrc") ? QLatin1Char(':') + file.path()
                                                               : file.toString();
    try {
        readLut(path.toStdString());
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
        reportError(QStringLiteral("Select a clip"));
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
        reportError(QStringLiteral("Unknown mask shape %1").arg(shape));
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
    if (!apply(std::make_unique<SetClipKeyingCommand>(clip->id, std::move(keying), "Add Mask")))
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
    const bool ok = apply(std::make_unique<SetClipKeyingCommand>(clip->id, std::move(keying), "Change Mask"));
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
    if (!apply(std::make_unique<SetClipKeyingCommand>(clip->id, std::move(keying), "Remove Mask")))
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
    const bool ok = apply(std::make_unique<SetClipKeyingCommand>(clip->id, std::move(keying), "Change Key"));
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
            return apply(std::make_unique<SetClipKeyingCommand>(clip->id, std::move(keying), "Change Matte"));
        }
    }
    reportError(QStringLiteral("Unknown matte %1").arg(mode));
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
    // A title is drawn at canvas size.
    const double width = clip->text ? m_project.settings.width : media ? media->width : 0;
    const double height = clip->text ? m_project.settings.height : media ? media->height : 0;
    if (width <= 0 || height <= 0)
        return {};
    const LayerPlacement placement = placeLayer(clip->visualAt(ticks(m_playhead)), m_project.settings.width,
                                                m_project.settings.height, width, height);
    QVariantList m;
    for (double v : placement.canvasToLayer)
        m << v;
    return {{QStringLiteral("m"), m}, {QStringLiteral("width"), width}, {QStringLiteral("height"), height}};
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
        {"pip", "Picture in picture", {{0.62, 0.06, 0.95, 0.39}}, true},
        {"side-by-side", "Side by side", {{0, 0, 0.5, 1}, {0.5, 0, 1, 1}}},
        {"top-bottom", "Top and bottom", {{0, 0, 1, 0.5}, {0, 0.5, 1, 1}}},
        {"three-columns", "Three columns", {{0, 0, 1.0 / 3, 1}, {1.0 / 3, 0, 2.0 / 3, 1}, {2.0 / 3, 0, 1, 1}}},
        {"one-and-two", "One and two", {{0, 0, 2.0 / 3, 1}, {2.0 / 3, 0, 1, 0.5}, {2.0 / 3, 0.5, 1, 1}}},
        {"grid", "Grid of four", {{0, 0, 0.5, 0.5}, {0.5, 0, 1, 0.5}, {0, 0.5, 0.5, 1}, {0.5, 0.5, 1, 1}}},
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
                            {QStringLiteral("title"), QString::fromLatin1(layout.title)},
                            {QStringLiteral("regions"), regions}};
    }
    return list;
}

bool ProjectController::applyLayout(const QString& layoutId)
{
    const auto& all = builtInLayouts();
    const auto layout = std::ranges::find_if(all, [&](const Layout& l) { return layoutId == QLatin1String(l.id); });
    if (layout == all.end()) {
        reportError(QStringLiteral("Unknown layout %1").arg(layoutId));
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
    return apply(std::make_unique<CompositeCommand>(std::move(commands), "Apply Layout"));
}

// --- Looks and adjustment layers ---

QVariantList ProjectController::looks() const
{
    QVariantList list;
    for (const LookInfo& look : builtInLooks())
        list << QVariantMap{{QStringLiteral("id"), QString::fromStdString(look.id)},
                            {QStringLiteral("title"), QString::fromStdString(look.title)},
                            {QStringLiteral("category"), QString::fromStdString(look.category)}};
    return list;
}

QVariantList ProjectController::lutLibrary() const
{
    QVariantList list;
    for (const LookInfo& look : builtInLooks()) {
        if (!look.grade.lutPath.empty())
            list << QVariantMap{{QStringLiteral("name"), QString::fromStdString(look.title)},
                                {QStringLiteral("path"), QString::fromStdString(look.grade.lutPath)}};
    }
    return list;
}

bool ProjectController::applyLook(double clipId, const QString& lookId)
{
    const LookInfo* look = findLook(lookId.toStdString());
    if (!look) {
        reportError(QStringLiteral("Unknown look %1").arg(lookId));
        return false;
    }
    const Clip* clip = findClip(toId(clipId));
    if (!clip) {
        reportError(QStringLiteral("Select a video clip"));
        return false;
    }
    return apply(std::make_unique<SetClipLookCommand>(clip->id, ClipLook{look->id, look->title, 1.0, look->grade},
                                                      "Apply Look"));
}

bool ProjectController::setLookIntensity(double clipId, double intensity)
{
    const Clip* clip = findClip(toId(clipId));
    if (!clip || !clip->look)
        return false;
    ClipLook look = *clip->look;
    look.intensity = std::clamp(intensity, 0.0, 1.0);
    return apply(std::make_unique<SetClipLookCommand>(clip->id, look, "Change Look"));
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
    return apply(std::make_unique<SetClipLookCommand>(clip->id, std::nullopt, "Remove Look"));
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
    {"mp4-h264", "MP4 · H.264 (plays everywhere)", Container::Mp4, VideoCodec::H264, ProResProfile::Hq, AudioCodec::Aac},
    {"mp4-hevc", "MP4 · HEVC (smaller files)", Container::Mp4, VideoCodec::Hevc, ProResProfile::Hq, AudioCodec::Aac},
    {"mov-h264", "MOV · H.264", Container::Mov, VideoCodec::H264, ProResProfile::Hq, AudioCodec::Aac},
    {"mov-prores-proxy", "MOV · ProRes Proxy", Container::Mov, VideoCodec::ProRes, ProResProfile::Proxy, AudioCodec::Pcm24},
    {"mov-prores-lt", "MOV · ProRes LT", Container::Mov, VideoCodec::ProRes, ProResProfile::Lt, AudioCodec::Pcm24},
    {"mov-prores-422", "MOV · ProRes 422", Container::Mov, VideoCodec::ProRes, ProResProfile::Standard, AudioCodec::Pcm24},
    {"mov-prores-hq", "MOV · ProRes 422 HQ", Container::Mov, VideoCodec::ProRes, ProResProfile::Hq, AudioCodec::Pcm24},
    {"mov-prores-4444", "MOV · ProRes 4444", Container::Mov, VideoCodec::ProRes, ProResProfile::P4444, AudioCodec::Pcm24},
    {"gif", "GIF · animated, no sound", Container::Gif, VideoCodec::Gif, ProResProfile::Hq, AudioCodec::Aac},
    {"m4a", "M4A · AAC, sound only", Container::M4a, VideoCodec::H264, ProResProfile::Hq, AudioCodec::Aac},
    {"wav-16", "WAV · 16-bit, sound only", Container::Wav, VideoCodec::H264, ProResProfile::Hq, AudioCodec::Pcm16},
    {"wav-24", "WAV · 24-bit, sound only", Container::Wav, VideoCodec::H264, ProResProfile::Hq, AudioCodec::Pcm24},
    {"flac", "FLAC · lossless, sound only", Container::Flac, VideoCodec::H264, ProResProfile::Hq, AudioCodec::Flac},
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
    return presetList();
}

QVariantList ProjectController::exportFormats() const
{
    QVariantList list;
    for (const ExportFormat& format : kExportFormats) {
        MediaSettings settings;
        settings.container = format.container;
        list << QVariantMap{
            {QStringLiteral("id"), QString::fromLatin1(format.id)},
            {QStringLiteral("label"), QString::fromUtf8(format.label)},
            {QStringLiteral("extension"), QString::fromLatin1(fileExtension(format.container))},
            {QStringLiteral("video"), settings.hasVideo()},
            {QStringLiteral("sound"), format.container != Container::Gif},
            {QStringLiteral("quality"), format.codec == VideoCodec::H264 || format.codec == VideoCodec::Hevc},
        };
    }
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
            return refuse(QStringLiteral("Unknown export preset: ") + preset);
        options = found->toMap();
    }
    for (auto it = given.begin(); it != given.end(); ++it)
        options.insert(it.key(), it.value());

    QString formatId = options.value(QStringLiteral("format")).toString();
    if (formatId.isEmpty())
        formatId = options.value(QStringLiteral("codec")).toString() == QLatin1String("hevc") ? QStringLiteral("mp4-hevc")
                                                                                              : QStringLiteral("mp4-h264");
    const ExportFormat* format = findExportFormat(formatId);
    if (!format)
        return refuse(QStringLiteral("Unknown export format: ") + formatId);

    Exporter::Options result;
    MediaSettings& settings = result.settings;
    settings.container = format->container;
    settings.codec = format->codec;
    settings.proRes = format->proRes;
    settings.audioCodec = format->audio;
    settings.audioBitrate = format->container == Container::M4a ? 256'000 : 192'000;
    if (options.contains(QStringLiteral("audio")) && settings.hasVideo())
        settings.audio = options.value(QStringLiteral("audio")).toBool();
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
    if (range == QLatin1String("custom")) {
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
            return refuse(QStringLiteral("Select the clips to export"));
    } else if (range == QLatin1String("marker")) {
        const auto marker = std::ranges::find(m_project.markers, toId(options.value(QStringLiteral("markerId")).toDouble()),
                                              &tve::Marker::id);
        if (marker == m_project.markers.end() || marker->duration <= 0)
            return refuse(QStringLiteral("Pick a marker with a length (a range) to export"));
        result.from = marker->time;
        result.to = std::min(end, marker->time + marker->duration);
    } else if (range == QLatin1String("all")) {
        result.from = 0;
        result.to = end;
    } else {
        return refuse(QStringLiteral("Unknown export range: ") + range);
    }
    if (end == 0)
        return refuse(QStringLiteral("Add something to the timeline to export"));
    if (result.to <= result.from)
        return refuse(QStringLiteral("There is nothing to export in that range"));
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
    const QString path = file.isLocalFile() ? file.toLocalFile() : file.toString();
    if (path.isEmpty()) {
        reportError(QStringLiteral("Choose where to save the video"));
        return false;
    }
    const bool queued = std::ranges::any_of(m_exportJobs, [&](const ExportJob& job) {
        return job.path == path && (job.state == QLatin1String("waiting") || job.state == QLatin1String("running"));
    });
    if (queued) {
        reportError(QStringLiteral("That file is already being exported"));
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
            job.format = QString::fromUtf8(format.label).section(QStringLiteral(" ("), 0, 0);
    }
    job.options = std::move(*planned);
    job.snapshot = std::make_shared<const Project>(m_project);
    m_exportJobs.push_back(std::move(job));
    emit exportChanged();
    startNextExport();
    return true;
}

int ProjectController::exportMarkerRanges(const QUrl& folder, const QVariantMap& options)
{
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
        reportError(QStringLiteral("Give markers a length (a range) to export them one by one"));
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
    return QStringLiteral("%1:%2:%3:%4")
        .arg(totalSeconds / 3600, 2, 10, QLatin1Char('0'))
        .arg((totalSeconds / 60) % 60, 2, 10, QLatin1Char('0'))
        .arg(totalSeconds % 60, 2, 10, QLatin1Char('0'))
        .arg(ff, 2, 10, QLatin1Char('0'));
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
        reportError(QStringLiteral("Select a clip with sound"));
        return false;
    }
    if (!AudioEffectLibrary::builtIn().find(effectId.toStdString())) {
        reportError(QStringLiteral("Unknown audio effect %1").arg(effectId));
        return false;
    }
    auto effects = clip->audioEffects;
    effects.push_back(ClipEffect{effectId.toStdString(), true, {}, {}});
    return apply(std::make_unique<SetClipAudioEffectsCommand>(clip->id, std::move(effects), "Add Audio Effect"));
}

bool ProjectController::removeAudioEffect(double clipId, int index)
{
    const Clip* clip = findClip(toId(clipId));
    if (!clip || index < 0 || index >= int(clip->audioEffects.size()))
        return false;
    auto effects = clip->audioEffects;
    effects.erase(effects.begin() + index);
    return apply(std::make_unique<SetClipAudioEffectsCommand>(clip->id, std::move(effects), "Remove Audio Effect"));
}

bool ProjectController::setAudioEffectEnabled(double clipId, int index, bool enabled)
{
    const Clip* clip = findClip(toId(clipId));
    if (!clip || index < 0 || index >= int(clip->audioEffects.size()))
        return false;
    auto effects = clip->audioEffects;
    effects[std::size_t(index)].enabled = enabled;
    return apply(std::make_unique<SetClipAudioEffectsCommand>(clip->id, std::move(effects),
                                                              enabled ? "Enable Audio Effect" : "Disable Audio Effect"));
}

bool ProjectController::setAudioEffectParam(double clipId, int index, const QString& paramId, const QVariant& value)
{
    const Clip* clip = findClip(toId(clipId));
    if (!clip || index < 0 || index >= int(clip->audioEffects.size()))
        return false;
    const EffectManifest* manifest = AudioEffectLibrary::builtIn().find(clip->audioEffects[std::size_t(index)].effectId);
    const EffectParamSpec* param = manifest ? manifest->findParam(paramId.toStdString()) : nullptr;
    const auto components = componentsOf(value);
    if (!param || components.empty()) {
        reportError(QStringLiteral("Unknown setting %1").arg(paramId));
        return false;
    }
    auto effects = clip->audioEffects;
    effects[std::size_t(index)].values[param->id] = {std::clamp(components.front(), float(param->min), float(param->max))};
    return apply(std::make_unique<SetClipAudioEffectsCommand>(clip->id, std::move(effects), "Change Audio Effect"));
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
        reportError(QStringLiteral("Select a clip with sound"));
        return false;
    }
    const Clip& clip = *location->clip;
    const MediaItem* media = m_project.findMedia(clip.mediaId);
    if (clip.generated() || !media || !media->hasAudio
        || (location->track->kind == TrackKind::Video && !clip.audioEnabled)) {
        reportError(QStringLiteral("This clip has no sound of its own"));
        return false;
    }
    if (clip.keyframesOf(property::kVolume)) {
        reportError(QStringLiteral("The volume is animated: remove its keyframes to normalize"));
        return false;
    }
    if (m_measuringLoudness)
        return false;
    // The clip alone at 0 dB, from the start of a project of its own: what its own volume
    // then has to make up for.
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
                self->reportError(QStringLiteral("This clip is silent"));
                emit self->loudnessMeasured(double(id), std::nan(""));
                return;
            }
            const float gain = std::clamp(float(targetLufs - measured), kMinGainDb, kMaxGainDb);
            ClipAudio audio{gain, clip->fadeIn, clip->fadeOut};
            std::vector<std::unique_ptr<Command>> steps;
            steps.push_back(std::make_unique<SetClipAudioCommand>(clip->id, audio));
            self->m_loudness[id] = {measured, targetLufs};
            if (self->apply(std::make_unique<CompositeCommand>(std::move(steps), "Normalize Loudness")))
                emit self->loudnessMeasured(double(id), measured);
        }, Qt::QueuedConnection);
    }).detach();
    return true;
}

QVariantList ProjectController::textAnimations() const
{
    QVariantList list;
    for (const TextAnimationPreset& preset : textAnimationPresets()) {
        list << QVariantMap{{QStringLiteral("id"), QString::fromLatin1(preset.id.data(), qsizetype(preset.id.size()))},
                            {QStringLiteral("title"), QString::fromLatin1(preset.title.data(), qsizetype(preset.title.size()))},
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
            if (before.end() == clip.start && !before.generated() && !clip.generated() && !before.compound()
                && !clip.compound())
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
        reportError(QStringLiteral("There are no cuts between clips to put transitions on"));
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
        reportError(QStringLiteral("Unknown transition %1").arg(transitionId));
        return 0;
    }
    return applyTransitions([&](std::size_t) { return id; }, duration, "Apply Transition to All");
}

int ProjectController::applyRandomTransitions(double duration)
{
    const auto& all = EffectLibrary::builtIn().transitions();
    if (all.empty())
        return 0;
    std::mt19937 random{std::random_device{}()};
    std::uniform_int_distribution<std::size_t> pick(0, all.size() - 1);
    std::size_t previous = all.size();
    return applyTransitions(
        [&](std::size_t) {
            std::size_t chosen = pick(random);
            while (all.size() > 1 && chosen == previous)
                chosen = pick(random);
            previous = chosen;
            return all[chosen].manifest.id;
        },
        duration, "Random Transitions");
}

// --- Stickers ---

QVariantList ProjectController::stickers() const
{
    return stickers::catalog();
}

bool ProjectController::addSticker(const QString& stickerId)
{
    const QString path = stickers::pathOf(stickerId);
    if (path.isEmpty()) {
        reportError(QStringLiteral("Unknown sticker %1").arg(stickerId));
        return false;
    }
    // Its media, once (a resource path, not a URL); then a clip at the playhead on the lowest
    // free picture track above the main one (a new track on top if none is free), small, in
    // the middle.
    auto media = std::ranges::find(m_project.media, path.toStdString(), &MediaItem::path);
    if (media == m_project.media.end()) {
        const auto animation = stickers::open(path);
        if (!animation
            || !apply(std::make_unique<ImportMediaCommand>(MediaItem{.path = path.toStdString(), .duration = kStillDuration,
                                                                     .hasVideo = true, .hasAudio = false,
                                                                     .width = animation->width(),
                                                                     .height = animation->height(), .still = true,
                                                                     .alpha = true, .lottie = true})))
            return false;
        media = std::ranges::find(m_project.media, path.toStdString(), &MediaItem::path);
    }
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
        auto added = std::make_unique<AddTrackCommand>(TrackKind::Video, "Stickers");
        auto* raw = added.get();
        if (!apply(std::move(added)))
            return false;
        track = raw->trackId();
    }
    ClipVisual visual;
    visual.transform.scale = 0.4;
    auto clip = std::make_unique<AddClipCommand>(track, media->id, at, media->duration, 0, visual, "Add Sticker");
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
        reportError(QStringLiteral("Select two or more clips (⇧-click) to group them"));
        return false;
    }
    return apply(std::make_unique<GroupClipsCommand>(std::move(ids)));
}

bool ProjectController::ungroupSelected()
{
    const Clip* clip = findClip(m_selectedClip);
    if (!clip || clip->groupId == kNoId) {
        reportError(QStringLiteral("Select a grouped clip to ungroup"));
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
        reportError(QStringLiteral("Select the clips to put in a compound clip"));
        return 0;
    }
    std::size_t number = m_project.sequences.size() + 1;
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
        reportError(QStringLiteral("Select a compound clip to break apart"));
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
        reportError(QStringLiteral("Select a clip with media to analyse"));
        return nullptr;
    }
    if (needsSound && !media->hasAudio) {
        reportError(QStringLiteral("%1 has no sound").arg(mediaName(media->path)));
        return nullptr;
    }
    if (needsPictures && (!media->hasVideo || media->still)) {
        reportError(QStringLiteral("%1 has no moving pictures").arg(mediaName(media->path)));
        return nullptr;
    }
    if (clip->speed != 1.0 || clip->reversed || clip->freeze || !clip->speedRamp.empty()) {
        reportError(QStringLiteral("Analysis works on clips playing at normal speed"));
        return nullptr;
    }
    if (!QFileInfo::exists(QString::fromStdString(media->path))) {
        reportError(QStringLiteral("%1 is missing: relink it first").arg(mediaName(media->path)));
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
                const QString message = error == "cancelled" ? QString() : QString::fromStdString(error);
                if (!message.isEmpty())
                    reportError(QStringLiteral("Analysis failed: ") + message);
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
    emit analysisChanged();
}

bool ProjectController::findSilences(double clipId, const QVariantMap& options)
{
    if (analyzing()) {
        reportError(QStringLiteral("An analysis is already running"));
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
    withFeatures({mediaId}, false, QStringLiteral("Listening for pauses"), [this, id, settings] {
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
        reportError(QStringLiteral("Find the pauses first"));
        return false;
    }
    std::vector<std::pair<Ticks, Ticks>> cut;
    for (const QVariant& r : ranges) {
        const QVariantMap range = r.toMap();
        cut.emplace_back(ticks(range.value(QStringLiteral("start")).toDouble()), ticks(range.value(QStringLiteral("end")).toDouble()));
    }
    if (!apply(std::make_unique<RemoveRangesCommand>(location->track->id, std::move(cut), "Remove Pauses")))
        return false;
    m_silences.clear();
    emit analysisChanged();
    return true;
}

bool ProjectController::findScenes(double clipId, const QVariantMap& options)
{
    if (analyzing()) {
        reportError(QStringLiteral("An analysis is already running"));
        return false;
    }
    const Clip* clip = analysableClip(toId(clipId), false, true);
    if (!clip)
        return false;
    const Id id = clip->id;
    tve::SceneOptions settings;
    settings.threshold = options.value(QStringLiteral("threshold"), settings.threshold).toDouble();
    settings.minScene = options.value(QStringLiteral("minScene"), settings.minScene).toDouble();
    withFeatures({clip->mediaId}, true, QStringLiteral("Looking for scene changes"), [this, id, settings] {
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
        reportError(QStringLiteral("Find the scene changes first"));
        return false;
    }
    // Latest first: the clip keeps its id on the left of each split.
    std::vector<std::unique_ptr<Command>> commands;
    for (auto it = cuts.rbegin(); it != cuts.rend(); ++it)
        commands.push_back(std::make_unique<SplitClipCommand>(id, ticks(it->toDouble())));
    if (!apply(std::make_unique<CompositeCommand>(std::move(commands), "Split at Scenes")))
        return false;
    m_scenes.clear();
    emit analysisChanged();
    return true;
}

bool ProjectController::markScenes()
{
    const QVariantList cuts = m_scenes.value(QStringLiteral("cuts")).toList();
    if (cuts.isEmpty()) {
        reportError(QStringLiteral("Find the scene changes first"));
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
    if (!apply(std::make_unique<CompositeCommand>(std::move(commands), "Mark Scenes")))
        return false;
    m_scenes.clear();
    emit analysisChanged();
    return true;
}

bool ProjectController::findBeats(double clipId)
{
    if (analyzing()) {
        reportError(QStringLiteral("An analysis is already running"));
        return false;
    }
    const Clip* clip = analysableClip(toId(clipId), true, false);
    if (!clip)
        return false;
    const Id id = clip->id;
    withFeatures({clip->mediaId}, false, QStringLiteral("Finding the beat"), [this, id] {
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
        reportError(QStringLiteral("Find the beat first"));
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
    return apply(std::make_unique<CompositeCommand>(std::move(commands), "Add Beat Markers"));
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
        reportError(QStringLiteral("An analysis is already running"));
        return false;
    }
    const auto location = m_project.locateClip(toId(clipId));
    const Clip* clip = location ? location->clip : nullptr;
    const MediaItem* media = clip ? m_project.findMedia(clip->mediaId) : nullptr;
    if (!clip || clip->generated() || clip->compound() || !media || !media->hasAudio
        || (location->track->kind == TrackKind::Video && !clip->audioEnabled)) {
        reportError(QStringLiteral("Select a clip with sound to enhance"));
        return false;
    }
    if (!QFileInfo::exists(QString::fromStdString(media->path))) {
        reportError(QStringLiteral("This clip's media is missing"));
        return false;
    }
    const auto modelPath = m_speechModels.path(QString::fromLatin1(kEnhanceModel));
    if (!modelPath) {
        reportError(QStringLiteral("Download the speech enhancement model first"));
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
    runInBackground(haveFull ? QStringLiteral("Changing the strength") : QStringLiteral("Enhancing speech"),
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
        reportError(QStringLiteral("This clip's sound is not enhanced"));
        return false;
    }
    return apply(std::make_unique<SetClipEnhancedAudioCommand>(clip->id, std::nullopt));
}

bool ProjectController::duckUnderSpeech(double clipId, const QVariantMap& options)
{
    if (analyzing()) {
        reportError(QStringLiteral("An analysis is already running"));
        return false;
    }
    const Clip* music = findClip(toId(clipId));
    const MediaItem* musicMedia = music ? m_project.findMedia(music->mediaId) : nullptr;
    if (!music || !musicMedia || !musicMedia->hasAudio) {
        reportError(QStringLiteral("Select the music to lower under speech"));
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
        reportError(QStringLiteral("No other clip with sound plays under this one"));
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
    withFeatures(mediaIds, false, QStringLiteral("Listening for speech"), [this, musicId, voices, amount, fade, speech] {
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
            reportError(QStringLiteral("No speech under this clip"));
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
        if (commitAnimation(*music, after, "Auto Ducking"))
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
        {"auto", "Detect the language"}, {"en", "English"}, {"es", "Spanish"}, {"fr", "French"}, {"de", "German"},
        {"it", "Italian"}, {"pt", "Portuguese"}, {"nl", "Dutch"}, {"sv", "Swedish"}, {"pl", "Polish"}, {"ru", "Russian"},
        {"uk", "Ukrainian"}, {"tr", "Turkish"}, {"ar", "Arabic"}, {"hi", "Hindi"}, {"ta", "Tamil"}, {"id", "Indonesian"},
        {"vi", "Vietnamese"}, {"ja", "Japanese"}, {"ko", "Korean"}, {"zh", "Chinese"}};
    QVariantList list;
    for (const auto& [code, name] : languages)
        list << QVariantMap{{QStringLiteral("code"), QString::fromLatin1(code)}, {QStringLiteral("name"), QString::fromUtf8(name)}};
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
            const QString message = error == "cancelled" ? QString() : QString::fromStdString(error);
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
        reportError(QStringLiteral("An analysis is already running"));
        return false;
    }
    const QString modelId = options.value(QStringLiteral("model"), QStringLiteral("base.en-q5_1")).toString();
    const SpeechModels::Model* model = SpeechModels::find(modelId);
    const auto modelPath = m_speechModels.path(modelId);
    if (!model || !modelPath) {
        reportError(QStringLiteral("Download the speech model first"));
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
        reportError(chosen.empty() ? QStringLiteral("Add clips with sound (at normal speed) to make captions from")
                                   : QStringLiteral("The selected clips have no sound to make captions from"));
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
            reportError(QStringLiteral("No speech found"));
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
        if (apply(std::make_unique<SetCaptionsCommand>(tve::layoutCaptions(words), "Generate Captions", style, visual)))
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
        reportError(QStringLiteral("Select the words to delete"));
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
    return cutRanges(byTrack, "Delete Words");
}

int ProjectController::removeFillerWords()
{
    QVariantList fillers;
    for (const QVariant& w : transcript())
        if (w.toMap().value(QStringLiteral("filler")).toBool())
            fillers << w.toMap().value(QStringLiteral("index"));
    if (fillers.isEmpty()) {
        reportError(QStringLiteral("No filler words in the transcript"));
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
        reportError(QStringLiteral("No pauses that long between words"));
        return 0;
    }
    return cutRanges(byTrack, "Remove Pauses") ? count : 0;
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
        reportError(QStringLiteral("Make captions first"));
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
    return apply(std::make_unique<CompositeCommand>(std::move(commands), "Caption Style"));
}

std::vector<tve::CaptionBlock> ProjectController::captionBlocks() const
{
    std::vector<tve::CaptionBlock> blocks;
    const auto track = std::ranges::find_if(m_project.tracks, [](const Track& t) { return t.captions; });
    if (track == m_project.tracks.end())
        return blocks;
    for (const Clip& clip : track->clips)
        if (clip.text)
            blocks.push_back({clip.start, clip.end(), clip.text->text, {}});
    return blocks;
}

bool ProjectController::exportSubtitles(const QUrl& file)
{
    const QString path = file.isLocalFile() ? file.toLocalFile() : file.toString();
    const auto blocks = captionBlocks();
    if (blocks.empty()) {
        reportError(QStringLiteral("There are no captions to export"));
        return false;
    }
    const bool vtt = path.endsWith(QLatin1String(".vtt"), Qt::CaseInsensitive);
    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly)) {
        reportError(QStringLiteral("Cannot write ") + path);
        return false;
    }
    out.write(QByteArray::fromStdString(vtt ? tve::writeVtt(blocks) : tve::writeSrt(blocks)));
    if (!out.commit()) {
        reportError(QStringLiteral("Cannot write ") + path);
        return false;
    }
    return true;
}

bool ProjectController::importSubtitles(const QUrl& file)
{
    const QString path = file.isLocalFile() ? file.toLocalFile() : file.toString();
    QFile in(path);
    if (!in.open(QIODevice::ReadOnly) || in.size() > 20 * 1024 * 1024) {
        reportError(QStringLiteral("Cannot read ") + QFileInfo(path).fileName());
        return false;
    }
    std::string error;
    const auto blocks = tve::readSubtitles(in.readAll().toStdString(), &error);
    if (!blocks) {
        reportError(QStringLiteral("%1: %2").arg(QFileInfo(path).fileName(), QString::fromStdString(error)));
        return false;
    }
    return apply(std::make_unique<SetCaptionsCommand>(*blocks, "Import Captions"));
}
