#include "WaveformItem.h"

#include "audio/WaveformCache.h"

#include <QPainter>
#include <QPainterPath>

#include <cmath>

namespace {

// Bridges the cache's worker-thread callback to the GUI thread.
class WaveformHub : public QObject {
    Q_OBJECT
public:
    static WaveformHub& instance()
    {
        static WaveformHub hub;
        return hub;
    }
    tve::WaveformCache cache{[](const std::string& path) {
        QMetaObject::invokeMethod(&instance(), [path] { emit instance().peaksReady(QString::fromStdString(path)); },
                                  Qt::QueuedConnection);
    }};

signals:
    void peaksReady(const QString& path);
};

tve::Ticks ticks(double seconds)
{
    return tve::fromSeconds(seconds);
}

} // namespace

WaveformItem::WaveformItem(QQuickItem* parent) : QQuickPaintedItem(parent)
{
    setAntialiasing(true);
    connect(&WaveformHub::instance(), &WaveformHub::peaksReady, this, &WaveformItem::onPeaksReady);
    connect(this, &WaveformItem::changed, this, [this] { update(); });
}

void WaveformItem::setSource(const QString& source)
{
    if (source == m_source)
        return;
    m_source = source;
    emit changed();
    emit readyChanged();
}

void WaveformItem::setSourceIn(double seconds)
{
    if (seconds != m_sourceIn) {
        m_sourceIn = seconds;
        emit changed();
    }
}

void WaveformItem::setDuration(double seconds)
{
    if (seconds != m_duration) {
        m_duration = seconds;
        emit changed();
    }
}

void WaveformItem::setSpeed(double speed)
{
    if (speed > 0 && speed != m_speed) {
        m_speed = speed;
        emit changed();
    }
}

void WaveformItem::setRamp(const QVariantList& ramp)
{
    if (ramp == m_rampList)
        return;
    m_rampList = ramp;
    m_ramp.clear();
    for (const QVariant& point : ramp) {
        const QVariantMap map = point.toMap();
        m_ramp.push_back({map.value(QStringLiteral("at")).toDouble(), map.value(QStringLiteral("speed")).toDouble()});
    }
    emit changed();
}

void WaveformItem::setReversed(bool reversed)
{
    if (reversed != m_reversed) {
        m_reversed = reversed;
        emit changed();
    }
}

void WaveformItem::setGainDb(double db)
{
    if (db != m_gainDb) {
        m_gainDb = db;
        emit changed();
    }
}

void WaveformItem::setFadeIn(double seconds)
{
    if (seconds != m_fadeIn) {
        m_fadeIn = seconds;
        emit changed();
    }
}

void WaveformItem::setFadeOut(double seconds)
{
    if (seconds != m_fadeOut) {
        m_fadeOut = seconds;
        emit changed();
    }
}

void WaveformItem::setColor(const QColor& color)
{
    if (color != m_color) {
        m_color = color;
        emit changed();
    }
}

bool WaveformItem::ready() const
{
    return !m_source.isEmpty() && WaveformHub::instance().cache.peaks(m_source.toStdString()) != nullptr;
}

void WaveformItem::onPeaksReady(const QString& path)
{
    if (path == m_source) {
        emit readyChanged();
        update();
    }
}

void WaveformItem::paint(QPainter* painter)
{
    if (m_source.isEmpty() || m_duration <= 0 || width() < 1)
        return;
    const auto peaks = WaveformHub::instance().cache.peaks(m_source.toStdString());
    if (!peaks)
        return; // queued; repaint on peaksReady

    const double gain = m_gainDb <= -60.0 ? 0.0 : std::pow(10.0, m_gainDb / 20.0);
    const double secondsPerPixel = m_duration / width();
    const double mid = height() / 2.0;
    QPainterPath path;
    path.moveTo(0, mid);
    const int columns = int(std::ceil(width()));
    std::vector<double> amplitude(static_cast<std::size_t>(columns), 0.0);
    for (int x = 0; x < columns; ++x) {
        const double t = x * secondsPerPixel; // time into the clip
        double fade = 1.0;
        if (m_fadeIn > 0 && t < m_fadeIn)
            fade *= t / m_fadeIn;
        if (m_fadeOut > 0 && m_duration - t < m_fadeOut)
            fade *= (m_duration - t) / m_fadeOut;
        // Source range under this column (through the ramp, if any); a reversed clip reads
        // its source from the top down.
        const double span = m_duration * m_speed;
        const double a = span * tve::rampProgress(m_ramp, t / m_duration);
        const double b = span * tve::rampProgress(m_ramp, (t + secondsPerPixel) / m_duration);
        const double from = m_reversed ? span - b : a;
        const double to = m_reversed ? span - a : b;
        const double peak = peaks->maxIn(ticks(m_sourceIn + from), ticks(m_sourceIn + to));
        amplitude[std::size_t(x)] = std::min(1.0, peak * gain * fade) * mid;
        path.lineTo(x, mid - amplitude[std::size_t(x)]);
    }
    for (int x = columns - 1; x >= 0; --x)
        path.lineTo(x, mid + amplitude[std::size_t(x)]);
    path.closeSubpath();
    painter->fillPath(path, m_color);
}

#include "WaveformItem.moc"
