#pragma once

#include <QQuickRhiItem>
#include <QTimer>
#include <QtQml/qqmlregistration.h>

#include <memory>

namespace tve {
class PlaybackSession;
}

// Plays a video file through the TVE engine: FFmpeg/VideoToolbox decode on a worker
// thread, zero-copy CVPixelBuffer → Metal textures, YUV→RGB in a shader.
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

    // Read by the renderer on the render thread during synchronize().
    std::shared_ptr<tve::PlaybackSession> session() const { return m_session; }

signals:
    void sourceChanged();
    void statsChanged();

protected:
    QQuickRhiItemRenderer* createRenderer() override;

private:
    void sampleStats();

    QString m_source;
    QString m_error;
    QString m_description;
    std::shared_ptr<tve::PlaybackSession> m_session;

    QTimer m_statsTimer;
    qint64 m_lastDecoded = 0;
    qint64 m_lastPresented = 0;
    qint64 m_lastSampleMs = 0;
    double m_decodeFps = 0;
    double m_presentFps = 0;
    qint64 m_skipped = 0;
    bool m_hardware = false;
};
