#include "OverlayTransitions.h"

#include "Language.h"
#include "encode/MediaWriter.h"
#include "model/Paths.h"

#include <QCoreApplication>
#include <QCache>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <QStandardPaths>

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <random>

namespace overlays {

namespace {

// Bumped when the drawing changes, so old movies in the cache are not used.
constexpr int kVersion = 1;

double clamp01(double v) { return std::clamp(v, 0.0, 1.0); }
double smooth(double edge0, double edge1, double v)
{
    const double t = clamp01((v - edge0) / (edge1 - edge0));
    return t * t * (3.0 - 2.0 * t);
}
// Up to full at the cut (half way) and back down.
double envelope(double t) { return std::pow(std::sin(std::numbers::pi * clamp01(t)), 1.5); }

QColor withAlpha(QColor c, double alpha)
{
    c.setAlphaF(float(clamp01(alpha)));
    return c;
}

// Soft glows of colour drifting across, brightest at the cut, with a wash over all of it
// there to hide the cut.
void lightLeak(QPainter& p, QSizeF size, double t, const std::array<QColor, 3>& colours)
{
    const double e = envelope(t);
    const double w = size.width(), h = size.height();
    p.setCompositionMode(QPainter::CompositionMode_Plus);
    p.setPen(Qt::NoPen);
    for (int i = 0; i < 3; ++i) {
        const QPointF centre((-0.25 + 0.42 * i + 0.55 * t) * w, (0.3 + 0.28 * std::sin(i * 2.1 + t * 2.4)) * h);
        const double radius = (0.36 + 0.14 * i) * w * (0.6 + 0.6 * e);
        QRadialGradient glow(centre, radius);
        glow.setColorAt(0.0, withAlpha(colours[std::size_t(i)], 0.95 * std::sqrt(e)));
        glow.setColorAt(0.45, withAlpha(colours[std::size_t(i)], 0.4 * e));
        glow.setColorAt(1.0, withAlpha(colours[std::size_t(i)], 0.0));
        p.setBrush(glow);
        p.drawEllipse(centre, radius, radius);
    }
    p.fillRect(QRectF(QPointF(0, 0), size), withAlpha(colours[2], 0.35 * std::pow(e, 4.0)));
}

// Hot blotches eating in from the right edge: white at the core, orange, then deep red,
// spreading over the frame at the cut and fading after.
void filmBurn(QPainter& p, QSizeF size, double t)
{
    const double w = size.width(), h = size.height();
    const double fade = smooth(0.0, 0.2, t) * (1.0 - smooth(0.68, 1.0, t));
    const double grow = smooth(0.0, 0.55, t);
    std::mt19937 random(7); // the same blotches every time
    std::uniform_real_distribution<double> unit(0.0, 1.0);
    p.setCompositionMode(QPainter::CompositionMode_Plus);
    p.setPen(Qt::NoPen);
    for (int i = 0; i < 8; ++i) {
        const double x = 0.62 + 0.5 * unit(random), y = unit(random), r0 = 0.25 + 0.3 * unit(random);
        const double start = 0.25 * unit(random);
        const double g = clamp01((grow - start) / (1.0 - start));
        const QPointF centre(x * w - 0.25 * w * g, y * h);
        const double radius = r0 * w * (0.12 + 1.4 * g);
        QRadialGradient blotch(centre, radius);
        blotch.setColorAt(0.0, QColor::fromRgbF(1.0f, 0.95f, 0.82f, float(fade)));
        blotch.setColorAt(0.3, QColor::fromRgbF(1.0f, 0.55f, 0.1f, float(0.9 * fade)));
        blotch.setColorAt(0.7, QColor::fromRgbF(0.75f, 0.14f, 0.0f, float(0.55 * fade)));
        blotch.setColorAt(1.0, QColor::fromRgbF(0.5f, 0.05f, 0.0f, 0.0f));
        p.setBrush(blotch);
        p.drawEllipse(centre, radius, radius);
    }
    p.fillRect(QRectF(QPointF(0, 0), size), QColor::fromRgbF(1.0f, 0.9f, 0.75f, float(0.8 * std::pow(envelope(t), 4.0))));
}

// A broad brush going back and forth down the frame, covering all of it at the cut, then
// lifting off from where it began.
void paintStroke(QPainter& p, QSizeF size, double t, const QColor& colour)
{
    const double w = size.width(), h = size.height();
    tve::Polyline zigzag;
    for (int row = 0; row < 5; ++row) {
        const double y = 0.1 + 0.2 * row;
        zigzag.push_back({row % 2 == 0 ? -0.1 : 1.1, y});
        zigzag.push_back({row % 2 == 0 ? 1.1 : -0.1, y});
    }
    const double aspect = w / h;
    const auto ease = [](double v) { v = clamp01(v); return v * v * (3.0 - 2.0 * v); };
    const double to = ease(t / 0.5), from = ease((t - 0.5) / 0.5);
    if (to <= from)
        return;
    constexpr int kSamples = 600;
    QPainterPath path;
    for (int i = int(std::floor(from * kSamples)); i <= int(std::ceil(to * kSamples)); ++i) {
        const tve::PathPoint at = tve::pointAlong(zigzag, std::clamp(double(i) / kSamples, from, to), aspect);
        const QPointF point(at.x * w, at.y * h);
        if (path.elementCount() == 0)
            path.moveTo(point);
        else
            path.lineTo(point);
    }
    // A few strokes a little apart for the bristles' ragged edges.
    const double width = 0.29 * h;
    std::mt19937 random(11);
    std::uniform_real_distribution<double> jitter(-1.0, 1.0);
    p.setCompositionMode(QPainter::CompositionMode_SourceOver);
    p.setBrush(Qt::NoBrush);
    for (int i = 0; i < 6; ++i) {
        QPen pen(colour, width * (0.86 + 0.14 * double(i) / 5.0), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        p.setPen(pen);
        p.drawPath(path.translated(0.0, jitter(random) * 0.05 * h));
    }
    // Dry streaks along both edges, broken where the brush ran out.
    std::uniform_real_distribution<double> unit(0.0, 1.0);
    for (int i = 0; i < 12; ++i) {
        const double side = i % 2 == 0 ? -1.0 : 1.0;
        QPen streak(colour, (0.008 + 0.014 * unit(random)) * h, Qt::CustomDashLine, Qt::FlatCap, Qt::RoundJoin);
        streak.setDashPattern({4 + 30 * unit(random), 1 + 6 * unit(random), 10 + 40 * unit(random), 2 + 8 * unit(random)});
        p.setPen(streak);
        p.drawPath(path.translated(0.0, side * (0.142 + 0.03 * unit(random)) * h));
    }
}

// Named for its title (as it shows in the media bin), in a folder for this version.
QString versionDirectory()
{
    return cacheDirectory() + QStringLiteral("/v%1").arg(kVersion);
}

} // namespace

const std::vector<Kind>& kinds()
{
    static const std::vector<Kind> list{
        {"leak.warm", QT_TRANSLATE_NOOP("catalogue", "Warm light leak"), tve::BlendMode::Screen},
        {"leak.cool", QT_TRANSLATE_NOOP("catalogue", "Cool light leak"), tve::BlendMode::Screen},
        {"burn", QT_TRANSLATE_NOOP("catalogue", "Film burn"), tve::BlendMode::Screen},
        {"paint.white", QT_TRANSLATE_NOOP("catalogue", "White paint stroke"), tve::BlendMode::Normal},
        {"paint.black", QT_TRANSLATE_NOOP("catalogue", "Black paint stroke"), tve::BlendMode::Normal},
    };
    return list;
}

const Kind* find(const std::string& id)
{
    const auto it = std::ranges::find(kinds(), id, &Kind::id);
    return it == kinds().end() ? nullptr : &*it;
}

QImage frame(const std::string& id, double t, QSize size)
{
    QImage image(size, QImage::Format_RGBA8888_Premultiplied);
    image.fill(Qt::transparent);
    {
        QPainter p(&image);
        p.setRenderHint(QPainter::Antialiasing);
        const QSizeF canvas(size);
        if (id == "leak.warm")
            lightLeak(p, canvas, t, {QColor(255, 140, 40), QColor(255, 70, 40), QColor(255, 214, 130)});
        else if (id == "leak.cool")
            lightLeak(p, canvas, t, {QColor(70, 150, 255), QColor(170, 90, 255), QColor(150, 235, 235)});
        else if (id == "burn")
            filmBurn(p, canvas, t);
        else if (id == "paint.white")
            paintStroke(p, canvas, t, QColor(246, 243, 236));
        else if (id == "paint.black")
            paintStroke(p, canvas, t, QColor(18, 18, 20));
    }
    return image.convertToFormat(QImage::Format_RGBA8888);
}

QString cacheDirectory()
{
    const QByteArray override = qgetenv("TVE_OVERLAY_CACHE_DIR");
    return !override.isEmpty() ? QString::fromLocal8Bit(override)
                               : QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/Overlay cache");
}

QString movie(const std::string& id, QString* error)
{
    const auto failed = [&](const QString& why) {
        if (error)
            *error = why;
        return QString();
    };
    if (!find(id))
        return failed(QCoreApplication::translate("OverlayTransitions", "Unknown overlay %1").arg(QString::fromStdString(id)));
    static QMutex mutex;
    QMutexLocker lock(&mutex);
    const QString path = versionDirectory() + QLatin1Char('/') + QString::fromStdString(find(id)->title) + QStringLiteral(".mov");
    if (QFileInfo(path).size() > 0)
        return path;
    if (!QDir().mkpath(versionDirectory()))
        return failed(QCoreApplication::translate("OverlayTransitions", "Cannot make the overlay cache folder"));

    tve::MediaSettings settings;
    settings.container = tve::Container::Mov;
    settings.codec = tve::VideoCodec::ProRes;
    settings.proRes = tve::ProResProfile::P4444;
    settings.alpha = true;
    settings.audio = false;
    settings.width = kWidth;
    settings.height = kHeight;
    settings.frameRate = {kFramesPerSecond, 1};
    // Written beside it and moved into place when whole.
    const QString part = path + QStringLiteral(".part");
    QFile::remove(part);
    std::string why;
    auto writer = tve::MediaWriter::open(part.toStdString(), settings, &why);
    if (!writer)
        return failed(QCoreApplication::translate("OverlayTransitions", "Cannot make the overlay: %1").arg(coreText(why)));
    const int frames = int(std::lround(kSeconds * kFramesPerSecond));
    for (int i = 0; i < frames; ++i) {
        const QImage picture = frame(id, (i + 0.5) / frames, QSize(kWidth, kHeight));
        if (!writer->writeVideoRgba(picture.constBits(), int(picture.bytesPerLine()))) {
            QFile::remove(part);
            return failed(QCoreApplication::translate("OverlayTransitions", "Cannot make the overlay: %1").arg(coreText(writer->lastError())));
        }
    }
    if (!writer->finish() || !QFile::rename(part, path)) {
        QFile::remove(part);
        return failed(QCoreApplication::translate("OverlayTransitions", "Cannot make the overlay: %1").arg(coreText(writer->lastError())));
    }
    return path;
}

} // namespace overlays

OverlayPreviewProvider::OverlayPreviewProvider() : QQuickImageProvider(QQuickImageProvider::Image) {}

QImage OverlayPreviewProvider::requestImage(const QString& id, QSize* size, const QSize&)
{
    static QMutex mutex;
    static QCache<QString, QImage> cache(400);
    QMutexLocker lock(&mutex);
    if (const QImage* cached = cache.object(id)) {
        *size = cached->size();
        return *cached;
    }
    // <id>@<t>: over a sample cut, a cool shot before half way and a green one after.
    const qsizetype at = id.lastIndexOf(QLatin1Char('@'));
    const std::string overlay = (at < 0 ? id : id.left(at)).toStdString();
    const double t = at < 0 ? 0.3 : id.mid(at + 1).toDouble();
    const overlays::Kind* kind = overlays::find(overlay);
    if (!kind)
        return {};
    const QSize canvas(320, 180);
    QImage image(canvas, QImage::Format_RGBA8888_Premultiplied);
    {
        QPainter p(&image);
        QLinearGradient shot(0, 0, 0, canvas.height());
        shot.setColorAt(0, t < 0.5 ? QColor(58, 84, 120) : QColor(70, 112, 72));
        shot.setColorAt(1, t < 0.5 ? QColor(24, 34, 52) : QColor(30, 46, 30));
        p.fillRect(image.rect(), shot);
        p.setCompositionMode(kind->blend == tve::BlendMode::Screen ? QPainter::CompositionMode_Screen
                                                                   : QPainter::CompositionMode_SourceOver);
        p.drawImage(0, 0, overlays::frame(overlay, t, canvas).convertToFormat(QImage::Format_RGBA8888_Premultiplied));
    }
    cache.insert(id, new QImage(image));
    *size = image.size();
    return image;
}
