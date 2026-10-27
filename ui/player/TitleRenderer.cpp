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

// The text as outlines, laid out on the canvas.
QPainterPath layoutText(const tve::ClipText& text, const QFont& font, QSize canvas)
{
    const double wrapWidth = canvas.width() * 0.9;
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
        option.setWrapMode(QTextOption::WordWrap);
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

    QPainterPath path;
    const double total = lineHeight * double(lines.size());
    double baseline = (canvas.height() - total) / 2.0 + (lineHeight - metrics.height()) / 2.0 + metrics.ascent();
    for (const Line& line : lines) {
        const double width = std::min(line.width, wrapWidth);
        double x = left;
        if (text.alignment == tve::TextAlignment::Center)
            x += (wrapWidth - width) / 2.0;
        else if (text.alignment == tve::TextAlignment::Right)
            x += wrapWidth - width;
        if (!line.text.isEmpty())
            path.addText(QPointF(x, baseline), font, line.text);
        baseline += lineHeight;
    }
    return path;
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

QImage renderTitle(const tve::ClipText& text, QSize canvas)
{
    QImage image(canvas, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    if (canvas.isEmpty() || text.text.empty())
        return image.convertToFormat(QImage::Format_RGBA8888);

    const double pixelSize = text.size * canvas.height();
    const QFont font = titleFont(text, pixelSize);
    const QPainterPath path = layoutText(text, font, canvas);

    if (text.shadow) {
        // A soft drop shadow: drawn at a quarter size, then scaled up smoothly (a cheap blur).
        const QSize small = (QSizeF(canvas) / 4.0).toSize().expandedTo(QSize(1, 1));
        QImage shadow(small, QImage::Format_ARGB32_Premultiplied);
        shadow.fill(Qt::transparent);
        QPainter p(&shadow);
        p.setRenderHint(QPainter::Antialiasing);
        p.scale(0.25, 0.25);
        p.translate(pixelSize * 0.05, pixelSize * 0.07);
        if (text.outlineWidth > 0)
            p.strokePath(path, QPen(QColor(0, 0, 0, 140), text.outlineWidth * pixelSize * 2.0, Qt::SolidLine,
                                    Qt::RoundCap, Qt::RoundJoin));
        p.fillPath(path, QColor(0, 0, 0, 140));
        p.end();
        QPainter painter(&image);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.drawImage(QRect(QPoint(0, 0), canvas), shadow.scaled(small * 2, Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
    }

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    if (text.outlineWidth > 0) {
        // Stroked under the fill, so the outline grows outwards.
        painter.strokePath(path, QPen(toColor(text.outlineColor), text.outlineWidth * pixelSize * 2.0, Qt::SolidLine,
                                      Qt::RoundCap, Qt::RoundJoin));
    }
    painter.fillPath(path, toColor(text.color));
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
    return text;
}

TitlePreviewProvider::TitlePreviewProvider() : QQuickImageProvider(QQuickImageProvider::Image) {}

QImage TitlePreviewProvider::requestImage(const QString& id, QSize* size, const QSize&)
{
    const QVariantMap style =
        QJsonDocument::fromJson(QUrl::fromPercentEncoding(id.toUtf8()).toUtf8()).toVariant().toMap();
    const QSize canvas(kWidth, kHeight);
    QImage title = renderTitle(textFrom(style), canvas);
    QImage image(canvas, QImage::Format_RGBA8888);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.drawImage(QPointF(0, style.value(QStringLiteral("y")).toDouble() * kHeight), title);
    painter.end();
    *size = image.size();
    return image;
}
