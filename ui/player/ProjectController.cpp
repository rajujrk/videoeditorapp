#include "ProjectController.h"

#include "VideoView.h"
#include "commands/EditCommands.h"
#include "decode/VideoDecoder.h"
#include "io/ProjectSerializer.h"
#include "model/TimelineQuery.h"
#include "playback/TimelinePlayback.h"

#include <QFileInfo>

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
    m_playheadTimer.setInterval(16);
    connect(&m_playheadTimer, &QTimer::timeout, this, &ProjectController::updatePlayhead);
    m_playheadTimer.start();
    resetProject(emptyProject(1920, 1080), {});
}

ProjectController::~ProjectController() = default;

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
    if (id == m_selectedClip)
        return;
    m_selectedClip = id;
    emit selectionChanged();
}

// --- Lifecycle ---

void ProjectController::resetProject(Project project, const QString& filePath)
{
    pause();
    m_project = std::move(project);
    m_stack = std::make_unique<CommandStack>(m_project);
    m_filePath = filePath;
    m_selectedClip = kNoId;
    m_playback->seek(0);
    publish();
    emit selectionChanged();
}

void ProjectController::newProject(int width, int height, const QString&)
{
    resetProject(emptyProject(std::max(width, 16), std::max(height, 16)), {});
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
        std::string error;
        auto decoder = VideoDecoder::open(path, &error);
        if (!decoder) {
            reportError(QStringLiteral("Could not import %1").arg(QString::fromStdString(path)));
            continue;
        }
        const VideoStreamInfo& info = decoder->info();
        if (info.duration <= 0) {
            reportError(QStringLiteral("%1 has no known duration").arg(fileName(path)));
            continue;
        }
        MediaItem item{.path = path, .duration = info.duration, .width = info.width, .height = info.height,
                       .frameRate = info.frameRate};
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
    const Id target = trackId < 0 ? mainTrackId() : toId(trackId);
    const Track* track = m_project.findTrack(target);
    if (!track || track->kind != TrackKind::Video) {
        reportError(QStringLiteral("Video clips go on video tracks (audio arrives in a later Phase 1 step)"));
        return false;
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
    emit projectChanged();
}

void ProjectController::reportError(const QString& message)
{
    m_lastError = message;
    emit errorOccurred(message);
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
