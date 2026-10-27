#pragma once

#include "TrackListModel.h"
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
class AudioOutput;
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
    // The same tracks as a model whose rows survive edits (use this for delegates).
    Q_PROPERTY(QAbstractListModel* trackModel READ trackModel CONSTANT)
    Q_PROPERTY(QVariantList media READ media NOTIFY projectChanged)

    Q_PROPERTY(bool canUndo READ canUndo NOTIFY projectChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY projectChanged)
    Q_PROPERTY(QString undoText READ undoText NOTIFY projectChanged)
    Q_PROPERTY(QString redoText READ redoText NOTIFY projectChanged)

    Q_PROPERTY(double playhead READ playhead NOTIFY playheadChanged)
    Q_PROPERTY(bool playing READ playing NOTIFY playingChanged)
    Q_PROPERTY(double selectedClip READ selectedClip WRITE setSelectedClip NOTIFY selectionChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY errorOccurred)
    // The selected clip's details for the inspector (empty map when nothing is selected).
    Q_PROPERTY(QVariantMap selectedClipInfo READ selectedClipInfo NOTIFY selectedClipInfoChanged)

    // Output meters (0..1 peak per channel) and the audio device.
    Q_PROPERTY(double levelLeft READ levelLeft NOTIFY levelsChanged)
    Q_PROPERTY(double levelRight READ levelRight NOTIFY levelsChanged)
    Q_PROPERTY(QString audioDevice READ audioDevice CONSTANT)

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
    QAbstractListModel* trackModel() { return &m_trackModel; }
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
    QVariantMap selectedClipInfo() const;
    double levelLeft() const { return m_levelLeft; }
    double levelRight() const { return m_levelRight; }
    QString audioDevice() const;

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

    // Audio. Times in seconds; gain in dB (-60 = silent … +12).
    Q_INVOKABLE bool setClipAudio(double clipId, double gainDb, double fadeIn, double fadeOut);
    // Moves a video clip's sound to the first audio track that is free at its position.
    Q_INVOKABLE bool detachAudio(double clipId);
    // flag: "locked", "hidden" or "muted".
    Q_INVOKABLE bool setTrackFlag(double trackId, const QString& flag, bool value);

    // Picture. `visual` keys: x, y (canvas fractions), scale, rotation (degrees), cropLeft,
    // cropTop, cropRight, cropBottom (fractions), opacity (0..1), blend (name). Missing keys
    // keep the clip's current value. preview* shows a change live without committing it.
    Q_INVOKABLE bool setClipVisual(double clipId, const QVariantMap& visual);
    Q_INVOKABLE void previewClipVisual(double clipId, const QVariantMap& visual);
    Q_INVOKABLE void cancelPreview(double clipId);
    Q_INVOKABLE bool resetClipVisual(double clipId);

    // Speed (0.1–10) and direction; the clip's length changes and later clips follow.
    Q_INVOKABLE bool setClipSpeed(double clipId, double speed, bool reversed);
    // Inserts a still of the frame at the playhead into the clip under it (or the selected
    // clip) for `holdSeconds`.
    Q_INVOKABLE bool freezeFrame(double holdSeconds = 2.0);

    // Per-clip effects (by index in the clip's stack).
    Q_INVOKABLE bool addClipEffect(double clipId, const QString& effectId);
    Q_INVOKABLE bool removeClipEffect(double clipId, int index);
    Q_INVOKABLE bool setClipEffectEnabled(double clipId, int index, bool enabled);
    Q_INVOKABLE bool setClipEffectParam(double clipId, int index, const QString& paramId, const QVariant& value);
    Q_INVOKABLE void previewClipEffectParam(double clipId, int index, const QString& paramId, const QVariant& value);

    // Transport
    Q_INVOKABLE void play();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void togglePlay();
    Q_INVOKABLE void seek(double seconds);
    Q_INVOKABLE void stepFrames(int frames);

    // Nearest edit point or playhead within `tolerance` seconds of `seconds`, ignoring the
    // edges of `ignoreClipId`; returns `seconds` when nothing is close enough.
    Q_INVOKABLE double snap(double seconds, double tolerance, double ignoreClipId = 0) const;
    // {device, running, latencyMs, underruns} for diagnostics and --stats.
    Q_INVOKABLE QVariantMap audioStats() const;

    // HH:MM:SS:FF at the project frame rate.
    Q_INVOKABLE QString timecode(double seconds) const;

    const tve::Project& project() const { return m_project; }

signals:
    void viewChanged();
    void projectChanged();
    void playheadChanged();
    void playingChanged();
    void selectionChanged();
    void selectedClipInfoChanged();
    void levelsChanged();
    void errorOccurred(const QString& message);

private:
    bool apply(std::unique_ptr<tve::Command> command);
    void publish();
    void reportError(const QString& message);
    void resetProject(tve::Project project, const QString& filePath);
    void updatePlayhead();
    tve::Id mainTrackId() const;
    const tve::Clip* findClip(tve::Id clipId) const;
    std::optional<tve::ClipVisual> visualFrom(tve::Id clipId, const QVariantMap& values) const;
    std::optional<std::vector<tve::ClipEffect>> effectsWithParam(tve::Id clipId, int index, const QString& paramId,
                                                                 const QVariant& value) const;
    tve::Ticks frameTicks() const;
    std::optional<tve::Id> clipAtPlayhead() const;

    tve::Project m_project;
    std::unique_ptr<tve::CommandStack> m_stack;
    TrackListModel m_trackModel;
    std::shared_ptr<tve::TimelinePlayback> m_playback;
    std::unique_ptr<tve::AudioOutput> m_audioOutput;
    double m_levelLeft = 0;
    double m_levelRight = 0;
    QPointer<VideoView> m_view;
    QString m_filePath;
    QString m_lastError;
    tve::Id m_selectedClip = tve::kNoId;
    double m_playhead = 0;
    bool m_canvasChosen = false; // false: the first video clip sets the canvas
    bool m_wasPlaying = false;
    QTimer m_playheadTimer;
};
