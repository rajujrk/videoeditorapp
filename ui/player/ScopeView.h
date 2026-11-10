#pragma once

#include "analysis/Scopes.h"

#include <QImage>
#include <QPointer>
#include <QQuickPaintedItem>
#include <QtQml/qqmlregistration.h>

class VideoView;

// Draws a scope of the preview: "waveform" (luma), "parade" (red, green and blue waveforms
// side by side), "vectorscope" or "histogram", with its scale. While visible it asks `view`
// for frames (VideoView::scopesEnabled) and redraws as they come. The picture is in the
// project's working colour (`colour`): an HDR project's waveforms are of its PQ or HLG
// signal, scaled in nits.
class ScopeView : public QQuickPaintedItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(VideoView* view READ view WRITE setView NOTIFY viewChanged)
    Q_PROPERTY(QString mode READ mode WRITE setMode NOTIFY modeChanged)
    // "sdr", "hdr-pq" or "hdr-hlg" (ProjectController::projectColour).
    Q_PROPERTY(QString colour READ colour WRITE setColour NOTIFY colourChanged)
    // The waveform's scale: [{label, level (0..1 of the signal)}], percent for SDR, nits for HDR.
    Q_PROPERTY(QVariantList scale READ scale NOTIFY colourChanged)
    // Frames analysed so far (tests wait on it).
    Q_PROPERTY(int frames READ frames NOTIFY framesChanged)

public:
    explicit ScopeView(QQuickItem* parent = nullptr);
    ~ScopeView() override;

    VideoView* view() const { return m_view; }
    void setView(VideoView* view);
    QString mode() const { return m_mode; }
    void setMode(const QString& mode);
    int frames() const { return m_frames; }
    QString colour() const { return m_colour; }
    void setColour(const QString& colour);
    QVariantList scale() const;

    void paint(QPainter* painter) override;

    // The scope as drawn, without its scale (tests read it).
    const QImage& trace() const { return m_trace; }
    Q_INVOKABLE QSize traceSize() const { return m_trace.size(); }
    Q_INVOKABLE QColor tracePixel(int x, int y) const { return m_trace.valid(x, y) ? m_trace.pixelColor(x, y) : QColor(); }

signals:
    void viewChanged();
    void modeChanged();
    void colourChanged();
    void framesChanged();

protected:
    void itemChange(ItemChange change, const ItemChangeData& value) override;

private:
    void updateWanted();
    void analyse();

    QPointer<VideoView> m_view;
    QString m_mode = QStringLiteral("waveform");
    QString m_colour = QStringLiteral("sdr");
    tve::ScopeImage m_image;
    QImage m_trace;
    int m_frames = 0;
    bool m_wanting = false;
    QMetaObject::Connection m_frameConnection;
};
