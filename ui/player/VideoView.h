#pragma once

#include "gpu/EffectChain.h"

#include <QQuickRhiItem>
#include <QTimer>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <memory>
#include <vector>

namespace tve {
class PlaybackSession;
}

// Plays a video file through the TVE engine: FFmpeg/VideoToolbox decode on a worker
// thread, zero-copy CVPixelBuffer → Metal textures, YUV→RGB in a shader, then the GPU
// effect stack, then aspect-fit into the item.
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

    // Read by the renderer on the render thread during synchronize().
    std::shared_ptr<tve::PlaybackSession> session() const { return m_session; }
    const std::vector<tve::EffectInstance>& effects() const { return m_effects; }
    quint64 effectsRevision() const { return m_effectsRevision; }

signals:
    void sourceChanged();
    void statsChanged();
    void effectsChanged();

protected:
    QQuickRhiItemRenderer* createRenderer() override;

private:
    void sampleStats();
    tve::EffectInstance* findEffect(const QString& effectId);
    void markEffectsChanged();

    QString m_source;
    QString m_error;
    QString m_description;
    std::shared_ptr<tve::PlaybackSession> m_session;

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
