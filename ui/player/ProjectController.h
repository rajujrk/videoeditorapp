#pragma once

#include "VideoView.h"
#include "commands/CommandStack.h"
#include "model/Project.h"

#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <memory>
#include <optional>

namespace tve {
class TimelinePlayback;
}

// The editor's view of one project: the model, its undo history, timeline playback and
// file I/O, exposed to QML. Every edit goes through a Command; after each one the
// playback engine gets a fresh immutable snapshot of the project.
//
// Times cross the QML boundary as seconds (double); ids as numbers.
class ProjectController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(VideoView* view READ view WRITE setView NOTIFY viewChanged)

    Q_PROPERTY(QString name READ name NOTIFY projectChanged)
    Q_PROPERTY(QString filePath READ filePath NOTIFY projectChanged)
    Q_PROPERTY(bool modified READ modified NOTIFY projectChanged)
    Q_PROPERTY(int canvasWidth READ canvasWidth NOTIFY projectChanged)
    Q_PROPERTY(int canvasHeight READ canvasHeight NOTIFY projectChanged)
    Q_PROPERTY(double frameRate READ frameRate NOTIFY projectChanged)
    Q_PROPERTY(double duration READ duration NOTIFY projectChanged)
    Q_PROPERTY(QVariantList tracks READ tracks NOTIFY projectChanged) // top to bottom
    Q_PROPERTY(QVariantList media READ media NOTIFY projectChanged)

    Q_PROPERTY(bool canUndo READ canUndo NOTIFY projectChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY projectChanged)
    Q_PROPERTY(QString undoText READ undoText NOTIFY projectChanged)
    Q_PROPERTY(QString redoText READ redoText NOTIFY projectChanged)

    Q_PROPERTY(double playhead READ playhead NOTIFY playheadChanged)
    Q_PROPERTY(bool playing READ playing NOTIFY playingChanged)
    Q_PROPERTY(double selectedClip READ selectedClip WRITE setSelectedClip NOTIFY selectionChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY errorOccurred)

public:
    explicit ProjectController(QObject* parent = nullptr);
    ~ProjectController() override;

    VideoView* view() const { return m_view; }
    void setView(VideoView* view);

    QString name() const;
    QString filePath() const { return m_filePath; }
    bool modified() const { return !m_stack->isClean(); }
    int canvasWidth() const { return m_project.settings.width; }
    int canvasHeight() const { return m_project.settings.height; }
    double frameRate() const;
    double duration() const;
    QVariantList tracks() const;
    QVariantList media() const;

    bool canUndo() const { return m_stack->canUndo(); }
    bool canRedo() const { return m_stack->canRedo(); }
    QString undoText() const;
    QString redoText() const;

    double playhead() const { return m_playhead; }
    bool playing() const;
    double selectedClip() const { return double(m_selectedClip); }
    void setSelectedClip(double clipId);
    QString lastError() const { return m_lastError; }

    // Project lifecycle
    Q_INVOKABLE void newProject(int width, int height, const QString& name = {});
    Q_INVOKABLE bool open(const QUrl& file);
    Q_INVOKABLE bool save(const QUrl& file = {});

    // Media: returns the number of files imported (already-imported files are reused).
    Q_INVOKABLE int importMedia(const QList<QUrl>& files);
    // Places a media item on a video track. trackId < 0 means the main track; start < 0
    // means right after that track's last clip.
    Q_INVOKABLE bool addClip(double mediaId, double trackId = -1, double start = -1);
    // Imports one file and appends it to the main track.
    Q_INVOKABLE bool importAndAppend(const QUrl& file);

    // Edits
    Q_INVOKABLE bool moveClip(double clipId, double trackId, double start);
    Q_INVOKABLE bool trimClip(double clipId, bool startEdge, double time);
    Q_INVOKABLE bool splitAtPlayhead();
    // Deletes the clip; on the main track the gap closes (ripple).
    Q_INVOKABLE bool deleteClip(double clipId);
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();

    // Transport
    Q_INVOKABLE void play();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void togglePlay();
    Q_INVOKABLE void seek(double seconds);
    Q_INVOKABLE void stepFrames(int frames);

    // Nearest edit point or playhead within `tolerance` seconds of `seconds`, ignoring the
    // edges of `ignoreClipId`; returns `seconds` when nothing is close enough.
    Q_INVOKABLE double snap(double seconds, double tolerance, double ignoreClipId = 0) const;
    // HH:MM:SS:FF at the project frame rate.
    Q_INVOKABLE QString timecode(double seconds) const;

    const tve::Project& project() const { return m_project; }

signals:
    void viewChanged();
    void projectChanged();
    void playheadChanged();
    void playingChanged();
    void selectionChanged();
    void errorOccurred(const QString& message);

private:
    bool apply(std::unique_ptr<tve::Command> command);
    void publish();
    void reportError(const QString& message);
    void resetProject(tve::Project project, const QString& filePath);
    void updatePlayhead();
    tve::Id mainTrackId() const;
    tve::Ticks frameTicks() const;
    std::optional<tve::Id> clipAtPlayhead() const;

    tve::Project m_project;
    std::unique_ptr<tve::CommandStack> m_stack;
    std::shared_ptr<tve::TimelinePlayback> m_playback;
    QPointer<VideoView> m_view;
    QString m_filePath;
    QString m_lastError;
    tve::Id m_selectedClip = tve::kNoId;
    double m_playhead = 0;
    bool m_wasPlaying = false;
    QTimer m_playheadTimer;
};
