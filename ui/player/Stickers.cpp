#include "Stickers.h"

#include <QCache>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>

#include <algorithm>
#include <map>

namespace stickers {

const QVariantList& catalog()
{
    static const QVariantList list = [] {
        QVariantList result;
        QFile file(QStringLiteral(":/effects/stickers/stickers.json"));
        if (!file.open(QIODevice::ReadOnly))
            return result;
        const QJsonArray entries = QJsonDocument::fromJson(file.readAll()).object().value(QStringLiteral("stickers")).toArray();
        for (const QJsonValue& entry : entries) {
            const QJsonObject o = entry.toObject();
            result << QVariantMap{{QStringLiteral("id"), o.value(QStringLiteral("id")).toString()},
                                  {QStringLiteral("title"), o.value(QStringLiteral("title")).toString()},
                                  {QStringLiteral("category"), o.value(QStringLiteral("category")).toString()},
                                  {QStringLiteral("path"),
                                   QStringLiteral(":/effects/stickers/") + o.value(QStringLiteral("file")).toString()}};
        }
        return result;
    }();
    return list;
}

QString pathOf(const QString& id)
{
    for (const QVariant& entry : catalog()) {
        const QVariantMap m = entry.toMap();
        if (m.value(QStringLiteral("id")).toString() == id)
            return m.value(QStringLiteral("path")).toString();
    }
    return {};
}

QString titleOf(const QString& path)
{
    for (const QVariant& entry : catalog()) {
        const QVariantMap m = entry.toMap();
        if (m.value(QStringLiteral("path")).toString() == path)
            return m.value(QStringLiteral("title")).toString();
    }
    return {};
}

std::unique_ptr<tve::LottieAnimation> open(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return nullptr;
    return tve::LottieAnimation::fromData(file.readAll().toStdString());
}

QImage frame(tve::LottieAnimation& animation, double seconds, int height)
{
    const int h = std::max(1, height);
    const int w = std::max(1, int(double(h) * animation.width() / std::max(1, animation.height())));
    const std::vector<std::uint8_t> pixels = animation.render(seconds, w, h);
    return QImage(pixels.data(), w, h, QImage::Format_RGBA8888).copy();
}

} // namespace stickers

StickerPreviewProvider::StickerPreviewProvider() : QQuickImageProvider(QQuickImageProvider::Image, ForceAsynchronousImageLoading)
{
}

QImage StickerPreviewProvider::requestImage(const QString& id, QSize* size, const QSize&)
{
    static QMutex mutex;
    static QCache<QString, QImage> cache(800);
    static std::map<QString, std::unique_ptr<tve::LottieAnimation>> animations;
    QMutexLocker lock(&mutex);
    if (const QImage* cached = cache.object(id)) {
        *size = cached->size();
        return *cached;
    }
    // <id>@<seconds>
    const qsizetype at = id.lastIndexOf(QLatin1Char('@'));
    const QString sticker = at < 0 ? id : id.left(at);
    const double seconds = at < 0 ? 0.5 : id.mid(at + 1).toDouble();
    auto& animation = animations[sticker];
    if (!animation)
        animation = stickers::open(stickers::pathOf(sticker));
    if (!animation)
        return {};
    const QImage image = stickers::frame(*animation, seconds, kSize);
    cache.insert(id, new QImage(image));
    *size = image.size();
    return image;
}
