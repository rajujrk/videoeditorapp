#include "TitleRenderer.h"

#include "model/Paths.h"

#include <QFontDatabase>
#include <QHash>
#include <QLinearGradient>
#include <QRadialGradient>
#include <QJsonDocument>
#include <QUrl>
#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>
#include <QTextBoundaryFinder>
#include <QTextLayout>

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <numbers>

namespace {

QColor toColor(const tve::ColorRgba& c)
{
    return QColor::fromRgbF(float(c.r), float(c.g), float(c.b), float(c.a));
}

QFont titleFont(const tve::ClipText& text, double pixelSize)
{
    const QString requested = QString::fromStdString(text.fontFamily);
    const bool installed = !requested.isEmpty() && QFontDatabase::hasFamily(requested);
    QFont font(installed ? requested : defaultTitleFamily());
    font.setPixelSize(std::max(1, int(std::lround(pixelSize))));
    font.setBold(text.bold);
    font.setItalic(text.italic);
    font.setLetterSpacing(QFont::AbsoluteSpacing, text.letterSpacing * pixelSize);
    font.setHintingPreference(QFont::PreferNoHinting); // scales smoothly with the canvas
    return font;
}

// One visible character of a title (a grapheme: a Tamil consonant with its vowel sign, an
// emoji with its modifiers), laid out on the canvas, with the units it belongs to.
struct Glyph {
    QPainterPath path;
    QRectF box; // its advance box: line height tall
    int character = 0;
    int word = 0;
    int line = 0;
};

struct TitleLayout {
    std::vector<Glyph> glyphs;
    std::vector<QRectF> words;
    std::vector<QRectF> lines;
    QRectF bounds; // all the text
};

// The text laid out on the canvas, character by character: wrapped at 90 % of the width
// (not at all for a crawl), aligned, the block centred vertically.
// Text on a path: the one line of characters moved along the path (by the alignment: from
// its start, centred on it, or ending at its end), each turned to the path's direction.
void bendAlong(TitleLayout& layout, const tve::ClipText& text, QSize canvas, double baseline)
{
    const double w = canvas.width(), h = canvas.height();
    const double aspect = w / h;
    const double length = tve::lineLength(text.path, aspect) * h; // pixels
    if (layout.glyphs.empty() || length <= 0)
        return;
    const double first = layout.glyphs.front().box.left();
    const double width = layout.glyphs.back().box.right() - first;
    const double start = text.alignment == tve::TextAlignment::Left    ? 0.0
                       : text.alignment == tve::TextAlignment::Right ? length - width
                                                                     : (length - width) / 2.0;
    layout.bounds = QRectF();
    for (QRectF& word : layout.words)
        word = QRectF();
    for (QRectF& line : layout.lines)
        line = QRectF();
    for (Glyph& glyph : layout.glyphs) {
        const double centre = glyph.box.center().x();
        const tve::PathPoint at = tve::pointAlong(text.path, (start + centre - first) / length, aspect);
        QTransform move;
        move.translate(at.x * w, at.y * h);
        move.rotate(at.angle);
        move.translate(-centre, -baseline);
        glyph.path = move.map(glyph.path);
        glyph.box = move.mapRect(glyph.box);
        layout.bounds = layout.bounds.united(glyph.box);
        if (glyph.word >= 0 && std::size_t(glyph.word) < layout.words.size())
            layout.words[std::size_t(glyph.word)] = layout.words[std::size_t(glyph.word)].united(glyph.box);
        if (std::size_t(glyph.line) < layout.lines.size())
            layout.lines[std::size_t(glyph.line)] = layout.lines[std::size_t(glyph.line)].united(glyph.box);
    }
}

// The glyphs moved by (dx, dy), the word, line and overall boxes made again.
void restack(TitleLayout& layout)
{
    layout.bounds = QRectF();
    for (QRectF& word : layout.words)
        word = QRectF();
    for (QRectF& line : layout.lines)
        line = QRectF();
    for (const Glyph& glyph : layout.glyphs) {
        layout.bounds = layout.bounds.united(glyph.box);
        if (glyph.word >= 0 && std::size_t(glyph.word) < layout.words.size())
            layout.words[std::size_t(glyph.word)] = layout.words[std::size_t(glyph.word)].united(glyph.box);
        if (std::size_t(glyph.line) < layout.lines.size())
            layout.lines[std::size_t(glyph.line)] = layout.lines[std::size_t(glyph.line)].united(glyph.box);
    }
}

// Vertical text: each line a column (left to right), its characters stacked down it, the
// columns placed by the alignment (top, middle or bottom).
void stackVertically(TitleLayout& layout, const tve::ClipText& text, QSize canvas, const QFontMetricsF& metrics)
{
    if (layout.lines.empty())
        return;
    const double column = metrics.height() * text.lineSpacing;
    const double step = metrics.height() * 0.92;
    const double left = (canvas.width() - column * double(layout.lines.size())) / 2.0;
    std::vector<int> count(layout.lines.size(), 0);
    for (const Glyph& glyph : layout.glyphs)
        ++count[std::size_t(glyph.line)];
    std::vector<int> index(layout.lines.size(), 0);
    for (Glyph& glyph : layout.glyphs) {
        const std::size_t l = std::size_t(glyph.line);
        const double height = step * count[l];
        const double top = text.alignment == tve::TextAlignment::Left    ? canvas.height() * 0.05
                         : text.alignment == tve::TextAlignment::Right ? canvas.height() * 0.95 - height
                                                                       : (canvas.height() - height) / 2.0;
        const QPointF centre(left + column * (double(l) + 0.5), top + step * (index[l]++ + 0.5));
        const QPointF by = centre - glyph.box.center();
        glyph.path.translate(by);
        glyph.box = QRectF(centre.x() - glyph.box.width() / 2.0, centre.y() - step / 2.0, glyph.box.width(), step);
    }
    restack(layout);
}

// A curved title: the arc the text bends along (canvas fractions), as long as the text is
// wide, its middle at the canvas's middle.
std::vector<std::array<double, 2>> arcFor(double curve, double width, QSize canvas)
{
    const double theta = curve * std::numbers::pi * 1.5; // up to 270° of a circle
    const double radius = width / std::max(std::abs(theta), 1e-3);
    const double cx = canvas.width() / 2.0, cy = canvas.height() / 2.0;
    std::vector<std::array<double, 2>> points;
    constexpr int kSteps = 48;
    for (int i = 0; i <= kSteps; ++i) {
        const double phi = std::abs(theta) * (double(i) / kSteps - 0.5);
        const double sag = radius * (1.0 - std::cos(phi)) * (curve > 0 ? 1.0 : -1.0);
        points.push_back({(cx + radius * std::sin(phi)) / canvas.width(), (cy + sag) / canvas.height()});
    }
    return points;
}

TitleLayout layoutText(const tve::ClipText& text, const QFont& font, QSize canvas)
{
    // On a path (or curved): one line, unwrapped, bent along it afterwards. Vertical: each
    // line a column, unwrapped.
    const bool curved = text.curve != 0.0 && text.path.size() < 2 && !text.vertical;
    const bool onPath = (text.path.size() >= 2 && !text.vertical) || curved;
    const bool wrap = text.scroll != tve::TextScroll::CrawlLeft && !onPath && !text.vertical;
    const double wrapWidth = wrap ? canvas.width() * 0.9 : 1e7;
    const double left = canvas.width() * 0.05;
    const QFontMetricsF metrics(font);
    const double lineHeight = metrics.height() * text.lineSpacing;

    struct Line {
        QString text;
        double width;
    };
    std::vector<Line> lines;
    QString all = QString::fromStdString(text.text);
    if (onPath)
        all.replace(QLatin1Char('\n'), QLatin1Char(' '));
    for (const QString& paragraph : all.split(QLatin1Char('\n'))) {
        QTextLayout layout(paragraph, font);
        QTextOption option;
        option.setWrapMode(wrap ? QTextOption::WordWrap : QTextOption::NoWrap);
        layout.setTextOption(option);
        layout.beginLayout();
        bool any = false;
        for (QTextLine line = layout.createLine(); line.isValid(); line = layout.createLine()) {
            line.setLineWidth(wrapWidth);
            lines.push_back({paragraph.mid(line.textStart(), line.textLength()).trimmed(), line.naturalTextWidth()});
            any = true;
        }
        layout.endLayout();
        if (!any)
            lines.push_back({QString(), 0.0}); // an empty paragraph still takes a line
    }

    TitleLayout result;
    const double total = lineHeight * double(lines.size());
    const double top = (canvas.height() - total) / 2.0;
    double baseline = top + (lineHeight - metrics.height()) / 2.0 + metrics.ascent();
    int character = 0;
    int word = -1;
    for (std::size_t l = 0; l < lines.size(); ++l) {
        const Line& line = lines[l];
        const double width = wrap ? std::min(line.width, wrapWidth) : line.width;
        double x = wrap ? left : (canvas.width() - width) / 2.0;
        if (wrap && text.alignment == tve::TextAlignment::Center)
            x += (wrapWidth - width) / 2.0;
        else if (wrap && text.alignment == tve::TextAlignment::Right)
            x += wrapWidth - width;
        const double lineTop = baseline - metrics.ascent() - (lineHeight - metrics.height()) / 2.0;
        result.lines.push_back(QRectF(x, lineTop, width, lineHeight));
        // Character positions from the line's own layout (kerning and letter spacing included).
        QTextLayout single(line.text, font);
        single.beginLayout();
        QTextLine measured = single.createLine();
        if (measured.isValid())
            measured.setLineWidth(1e7);
        single.endLayout();
        bool inWord = false;
        QTextBoundaryFinder graphemes(QTextBoundaryFinder::Grapheme, line.text);
        for (int i = 0; i < line.text.size(); ++i) {
            const QChar c = line.text.at(i);
            if (c.isSpace()) {
                inWord = false;
                continue;
            }
            if (!inWord) {
                ++word;
                result.words.push_back(QRectF());
                inWord = true;
            }
            // The whole grapheme, drawn shaped as one.
            graphemes.setPosition(i);
            const qsizetype end = graphemes.toNextBoundary();
            const int length = end > i ? int(end - i) : 1;
            const double cx = measured.isValid() ? measured.cursorToX(i) : 0.0;
            const double next = measured.isValid() ? measured.cursorToX(i + length) : cx;
            Glyph glyph;
            glyph.path.addText(QPointF(x + cx, baseline), font, line.text.mid(i, length));
            glyph.box = QRectF(x + cx, lineTop, std::max(next - cx, 1.0), lineHeight);
            glyph.character = character++;
            glyph.word = word;
            glyph.line = int(l);
            result.words.back() = result.words.back().united(glyph.box);
            result.bounds = result.bounds.united(glyph.box);
            result.glyphs.push_back(std::move(glyph));
            i += length - 1;
        }
        // An underline under the line: each character's piece reaching the next one (across
        // the space between words), so it moves with its character.
        if (text.underline && !text.vertical) {
            const double y = baseline + std::max(metrics.underlinePos(), font.pixelSize() * 0.08);
            const double thickness = std::max(metrics.lineWidth(), font.pixelSize() * 0.06);
            for (std::size_t g = 0; g < result.glyphs.size(); ++g) {
                Glyph& glyph = result.glyphs[g];
                if (glyph.line != int(l))
                    continue;
                const bool last = g + 1 == result.glyphs.size() || result.glyphs[g + 1].line != int(l);
                const double right = last ? glyph.box.right() : result.glyphs[g + 1].box.left();
                glyph.path.addRect(QRectF(glyph.box.left(), y, right - glyph.box.left(), thickness));
            }
        }
        if (onPath) {
            if (curved) {
                tve::ClipText bent = text;
                const double width = result.glyphs.empty() ? 0.0 : result.glyphs.back().box.right() - result.glyphs.front().box.left();
                bent.path = arcFor(text.curve, width, canvas);
                bent.alignment = tve::TextAlignment::Center;
                bendAlong(result, bent, canvas, baseline);
            } else {
                bendAlong(result, text, canvas, baseline);
            }
            return result;
        }
        baseline += lineHeight;
    }
    if (text.vertical)
        stackVertically(result, text, canvas, metrics);
    return result;
}

// The outline of a shape around `r` (the text and its padding), `em` pixels to the em.
QPainterPath shapePath(const tve::TitleShape& shape, const QRectF& r, double em)
{
    QPainterPath path;
    switch (shape.kind) {
    case tve::TitleShapeKind::None:
        break;
    case tve::TitleShapeKind::Box:
        path.addRect(r);
        break;
    case tve::TitleShapeKind::Rounded:
        path.addRoundedRect(r, em * 0.3, em * 0.3);
        break;
    case tve::TitleShapeKind::Pill:
        path.addRoundedRect(r, r.height() / 2.0, r.height() / 2.0);
        break;
    case tve::TitleShapeKind::Speech: {
        path.addRoundedRect(r, em * 0.35, em * 0.35);
        // A tail below, pointing down and outwards from `tail` along the bottom.
        const double x = r.left() + std::clamp(shape.tail, 0.08, 0.92) * r.width();
        const double side = shape.tail < 0.5 ? -1.0 : 1.0;
        QPainterPath tail;
        tail.moveTo(x - em * 0.3, r.bottom() - 1.0);
        tail.lineTo(x + side * em * 0.55, r.bottom() + em * 0.65);
        tail.lineTo(x + em * 0.3, r.bottom() - 1.0);
        tail.closeSubpath();
        path = path.united(tail);
        break;
    }
    case tve::TitleShapeKind::Thought: {
        const QRectF cloud = r.adjusted(-em * 0.2, -em * 0.1, em * 0.2, em * 0.1);
        path.addEllipse(cloud);
        const double x = cloud.left() + std::clamp(shape.tail, 0.1, 0.9) * cloud.width();
        const double side = shape.tail < 0.5 ? -1.0 : 1.0;
        path.addEllipse(QPointF(x + side * em * 0.2, cloud.bottom() + em * 0.25), em * 0.2, em * 0.2);
        path.addEllipse(QPointF(x + side * em * 0.5, cloud.bottom() + em * 0.6), em * 0.12, em * 0.12);
        break;
    }
    case tve::TitleShapeKind::Banner: {
        // A ribbon with notched ends.
        const double notch = r.height() * 0.35;
        path.moveTo(r.left() - notch, r.top());
        path.lineTo(r.right() + notch, r.top());
        path.lineTo(r.right(), r.center().y());
        path.lineTo(r.right() + notch, r.bottom());
        path.lineTo(r.left() - notch, r.bottom());
        path.lineTo(r.left(), r.center().y());
        path.closeSubpath();
        break;
    }
    case tve::TitleShapeKind::Underline:
        path.addRoundedRect(QRectF(r.left(), r.bottom() - em * 0.1, r.width(), em * 0.1), em * 0.05, em * 0.05);
        break;
    case tve::TitleShapeKind::Circle: {
        const double d = std::max(r.width(), r.height()) * 1.15;
        path.addEllipse(r.center(), d / 2.0, d / 2.0);
        break;
    }
    case tve::TitleShapeKind::Arrow: {
        // A box pointing right.
        const double head = r.height() * 0.5;
        path.moveTo(r.topLeft());
        path.lineTo(r.right(), r.top());
        path.lineTo(r.right() + head, r.center().y());
        path.lineTo(r.right(), r.bottom());
        path.lineTo(r.bottomLeft());
        path.closeSubpath();
        break;
    }
    case tve::TitleShapeKind::Tag: {
        // A label pointing left, with a hole.
        const double point = r.height() * 0.45;
        path.moveTo(r.left() - point, r.center().y());
        path.lineTo(r.left(), r.top());
        path.lineTo(r.right(), r.top());
        path.lineTo(r.right(), r.bottom());
        path.lineTo(r.left(), r.bottom());
        path.closeSubpath();
        QPainterPath hole;
        hole.addEllipse(QPointF(r.left() - point * 0.35, r.center().y()), em * 0.08, em * 0.08);
        path = path.subtracted(hole);
        break;
    }
    }
    return path;
}

// Where a unit stands (for a character, word or line, or all of the text), and which of how
// many it is.
struct UnitPlace {
    std::size_t index = 0;
    std::size_t count = 1;
    QRectF rect;
};

UnitPlace unitOf(const TitleLayout& layout, const Glyph& glyph, tve::TextUnit unit)
{
    switch (unit) {
    case tve::TextUnit::Character:
        return {std::size_t(glyph.character), layout.glyphs.size(), glyph.box};
    case tve::TextUnit::Word:
        return {std::size_t(glyph.word), layout.words.size(), layout.words[std::size_t(glyph.word)]};
    case tve::TextUnit::Line:
        return {std::size_t(glyph.line), layout.lines.size(), layout.lines[std::size_t(glyph.line)]};
    case tve::TextUnit::Whole:
        break;
    }
    return {0, 1, layout.bounds};
}

// How a unit in state `s` is drawn, given the unit's place: a transform about its centre,
// its opacity and blur, and the part of it that shows.
struct Drawn {
    QTransform transform;
    double opacity = 1.0;
    double blur = 0.0;
    QRectF clip; // empty: no clipping
};

void apply(Drawn& d, const tve::TextUnitState& s, const QRectF& rect, double em)
{
    if (s.identity())
        return;
    const QPointF c = rect.center();
    QTransform t;
    t.translate(c.x() + s.dx * em, c.y() + s.dy * em);
    t.rotate(s.rotation);
    t.scale(s.scale * s.scaleX, s.scale);
    t.translate(-c.x(), -c.y());
    d.transform = d.transform * t; // after what is already there
    d.opacity *= std::clamp(s.opacity, 0.0, 1.0);
    d.blur = std::max(d.blur, s.blur * em);
    if (s.reveal < 1.0) {
        const QRectF shown(rect.left(), rect.top() - rect.height(), rect.width() * std::max(0.0, s.reveal), rect.height() * 3.0);
        d.clip = d.clip.isEmpty() ? shown : d.clip.intersected(shown);
    }
}

// Draws with `fill` as `d` says; a blur is a ring of fainter copies (cheap, and soft enough).
void drawPath(QPainter& painter, const Drawn& d, const std::function<void(QPainter&)>& fill)
{
    if (d.opacity <= 0.001 || (!d.clip.isEmpty() && d.clip.width() <= 0.0))
        return;
    painter.save();
    painter.setTransform(d.transform, true);
    if (!d.clip.isEmpty())
        painter.setClipRect(d.clip, Qt::IntersectClip);
    if (d.blur > 0.5) {
        constexpr int kCopies = 8;
        painter.setOpacity(painter.opacity() * d.opacity / 3.0);
        for (int i = 0; i < kCopies; ++i) {
            const double a = 2.0 * std::numbers::pi * i / kCopies;
            painter.save();
            painter.translate(std::cos(a) * d.blur, std::sin(a) * d.blur);
            fill(painter);
            painter.restore();
        }
    } else {
        painter.setOpacity(painter.opacity() * d.opacity);
    }
    fill(painter);
    painter.restore();
}

// A picture to fill text with (kept: titles are drawn every frame).
QImage fillPicture(const std::string& path)
{
    static QHash<QString, QImage> cache;
    const QString key = QString::fromStdString(path);
    if (auto it = cache.constFind(key); it != cache.constEnd())
        return *it;
    QImage image(key);
    if (cache.size() > 16)
        cache.clear();
    cache.insert(key, image);
    return image;
}

// What fills the letters: their colour, a gradient across `bounds` (the text), or a picture
// covering it.
QBrush textBrush(const tve::ClipText& text, const QRectF& bounds)
{
    const tve::TextFill& fill = text.fill;
    const QColor from = toColor(text.color), to = toColor(fill.to);
    switch (fill.kind) {
    case tve::TextFillKind::Linear: {
        const double a = fill.angle * std::numbers::pi / 180.0;
        const QPointF d(std::cos(a), std::sin(a));
        const double half = (std::abs(d.x()) * bounds.width() + std::abs(d.y()) * bounds.height()) / 2.0;
        QLinearGradient gradient(bounds.center() - d * half, bounds.center() + d * half);
        gradient.setColorAt(0, from);
        gradient.setColorAt(1, to);
        return QBrush(gradient);
    }
    case tve::TextFillKind::Radial: {
        QRadialGradient gradient(bounds.center(), std::max(bounds.width(), bounds.height()) / 2.0);
        gradient.setColorAt(0, from);
        gradient.setColorAt(1, to);
        return QBrush(gradient);
    }
    case tve::TextFillKind::Image: {
        const QImage picture = fillPicture(fill.imagePath);
        if (picture.isNull() || bounds.isEmpty())
            return QBrush(from);
        // Covering the text, centred on it.
        const double scale = std::max(bounds.width() / picture.width(), bounds.height() / picture.height());
        QBrush brush(picture);
        QTransform place;
        place.translate(bounds.center().x() - picture.width() * scale / 2.0, bounds.center().y() - picture.height() * scale / 2.0);
        place.scale(scale, scale);
        brush.setTransform(place);
        return brush;
    }
    case tve::TextFillKind::Solid:
        break;
    }
    return QBrush(from);
}

} // namespace

QString defaultTitleFamily()
{
    static const QString family = [] {
        for (const char* candidate : {"Helvetica Neue", "Helvetica", "Arial"}) {
            if (QFontDatabase::hasFamily(QString::fromLatin1(candidate)))
                return QString::fromLatin1(candidate);
        }
        return QFontDatabase::systemFont(QFontDatabase::GeneralFont).family();
    }();
    return family;
}

QStringList titleFontFamilies()
{
    QStringList families;
    for (const char* name : {"Helvetica Neue", "Avenir Next", "Futura", "Gill Sans", "Optima", "Georgia", "Baskerville",
                             "Didot", "American Typewriter", "Menlo", "Rockwell", "Impact", "Marker Felt",
                             "Chalkboard SE", "Snell Roundhand"}) {
        const QString family = QString::fromLatin1(name);
        if (QFontDatabase::hasFamily(family))
            families << family;
    }
    if (!families.contains(defaultTitleFamily()))
        families.prepend(defaultTitleFamily());
    return families;
}

namespace {

double easeOut(double p)
{
    p = std::clamp(p, 0.0, 1.0);
    return 1.0 - std::pow(1.0 - p, 3.0);
}

// A chart value as written on it: whole numbers as they are, others to the decimals the data
// uses (at most two).
QString chartNumber(double value, int decimals)
{
    return QString::number(value, 'f', decimals);
}

int chartDecimals(const std::vector<tve::ChartPoint>& points)
{
    int decimals = 0;
    for (const tve::ChartPoint& p : points) {
        for (int d = 0; d <= 2; ++d) {
            const double scaled = p.value * std::pow(10.0, d);
            if (std::abs(scaled - std::round(scaled)) < 1e-6) {
                decimals = std::max(decimals, d);
                break;
            }
            if (d == 2)
                decimals = 2;
        }
    }
    return decimals;
}

// A chart title: the heading (the text) above a box in the middle of the canvas holding the
// bars or line, the labels under it. Bars grow one after another, a line draws left to right.
QImage renderChart(const tve::ClipText& text, QSize canvas, double seconds, double duration)
{
    const tve::TextChart& chart = *text.chart;
    QImage image(canvas, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    if (canvas.isEmpty() || chart.points.empty())
        return image.convertToFormat(QImage::Format_RGBA8888);
    const double em = text.size * canvas.height();
    const QRectF box(canvas.width() * 0.15, canvas.height() * 0.3, canvas.width() * 0.7, canvas.height() * 0.52);
    QPainter painter(&image);
    painter.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing | QPainter::SmoothPixmapTransform);

    // The heading, its middle a little above the box.
    tve::ClipText heading = text;
    heading.chart.reset();
    const QImage top = renderTitle(heading, canvas, seconds, duration);
    painter.drawImage(QPointF(0.0, box.top() - em * 0.8 - canvas.height() / 2.0), top.convertToFormat(QImage::Format_ARGB32_Premultiplied));

    const double labelPx = std::clamp(em * 0.4, canvas.height() * 0.022, canvas.height() * 0.05);
    QFont font = titleFont(text, labelPx);
    font.setLetterSpacing(QFont::AbsoluteSpacing, 0.0);
    painter.setFont(font);
    const QFontMetricsF metrics(font);
    const QColor ink = toColor(text.color);
    const QColor colour = toColor(chart.color);
    const QRectF plot = box.adjusted(0.0, metrics.height() * 1.3, 0.0, -metrics.height() * 1.6);
    const std::size_t n = chart.points.size();
    double lo = 0.0, hi = 0.0;
    for (const tve::ChartPoint& p : chart.points) {
        lo = std::min(lo, p.value);
        hi = std::max(hi, p.value);
    }
    if (hi - lo < 1e-12)
        hi = lo + 1.0;
    const auto yOf = [&](double v) { return plot.bottom() - (v - lo) / (hi - lo) * plot.height(); };
    const double slot = plot.width() / double(n);
    const auto xOf = [&](std::size_t i) { return plot.left() + slot * (double(i) + 0.5); };
    const int decimals = chartDecimals(chart.points);
    const bool growing = chart.grow > 0.0 && seconds >= 0.0;

    // The axis at zero, and the labels under the box.
    const double zero = yOf(0.0);
    QColor axis = ink;
    axis.setAlphaF(axis.alphaF() * 0.55f);
    painter.setPen(QPen(axis, std::max(1.0, canvas.height() / 360.0)));
    painter.drawLine(QPointF(plot.left(), zero), QPointF(plot.right(), zero));
    painter.setPen(ink);
    for (std::size_t i = 0; i < n; ++i) {
        const QString label = metrics.elidedText(QString::fromStdString(chart.points[i].label), Qt::ElideRight, slot * 0.96);
        painter.drawText(QRectF(xOf(i) - slot / 2.0, plot.bottom() + metrics.height() * 0.3, slot, metrics.height() * 1.2),
                         Qt::AlignHCenter | Qt::AlignTop, label);
    }

    const auto valueAt = [&](std::size_t i, double v, double y) {
        if (!chart.values)
            return;
        const bool below = v < 0.0;
        const QRectF r(xOf(i) - slot / 2.0, below ? y + metrics.height() * 0.15 : y - metrics.height() * 1.2, slot,
                       metrics.height());
        painter.setPen(ink);
        painter.drawText(r, Qt::AlignHCenter | Qt::AlignVCenter, chartNumber(v, decimals));
    };

    if (chart.kind == tve::ChartKind::Bar) {
        // Each bar grows over 60 % of the time, the next starting a little later.
        const double each = n > 1 ? chart.grow * 0.6 : chart.grow;
        const double stagger = n > 1 ? chart.grow * 0.4 / double(n - 1) : 0.0;
        const double width = slot * 0.62;
        for (std::size_t i = 0; i < n; ++i) {
            const double p = growing ? easeOut((seconds - double(i) * stagger) / std::max(each, 1e-6)) : 1.0;
            const double v = chart.points[i].value * p;
            const double y = yOf(v);
            const QRectF bar(xOf(i) - width / 2.0, std::min(y, zero), width, std::abs(zero - y));
            if (bar.height() > 0.5) {
                QPainterPath path;
                const double r = std::min(width * 0.12, bar.height() / 2.0);
                path.addRoundedRect(bar, r, r);
                painter.fillPath(path, colour);
            }
            if (p > 0.0)
                valueAt(i, v, y);
        }
    } else {
        // The line, as far along as it has drawn, over a faint fill down to zero.
        const double u = growing ? easeOut(seconds / chart.grow) : 1.0;
        const double reach = xOf(0) + (xOf(n - 1) - xOf(0)) * u;
        QPolygonF line;
        for (std::size_t i = 0; i < n; ++i) {
            const QPointF point(xOf(i), yOf(chart.points[i].value));
            if (point.x() <= reach + 1e-6) {
                line << point;
            } else {
                const QPointF before = line.isEmpty() ? point : line.back();
                const double t = (reach - before.x()) / std::max(point.x() - before.x(), 1e-6);
                line << before + (point - before) * std::clamp(t, 0.0, 1.0);
                break;
            }
        }
        const double stroke = std::max(2.0, canvas.height() * 0.008);
        if (line.size() >= 2) {
            QPolygonF area = line;
            area << QPointF(line.back().x(), zero) << QPointF(line.front().x(), zero);
            QColor faint = colour;
            faint.setAlphaF(faint.alphaF() * 0.2f);
            painter.setPen(Qt::NoPen);
            painter.setBrush(faint);
            painter.drawPolygon(area);
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(colour, stroke, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            painter.drawPolyline(line);
        }
        painter.setPen(Qt::NoPen);
        for (std::size_t i = 0; i < n; ++i) {
            if (xOf(i) > reach + 1e-6)
                break;
            const QPointF point(xOf(i), yOf(chart.points[i].value));
            painter.setBrush(colour);
            painter.drawEllipse(point, stroke * 1.5, stroke * 1.5);
            valueAt(i, chart.points[i].value, point.y() - stroke);
            painter.setPen(Qt::NoPen);
        }
    }
    painter.end();
    return image.convertToFormat(QImage::Format_RGBA8888);
}

// An audio visualiser title: the bars, mirrored bars, wave or ring in the middle of the canvas
// (size × its height tall), each band coloured from `color` towards `peak` as it rises; the
// text as a heading above it, or inside the ring.
QImage renderVisualiser(const tve::ClipText& text, QSize canvas, double seconds, double duration)
{
    const tve::TextVisualiser& v = *text.visualiser;
    QImage image(canvas, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    if (canvas.isEmpty())
        return image.convertToFormat(QImage::Format_RGBA8888);
    constexpr int n = tve::TextVisualiser::kBands;
    // Unmeasured: a low line, so it can be seen and placed.
    std::array<double, n> level{};
    for (int b = 0; b < n; ++b)
        level[std::size_t(b)] = v.frames.empty() ? 0.04 : std::max(0.02, v.levelAt(std::max(seconds, 0.0), b));
    const auto shade = [&](double l) {
        const auto mix = [l](double a, double b) { return float(a + (b - a) * std::clamp(l, 0.0, 1.0)); };
        return QColor::fromRgbF(mix(v.color.r, v.peak.r), mix(v.color.g, v.peak.g), mix(v.color.b, v.peak.b),
                                mix(v.color.a, v.peak.a));
    };
    const double h = v.size * canvas.height();
    const double w = canvas.width() * 0.8;
    const QPointF centre(canvas.width() / 2.0, canvas.height() / 2.0);
    const QRectF area(centre.x() - w / 2.0, centre.y() - h / 2.0, w, h);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    const double slot = w / n;

    switch (v.style) {
    case tve::VisualiserStyle::Bars:
    case tve::VisualiserStyle::Mirror: {
        const bool mirror = v.style == tve::VisualiserStyle::Mirror;
        const double width = slot * 0.7;
        for (int b = 0; b < n; ++b) {
            const double l = level[std::size_t(b)];
            const double tall = std::max(l * h, width * 0.5);
            const double x = area.left() + slot * (b + 0.5) - width / 2.0;
            const QRectF bar = mirror ? QRectF(x, centre.y() - tall / 2.0, width, tall) : QRectF(x, area.bottom() - tall, width, tall);
            QPainterPath path;
            path.addRoundedRect(bar, width * 0.3, width * 0.3);
            painter.fillPath(path, shade(l));
        }
        break;
    }
    case tve::VisualiserStyle::Wave: {
        // A smooth band through the levels, as thick as they are loud, about the middle.
        QPolygonF top, bottom;
        for (int b = 0; b < n; ++b) {
            const double x = area.left() + slot * (b + 0.5);
            const double half = std::max(level[std::size_t(b)] * h / 2.0, canvas.height() * 0.004);
            top << QPointF(x, centre.y() - half);
            bottom << QPointF(x, centre.y() + half);
        }
        const auto smooth = [](QPainterPath& path, const QPolygonF& points) {
            for (int i = 1; i < points.size(); ++i) {
                const QPointF mid = (points[i - 1] + points[i]) / 2.0;
                path.quadTo(points[i - 1], mid);
            }
            path.lineTo(points.back());
        };
        QPainterPath path(top.front());
        smooth(path, top);
        std::reverse(bottom.begin(), bottom.end());
        path.lineTo(bottom.front());
        smooth(path, bottom);
        path.closeSubpath();
        double loudest = 0.0;
        for (double l : level)
            loudest = std::max(loudest, l);
        QLinearGradient across(area.topLeft(), area.topRight());
        across.setColorAt(0.0, shade(level[0]));
        across.setColorAt(0.5, shade(loudest));
        across.setColorAt(1.0, shade(level[n - 1]));
        painter.fillPath(path, across);
        break;
    }
    case tve::VisualiserStyle::Ring: {
        // 64 spokes round a circle, the bands going round one side and back the other; the
        // circle wide enough for the text inside it.
        double inside = 0.0;
        if (!QString::fromStdString(text.text).trimmed().isEmpty()) {
            const QFontMetricsF metrics(titleFont(text, text.size * canvas.height()));
            for (const QString& line : QString::fromStdString(text.text).split(QLatin1Char('\n')))
                inside = std::max(inside, metrics.horizontalAdvance(line) / 2.0 + text.size * canvas.height() * 0.5);
        }
        const double radius = std::max(h * 0.28, inside);
        const double longest = h * 0.22;
        const double width = 2.0 * std::numbers::pi * radius / 64.0 * 0.55;
        QColor rim = shade(0.0);
        rim.setAlphaF(rim.alphaF() * 0.5f);
        painter.setPen(QPen(rim, std::max(1.0, width * 0.4)));
        painter.drawEllipse(centre, radius - width, radius - width);
        for (int i = 0; i < 64; ++i) {
            const int band = i < n ? i : 2 * n - 1 - i;
            const double l = level[std::size_t(band)];
            const double angle = -std::numbers::pi / 2.0 + 2.0 * std::numbers::pi * i / 64.0;
            const QPointF dir(std::cos(angle), std::sin(angle));
            painter.setPen(QPen(shade(l), width, Qt::SolidLine, Qt::RoundCap));
            painter.drawLine(centre + dir * radius, centre + dir * (radius + std::max(l * longest, width * 0.2)));
        }
        break;
    }
    }

    // The text: inside the ring, else a heading above.
    tve::ClipText heading = text;
    heading.visualiser.reset();
    const QImage words = renderTitle(heading, canvas, seconds, duration).convertToFormat(QImage::Format_ARGB32_Premultiplied);
    const double em = text.size * canvas.height();
    const double dy = v.style == tve::VisualiserStyle::Ring ? 0.0 : area.top() - em * 0.8 - canvas.height() / 2.0;
    painter.drawImage(QPointF(0.0, dy), words);
    painter.end();
    return image.convertToFormat(QImage::Format_RGBA8888);
}

// A repeatable random number in 0..1 for (a, b).
double jitter(double a, double b)
{
    const double v = std::sin(a * 12.9898 + b * 78.233) * 43758.5453;
    return v - std::floor(v);
}

} // namespace

QImage renderTitle(const tve::ClipText& text, QSize canvas, double seconds, double duration)
{
    if (text.visualiser)
        return renderVisualiser(text, canvas, seconds, duration);
    if (text.chart)
        return renderChart(text, canvas, seconds, duration);
    QImage image(canvas, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    const bool hasText = QString::fromStdString(text.text).trimmed().size() > 0;
    if (canvas.isEmpty() || !hasText)
        return image.convertToFormat(QImage::Format_RGBA8888);

    const double em = text.size * canvas.height();
    const QFont font = titleFont(text, em);
    const TitleLayout layout = layoutText(text, font, canvas);
    const bool moving = text.animated() && seconds >= 0.0;
    const double clipSeconds = std::max(duration, 0.001);

    // Scrolling moves everything across the frame over the whole clip.
    QTransform scroll;
    if (moving && text.scroll != tve::TextScroll::None) {
        const double u = std::clamp(seconds / clipSeconds, 0.0, 1.0);
        if (text.scroll == tve::TextScroll::RollUp) {
            const double travel = (canvas.height() + layout.bounds.height()) / 2.0;
            scroll.translate(0.0, travel * (1.0 - 2.0 * u));
        } else {
            const double travel = (canvas.width() + layout.bounds.width()) / 2.0;
            scroll.translate(travel * (1.0 - 2.0 * u), 0.0);
        }
    }

    // Each character's drawing: its in, loop and out animations, each about its own unit.
    std::vector<Drawn> drawn(layout.glyphs.size());
    for (std::size_t g = 0; g < layout.glyphs.size(); ++g) {
        Drawn& d = drawn[g];
        if (moving) {
            for (const auto& [animation, role] : {std::pair{&text.animationIn, tve::TextAnimationRole::In},
                                                  std::pair{&text.animationLoop, tve::TextAnimationRole::Loop},
                                                  std::pair{&text.animationOut, tve::TextAnimationRole::Out}}) {
                if (!animation->active())
                    continue;
                const UnitPlace unit = unitOf(layout, layout.glyphs[g], animation->unit);
                apply(d, tve::animateTextUnit(*animation, role, unit.index, unit.count, seconds, clipSeconds), unit.rect, em);
            }
        }
    }
    // Moving with the music: grown as a whole, each character jumping a moment after the one
    // before, or shaken (the colour's flash comes with the fills).
    const double beat = moving && text.react.active() ? text.react.amount : 0.0;
    tve::TextUnitState pulse;
    if (beat > 0.0 && text.react.kind == tve::TextReactKind::Pulse)
        pulse.scale = 1.0 + 0.45 * beat * text.react.levelAt(seconds);
    for (std::size_t g = 0; g < layout.glyphs.size() && beat > 0.0; ++g) {
        const Glyph& glyph = layout.glyphs[g];
        tve::TextUnitState s;
        switch (text.react.kind) {
        case tve::TextReactKind::Pulse:
            apply(drawn[g], pulse, layout.bounds, em);
            break;
        case tve::TextReactKind::Bounce: {
            const double level = text.react.levelAt(seconds - 0.04 * glyph.character);
            s.dy = -0.5 * beat * level;
            s.scale = 1.0 + 0.12 * beat * level;
            apply(drawn[g], s, glyph.box, em);
            break;
        }
        case tve::TextReactKind::Shake: {
            const double level = text.react.levelAt(seconds);
            const double frame = std::floor(seconds * tve::TextReact::kRate);
            s.dx = (jitter(glyph.character, frame) - 0.5) * 0.5 * beat * level;
            s.dy = (jitter(glyph.character + 0.5, frame) - 0.5) * 0.5 * beat * level;
            s.rotation = (jitter(glyph.character + 0.25, frame) - 0.5) * 24.0 * beat * level;
            apply(drawn[g], s, glyph.box, em);
            break;
        }
        case tve::TextReactKind::Flash:
        case tve::TextReactKind::None:
            break;
        }
    }
    for (Drawn& d : drawn)
        d.transform = d.transform * scroll;
    // A caption following its speech: the words said so far, and the one being said (the last
    // started, until the next starts). Layout words and caption words match one for one.
    std::vector<QBrush> fills(layout.glyphs.size(), textBrush(text, layout.bounds));
    if (text.captionMotion != tve::CaptionMotion::None && !text.words.empty() && seconds >= 0.0) {
        const auto now = tve::Ticks(seconds * double(tve::kTicksPerSecond));
        int current = -1;
        for (std::size_t i = 0; i < text.words.size(); ++i)
            if (text.words[i].start <= now)
                current = int(i);
        const QColor highlight = toColor(text.highlightColor);
        for (std::size_t g = 0; g < layout.glyphs.size(); ++g) {
            const int word = layout.glyphs[g].word;
            if (word < 0 || word >= int(text.words.size()))
                continue;
            const bool said = word <= current;
            const bool saying = word == current;
            switch (text.captionMotion) {
            case tve::CaptionMotion::Highlight:
                if (saying)
                    fills[g] = QBrush(highlight);
                break;
            case tve::CaptionMotion::Pop:
                if (saying) {
                    fills[g] = QBrush(highlight);
                    // Grows to 115 % over its first tenth of a second.
                    const double into = tve::toSeconds(now - text.words[std::size_t(word)].start);
                    tve::TextUnitState pop;
                    pop.scale = 1.0 + 0.15 * std::clamp(into / 0.1, 0.0, 1.0);
                    apply(drawn[g], pop, layout.words[std::size_t(word)], em);
                }
                break;
            case tve::CaptionMotion::Karaoke:
                if (said)
                    fills[g] = QBrush(highlight);
                break;
            case tve::CaptionMotion::Reveal:
                if (!said)
                    drawn[g].opacity = 0.0;
                break;
            case tve::CaptionMotion::None:
                break;
            }
        }
    }

    // A flash: the letters' colour towards the highlight colour as the music hits.
    if (beat > 0.0 && text.react.kind == tve::TextReactKind::Flash) {
        const double k = std::clamp(beat * text.react.levelAt(seconds), 0.0, 1.0);
        if (k > 0.005) {
            const auto mix = [k](double a, double b) { return float(a + (b - a) * k); };
            const tve::ColorRgba& a = text.color;
            const tve::ColorRgba& b = text.highlightColor;
            const QColor flash = QColor::fromRgbF(mix(a.r, b.r), mix(a.g, b.g), mix(a.b, b.b), mix(a.a, b.a));
            std::ranges::fill(fills, QBrush(flash));
        }
    }

    // The shape moves with the text as a whole (in and out, not the loop), and its pulse.
    Drawn shapeDrawn;
    if (!pulse.identity())
        apply(shapeDrawn, pulse, layout.bounds, em);
    if (moving) {
        for (const auto& [animation, role] : {std::pair{&text.animationIn, tve::TextAnimationRole::In},
                                              std::pair{&text.animationOut, tve::TextAnimationRole::Out}}) {
            if (animation->active() && animation->preset != "typewriter")
                apply(shapeDrawn, tve::animateTextUnit(*animation, role, 0, 1, seconds, clipSeconds), layout.bounds, em);
        }
    }
    shapeDrawn.transform = shapeDrawn.transform * scroll;
    const QPainterPath shape = shapePath(text.shape, layout.bounds.adjusted(-text.shape.padding * em, -text.shape.padding * em * 0.6,
                                                                             text.shape.padding * em, text.shape.padding * em * 0.6),
                                         em);

    const auto drawGlyphs = [&](QPainter& painter,
                                const std::function<void(QPainter&, const QPainterPath&, std::size_t)>& paint) {
        for (std::size_t g = 0; g < layout.glyphs.size(); ++g) {
            const QPainterPath& path = layout.glyphs[g].path;
            drawPath(painter, drawn[g], [&](QPainter& p) { paint(p, path, g); });
        }
    };

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    if (!shape.isEmpty())
        drawPath(painter, shapeDrawn, [&](QPainter& p) { p.fillPath(shape, toColor(text.shape.color)); });
    painter.end();

    if (text.shadow) {
        // A soft drop shadow: drawn at a quarter size, then scaled up smoothly (a cheap blur).
        const QSize small = (QSizeF(canvas) / 4.0).toSize().expandedTo(QSize(1, 1));
        QImage shadow(small, QImage::Format_ARGB32_Premultiplied);
        shadow.fill(Qt::transparent);
        QPainter p(&shadow);
        p.setRenderHint(QPainter::Antialiasing);
        p.scale(0.25, 0.25);
        p.translate(em * 0.05, em * 0.07);
        drawGlyphs(p, [&](QPainter& q, const QPainterPath& path, std::size_t) {
            if (text.outlineWidth > 0)
                q.strokePath(path, QPen(QColor(0, 0, 0, 140), text.outlineWidth * em * 2.0, Qt::SolidLine, Qt::RoundCap,
                                        Qt::RoundJoin));
            q.fillPath(path, QColor(0, 0, 0, 140));
        });
        p.end();
        QPainter onto(&image);
        onto.setRenderHint(QPainter::SmoothPixmapTransform);
        onto.drawImage(QRect(QPoint(0, 0), canvas), shadow.scaled(small * 2, Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
    }

    painter.begin(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    // 3D: each letter's sides first (copies stepping down and right, darkening further back),
    // all of them under every letter's face.
    if (text.depth > 0.0) {
        const double length = text.depth * em;
        const int steps = std::clamp(int(std::ceil(length / 1.5)), 1, 80);
        const QColor side = toColor(text.depthColor);
        for (int i = steps; i >= 1; --i) {
            const double t = double(i) / steps;
            const QColor shade = side.darker(100 + int(40 * t));
            drawGlyphs(painter, [&](QPainter& p, const QPainterPath& path, std::size_t) {
                p.save();
                p.translate(length * t * 0.6, length * t * 0.8);
                if (text.outlineWidth > 0)
                    p.strokePath(path, QPen(shade, text.outlineWidth * em * 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                p.fillPath(path, shade);
                p.restore();
            });
        }
    }
    drawGlyphs(painter, [&](QPainter& p, const QPainterPath& path, std::size_t g) {
        // The outline is stroked under the fill, so it grows outwards.
        if (text.outlineWidth > 0)
            p.strokePath(path, QPen(toColor(text.outlineColor), text.outlineWidth * em * 2.0, Qt::SolidLine, Qt::RoundCap,
                                    Qt::RoundJoin));
        p.fillPath(path, fills[g]);
    });
    painter.end();
    // Tilted: the whole title turned in perspective about the canvas's middle.
    if (text.tiltX != 0.0 || text.tiltY != 0.0) {
        QImage tilted(canvas, QImage::Format_ARGB32_Premultiplied);
        tilted.fill(Qt::transparent);
        QPainter p(&tilted);
        p.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
        const double distance = canvas.height() * 2.0;
        QTransform turn;
        turn.translate(canvas.width() / 2.0, canvas.height() / 2.0);
        turn.rotate(text.tiltX, Qt::XAxis, distance);
        turn.rotate(text.tiltY, Qt::YAxis, distance);
        turn.translate(-canvas.width() / 2.0, -canvas.height() / 2.0);
        p.setTransform(turn);
        p.drawImage(0, 0, image);
        p.end();
        image = std::move(tilted);
    }
    return image.convertToFormat(QImage::Format_RGBA8888);
}

namespace {

QVariantList colorList(const tve::ColorRgba& c)
{
    return {c.r, c.g, c.b, c.a};
}

tve::ColorRgba colorFrom(const QVariant& value, tve::ColorRgba fallback)
{
    // From QML a JavaScript array arrives wrapped (as a QJSValue); convert, don't cast.
    const QVariantList list = value.value<QVariantList>();
    if (list.size() < 3)
        return fallback;
    return {list[0].toDouble(), list[1].toDouble(), list[2].toDouble(), list.size() > 3 ? list[3].toDouble() : 1.0};
}

constexpr const char* kShapeKinds[] = {"none", "box", "rounded", "pill", "speech", "thought",
                                       "banner", "underline", "circle", "arrow", "tag"};
constexpr const char* kUnits[] = {"whole", "line", "word", "character"};
constexpr const char* kScrolls[] = {"none", "rollUp", "crawlLeft"};
constexpr const char* kFillKinds[] = {"solid", "linear", "radial", "image"};
constexpr const char* kCaptionMotions[] = {"none", "highlight", "pop", "karaoke", "reveal"};
constexpr const char* kReactKinds[] = {"none", "pulse", "bounce", "flash", "shake"};
constexpr const char* kChartKinds[] = {"bar", "line"};
constexpr const char* kVisualiserStyles[] = {"bars", "mirror", "wave", "ring"};

template <std::size_t N>
int indexOf(const char* const (&names)[N], const QString& name, int fallback)
{
    for (std::size_t i = 0; i < N; ++i) {
        if (name == QLatin1String(names[i]))
            return int(i);
    }
    return fallback;
}

QVariantMap animationMap(const tve::TextAnimation& a)
{
    return {{QStringLiteral("preset"), QString::fromStdString(a.preset)},
            {QStringLiteral("duration"), a.duration},
            {QStringLiteral("unit"), QString::fromLatin1(kUnits[int(a.unit)])},
            {QStringLiteral("stagger"), a.stagger}};
}

tve::TextAnimation animationFrom(const QVariant& value, tve::TextAnimation a)
{
    const QVariantMap m = value.toMap();
    if (m.contains(QStringLiteral("preset")))
        a.preset = m.value(QStringLiteral("preset")).toString().toStdString();
    if (m.contains(QStringLiteral("duration")))
        a.duration = m.value(QStringLiteral("duration")).toDouble();
    if (m.contains(QStringLiteral("unit")))
        a.unit = tve::TextUnit(indexOf(kUnits, m.value(QStringLiteral("unit")).toString(), int(a.unit)));
    if (m.contains(QStringLiteral("stagger")))
        a.stagger = m.value(QStringLiteral("stagger")).toDouble();
    return a;
}

const char* alignmentName(tve::TextAlignment alignment)
{
    switch (alignment) {
    case tve::TextAlignment::Left:
        return "left";
    case tve::TextAlignment::Right:
        return "right";
    case tve::TextAlignment::Center:
        break;
    }
    return "center";
}

QVariantMap chartMap(const tve::TextChart& chart)
{
    QVariantList points;
    for (const tve::ChartPoint& p : chart.points)
        points << QVariantMap{{QStringLiteral("label"), QString::fromStdString(p.label)}, {QStringLiteral("value"), p.value}};
    return {{QStringLiteral("kind"), QString::fromLatin1(kChartKinds[int(chart.kind)])},
            {QStringLiteral("points"), points},
            {QStringLiteral("data"), chartText(chart.points)},
            {QStringLiteral("color"), colorList(chart.color)},
            {QStringLiteral("grow"), chart.grow},
            {QStringLiteral("values"), chart.values}};
}

} // namespace

std::vector<tve::ChartPoint> parseChartData(const QString& data)
{
    std::vector<tve::ChartPoint> points;
    for (QString line : data.split(QLatin1Char('\n'))) {
        line = line.trimmed();
        if (line.isEmpty())
            continue;
        // The value is the last field (after a tab, semicolon or comma); the label the rest.
        qsizetype cut = -1;
        for (QChar separator : {QLatin1Char('\t'), QLatin1Char(';'), QLatin1Char(',')}) {
            cut = line.lastIndexOf(separator);
            if (cut >= 0)
                break;
        }
        const QString number = (cut >= 0 ? line.mid(cut + 1) : line).trimmed().remove(QLatin1Char('"'));
        bool ok = false;
        const double value = number.toDouble(&ok);
        if (!ok || !std::isfinite(value))
            continue; // a heading, or not a number
        QString label = cut >= 0 ? line.left(cut).trimmed() : QString();
        if (label.size() >= 2 && label.startsWith(QLatin1Char('"')) && label.endsWith(QLatin1Char('"')))
            label = label.mid(1, label.size() - 2);
        points.push_back({label.left(qsizetype(tve::TextChart::kMaxLabel)).toStdString(), value});
        if (points.size() == tve::TextChart::kMaxPoints)
            break;
    }
    return points;
}

QString chartText(const std::vector<tve::ChartPoint>& points)
{
    QStringList lines;
    for (const tve::ChartPoint& p : points)
        lines << QStringLiteral("%1, %2").arg(QString::fromStdString(p.label), QString::number(p.value, 'g', 12));
    return lines.join(QLatin1Char('\n'));
}

QVariantMap textMap(const tve::ClipText& text)
{
    return {
        {QStringLiteral("text"), QString::fromStdString(text.text)},
        {QStringLiteral("fontFamily"), QString::fromStdString(text.fontFamily)},
        {QStringLiteral("size"), text.size},
        {QStringLiteral("bold"), text.bold},
        {QStringLiteral("italic"), text.italic},
        {QStringLiteral("color"), colorList(text.color)},
        {QStringLiteral("alignment"), QString::fromLatin1(alignmentName(text.alignment))},
        {QStringLiteral("lineSpacing"), text.lineSpacing},
        {QStringLiteral("letterSpacing"), text.letterSpacing},
        {QStringLiteral("outlineColor"), colorList(text.outlineColor)},
        {QStringLiteral("outlineWidth"), text.outlineWidth},
        {QStringLiteral("shadow"), text.shadow},
        {QStringLiteral("shape"), QVariantMap{{QStringLiteral("kind"), QString::fromLatin1(kShapeKinds[int(text.shape.kind)])},
                                              {QStringLiteral("color"), colorList(text.shape.color)},
                                              {QStringLiteral("padding"), text.shape.padding},
                                              {QStringLiteral("tail"), text.shape.tail}}},
        {QStringLiteral("animationIn"), animationMap(text.animationIn)},
        {QStringLiteral("animationOut"), animationMap(text.animationOut)},
        {QStringLiteral("animationLoop"), animationMap(text.animationLoop)},
        {QStringLiteral("scroll"), QString::fromLatin1(kScrolls[int(text.scroll)])},
        {QStringLiteral("captionMotion"), QString::fromLatin1(kCaptionMotions[int(text.captionMotion)])},
        {QStringLiteral("highlightColor"), colorList(text.highlightColor)},
        {QStringLiteral("wordCount"), int(text.words.size())},
        {QStringLiteral("fill"), QVariantMap{{QStringLiteral("kind"), QString::fromLatin1(kFillKinds[int(text.fill.kind)])},
                                             {QStringLiteral("to"), colorList(text.fill.to)},
                                             {QStringLiteral("angle"), text.fill.angle},
                                             {QStringLiteral("imagePath"), QString::fromStdString(text.fill.imagePath)}}},
        {QStringLiteral("underline"), text.underline},
        {QStringLiteral("vertical"), text.vertical},
        {QStringLiteral("curve"), text.curve},
        {QStringLiteral("depth"), text.depth},
        {QStringLiteral("depthColor"), colorList(text.depthColor)},
        {QStringLiteral("tiltX"), text.tiltX},
        {QStringLiteral("tiltY"), text.tiltY},
        {QStringLiteral("react"), QVariantMap{{QStringLiteral("kind"), QString::fromLatin1(kReactKinds[int(text.react.kind)])},
                                              {QStringLiteral("amount"), text.react.amount},
                                              {QStringLiteral("measured"), !text.react.levels.empty()}}},
        {QStringLiteral("chart"), text.chart ? QVariant(chartMap(*text.chart)) : QVariant()},
        {QStringLiteral("visualiser"),
         text.visualiser ? QVariant(QVariantMap{{QStringLiteral("style"), QString::fromLatin1(kVisualiserStyles[int(text.visualiser->style)])},
                                                {QStringLiteral("color"), colorList(text.visualiser->color)},
                                                {QStringLiteral("peak"), colorList(text.visualiser->peak)},
                                                {QStringLiteral("size"), text.visualiser->size},
                                                {QStringLiteral("seconds"), double(text.visualiser->frameCount()) / tve::TextVisualiser::kRate},
                                                {QStringLiteral("measured"), !text.visualiser->frames.empty()}})
                         : QVariant()},
        {QStringLiteral("path"), [&] {
             QVariantList points;
             for (const auto& [x, y] : text.path)
                 points << QVariant(QVariantList{x, y});
             return points;
         }()}
    };
}

tve::ClipText textFrom(const QVariantMap& changes, tve::ClipText text)
{
    const auto has = [&](const char* key) { return changes.contains(QLatin1String(key)); };
    const auto get = [&](const char* key) { return changes.value(QLatin1String(key)); };
    if (has("text"))
        text.text = get("text").toString().toStdString();
    if (has("fontFamily"))
        text.fontFamily = get("fontFamily").toString().toStdString();
    if (has("size"))
        text.size = get("size").toDouble();
    if (has("bold"))
        text.bold = get("bold").toBool();
    if (has("italic"))
        text.italic = get("italic").toBool();
    if (has("color"))
        text.color = colorFrom(get("color"), text.color);
    if (has("alignment")) {
        const QString alignment = get("alignment").toString();
        text.alignment = alignment == QLatin1String("left")    ? tve::TextAlignment::Left
                       : alignment == QLatin1String("right") ? tve::TextAlignment::Right
                                                             : tve::TextAlignment::Center;
    }
    if (has("lineSpacing"))
        text.lineSpacing = get("lineSpacing").toDouble();
    if (has("letterSpacing"))
        text.letterSpacing = get("letterSpacing").toDouble();
    if (has("outlineColor"))
        text.outlineColor = colorFrom(get("outlineColor"), text.outlineColor);
    if (has("outlineWidth"))
        text.outlineWidth = get("outlineWidth").toDouble();
    if (has("shadow"))
        text.shadow = get("shadow").toBool();
    if (has("shape")) {
        const QVariantMap shape = get("shape").toMap();
        if (shape.contains(QStringLiteral("kind")))
            text.shape.kind = tve::TitleShapeKind(indexOf(kShapeKinds, shape.value(QStringLiteral("kind")).toString(),
                                                          int(text.shape.kind)));
        if (shape.contains(QStringLiteral("color")))
            text.shape.color = colorFrom(shape.value(QStringLiteral("color")), text.shape.color);
        if (shape.contains(QStringLiteral("padding")))
            text.shape.padding = shape.value(QStringLiteral("padding")).toDouble();
        if (shape.contains(QStringLiteral("tail")))
            text.shape.tail = shape.value(QStringLiteral("tail")).toDouble();
    }
    if (has("animationIn"))
        text.animationIn = animationFrom(get("animationIn"), text.animationIn);
    if (has("animationOut"))
        text.animationOut = animationFrom(get("animationOut"), text.animationOut);
    if (has("animationLoop"))
        text.animationLoop = animationFrom(get("animationLoop"), text.animationLoop);
    if (has("scroll"))
        text.scroll = tve::TextScroll(indexOf(kScrolls, get("scroll").toString(), int(text.scroll)));
    if (has("captionMotion"))
        text.captionMotion = tve::CaptionMotion(indexOf(kCaptionMotions, get("captionMotion").toString(), int(text.captionMotion)));
    if (has("highlightColor"))
        text.highlightColor = colorFrom(get("highlightColor"), text.highlightColor);
    // Text on a path: points ([x, y] canvas fractions), or a preset ("arc", "dip", "wave",
    // "circle"; anything else straightens it).
    if (has("path")) {
        text.path.clear();
        for (const QVariant& point : get("path").toList()) {
            const QVariantList xy = point.toList();
            if (xy.size() == 2 && text.path.size() < tve::kMaxPathPoints)
                text.path.push_back({std::clamp(xy[0].toDouble(), -1.0, 2.0), std::clamp(xy[1].toDouble(), -1.0, 2.0)});
        }
        if (text.path.size() < 2)
            text.path.clear();
    }
    if (has("pathPreset"))
        text.path = tve::presetPath(get("pathPreset").toString().toStdString());
    if (has("fill")) {
        const QVariantMap fill = get("fill").toMap();
        if (fill.contains(QStringLiteral("kind")))
            text.fill.kind = tve::TextFillKind(indexOf(kFillKinds, fill.value(QStringLiteral("kind")).toString(), int(text.fill.kind)));
        if (fill.contains(QStringLiteral("to")))
            text.fill.to = colorFrom(fill.value(QStringLiteral("to")), text.fill.to);
        if (fill.contains(QStringLiteral("angle")))
            text.fill.angle = std::fmod(fill.value(QStringLiteral("angle")).toDouble(), 360.0);
        if (fill.contains(QStringLiteral("imagePath"))) {
            const QVariant path = fill.value(QStringLiteral("imagePath"));
            const QUrl url = path.toUrl();
            text.fill.imagePath = (url.isLocalFile() ? url.toLocalFile() : path.toString()).toStdString();
        }
    }
    if (has("underline"))
        text.underline = get("underline").toBool();
    if (has("vertical"))
        text.vertical = get("vertical").toBool();
    if (has("curve"))
        text.curve = get("curve").toDouble();
    if (has("depth"))
        text.depth = get("depth").toDouble();
    if (has("depthColor"))
        text.depthColor = colorFrom(get("depthColor"), text.depthColor);
    if (has("tiltX"))
        text.tiltX = get("tiltX").toDouble();
    if (has("tiltY"))
        text.tiltY = get("tiltY").toDouble();
    // Moving with the music: kind and amount (the levels come from measuring the music).
    if (has("react")) {
        const QVariantMap react = get("react").toMap();
        if (react.contains(QStringLiteral("kind")))
            text.react.kind = tve::TextReactKind(indexOf(kReactKinds, react.value(QStringLiteral("kind")).toString(),
                                                         int(text.react.kind)));
        if (react.contains(QStringLiteral("amount")))
            text.react.amount = react.value(QStringLiteral("amount")).toDouble();
        if (react.contains(QStringLiteral("levels"))) {
            text.react.levels.clear();
            for (const QVariant& v : react.value(QStringLiteral("levels")).value<QVariantList>())
                text.react.levels.push_back(float(v.toDouble()));
        }
    }
    // A visualiser: null (or false) takes it off; else its look (the levels come from
    // measuring the music).
    if (has("visualiser")) {
        const QVariant value = get("visualiser");
        const QVariantMap m = value.toMap();
        if (m.isEmpty() && !value.toBool()) {
            text.visualiser.reset();
        } else {
            tve::TextVisualiser v = text.visualiser.value_or(tve::TextVisualiser{});
            if (m.contains(QStringLiteral("style")))
                v.style = tve::VisualiserStyle(indexOf(kVisualiserStyles, m.value(QStringLiteral("style")).toString(), int(v.style)));
            if (m.contains(QStringLiteral("color")))
                v.color = colorFrom(m.value(QStringLiteral("color")), v.color);
            if (m.contains(QStringLiteral("peak")))
                v.peak = colorFrom(m.value(QStringLiteral("peak")), v.peak);
            if (m.contains(QStringLiteral("size")))
                v.size = m.value(QStringLiteral("size")).toDouble();
            text.visualiser = std::move(v);
        }
    }
    // A chart: null (or false) takes it off; else its fields, `data` being "label, value" lines.
    if (has("chart")) {
        const QVariant value = get("chart");
        const QVariantMap m = value.toMap();
        if (m.isEmpty() && !value.toBool()) {
            text.chart.reset();
        } else {
            tve::TextChart chart = text.chart.value_or(tve::TextChart{});
            if (m.contains(QStringLiteral("kind")))
                chart.kind = tve::ChartKind(indexOf(kChartKinds, m.value(QStringLiteral("kind")).toString(), int(chart.kind)));
            if (m.contains(QStringLiteral("points"))) {
                chart.points.clear();
                for (const QVariant& v : m.value(QStringLiteral("points")).value<QVariantList>()) {
                    const QVariantMap point = v.toMap();
                    chart.points.push_back({point.value(QStringLiteral("label")).toString().toStdString(),
                                            point.value(QStringLiteral("value")).toDouble()});
                }
            }
            if (m.contains(QStringLiteral("data")))
                chart.points = parseChartData(m.value(QStringLiteral("data")).toString());
            if (m.contains(QStringLiteral("color")))
                chart.color = colorFrom(m.value(QStringLiteral("color")), chart.color);
            if (m.contains(QStringLiteral("grow")))
                chart.grow = m.value(QStringLiteral("grow")).toDouble();
            if (m.contains(QStringLiteral("values")))
                chart.values = m.value(QStringLiteral("values")).toBool();
            text.chart = std::move(chart);
        }
    }
    return text;
}

TitlePreviewProvider::TitlePreviewProvider() : QQuickImageProvider(QQuickImageProvider::Image) {}

QImage TitlePreviewProvider::requestImage(const QString& id, QSize* size, const QSize&)
{
    const QVariantMap style =
        QJsonDocument::fromJson(QUrl::fromPercentEncoding(id.toUtf8()).toUtf8()).toVariant().toMap();
    const QSize canvas(kWidth, kHeight);
    // An animated title shows the middle of its clip (arrived, not leaving; credits half
    // way), or the moment `t` asks for.
    const double duration = style.value(QStringLiteral("duration"), 5.0).toDouble();
    const double seconds = style.value(QStringLiteral("t"), duration / 2.0).toDouble();
    tve::ClipText text = textFrom(style);
    // A visualiser not measured yet moves to a made-up tune, to show what it looks like.
    if (text.visualiser && text.visualiser->frames.empty()) {
        constexpr int n = tve::TextVisualiser::kBands;
        const int frames = int(std::ceil(duration * tve::TextVisualiser::kRate)) + 1;
        text.visualiser->frames.resize(std::size_t(frames) * n);
        for (int k = 0; k < frames; ++k) {
            const double beat = std::exp(-std::fmod(k / tve::TextVisualiser::kRate, 0.5) / 0.15);
            for (int b = 0; b < n; ++b) {
                const double l = 0.55 * std::exp(-b / 16.0) + 0.25 * (0.5 + 0.5 * std::sin(b * 0.9 + k * 0.35)) + 0.2 * beat;
                text.visualiser->frames[std::size_t(k) * n + std::size_t(b)] = std::uint8_t(std::lround(std::clamp(l, 0.0, 1.0) * 255));
            }
        }
    }
    QImage title = renderTitle(text, canvas, seconds, duration);
    QImage image(canvas, QImage::Format_RGBA8888);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.drawImage(QPointF(style.value(QStringLiteral("x")).toDouble() * kWidth,
                              style.value(QStringLiteral("y")).toDouble() * kHeight), title);
    painter.end();
    *size = image.size();
    return image;
}
