#include "TitleRenderer.h"

#include <QFontDatabase>
#include <QJsonDocument>
#include <QUrl>
#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>
#include <QTextLayout>

#include <algorithm>
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

// One visible character of a title, laid out on the canvas, with the units it belongs to.
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
TitleLayout layoutText(const tve::ClipText& text, const QFont& font, QSize canvas)
{
    const bool wrap = text.scroll != tve::TextScroll::CrawlLeft;
    const double wrapWidth = wrap ? canvas.width() * 0.9 : 1e7;
    const double left = canvas.width() * 0.05;
    const QFontMetricsF metrics(font);
    const double lineHeight = metrics.height() * text.lineSpacing;

    struct Line {
        QString text;
        double width;
    };
    std::vector<Line> lines;
    for (const QString& paragraph : QString::fromStdString(text.text).split(QLatin1Char('\n'))) {
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
            // Keep surrogate pairs together.
            const int length = (c.isHighSurrogate() && i + 1 < line.text.size()) ? 2 : 1;
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
        baseline += lineHeight;
    }
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

QImage renderTitle(const tve::ClipText& text, QSize canvas, double seconds, double duration)
{
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
        d.transform = d.transform * scroll;
    }
    // A caption following its speech: the words said so far, and the one being said (the last
    // started, until the next starts). Layout words and caption words match one for one.
    std::vector<QColor> fills(layout.glyphs.size(), toColor(text.color));
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
                    fills[g] = highlight;
                break;
            case tve::CaptionMotion::Pop:
                if (saying) {
                    fills[g] = highlight;
                    // Grows to 115 % over its first tenth of a second.
                    const double into = tve::toSeconds(now - text.words[std::size_t(word)].start);
                    tve::TextUnitState pop;
                    pop.scale = 1.0 + 0.15 * std::clamp(into / 0.1, 0.0, 1.0);
                    apply(drawn[g], pop, layout.words[std::size_t(word)], em);
                }
                break;
            case tve::CaptionMotion::Karaoke:
                if (said)
                    fills[g] = highlight;
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

    // The shape moves with the text as a whole (in and out, not the loop).
    Drawn shapeDrawn;
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
    drawGlyphs(painter, [&](QPainter& p, const QPainterPath& path, std::size_t g) {
        // The outline is stroked under the fill, so it grows outwards.
        if (text.outlineWidth > 0)
            p.strokePath(path, QPen(toColor(text.outlineColor), text.outlineWidth * em * 2.0, Qt::SolidLine, Qt::RoundCap,
                                    Qt::RoundJoin));
        p.fillPath(path, fills[g]);
    });
    painter.end();
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
constexpr const char* kCaptionMotions[] = {"none", "highlight", "pop", "karaoke", "reveal"};

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

} // namespace

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
    QImage title = renderTitle(textFrom(style), canvas, seconds, duration);
    QImage image(canvas, QImage::Format_RGBA8888);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.drawImage(QPointF(style.value(QStringLiteral("x")).toDouble() * kWidth,
                              style.value(QStringLiteral("y")).toDouble() * kHeight), title);
    painter.end();
    *size = image.size();
    return image;
}
