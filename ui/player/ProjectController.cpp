#include "ProjectController.h"

#include "VideoView.h"
#include "audio/AudioOutput.h"
#include "commands/EditCommands.h"
#include "decode/AudioDecoder.h"
#include "decode/VideoDecoder.h"
#include "io/ProjectSerializer.h"
#include "model/TimelineQuery.h"
#include "gpu/EffectLibrary.h"
#include "EffectInfo.h"
#include "model/TimelineQuery.h"
#include "playback/TimelinePlayback.h"

#include <QFileInfo>
#include <QtGlobal>

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
    resetProject(emptyProject(1920, 1080), {});
}

ProjectController::~ProjectController()
{
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
            clips << QVariantMap{
                {QStringLiteral("id"), double(clip.id)},
                {QStringLiteral("mediaId"), double(clip.mediaId)},
                {QStringLiteral("label"), media ? fileName(media->path) : QStringLiteral("Missing media")},
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
    pause();
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
            reportError(QStringLiteral("Could not import %1: no video or audio found").arg(fileName(path)));
            continue;
        }
        if (item.duration <= 0) {
            reportError(QStringLiteral("%1 has no known duration").arg(fileName(path)));
            continue;
        }
        if (apply(std::make_unique<ImportMediaCommand>(std::move(item))))
            ++imported;
    }
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
    m_playback->setProject(std::make_shared<const Project>(m_project));
    m_trackModel.update(tracks());
    emit projectChanged();
    emit selectedClipInfoChanged();
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
        return {
            {QStringLiteral("id"), double(clip->id)},
            {QStringLiteral("label"), media ? fileName(media->path) : QString()},
            {QStringLiteral("trackKind"), track.kind == TrackKind::Audio ? QStringLiteral("audio") : QStringLiteral("video")},
            {QStringLiteral("locked"), track.locked},
            {QStringLiteral("duration"), seconds(clip->duration)},
            {QStringLiteral("hasAudio"), media && media->hasAudio},
            {QStringLiteral("audioEnabled"), clip->audioEnabled},
            {QStringLiteral("gainDb"), double(clip->gainDb)},
            {QStringLiteral("fadeIn"), seconds(clip->fadeIn)},
            {QStringLiteral("fadeOut"), seconds(clip->fadeOut)},
            {QStringLiteral("visual"), visualMap(clip->visual)},
            {QStringLiteral("effects"), effectsList(clip->effects)},
            {QStringLiteral("speed"), clip->speed},
            {QStringLiteral("reversed"), clip->reversed},
            {QStringLiteral("freeze"), clip->freeze},
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
    ClipVisual v = clip->visual;
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
    if (!v) {
        reportError(QStringLiteral("Select a video clip"));
        return false;
    }
    const bool ok = apply(std::make_unique<SetClipVisualCommand>(toId(clipId), *v));
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
    return apply(std::make_unique<SetClipVisualCommand>(toId(clipId), ClipVisual{}));
}

// --- Speed ---

bool ProjectController::setClipSpeed(double clipId, double speed, bool reversed)
{
    return apply(std::make_unique<SetClipSpeedCommand>(toId(clipId), std::clamp(speed, kMinSpeed, kMaxSpeed), reversed));
}

bool ProjectController::freezeFrame(double holdSeconds)
{
    const Ticks at = ticks(m_playhead);
    // The selected clip if the playhead is on it, else the visible clip there.
    Id target = kNoId;
    if (const Clip* selected = findClip(m_selectedClip); selected && selected->start <= at && at < selected->end())
        target = selected->id;
    else if (auto visible = visibleVideoAt(m_project, at))
        target = visible->clip->id;
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
    auto effects = effectsWithParam(toId(clipId), index, paramId, value);
    if (!effects)
        return false;
    return apply(std::make_unique<SetClipEffectsCommand>(toId(clipId), std::move(*effects), "Change Effect"));
}

void ProjectController::previewClipEffectParam(double clipId, int index, const QString& paramId, const QVariant& value)
{
    if (auto effects = effectsWithParam(toId(clipId), index, paramId, value); effects && m_view)
        m_view->setPreviewEffects(toId(clipId), std::move(*effects));
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
    m_playback->pause();
    updatePlayhead();
}

void ProjectController::togglePlay()
{
    playing() ? pause() : play();
}

void ProjectController::seek(double seconds)
{
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
    if (m_playback->clock().playing() && now >= end) {
        m_playback->pause();
        m_playback->seek(end);
        now = end;
    }
    const double value = seconds(now);
    if (value != m_playhead) {
        m_playhead = value;
        emit playheadChanged();
    }
    const double left = m_audioOutput->peakLeft();
    const double right = m_audioOutput->peakRight();
    if (left != m_levelLeft || right != m_levelRight) {
        m_levelLeft = left;
        m_levelRight = right;
        emit levelsChanged();
    }
    const bool isPlaying = m_playback->clock().playing();
    if (isPlaying != m_wasPlaying) {
        m_wasPlaying = isPlaying;
        emit playingChanged();
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
