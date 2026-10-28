#pragma once

#include "analysis/Scopes.h"

#include "gpu/EffectChain.h"
#include "model/Project.h"

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
}

// Live, uncommitted picture changes (e.g. while an inspector slider is dragged), shared
// with the render thread. An override disappears by itself once rendered frames carry the
// same values, i.e. once the edit has been committed and played back.
class PreviewOverrides {
public:
    void setVisual(tve::Id clipId, const tve::ClipVisual& visual);
    void setEffects(tve::Id clipId, std::vector<tve::ClipEffect> effects);
    void setGrade(tve::Id clipId, const tve::ColorGrade& grade);
    void clear(tve::Id clipId);
    quint64 revision() const { return m_revision.load(); }

    // Replaces `visual`/`effects`/`grade` with any overrides for `clipId` (render thread).
    void apply(tve::Id clipId, tve::ClipVisual& visual, std::vector<tve::ClipEffect>& effects, tve::ColorGrade& grade);

private:
    std::mutex m_mutex;
    std::map<tve::Id, tve::ClipVisual> m_visual;
    std::map<tve::Id, std::vector<tve::ClipEffect>> m_effects;
    std::map<tve::Id, tve::ColorGrade> m_grades;
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
// compositor onto the project canvas, output effects, then aspect-fit into the item.
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
    Q_PROPERTY(QVariantList availableEffects READ availableEffects CONSTANT)
    Q_PROPERTY(int enabledEffectCount READ enabledEffectCount NOTIFY effectsChanged)
    // While true, a small copy of each new preview frame (at most ~15 a second) goes to
    // scopeFrame() for the scopes; off, it costs nothing.
    Q_PROPERTY(bool scopesEnabled READ scopesEnabled WRITE setScopesEnabled NOTIFY scopesEnabledChanged)

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

    // One entry per built-in effect: {id, title, params: [{id, title, type, min, max,
    // default, values}]}.
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
    void clearPreview(tve::Id clipId);

    bool scopesEnabled() const { return m_scopesEnabled; }
    void setScopesEnabled(bool enabled);
    std::shared_ptr<ScopeFrame> scopeFrame() const { return m_scopeFrame; }

    // Read by the renderer on the render thread during synchronize().
    std::shared_ptr<tve::FrameSource> frameSource() const { return m_frameSource; }
    std::shared_ptr<PreviewOverrides> previewOverrides() const { return m_previewOverrides; }
    const std::vector<tve::EffectInstance>& effects() const { return m_effects; }
    quint64 effectsRevision() const { return m_effectsRevision; }

signals:
    void sourceChanged();
    void statsChanged();
    void effectsChanged();
    void scopesEnabledChanged();

protected:
    QQuickRhiItemRenderer* createRenderer() override;

private:
    void sampleStats();
    tve::EffectInstance* findEffect(const QString& effectId);
    void markEffectsChanged();

    QString m_source;
    QString m_error;
    QString m_description;
    std::shared_ptr<tve::FrameSource> m_frameSource;
    std::shared_ptr<PreviewOverrides> m_previewOverrides = std::make_shared<PreviewOverrides>();
    std::shared_ptr<ScopeFrame> m_scopeFrame = std::make_shared<ScopeFrame>();
    bool m_scopesEnabled = false;
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
