#include "DrawingRenderer.h"

#include "model/Paths.h"

#include <QPainter>
#include <QPainterPath>

#include <cmath>
#include <numbers>

using namespace tve;

namespace {

QColor qcolor(const ColorRgba& c, double alpha = 1.0)
{
    return QColor::fromRgbF(float(c.r), float(c.g), float(c.b), float(std::clamp(c.a * alpha, 0.0, 1.0)));
}

} // namespace

QImage renderDrawing(const ClipDrawing& drawing, QSize canvas, double seconds, double duration)
{
    QImage image(canvas.expandedTo(QSize(1, 1)), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    const double w = image.width(), h = image.height();
    const double aspect = w / h;
    const double unit = std::min(w, h);
    const std::vector<double> amounts = seconds < 0 ? std::vector<double>(drawing.paths.size(), 1.0)
                                                    : drawnAmounts(drawing, seconds, duration, aspect);
    for (std::size_t i = 0; i < drawing.paths.size(); ++i) {
        const DrawnPath& path = drawing.paths[i];
        const double amount = amounts[i];
        if (amount <= 0.0)
            continue;
        const Polyline line = lineUpTo(pathLine(path), amount, aspect);
        if (line.size() < 2)
            continue;
        QPainterPath shape(QPointF(line[0][0] * w, line[0][1] * h));
        for (std::size_t p = 1; p < line.size(); ++p)
            shape.lineTo(line[p][0] * w, line[p][1] * h);
        // The fill comes in as its outline closes.
        if ((path.kind == PathKind::Rectangle || path.kind == PathKind::Ellipse) && path.fill.a > 0) {
            QPainterPath full;
            const Polyline whole = pathLine(path);
            full.moveTo(whole[0][0] * w, whole[0][1] * h);
            for (std::size_t p = 1; p < whole.size(); ++p)
                full.lineTo(whole[p][0] * w, whole[p][1] * h);
            full.closeSubpath();
            painter.fillPath(full, qcolor(path.fill, amount * amount));
        }
        const double width = std::max(0.5, path.width * unit);
        if (path.stroke.a > 0) {
            QPen pen(qcolor(path.stroke), width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
            painter.strokePath(shape, pen);
        }
        // An arrow's head where the drawn part ends.
        if (path.kind == PathKind::Arrow && path.stroke.a > 0) {
            const auto& a = line[line.size() - 2];
            const auto& b = line.back();
            const double angle = std::atan2((b[1] - a[1]) * h, (b[0] - a[0]) * w);
            const double size = width * 3.5;
            const QPointF tip(b[0] * w, b[1] * h);
            QPainterPath head(tip);
            head.lineTo(tip - QPointF(std::cos(angle - 0.45) * size, std::sin(angle - 0.45) * size));
            head.lineTo(tip - QPointF(std::cos(angle + 0.45) * size, std::sin(angle + 0.45) * size));
            head.closeSubpath();
            painter.fillPath(head, qcolor(path.stroke));
        }
    }
    painter.end();
    return image.convertToFormat(QImage::Format_RGBA8888);
}
