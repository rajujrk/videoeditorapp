#pragma once

#include "analysis/Scopes.h"

#include "gpu/EffectChain.h"
#include "model/Project.h"

#include <QPointer>
#include <QQuickRhiItem>
#include <QTimer>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

namespace tve {
class FrameSource;
class FrameMirrors;
}

// Live, uncommitted picture changes (e.g. while an inspector slider is dragged), shared
// with the render thread. An override disappears by itself once rendered frames carry the
// same values, i.e. once the edit has been committed and played back.
class PreviewOverrides {
public:
    void setVisual(tve::Id clipId, const tve::ClipVisual& visual);
    void setEffects(tve::Id clipId, std::vector<tve::ClipEffect> effects);
    void setGrade(tve::Id clipId, const tve::ColorGrade& grade);
    void setLook(tve::Id clipId, const tve::ColorGrade& look);
    void setKeying(tve::Id clipId, const tve::ChromaKey& key, const std::vector<tve::ClipMask>& masks);
    void clear(tve::Id clipId);
    quint64 revision() const { return m_revision.load(); }

    // Replaces `visual`/`effects`/`grade`/`look` with any overrides for `clipId` (render thread).
    void apply(tve::Id clipId, tve::ClipVisual& visual, std::vector<tve::ClipEffect>& effects, tve::ColorGrade& grade,
               tve::ColorGrade& look);
    void applyKeying(tve::Id clipId, tve::ChromaKey& key, std::vector<tve::ClipMask>& masks);

private:
    std::mutex m_mutex;
    std::map<tve::Id, tve::ClipVisual> m_visual;
    std::map<tve::Id, std::vector<tve::ClipEffect>> m_effects;
    std::map<tve::Id, tve::ColorGrade> m_grades;
    std::map<tve::Id, tve::ColorGrade> m_looks;
    std::map<tve::Id, std::pair<tve::ChromaKey, std::vector<tve::ClipMask>>> m_keying;
    std::atomic<quint64> m_revision{1};
};

// The latest preview frame, small (256 pixels across), for the scopes: written by the
// renderer, read on the GUI thread when `changed` arrives.
class ScopeFrame : public QObject {
    Q_OBJECT
public:
    void set(tve::ScopeImage image); // any thread
    tve::ScopeImage image() const;
    quint64 serial() const;

signals:
    void changed();

private:
    mutable std::mutex m_mutex;
    tve::ScopeImage m_image;
    quint64 m_serial = 0;
};

// Shows composite frames from a FrameSource (the timeline, or a single file via `source`):
// zero-copy CVPixelBuffer → Metal textures, YUV→RGB per layer, per-clip effects, the
// compositor onto the project canvas, output effects, the display transform (the project's
// working colour to the display's), then aspect-fit into the item.
class VideoView : public QQuickRhiItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(QString errorString READ errorString NOTIFY sourceChanged)
    Q_PROPERTY(QString streamDescription READ streamDescription NOTIFY sourceChanged)
    Q_PROPERTY(double decodeFps READ decodeFps NOTIFY statsChanged)
    Q_PROPERTY(double presentFps READ presentFps NOTIFY statsChanged)
    Q_PROPERTY(qint64 skippedFrames READ skippedFrames NOTIFY statsChanged)
    Q_PROPERTY(bool hardwareAccelerated READ hardwareAccelerated NOTIFY statsChanged)
    Q_PROPERTY(QVariantList availableEffects READ availableEffects NOTIFY languageChanged)
    Q_PROPERTY(int enabledEffectCount READ enabledEffectCount NOTIFY effectsChanged)
    // While true, a small copy of each new preview frame (at most ~15 a second) goes to
    // scopeFrame() for the scopes; off, it costs nothing.
    Q_PROPERTY(bool scopesEnabled READ scopesEnabled WRITE setScopesEnabled NOTIFY scopesEnabledChanged)
    // A second view of another's frames (the HDR viewer): what `follows` shows, by its clock,
    // with its live previews and output effects.
    Q_PROPERTY(VideoView* follows READ follows WRITE setFollows NOTIFY followsChanged)
    // Shows HDR as HDR: extended linear light for an EDR window (one whose
    // `_qt_sg_hdr_format` is "scrgb", set here when the view is put in its window, before
    // it is shown; give the view colorBufferFormat RGBA16F). Highlights are rolled off to
    // what the display can show now; on an SDR display it shows as the player does.
    Q_PROPERTY(bool hdrOutput READ hdrOutput WRITE setHdrOutput NOTIFY hdrOutputChanged)
    // Whether the window really is EDR, and how much brighter than SDR white it can go now.
    Q_PROPERTY(bool hdrActive READ hdrActive NOTIFY displayChanged)
    Q_PROPERTY(double displayHeadroom READ displayHeadroom NOTIFY displayChanged)
    // A 360° project's picture as a view to look around in (where `lookYaw`, `lookPitch`
    // point, `lookFov` wide, degrees) instead of the whole equirectangular sphere. Only the
    // preview looks; the project is unchanged.
    Q_PROPERTY(bool lookAround READ lookAround WRITE setLookAround NOTIFY lookChanged)
    Q_PROPERTY(double lookYaw READ lookYaw WRITE setLookYaw NOTIFY lookChanged)
    Q_PROPERTY(double lookPitch READ lookPitch WRITE setLookPitch NOTIFY lookChanged)
    Q_PROPERTY(double lookFov READ lookFov WRITE setLookFov NOTIFY lookChanged)

public:
    explicit VideoView(QQuickItem* parent = nullptr);
    ~VideoView() override;

    QString source() const { return m_source; }
    void setSource(const QString& source);
    QString errorString() const { return m_error; }
    QString streamDescription() const { return m_description; }

    double decodeFps() const { return m_decodeFps; }
    double presentFps() const { return m_presentFps; }
    qint64 skippedFrames() const { return m_skipped; }
    bool hardwareAccelerated() const { return m_hardware; }

    // One entry per built-in effect, overlays last: {id, title, categories, params: [{id,
    // title, type, min, max, default, values, keyframable}]}.
    QVariantList availableEffects() const;
    int enabledEffectCount() const;

    Q_INVOKABLE bool isEffectEnabled(const QString& effectId) const;
    Q_INVOKABLE void setEffectEnabled(const QString& effectId, bool enabled);
    // `value` is a number (float/int/bool/enum index) or a list of numbers (color).
    Q_INVOKABLE void setEffectParam(const QString& effectId, const QString& paramId, const QVariant& value);

    // Shows frames from `source` (e.g. timeline playback) instead of a file. `source`
    // property and this are alternatives; setting one replaces the other.
    void setFrameSource(std::shared_ptr<tve::FrameSource> source, const QString& description);

    // Live preview of uncommitted clip changes; see PreviewOverrides.
    void setPreviewVisual(tve::Id clipId, const tve::ClipVisual& visual);
    void setPreviewEffects(tve::Id clipId, std::vector<tve::ClipEffect> effects);
    void setPreviewGrade(tve::Id clipId, const tve::ColorGrade& grade);
    void setPreviewLook(tve::Id clipId, const tve::ColorGrade& look);
    void setPreviewKeying(tve::Id clipId, const tve::ChromaKey& key, const std::vector<tve::ClipMask>& masks);
    void clearPreview(tve::Id clipId);

    bool scopesEnabled() const { return m_scopesEnabled; }
    void setScopesEnabled(bool enabled);
    std::shared_ptr<ScopeFrame> scopeFrame() const { return m_scopeFrame; }

    VideoView* follows() const { return m_follows; }
    void setFollows(VideoView* leader);
    bool hdrOutput() const { return m_hdrOutput; }
    void setHdrOutput(bool on);
    bool hdrActive() const { return m_hdrActive; }
    double displayHeadroom() const { return m_displayHeadroom; }
    void setDisplay(bool hdrActive, double headroom); // from the renderer
    bool lookAround() const { return m_lookAround; }
    void setLookAround(bool on);
    double lookYaw() const { return m_look.yaw; }
    void setLookYaw(double yaw);
    double lookPitch() const { return m_look.pitch; }
    void setLookPitch(double pitch);
    double lookFov() const { return m_look.fov; }
    void setLookFov(double fov);
    const tve::ClipSphereView& look() const { return m_look; }
    // The views following this one (render thread).
    std::shared_ptr<tve::FrameMirrors> mirrors() const { return m_mirrors; }

    // Read by the renderer on the render thread during synchronize().
    std::shared_ptr<tve::FrameSource> frameSource() const { return m_frameSource; }
    std::shared_ptr<PreviewOverrides> previewOverrides() const { return m_previewOverrides; }
    const std::vector<tve::EffectInstance>& effects() const { return m_effects; }
    quint64 effectsRevision() const { return m_effectsRevision; }

signals:
    void languageChanged(); // the effects' names are read again
    void sourceChanged();
    void statsChanged();
    void effectsChanged();
    void scopesEnabledChanged();
    void followsChanged();
    void hdrOutputChanged();
    void displayChanged();
    void lookChanged();

protected:
    QQuickRhiItemRenderer* createRenderer() override;
    void itemChange(ItemChange change, const ItemChangeData& value) override;

private:
    void sampleStats();
    tve::EffectInstance* findEffect(const QString& effectId);
    void markEffectsChanged();
    void mirrorLeader();
    void requestHdrWindow();

    QString m_source;
    QString m_error;
    QString m_description;
    std::shared_ptr<tve::FrameSource> m_frameSource;
    std::shared_ptr<PreviewOverrides> m_previewOverrides = std::make_shared<PreviewOverrides>();
    std::shared_ptr<ScopeFrame> m_scopeFrame = std::make_shared<ScopeFrame>();
    bool m_scopesEnabled = false;
    std::shared_ptr<tve::FrameMirrors> m_mirrors;
    QPointer<VideoView> m_follows;
    QList<QMetaObject::Connection> m_followConnections;
    bool m_hdrOutput = false;
    bool m_hdrActive = false;
    double m_displayHeadroom = 1.0;
    bool m_lookAround = false;
    tve::ClipSphereView m_look; // (its roll unused)
    std::function<void()> m_stopFileSession; // set while playing a file via `source`

    std::vector<tve::EffectInstance> m_effects;
    quint64 m_effectsRevision = 1;

    QTimer m_statsTimer;
    qint64 m_lastDecoded = 0;
    qint64 m_lastPresented = 0;
    qint64 m_lastSampleMs = 0;
    double m_decodeFps = 0;
    double m_presentFps = 0;
    qint64 m_skipped = 0;
    bool m_hardware = false;
};
