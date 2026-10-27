#pragma once

#include "model/SpeedRamp.h"

#include <QColor>
#include <QVariantList>
#include <QQuickPaintedItem>
#include <QtQml/qqmlregistration.h>

// Draws a media file's waveform for a span of it (a clip's source range), mirrored around
// the vertical center. Peaks come from a shared background cache; the item repaints when
// they are ready. Volume and fades are drawn into the shape.
class WaveformItem : public QQuickPaintedItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString source READ source WRITE setSource NOTIFY changed)
    Q_PROPERTY(double sourceIn READ sourceIn WRITE setSourceIn NOTIFY changed)
    Q_PROPERTY(double duration READ duration WRITE setDuration NOTIFY changed)
    Q_PROPERTY(double speed READ speed WRITE setSpeed NOTIFY changed)
    Q_PROPERTY(bool reversed READ reversed WRITE setReversed NOTIFY changed)
    // A speed ramp, [{at, speed}] (empty: constant `speed`, which is then the average).
    Q_PROPERTY(QVariantList ramp READ ramp WRITE setRamp NOTIFY changed)
    Q_PROPERTY(double gainDb READ gainDb WRITE setGainDb NOTIFY changed)
    Q_PROPERTY(double fadeIn READ fadeIn WRITE setFadeIn NOTIFY changed)
    Q_PROPERTY(double fadeOut READ fadeOut WRITE setFadeOut NOTIFY changed)
    Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY changed)
    Q_PROPERTY(bool ready READ ready NOTIFY readyChanged)

public:
    explicit WaveformItem(QQuickItem* parent = nullptr);

    QString source() const { return m_source; }
    void setSource(const QString& source);
    double sourceIn() const { return m_sourceIn; }
    void setSourceIn(double seconds);
    double duration() const { return m_duration; }
    void setDuration(double seconds);
    double speed() const { return m_speed; }
    void setSpeed(double speed);
    bool reversed() const { return m_reversed; }
    void setReversed(bool reversed);
    QVariantList ramp() const { return m_rampList; }
    void setRamp(const QVariantList& ramp);
    double gainDb() const { return m_gainDb; }
    void setGainDb(double db);
    double fadeIn() const { return m_fadeIn; }
    void setFadeIn(double seconds);
    double fadeOut() const { return m_fadeOut; }
    void setFadeOut(double seconds);
    QColor color() const { return m_color; }
    void setColor(const QColor& color);
    bool ready() const;

    void paint(QPainter* painter) override;

signals:
    void changed();
    void readyChanged();

private:
    void onPeaksReady(const QString& path);

    QString m_source;
    double m_sourceIn = 0;
    double m_duration = 0;
    double m_speed = 1;
    bool m_reversed = false;
    QVariantList m_rampList;
    tve::SpeedRamp m_ramp;
    double m_gainDb = 0;
    double m_fadeIn = 0;
    double m_fadeOut = 0;
    QColor m_color = Qt::white;
};
