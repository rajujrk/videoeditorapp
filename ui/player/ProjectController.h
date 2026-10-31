#pragma once

#include "Exporter.h"
#include "ProxyManager.h"
#include "SpeechModels.h"
#include "TrackListModel.h"
#include "VideoView.h"
#include "analysis/MediaAnalysis.h"
#include "model/Captions.h"
#include "speech/Transcriber.h"
#include "commands/CommandStack.h"
#include "encode/MediaWriter.h"
#include "model/Project.h"

#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <atomic>
#include <set>
#include <condition_variable>
#include <mutex>
#include <memory>
#include <optional>
#include <thread>

namespace tve {
class AudioOutput;
class AudioRecorder;
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
    // Media whose files are not where the project says: {id, name, path, folder}.
    Q_PROPERTY(QVariantList missingMedia READ missingMedia NOTIFY projectChanged)

    Q_PROPERTY(bool canUndo READ canUndo NOTIFY projectChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY projectChanged)
    Q_PROPERTY(QString undoText READ undoText NOTIFY projectChanged)
    Q_PROPERTY(QString redoText READ redoText NOTIFY projectChanged)

    Q_PROPERTY(double playhead READ playhead NOTIFY playheadChanged)
    Q_PROPERTY(bool playing READ playing NOTIFY playingChanged)
    Q_PROPERTY(double selectedClip READ selectedClip WRITE setSelectedClip NOTIFY selectionChanged)
    // Every selected clip (the selected clip, its group, and any added with toggleClipSelection).
    Q_PROPERTY(QVariantList selectedClips READ selectedClips NOTIFY selectionChanged)
    // {id, time, duration, color, note, chapter} in time order (seconds).
    Q_PROPERTY(QVariantList markers READ markers NOTIFY projectChanged)
    Q_PROPERTY(QStringList markerColors READ markerColors CONSTANT)
    Q_PROPERTY(QString lastError READ lastError NOTIFY errorOccurred)
    // The selected clip's details for the inspector (empty map when nothing is selected).
    Q_PROPERTY(QVariantMap selectedClipInfo READ selectedClipInfo NOTIFY selectedClipInfoChanged)
    // A selected transition is named by the clip it leads into (0 = none). Selecting one
    // clears the clip selection and the other way round.
    Q_PROPERTY(double selectedTransition READ selectedTransition WRITE setSelectedTransition NOTIFY selectionChanged)
    Q_PROPERTY(QVariantMap selectedTransitionInfo READ selectedTransitionInfo NOTIFY selectedClipInfoChanged)
    // The transition library: [{id, title, categories, params}].
    Q_PROPERTY(QVariantList availableTransitions READ availableTransitions CONSTANT)
    // Colour: a copied grade waiting to be pasted, and saved colour presets (by name, in
    // colorPresetDirectory: Application Support/…/Color Presets; TVE_COLOR_PRESET_DIR overrides).
    Q_PROPERTY(bool hasCopiedGrade READ hasCopiedGrade NOTIFY colorClipboardChanged)
    Q_PROPERTY(QStringList colorPresets READ colorPresets NOTIFY colorPresetsChanged)
    Q_PROPERTY(QString colorPresetDirectory READ colorPresetDirectory CONSTANT)
    // Editing state shared by the inspector and the player: the selected clip's mask being
    // edited (-1: none), and whether the next click on the player picks the key colour.
    Q_PROPERTY(int activeMask READ activeMask WRITE setActiveMask NOTIFY editingChanged)
    Q_PROPERTY(bool pickingKeyColor READ pickingKeyColor WRITE setPickingKeyColor NOTIFY editingChanged)
    // Split-screen and picture-in-picture layouts: [{id, title, slots: [[x0, y0, x1, y1]]}],
    // regions of the canvas from the top clip down.
    Q_PROPERTY(QVariantList layouts READ layouts CONSTANT)
    // Looks (filters) from the built-in library: [{id, title, category}]; its LUTs alone:
    // [{name, path}] (paths start ":/", for chooseLut).
    Q_PROPERTY(QVariantList looks READ looks CONSTANT)
    Q_PROPERTY(QVariantList lutLibrary READ lutLibrary CONSTANT)
    // Ready-made speed ramps: [{id, title, points: [{at, speed}]}].
    Q_PROPERTY(QVariantList speedRampPresets READ speedRampPresets CONSTANT)
    // Font families offered for titles.
    Q_PROPERTY(QStringList titleFonts READ titleFonts CONSTANT)
    // Built-in stickers: [{id, title, category, path}].
    Q_PROPERTY(QVariantList stickers READ stickers CONSTANT)
    // Title text animations: [{id, title, loop}] (loops keep moving; the others arrive or leave).
    Q_PROPERTY(QVariantList textAnimations READ textAnimations CONSTANT)

    // Autosave: unsaved work is written to `autosaveDirectory` a moment after edits settle
    // (default: Application Support/…/Autosave; TVE_AUTOSAVE_DIR overrides). Work left
    // there by a session that crashed, quit, or moved on without saving is listed in
    // recoverableProjects: [{id, name, originalPath, savedAt (ms since epoch), clipCount}],
    // newest first.
    Q_PROPERTY(bool autosaveEnabled READ autosaveEnabled WRITE setAutosaveEnabled NOTIFY recoveryChanged)
    Q_PROPERTY(QString autosaveDirectory READ autosaveDirectory WRITE setAutosaveDirectory NOTIFY recoveryChanged)
    Q_PROPERTY(QVariantList recoverableProjects READ recoverableProjects NOTIFY recoveryChanged)
    // When this document was last autosaved (ms since epoch; 0: not since its last change).
    Q_PROPERTY(double lastAutosave READ lastAutosave NOTIFY recoveryChanged)

    // Proxies: lighter copies of large media for the preview (export always uses the
    // originals). useProxies plays them where ready; autoProxies makes them for imported
    // video larger than 1080p. proxyStates: {media path: {state, progress, error}}.
    Q_PROPERTY(bool useProxies READ useProxies WRITE setUseProxies NOTIFY proxiesChanged)
    Q_PROPERTY(bool autoProxies READ autoProxies WRITE setAutoProxies NOTIFY proxiesChanged)
    Q_PROPERTY(QVariantMap proxyStates READ proxyStates NOTIFY proxiesChanged)

    // Analysis (Analyze tab): a background job measuring media (progress 0–1, cancellable),
    // then its findings for one clip, in timeline seconds, until applied or cleared.
    Q_PROPERTY(bool analyzing READ analyzing NOTIFY analysisChanged)
    Q_PROPERTY(double analysisProgress READ analysisProgress NOTIFY analysisChanged)
    Q_PROPERTY(QString analysisTask READ analysisTask NOTIFY analysisChanged)
    // {clipId, ranges: [{start, end}], total}: pauses to remove.
    Q_PROPERTY(QVariantMap silences READ silences NOTIFY analysisChanged)
    // {clipId, cuts: [seconds]}: scene changes.
    Q_PROPERTY(QVariantMap scenes READ scenes NOTIFY analysisChanged)
    // {clipId, bpm, times: [seconds]}: the beat.
    Q_PROPERTY(QVariantMap beats READ beats NOTIFY analysisChanged)

    // Captions: the speech models ({id, title, size, englishOnly, downloaded}), a download in
    // progress ({id, received, total, progress}), and the languages captions can be made in.
    Q_PROPERTY(QVariantList speechModels READ speechModels NOTIFY speechModelsChanged)
    Q_PROPERTY(QVariantMap modelDownload READ modelDownload NOTIFY speechModelsChanged)
    Q_PROPERTY(QVariantList captionLanguages READ captionLanguages CONSTANT)
    // How many captions the caption track holds, and how they follow the speech ("none",
    // "highlight", "pop", "karaoke", "reveal").
    Q_PROPERTY(int captionCount READ captionCount NOTIFY projectChanged)
    Q_PROPERTY(QString captionMotion READ captionMotion NOTIFY projectChanged)
    // The words said in the clips on the timeline (once transcribed), in timeline order:
    // {index, text, start, end, clipId, filler}.
    Q_PROPERTY(QVariantList transcript READ transcript NOTIFY transcriptChanged)

    // Export: the render queue. Jobs run one at a time, in order; `exporting` while one is
    // running (or paused), with its progress (0–1).
    Q_PROPERTY(bool exporting READ exporting NOTIFY exportChanged)
    Q_PROPERTY(double exportProgress READ exportProgress NOTIFY exportChanged)
    Q_PROPERTY(bool exportPaused READ exportPaused NOTIFY exportChanged)
    // {id, name, path, format, state ("waiting", "running", "done", "failed", "cancelled"),
    // progress, error, duration (seconds)} in queue order.
    Q_PROPERTY(QVariantList exportJobs READ exportJobs NOTIFY exportChanged)
    // {id, title, description, format, …} from export_presets.json.
    Q_PROPERTY(QVariantList exportPresets READ exportPresets CONSTANT)
    // {id, label, extension, video, quality}: the file formats export can write.
    Q_PROPERTY(QVariantList exportFormats READ exportFormats CONSTANT)

    // Output meters (0..1 peak per channel) and the audio device.
    // Audio effect packages for the Audio tab, by category: {id, title, categories, params}.
    Q_PROPERTY(QVariantList audioEffects READ audioEffects CONSTANT)
    // The mixer's meters while playing: {tracks: {<track id>: [peakL, peakR, rmsL, rmsR]},
    // master: [...], momentary, shortTerm, integrated} (LUFS; null while unknown); empty
    // when stopped.
    Q_PROPERTY(QVariantMap mixLevels READ mixLevels NOTIFY mixLevelsChanged)
    // A clip's loudness is being measured (normalizeLoudness).
    Q_PROPERTY(bool measuringLoudness READ measuringLoudness NOTIFY measuringLoudnessChanged)
    Q_PROPERTY(double levelLeft READ levelLeft NOTIFY levelsChanged)
    Q_PROPERTY(double levelRight READ levelRight NOTIFY levelsChanged)
    Q_PROPERTY(QString audioDevice READ audioDevice CONSTANT)

    // Voice-over: recording from a microphone at the playhead. The input is open (and its
    // level metered) while voiceOverActive; a take plays the timeline from the playhead and,
    // when it stops, lands on a free audio track there as one undoable step. Takes are WAV
    // files in recordingsDirectory: "<project> Voice-overs" beside a saved project, else
    // Movies/TechieVideoEditor Voice-overs (TVE_RECORDING_DIR overrides; TVE_AUDIO_INPUT=null
    // or none records without a microphone). microphoneAccess: granted, denied, undetermined.
    Q_PROPERTY(bool voiceOverActive READ voiceOverActive NOTIFY voiceOverChanged)
    Q_PROPERTY(bool recording READ recording NOTIFY voiceOverChanged)
    Q_PROPERTY(double recordingStart READ recordingStart NOTIFY voiceOverChanged)
    Q_PROPERTY(double recordingTime READ recordingTime NOTIFY inputLevelChanged)
    Q_PROPERTY(double inputLevel READ inputLevel NOTIFY inputLevelChanged)
    Q_PROPERTY(QStringList inputDevices READ inputDevices NOTIFY voiceOverChanged)
    Q_PROPERTY(QString inputDevice READ inputDevice WRITE setInputDevice NOTIFY voiceOverChanged)
    Q_PROPERTY(QString microphoneAccess READ microphoneAccessName NOTIFY voiceOverChanged)
    Q_PROPERTY(bool muteWhileRecording READ muteWhileRecording WRITE setMuteWhileRecording NOTIFY voiceOverChanged)
    Q_PROPERTY(QString recordingsDirectory READ recordingsDirectory NOTIFY projectChanged)

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
    QVariantList missingMedia() const;

    bool canUndo() const { return m_stack->canUndo(); }
    bool canRedo() const { return m_stack->canRedo(); }
    QString undoText() const;
    QString redoText() const;

    double playhead() const { return m_playhead; }
    bool playing() const;
    double selectedClip() const { return double(m_selectedClip); }
    void setSelectedClip(double clipId);
    QVariantList selectedClips() const;
    QVariantList markers() const;
    QStringList markerColors() const;
    QString lastError() const { return m_lastError; }
    QVariantMap selectedClipInfo() const;
    double selectedTransition() const { return double(m_selectedTransition); }
    void setSelectedTransition(double clipId);
    QVariantMap selectedTransitionInfo() const;
    QVariantList availableTransitions() const;
    QStringList titleFonts() const;
    QVariantList textAnimations() const;
    QVariantList stickers() const;
    QVariantList speedRampPresets() const;
    QVariantList looks() const;
    QVariantList layouts() const;
    int activeMask() const { return m_activeMask; }
    void setActiveMask(int index);
    bool pickingKeyColor() const { return m_pickingKeyColor; }
    void setPickingKeyColor(bool picking);
    QVariantList lutLibrary() const;
    bool hasCopiedGrade() const { return m_copiedGrade.has_value(); }
    QStringList colorPresets() const;
    QString colorPresetDirectory() const;
    bool autosaveEnabled() const { return m_autosaveEnabled; }
    void setAutosaveEnabled(bool enabled);
    QString autosaveDirectory() const { return m_autosaveDir; }
    void setAutosaveDirectory(const QString& directory);
    QVariantList recoverableProjects() const;
    double lastAutosave() const { return double(m_lastAutosave); }
    bool useProxies() const { return m_useProxies; }
    void setUseProxies(bool use);
    bool autoProxies() const { return m_autoProxies; }
    void setAutoProxies(bool automatic);
    QVariantMap proxyStates() const;
    ProxyManager& proxies() { return m_proxies; }
    bool exporting() const { return m_exportThread.joinable(); }
    double exportProgress() const { return m_exportProgress; }
    bool exportPaused() const;
    bool analyzing() const { return m_analysisThread.joinable(); }
    double analysisProgress() const { return m_analysisProgress; }
    QString analysisTask() const { return m_analysisTask; }
    QVariantMap silences() const { return m_silences; }
    QVariantMap scenes() const { return m_scenes; }
    QVariantMap beats() const { return m_beats; }
    QVariantList speechModels() const { return m_speechModels.list(); }
    QVariantMap modelDownload() const { return m_speechModels.progress(); }
    QVariantList captionLanguages() const;
    int captionCount() const;
    QString captionMotion() const;
    QVariantList transcript() const;
    QVariantList exportJobs() const;
    QVariantList exportPresets() const;
    QVariantList exportFormats() const;
    QVariantList audioEffects() const;
    void updateMixLevels(tve::Ticks now);
    QVariantMap mixLevels() const { return m_mixLevels; }
    bool measuringLoudness() const { return m_measuringLoudness; }
    double levelLeft() const { return m_levelLeft; }
    double levelRight() const { return m_levelRight; }
    QString audioDevice() const;
    bool voiceOverActive() const { return m_recorder != nullptr; }
    bool recording() const;
    double recordingStart() const;
    double recordingTime() const { return m_recordingTime; }
    double inputLevel() const { return m_inputLevel; }
    QStringList inputDevices() const { return m_inputDevices; }
    QString inputDevice() const;
    void setInputDevice(const QString& name);
    QString microphoneAccessName() const;
    bool muteWhileRecording() const { return m_muteWhileRecording; }
    void setMuteWhileRecording(bool mute);
    QString recordingsDirectory() const;

    // Project lifecycle
    Q_INVOKABLE void newProject(int width, int height, const QString& name = {});
    Q_INVOKABLE bool open(const QUrl& file);
    Q_INVOKABLE bool save(const QUrl& file = {});

    // Media: returns the number of files imported (already-imported files are reused).
    Q_INVOKABLE int importMedia(const QList<QUrl>& files);
    // Places a media item on a video track. trackId < 0 means the main track; start < 0
    // means right after that track's last clip.
    Q_INVOKABLE bool addClip(double mediaId, double trackId = -1, double start = -1);
    // Points media at another file (moved or renamed). A file whose content differs from the
    // one imported is refused unless `force`; one of another kind (no pictures for a video…)
    // always is.
    Q_INVOKABLE bool relinkMedia(double mediaId, const QUrl& file, bool force = false);
    // Finds the missing media's files in `folder` (and the folders in it) by name and content,
    // and relinks them all in one undo step; returns how many.
    Q_INVOKABLE int relinkFromFolder(const QUrl& folder);
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
    // Mixer: any of {gainDb, pan (-1..1), solo}.
    Q_INVOKABLE bool setTrackMix(double trackId, const QVariantMap& changes);

    // Audio effects on a clip's sound, like the picture effects (values are numbers).
    Q_INVOKABLE bool addAudioEffect(double clipId, const QString& effectId);
    Q_INVOKABLE bool removeAudioEffect(double clipId, int index);
    Q_INVOKABLE bool setAudioEffectEnabled(double clipId, int index, bool enabled);
    Q_INVOKABLE bool setAudioEffectParam(double clipId, int index, const QString& paramId, const QVariant& value);
    // Whether the clip's sound keeps its pitch when it plays faster or slower.
    Q_INVOKABLE bool setKeepPitch(double clipId, bool keep);
    // Measures the clip's loudness (after its effects, before its volume) in the background,
    // then sets its volume so it plays at `targetLufs`: one "Normalize Loudness" step.
    // loudnessMeasured() reports the measurement (NaN when silent).
    Q_INVOKABLE bool normalizeLoudness(double clipId, double targetLufs);

    // Picture. `visual` keys: x, y (canvas fractions), scale, rotation (degrees), cropLeft,
    // cropTop, cropRight, cropBottom (fractions), opacity (0..1), blend (name). Missing keys
    // keep the clip's current value. preview* shows a change live without committing it.
    Q_INVOKABLE bool setClipVisual(double clipId, const QVariantMap& visual);
    Q_INVOKABLE void previewClipVisual(double clipId, const QVariantMap& visual);
    Q_INVOKABLE void cancelPreview(double clipId);
    Q_INVOKABLE bool resetClipVisual(double clipId);

    // Keyframes, at the playhead (snapped to the frame grid). `properties` are channels:
    // animatable property ids (x, y, scale, rotation, opacity, cropLeft…, volume) or an
    // effect's keyframable parameter, "effect:<stack index>:<parameter id>"; empty means the
    // usual set: placement and opacity for pictures, volume for sound-only clips (add), or
    // every property with a keyframe there (remove). While a property is animated, editing
    // it in the inspector sets (or adds) its keyframe at the playhead. Times in and out of
    // these calls are seconds from the clip's start. selectedClipInfo carries
    // keyframes {property: [{time, value, easing}]} and keyframeStates {property: "key" |
    // "animated"} for the playhead, and for a keyframe there its keyframeEasing and
    // keyframeCurve [x1, y1, x2, y2]; easing: linear, easeIn, easeOut, easeInOut, hold, curve.
    Q_INVOKABLE bool addKeyframes(double clipId, const QStringList& properties = {});
    Q_INVOKABLE bool removeKeyframes(double clipId, const QStringList& properties = {});
    // Stops animating (all properties when empty); each keeps its value at the playhead.
    Q_INVOKABLE bool clearKeyframes(double clipId, const QStringList& properties = {});
    Q_INVOKABLE bool setKeyframeEasing(double clipId, const QString& easing);
    // Gives the keyframes at the playhead a custom curve (cubic Bezier control points).
    Q_INVOKABLE bool setKeyframeCurve(double clipId, double x1, double y1, double x2, double y2);
    // Moves every keyframe at `from` to `to` (seconds from the clip's start, snapped to the
    // frame grid) and puts the playhead on it.
    Q_INVOKABLE bool moveKeyframes(double clipId, double from, double to);
    // Jumps the playhead to the clip's previous (-1) or next (+1) keyframe.
    Q_INVOKABLE bool seekKeyframe(double clipId, int direction);

    // Speed (0.1–10) and direction; the clip's length changes and later clips follow.
    Q_INVOKABLE bool setClipSpeed(double clipId, double speed, bool reversed);
    // Inserts a still of the frame at the playhead into the clip under it (or the selected
    // clip) for `holdSeconds`.
    Q_INVOKABLE bool freezeFrame(double holdSeconds = 2.0);
    // Colour grades. `changes` holds any of: exposure, contrast, saturation, temperature,
    // tint (numbers); lift, gamma, gain, offset ({x, y, master}); master, red, green, blue
    // (curves: [{x, y}], empty for none); hueVsSat, hueVsHue, lumaVsSat (curves where y 0.5
    // changes nothing); hsl ([{band 0–7, hue, saturation, luminance}], the bands to change:
    // red, orange, yellow, green, aqua, blue, purple, magenta); lutIntensity.
    // selectedClipInfo.color has them all (hsl as eight {hue, saturation, luminance}),
    // plus lutPath, lutName, lutMissing and neutral.
    Q_INVOKABLE bool setClipColor(double clipId, const QVariantMap& changes);
    Q_INVOKABLE void previewClipColor(double clipId, const QVariantMap& changes);
    Q_INVOKABLE bool resetClipColor(double clipId);
    // Reads the .cube or .3dl file first: refuses one it cannot use. qrc:/… picks a built-in
    // one (see lutLibrary).
    Q_INVOKABLE bool chooseLut(double clipId, const QUrl& file);
    Q_INVOKABLE bool removeLut(double clipId);
    Q_INVOKABLE bool copyGrade(double clipId);
    Q_INVOKABLE bool pasteGrade(double clipId);
    Q_INVOKABLE bool saveColorPreset(double clipId, const QString& name);
    Q_INVOKABLE bool applyColorPreset(double clipId, const QString& name);
    Q_INVOKABLE bool deleteColorPreset(const QString& name);
    // Masks, chroma key and track matte (applied after colour, before effects).
    // selectedClipInfo has masks [{shape, x, y, width, height, rotation, roundness, feather,
    // invert, points [[x, y]]}], chromaKey {enabled, color [r, g, b], tolerance, softness,
    // spill} and matte (none, alpha, invertedAlpha, luma, invertedLuma). shape: rectangle,
    // ellipse, line, star, heart, pen; positions are fractions of the picture.
    Q_INVOKABLE bool addMask(double clipId, const QString& shape, const QVariantList& points = {});
    Q_INVOKABLE bool setMask(double clipId, int index, const QVariantMap& changes);
    Q_INVOKABLE void previewMask(double clipId, int index, const QVariantMap& changes);
    Q_INVOKABLE bool removeMask(double clipId, int index);
    Q_INVOKABLE bool setChromaKey(double clipId, const QVariantMap& changes);
    Q_INVOKABLE void previewChromaKey(double clipId, const QVariantMap& changes);
    Q_INVOKABLE bool setMatte(double clipId, const QString& mode);
    // The clip's own colour [r, g, b] at picture point (u, v) under the playhead (before its
    // grade and key), for picking a key colour; empty if it cannot be read.
    Q_INVOKABLE QVariantList clipColorAt(double clipId, double u, double v);
    // How the clip's picture sits on the canvas at the playhead: {m: canvasToPicture [6]
    // (picture.u = m0·u + m1·v + m2, picture.v = m3·u + m4·v + m5, canvas and picture as
    // 0–1 fractions), width, height (the picture's pixels)}; empty for no picture.
    Q_INVOKABLE QVariantMap clipPlacement(double clipId) const;
    // Places the pictures at the playhead (top track first) in the layout's regions, as one
    // step; a picture-in-picture sets only the top one.
    Q_INVOKABLE bool applyLayout(const QString& layoutId);

    // Looks: a clip has at most one, applied before its grade at an intensity (0–1).
    // selectedClipInfo.look is {id, title, intensity} or absent.
    Q_INVOKABLE bool applyLook(double clipId, const QString& lookId);
    Q_INVOKABLE bool setLookIntensity(double clipId, double intensity);
    Q_INVOKABLE void previewLookIntensity(double clipId, double intensity);
    Q_INVOKABLE bool removeLook(double clipId);
    // Adds a 5-second adjustment layer at `start` (default: the playhead) on `trackId`, or the
    // lowest free video track above the main one (else a new one on top), and selects it.
    Q_INVOKABLE bool addAdjustmentLayer(double trackId = -1, double start = -1);

    // `count` outputs of a tone curve ([{x, y}]) at evenly spaced inputs, for drawing it.
    Q_INVOKABLE QVariantList sampleColorCurve(const QVariantList& points, int count) const;

    // Speed ramps: `points` [{at (0–1 of the clip), speed}], first at 0, last at 1. The clip
    // keeps its source span, so its length follows the average speed. Clip maps carry
    // speedRamp (the same list; empty when constant).
    Q_INVOKABLE bool setClipSpeedRamp(double clipId, const QVariantList& points);
    Q_INVOKABLE bool applySpeedRampPreset(double clipId, const QString& presetId);
    // Back to a constant speed (the ramp's average, keeping the clip's length).
    Q_INVOKABLE bool removeSpeedRamp(double clipId);
    // Source time (seconds) a clip shows `offset` seconds after its start (for thumbnails).
    Q_INVOKABLE double clipSourceTime(double clipId, double offset) const;

    // Per-clip effects (by index in the clip's stack).
    Q_INVOKABLE bool addClipEffect(double clipId, const QString& effectId);
    Q_INVOKABLE bool removeClipEffect(double clipId, int index);
    Q_INVOKABLE bool setClipEffectEnabled(double clipId, int index, bool enabled);
    Q_INVOKABLE bool setClipEffectParam(double clipId, int index, const QString& paramId, const QVariant& value);
    Q_INVOKABLE void previewClipEffectParam(double clipId, int index, const QString& paramId, const QVariant& value);

    // Transitions, named by the clip they lead into. addTransition puts one on that clip's
    // cut (replacing any there, keeping its length); with clipId < 0, on the cut nearest the
    // playhead on the selected clip's track or the main track. addTransitionAt uses the cut
    // on `trackId` nearest `seconds`, within `tolerance`. Selects the new transition.
    Q_INVOKABLE bool addTransition(const QString& transitionId, double clipId = -1);
    Q_INVOKABLE bool addTransitionAt(const QString& transitionId, double trackId, double seconds, double tolerance);
    // `changes` keys: duration (seconds), alignment ("center", "endsAtCut", "startsAtCut"),
    // values ({paramId: value}, merged). One undo step.
    Q_INVOKABLE bool setTransition(double clipId, const QVariantMap& changes);
    // Every cut between two pictures on the unlocked video tracks gets `transitionId` (or, with
    // applyRandomTransitions, a random one each, no two neighbours alike), `duration` seconds
    // long (< 0: the default). One undo step; returns how many cuts.
    Q_INVOKABLE int applyTransitionToAll(const QString& transitionId, double duration = -1);
    Q_INVOKABLE int applyRandomTransitions(double duration = -1);
    Q_INVOKABLE bool removeTransition(double clipId);

    // Titles. `style` holds text fields (see TitleRenderer.h textMap) plus optional x and y
    // (the clip's position). trackId < 0: the lowest free video or title track above the
    // main track, or a new track on top; start < 0: the playhead. Lasts 5 s; selected.
    Q_INVOKABLE bool addTitle(const QVariantMap& style, double trackId = -1, double start = -1);
    // Puts a built-in sticker at the playhead on a free picture track above the main one.
    Q_INVOKABLE bool addSticker(const QString& stickerId);
    // Applies the text fields in `changes` to a title clip (one undo step).
    Q_INVOKABLE bool setClipText(double clipId, const QVariantMap& changes);

    // Markers. addMarker puts one at `seconds` (the playhead when negative) and returns its
    // id (0 on failure). setMarker takes any of time, duration, color, note, chapter.
    Q_INVOKABLE double addMarker(double seconds = -1, const QString& note = {});
    Q_INVOKABLE bool setMarker(double markerId, const QVariantMap& changes);
    Q_INVOKABLE bool removeMarker(double markerId);
    // Moves the playhead to the next (direction > 0) or previous marker.
    Q_INVOKABLE bool seekMarker(int direction);

    // Selection of several clips, groups and compound clips.
    Q_INVOKABLE void toggleClipSelection(double clipId);
    Q_INVOKABLE bool groupSelected();
    Q_INVOKABLE bool ungroupSelected();
    // Makes the selected clips one compound clip ("Compound N"); returns its id (0 on failure).
    Q_INVOKABLE double createCompound();
    Q_INVOKABLE bool breakCompound(double clipId);

    // Makes (in the background) or deletes the proxy of a video.
    // Voice-over. openVoiceOver asks for microphone access first if it has not been given
    // (returning false until it is; voiceOverChanged follows the answer). closeVoiceOver
    // ends a take in progress, keeping it.
    Q_INVOKABLE bool openVoiceOver();
    Q_INVOKABLE void closeVoiceOver();
    Q_INVOKABLE bool startRecording();
    Q_INVOKABLE bool stopRecording();
    Q_INVOKABLE void openMicrophoneSettings();

    Q_INVOKABLE bool createProxy(double mediaId);
    Q_INVOKABLE bool deleteProxy(double mediaId);

    // Writes unsaved work to the autosave folder now (normally done by a timer).
    Q_INVOKABLE void autosaveNow();
    // Opens recovered work as an unsaved project (saving goes to its original file, if it
    // had one) and removes it from the list. discardRecovery deletes it.
    Q_INVOKABLE bool recoverProject(const QString& id);
    Q_INVOKABLE void discardRecovery(const QString& id);

    // Analysis. Each measures the clip's media in the background (once: results are cached
    // by the file's fingerprint), then publishes what it found; false (with lastError) if it
    // cannot start. Clips must play at normal speed.
    // options: thresholdDb, minDuration, padding (seconds).
    Q_INVOKABLE bool findSilences(double clipId, const QVariantMap& options = {});
    // Cuts the found pauses out of the clip's track, closing the gaps (one undo step).
    Q_INVOKABLE bool removeSilences();
    // options: threshold (0..1), minScene (seconds).
    Q_INVOKABLE bool findScenes(double clipId, const QVariantMap& options = {});
    Q_INVOKABLE bool splitAtScenes();
    Q_INVOKABLE bool markScenes();
    Q_INVOKABLE bool findBeats(double clipId);
    // A marker on every `every`th beat (1, 2, 4…), from the first.
    Q_INVOKABLE bool addBeatMarkers(int every = 1);
    // Lowers the clip (music) under the sound of the other clips playing with it (speech):
    // volume keyframes dipping by amountDb with fades of `fade` seconds. options: amountDb,
    // fade, thresholdDb.
    Q_INVOKABLE bool duckUnderSpeech(double clipId, const QVariantMap& options = {});
    Q_INVOKABLE void cancelAnalysis();

    // Captions. Downloads a speech model (checked by SHA-256); modelDownloadFinished reports.
    Q_INVOKABLE bool downloadSpeechModel(const QString& modelId);
    Q_INVOKABLE void cancelModelDownload();
    // Transcribes the selected clips (or, with none selected, every clip with sound) in the
    // background and puts captions on the caption track (replacing it; one undo step).
    // options: model (a speechModels id), language (a code, or "auto").
    Q_INVOKABLE bool generateCaptions(const QVariantMap& options = {});
    // The same, without making captions: just the transcript.
    Q_INVOKABLE bool transcribe(const QVariantMap& options = {});
    // Cuts the transcript's words (by index) out of their clips' tracks, closing the gaps; the
    // captions are made again to match. One undo step.
    Q_INVOKABLE bool deleteWords(const QVariantList& indices);
    // Deletes every filler word ("um", "uh"…); returns how many.
    Q_INVOKABLE int removeFillerWords();
    // Shortens pauses between words longer than `longerThan` seconds to `keep` on each side;
    // returns how many.
    Q_INVOKABLE int removeTranscriptPauses(double longerThan = 0.7, double keep = 0.15);
    // How every caption follows the speech, and the colour of the words said.
    Q_INVOKABLE bool setCaptionStyle(const QString& motion, const QVariant& highlightColor = {});
    // SRT or WebVTT (by the file's extension): the caption track out, or a file in (replacing it).
    Q_INVOKABLE bool exportSubtitles(const QUrl& file);
    Q_INVOKABLE bool importSubtitles(const QUrl& file);
    Q_INVOKABLE void clearAnalysis();

    // Queues an export of the project as it is now; it runs when the jobs before it are done,
    // and exportFinished reports its end. `options`: preset (an exportPresets id, whose values
    // come first), format (an exportFormats id; "mp4-h264" by default; codec "hevc" also picks
    // HEVC MP4), width and height (missing: the canvas, or shortSide high in its shape),
    // fill (another shape: fill and crop rather than letterbox), frameRate (missing: the
    // project's), quality ("high", "standard", "small") or videoBitrate (bits/s), audio
    // (false: no sound track), normalizeLoudness and loudnessTarget (LUFS), and range: "all"
    // (default), "selection" (the selected clips' span), "marker" (markerId, a marker with a
    // length) or "custom" (from, to in seconds). Returns false (with lastError) if it cannot:
    // an empty range, a file already in the queue, or settings that do not go together.
    Q_INVOKABLE bool exportVideo(const QUrl& file, const QVariantMap& options = {});
    // Queues one export per marker with a length into `folder`, named by their notes; returns
    // how many.
    Q_INVOKABLE int exportMarkerRanges(const QUrl& folder, const QVariantMap& options = {});
    // Stops the running job (jobId 0) or the job given; a waiting one just leaves the queue.
    Q_INVOKABLE void cancelExport(double jobId = 0);
    Q_INVOKABLE void pauseExport();
    Q_INVOKABLE void resumeExport();
    // Takes a waiting or finished job off the list.
    Q_INVOKABLE bool removeExportJob(double jobId);
    Q_INVOKABLE void clearFinishedExports();
    // Bits per second an export with these options would use (for size estimates).
    Q_INVOKABLE double exportBitrate(const QVariantMap& options) const;
    // {from, to, duration} in seconds of the range `options` cover; empty when there is none.
    Q_INVOKABLE QVariantMap exportRange(const QVariantMap& options) const;

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
    void mixLevelsChanged();
    void measuringLoudnessChanged();
    void loudnessMeasured(double clipId, double lufs);
    void errorOccurred(const QString& message);
    void exportChanged();
    void recoveryChanged();
    void proxiesChanged();
    void voiceOverChanged();
    void colorClipboardChanged();
    void editingChanged();
    void colorPresetsChanged();
    void inputLevelChanged();
    // ok: `message` is the file's path; otherwise the error, or empty when cancelled.
    void exportFinished(bool ok, const QString& message, double jobId);
    void analysisChanged();
    void speechModelsChanged();
    void modelDownloadFinished(const QString& modelId, bool ok, const QString& error);
    void transcriptChanged();
    // An analysis finished (ok) or failed or was cancelled (message, empty when cancelled).
    void analysisFinished(bool ok, const QString& message);

private:
    bool apply(std::unique_ptr<tve::Command> command);
    void publish();
    void reportError(const QString& message);
    void resetProject(tve::Project project, const QString& filePath);
    void updatePlayhead();
    tve::Id mainTrackId() const;
    const tve::Clip* findClip(tve::Id clipId) const;
    std::optional<tve::ClipVisual> visualFrom(tve::Id clipId, const QVariantMap& values) const;
    // Playhead on the frame grid, from the clip's start.
    tve::Ticks keyTime(const tve::Clip& clip) const;
    tve::Ticks frameTolerance() const { return frameTicks() / 2; }
    // Sets channel `id`'s keyframe at keyTime(clip) to `value` (adding one if needed). A
    // channel is a property id or "effect:<stack index>:<parameter id>".
    void setKey(tve::Clip& clip, const std::string& id, double value) const;
    tve::Keyframes withKey(const tve::Clip& clip, std::string_view id, double value) const;
    // Applies the difference between `before` and `after` (keyframes, static picture and
    // volume, effects) as one undo step. False when nothing changed or it was refused.
    bool commitAnimation(const tve::Clip& before, tve::Clip after, const char* label);
    std::optional<std::vector<tve::ClipEffect>> effectsWithParam(tve::Id clipId, int index, const QString& paramId,
                                                                 const QVariant& value) const;
    tve::Ticks frameTicks() const;
    std::optional<tve::Id> clipAtPlayhead() const;
    // The clip after the cut nearest `time` on `track` (a cut: a clip starting where another
    // ends), within `tolerance`.
    // What a media file is (probed, or a still or sticker), with its fingerprint; nullopt and
    // `error` if it cannot be used.
    std::optional<tve::MediaItem> describeMedia(const std::string& path, QString* error) const;
    // The settings and range `options` ask for (a preset's values first), or why they cannot be.
    std::optional<Exporter::Options> exportOptions(const QVariantMap& options, QString* error = nullptr) const;
    // Runs the first waiting job, if none is running.
    void startNextExport();
    std::optional<tve::Id> cutNear(const tve::Track& track, tve::Ticks time, tve::Ticks tolerance) const;
    // The clips that start a cut a transition can go on (see applyTransitionToAll).
    std::vector<tve::Id> transitionCuts() const;
    int applyTransitions(const std::function<std::string(std::size_t)>& choose, double duration, const char* label);

    tve::Project m_project;
    std::unique_ptr<tve::CommandStack> m_stack;
    TrackListModel m_trackModel;
    std::shared_ptr<tve::TimelinePlayback> m_playback;
    std::unique_ptr<tve::AudioOutput> m_audioOutput;
    QVariantMap m_mixLevels;
    bool m_measuringLoudness = false;
    // Measured and normalized loudness by clip (for the inspector), LUFS.
    std::map<tve::Id, std::pair<double, double>> m_loudness;
    double m_levelLeft = 0;
    double m_levelRight = 0;
    QPointer<VideoView> m_view;
    QString m_filePath;
    QString m_lastError;
    tve::Id m_selectedClip = tve::kNoId;
    std::vector<tve::Id> m_selectedClips; // includes m_selectedClip
    tve::Id m_selectedTransition = tve::kNoId;
    double m_playhead = 0;
    bool m_canvasChosen = false; // false: the first video clip sets the canvas
    bool m_wasPlaying = false;
    QTimer m_playheadTimer;

    // The project as playback sees it: with proxy paths where proxies are used and ready.
    std::shared_ptr<const tve::Project> playbackSnapshot() const;
    // Queues proxies for the project's large videos when autoProxies is on.
    void requestAutoProxies();
    ProxyManager m_proxies;
    bool m_useProxies = true;
    bool m_autoProxies = true;

    // Autosave: this document's files are <dir>/<session>.tve and .json. A new session id
    // starts with each document, so work left unsaved stays recoverable.
    void scheduleAutosave();
    void removeAutosave();
    QString autosavePath(const QString& id, const char* suffix) const;
    QString m_autosaveDir;
    QString m_sessionId;
    bool m_autosaveEnabled = true;
    qint64 m_unsavedSince = 0; // ms since epoch of the first edit not yet autosaved; 0 = none
    qint64 m_lastAutosave = 0;
    QTimer m_autosaveTimer;

    // Colour
    std::optional<tve::ColorGrade> gradeWith(tve::Id clipId, const QVariantMap& changes) const;
    QString colorPresetPath(const QString& name) const;
    std::optional<tve::ColorGrade> m_copiedGrade;
    int m_activeMask = -1;
    bool m_pickingKeyColor = false;

    // Voice-over
    QString takePath() const;
    void updateInput();
    std::unique_ptr<tve::AudioRecorder> m_recorder;
    QStringList m_inputDevices;
    QString m_inputDevice; // empty: the system default
    std::optional<tve::Ticks> m_recordingStart;
    double m_recordingTime = 0;
    double m_inputLevel = 0;
    bool m_muteWhileRecording = true;
    bool m_requestingAccess = false;

    struct ExportJob {
        tve::Id id = tve::kNoId;
        QString path;
        QString format; // its label
        Exporter::Options options;
        std::shared_ptr<const tve::Project> snapshot; // the project when it was queued
        QString state = QStringLiteral("waiting");
        double progress = 0;
        QString error;
    };
    ExportJob* findExportJob(tve::Id id);
    // Analysis: features by media fingerprint, measured on a worker thread; `then` runs on
    // this thread once every media has them.
    void withFeatures(const std::vector<tve::Id>& mediaIds, bool video, const QString& task, std::function<void()> then);
    std::string fingerprintOf(const tve::MediaItem& media) const;
    // The clip, when its media can be analysed at normal speed; otherwise nullptr and an error.
    const tve::Clip* analysableClip(tve::Id clipId, bool needsSound, bool needsPictures);
    // Runs `work` on the analysis thread (it returns an error, "cancelled", or empty when
    // done), then `then` here.
    void runInBackground(const QString& task, std::function<std::string(const tve::AnalysisProgress&)> work,
                         std::function<void()> then);
    std::vector<tve::CaptionBlock> captionBlocks() const;
    bool transcribeClips(const QVariantMap& options, bool makeCaptions);
    // A transcript word on the timeline.
    struct PlacedWord {
        tve::Ticks start = 0;
        tve::Ticks end = 0;
        std::string text;
        tve::Id clipId = tve::kNoId;
        tve::Id trackId = tve::kNoId;
    };
    // The words of `project`'s clips (those in `only`, if given) with a transcript in `variant`.
    std::vector<PlacedWord> placedWords(const tve::Project& project, const std::string& variant,
                                        const std::set<tve::Id>* only = nullptr) const;
    // How the captions look now (from the first caption), to keep when they are made again.
    std::pair<std::optional<tve::ClipText>, std::optional<tve::ClipVisual>> captionLook() const;
    // Cuts ranges out of tracks, remaking the captions to match; one undo step.
    bool cutRanges(const std::map<tve::Id, std::vector<std::pair<tve::Ticks, tve::Ticks>>>& byTrack, const char* label);
    std::string m_transcriptVariant; // the model and language of the transcript shown
    SpeechModels m_speechModels;
    std::map<std::string, tve::Transcript> m_transcripts; // by fingerprint + model + language
    std::map<std::string, tve::AudioFeatures> m_audioFeatures;
    std::map<std::string, tve::VideoFeatures> m_videoFeatures;
    std::thread m_analysisThread;
    std::atomic<bool> m_cancelAnalysis{false};
    double m_analysisProgress = 0;
    QString m_analysisTask;
    QVariantMap m_silences;
    QVariantMap m_scenes;
    QVariantMap m_beats;

    std::vector<ExportJob> m_exportJobs;
    tve::Id m_nextExportJob = 1;
    tve::Id m_runningExport = tve::kNoId;
    std::thread m_exportThread;
    std::atomic<bool> m_cancelExport{false};
    double m_exportProgress = 0;
    // Pausing holds the running job between frames.
    mutable std::mutex m_pauseMutex;
    std::condition_variable m_pauseChanged;
    bool m_exportPausedFlag = false;
};
