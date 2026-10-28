#include "ScopeView.h"

#include "VideoView.h"

#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace {

// A count as brightness (0..1): logarithmic, so a few pixels still show.
float brightness(std::uint32_t count, std::uint32_t peak)
{
    if (count == 0 || peak == 0)
        return 0.0f;
    return std::min(1.0f, 0.25f + 0.75f * float(std::log1p(double(count)) / std::log1p(double(peak))));
}

// Paints `grid` into `image` at x offset `left`, tinted `tint`, adding to what is there.
void plot(QImage& image, const tve::ScopeGrid& grid, int left, QColor tint)
{
    for (int row = 0; row < grid.rows; ++row) {
        auto* line = reinterpret_cast<QRgb*>(image.scanLine(grid.rows - 1 - row));
        for (int column = 0; column < grid.columns; ++column) {
            const float b = brightness(grid.at(column, row), grid.peak);
            if (b <= 0.0f)
                continue;
            QRgb& pixel = line[left + column];
            pixel = qRgb(std::min(255, qRed(pixel) + int(float(tint.red()) * b)),
                         std::min(255, qGreen(pixel) + int(float(tint.green()) * b)),
                         std::min(255, qBlue(pixel) + int(float(tint.blue()) * b)));
        }
    }
}

const QColor kRed(255, 90, 90);
const QColor kGreen(90, 230, 120);
const QColor kBlue(100, 150, 255);
const QColor kTrace(220, 235, 225);
const QColor kScale(255, 255, 255, 60);
const QColor kLabel(255, 255, 255, 140);

} // namespace

ScopeView::ScopeView(QQuickItem* parent) : QQuickPaintedItem(parent)
{
    setAntialiasing(true);
}

ScopeView::~ScopeView()
{
    if (m_view && m_wanting)
        m_view->setScopesEnabled(false);
}

void ScopeView::setView(VideoView* view)
{
    if (view == m_view)
        return;
    if (m_view && m_wanting)
        m_view->setScopesEnabled(false);
    m_wanting = false;
    disconnect(m_frameConnection);
    m_view = view;
    if (m_view) {
        m_frameConnection = connect(m_view->scopeFrame().get(), &ScopeFrame::changed, this, [this] {
            if (!isVisible())
                return;
            m_image = m_view->scopeFrame()->image();
            analyse();
        });
    }
    updateWanted();
    emit viewChanged();
}

void ScopeView::setMode(const QString& mode)
{
    if (mode == m_mode)
        return;
    m_mode = mode;
    analyse();
    emit modeChanged();
}

void ScopeView::itemChange(ItemChange change, const ItemChangeData& value)
{
    QQuickPaintedItem::itemChange(change, value);
    if (change == ItemVisibleHasChanged || change == ItemSceneChange)
        updateWanted();
}

void ScopeView::updateWanted()
{
    const bool want = m_view && isVisible() && window();
    if (want == m_wanting)
        return;
    m_wanting = want;
    if (m_view)
        m_view->setScopesEnabled(want);
    if (want && m_view && m_view->scopeFrame()->serial() > 0) {
        m_image = m_view->scopeFrame()->image(); // what was last captured, until a new one comes
        analyse();
    }
}

void ScopeView::analyse()
{
    if (m_image.width <= 0) {
        m_trace = {};
        update();
        return;
    }
    if (m_mode == QLatin1String("vectorscope")) {
        const tve::ScopeGrid grid = tve::vectorscope(m_image, 256);
        m_trace = QImage(256, 256, QImage::Format_RGB32);
        m_trace.fill(Qt::black);
        // Each spot as bright as the number of pixels that land there.
        for (int row = 0; row < grid.rows; ++row) {
            auto* line = reinterpret_cast<QRgb*>(m_trace.scanLine(grid.rows - 1 - row));
            for (int column = 0; column < grid.columns; ++column) {
                const float b = brightness(grid.at(column, row), grid.peak);
                if (b > 0.0f)
                    line[column] = qRgb(int(kTrace.red() * b), int(kTrace.green() * b), int(kTrace.blue() * b));
            }
        }
    } else if (m_mode == QLatin1String("histogram")) {
        const tve::Histogram h = tve::histogram(m_image);
        m_trace = QImage(256, 128, QImage::Format_RGB32);
        m_trace.fill(Qt::black);
        const QColor colors[3] = {kRed, kGreen, kBlue};
        for (int x = 0; x < 256; ++x) {
            for (int c = 0; c < 3; ++c) {
                // Square-root height, so small counts stay visible next to a tall peak.
                const double v = h.peak ? std::sqrt(double(h.bins[std::size_t(c)][std::size_t(x)]) / double(h.peak)) : 0.0;
                const int top = 127 - int(v * 127.0);
                for (int y = 127; y >= top; --y) {
                    QRgb& pixel = reinterpret_cast<QRgb*>(m_trace.scanLine(y))[x];
                    pixel = qRgb(std::min(255, qRed(pixel) + colors[c].red() / 2),
                                 std::min(255, qGreen(pixel) + colors[c].green() / 2),
                                 std::min(255, qBlue(pixel) + colors[c].blue() / 2));
                }
            }
        }
    } else if (m_mode == QLatin1String("parade")) {
        m_trace = QImage(3 * 86, 256, QImage::Format_RGB32);
        m_trace.fill(Qt::black);
        plot(m_trace, tve::waveform(m_image, tve::ScopeChannel::Red, 86), 0, kRed);
        plot(m_trace, tve::waveform(m_image, tve::ScopeChannel::Green, 86), 86, kGreen);
        plot(m_trace, tve::waveform(m_image, tve::ScopeChannel::Blue, 86), 172, kBlue);
    } else {
        m_trace = QImage(256, 256, QImage::Format_RGB32);
        m_trace.fill(Qt::black);
        plot(m_trace, tve::waveform(m_image, tve::ScopeChannel::Luma, 256), 0, kTrace);
    }
    ++m_frames;
    emit framesChanged();
    update();
}

void ScopeView::paint(QPainter* painter)
{
    const QRectF area = boundingRect();
    painter->fillRect(area, QColor(11, 13, 16));
    QFont font = painter->font();
    font.setPixelSize(9);
    painter->setFont(font);

    if (m_mode == QLatin1String("vectorscope")) {
        const double size = std::min(area.width(), area.height()) - 8;
        const QRectF box((area.width() - size) / 2, (area.height() - size) / 2, size, size);
        if (!m_trace.isNull())
            painter->drawImage(box, m_trace);
        painter->setPen(QPen(kScale, 1));
        painter->drawEllipse(box);
        painter->drawEllipse(box.center(), size * 0.375, size * 0.375); // 75 %
        painter->drawLine(QPointF(box.left(), box.center().y()), QPointF(box.right(), box.center().y()));
        painter->drawLine(QPointF(box.center().x(), box.top()), QPointF(box.center().x(), box.bottom()));
        // Targets for 75 % primaries and secondaries.
        const struct { const char* name; float r, g, b; } targets[] = {
            {"R", 0.75f, 0, 0}, {"Mg", 0.75f, 0, 0.75f}, {"B", 0, 0, 0.75f},
            {"Cy", 0, 0.75f, 0.75f}, {"G", 0, 0.75f, 0}, {"Yl", 0.75f, 0.75f, 0}};
        for (const auto& t : targets) {
            const auto p = tve::vectorscopePosition(t.r, t.g, t.b, 256);
            const QPointF at(box.left() + p[0] / 256.0 * size, box.bottom() - p[1] / 256.0 * size);
            painter->setPen(QPen(kLabel, 1));
            painter->drawRect(QRectF(at.x() - 4, at.y() - 4, 8, 8));
            painter->drawText(QPointF(at.x() + 6, at.y() + 3), QString::fromLatin1(t.name));
        }
        // Skin tones fall along this line (about 123° from the blue axis).
        const double angle = 123.0 * std::numbers::pi / 180.0;
        painter->setPen(QPen(QColor(255, 200, 150, 90), 1, Qt::DashLine));
        painter->drawLine(box.center(), box.center() + QPointF(std::cos(angle), -std::sin(angle)) * size / 2);
        return;
    }

    const QRectF plotArea = area.adjusted(22, 6, -4, -6);
    if (!m_trace.isNull())
        painter->drawImage(plotArea, m_trace);
    painter->setPen(QPen(kScale, 1));
    if (m_mode == QLatin1String("histogram")) {
        for (int i = 0; i <= 4; ++i) {
            const double x = plotArea.left() + plotArea.width() * i / 4.0;
            painter->drawLine(QPointF(x, plotArea.top()), QPointF(x, plotArea.bottom()));
        }
        return;
    }
    for (int percent = 0; percent <= 100; percent += 25) {
        const double y = plotArea.bottom() - plotArea.height() * percent / 100.0;
        painter->setPen(QPen(kScale, 1));
        painter->drawLine(QPointF(plotArea.left(), y), QPointF(plotArea.right(), y));
        painter->setPen(kLabel);
        painter->drawText(QRectF(0, y - 6, 20, 12), Qt::AlignRight | Qt::AlignVCenter, QString::number(percent));
    }
    if (m_mode == QLatin1String("parade")) {
        for (int i = 1; i < 3; ++i) {
            const double x = plotArea.left() + plotArea.width() * i / 3.0;
            painter->setPen(QPen(kScale, 1));
            painter->drawLine(QPointF(x, plotArea.top()), QPointF(x, plotArea.bottom()));
        }
    }
}
