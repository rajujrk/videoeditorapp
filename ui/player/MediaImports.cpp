#include "MediaImports.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QImage>
#include <QMap>
#include <QMutex>
#include <QPointer>
#include <QRunnable>
#include <QUrl>

using namespace tve;

namespace {

QMutex& sourcesMutex()
{
    static QMutex mutex;
    return mutex;
}

QMap<QString, std::shared_ptr<ImportSource>>& sources()
{
    static QMap<QString, std::shared_ptr<ImportSource>> instance;
    return instance;
}

QString accessName(ImportAccess access)
{
    return access == ImportAccess::Granted  ? QStringLiteral("granted")
         : access == ImportAccess::Denied ? QStringLiteral("denied")
                                          : QStringLiteral("undetermined");
}

} // namespace

std::shared_ptr<ImportSource> MediaImports::source(const QString& name, bool make)
{
    QMutexLocker lock(&sourcesMutex());
    if (auto found = sources().value(name))
        return found;
    if (!make)
        return nullptr;
    const auto testFolder = [](const char* variable) { return qEnvironmentVariable(variable).toStdString(); };
    std::shared_ptr<ImportSource> made;
    if (name == QLatin1String("photos")) {
        const std::string folder = testFolder("TVE_PHOTOS_SOURCE");
        made = folder.empty() ? makePhotosSource() : makeFolderSource(folder, true);
    } else if (name == QLatin1String("music")) {
        const std::string folder = testFolder("TVE_MUSIC_SOURCE");
        made = folder.empty() ? makeMusicSource() : makeFolderSource(folder, false);
    } else if (name == QLatin1String("devices")) {
        const std::string folder = testFolder("TVE_DEVICE_SOURCE");
        made = folder.empty() ? makeDeviceSource() : makeFolderSource(folder, true);
    }
    if (made)
        sources().insert(name, made);
    return made;
}

MediaImports::MediaImports(QObject* parent) : QObject(parent) {}

MediaImports::~MediaImports()
{
    // Sources outlive this (the thumbnail provider may still use them); stop telling it.
    QMutexLocker lock(&sourcesMutex());
    for (const auto& made : std::as_const(sources()))
        made->onChanged(nullptr);
}

QString MediaImports::access(const QString& name) const
{
    const auto found = source(name);
    return found ? accessName(found->access()) : QStringLiteral("denied");
}

void MediaImports::requestAccess(const QString& name)
{
    const auto found = source(name);
    if (!found)
        return;
    QPointer<MediaImports> self(this);
    found->requestAccess([self](bool) {
        QMetaObject::invokeMethod(qApp, [self] {
            if (self)
                emit self->changed();
        });
    });
}

QVariantList MediaImports::collections(const QString& name) const
{
    QVariantList list;
    const auto found = source(name);
    if (!found)
        return list;
    // Devices say when they come and go.
    found->onChanged([self = QPointer<MediaImports>(const_cast<MediaImports*>(this))] {
        if (self)
            emit self->changed();
    });
    for (const ImportCollection& collection : found->collections())
        list << QVariantMap{{QStringLiteral("id"), QString::fromStdString(collection.id)},
                            {QStringLiteral("title"), QString::fromStdString(collection.title)},
                            {QStringLiteral("count"), collection.count}};
    return list;
}

QVariantList MediaImports::items(const QString& name, const QString& collection) const
{
    QVariantList list;
    const auto found = source(name);
    if (!found)
        return list;
    for (const ImportItem& item : found->items(collection.toStdString())) {
        const QString path = QString::fromStdString(item.localPath);
        QString thumbnail;
        if (!path.isEmpty() && item.kind != "audio")
            thumbnail = QStringLiteral("image://tvethumb/") + QString::fromLatin1(QUrl::toPercentEncoding(path))
                      + QStringLiteral("@%1@160").arg(item.duration * 0.1, 0, 'f', 2);
        else if (path.isEmpty() || name == QLatin1String("music"))
            thumbnail = QStringLiteral("image://tveimport/%1/%2@160")
                            .arg(name, QString::fromLatin1(QUrl::toPercentEncoding(QString::fromStdString(item.id))));
        list << QVariantMap{{QStringLiteral("id"), QString::fromStdString(item.id)},
                            {QStringLiteral("title"), QString::fromStdString(item.title)},
                            {QStringLiteral("detail"), QString::fromStdString(item.detail)},
                            {QStringLiteral("kind"), QString::fromStdString(item.kind)},
                            {QStringLiteral("duration"), item.duration},
                            {QStringLiteral("width"), item.width},
                            {QStringLiteral("height"), item.height},
                            {QStringLiteral("created"), item.created * 1000.0},
                            {QStringLiteral("thumbnail"), thumbnail}};
    }
    return list;
}

bool MediaImports::fetch(const QString& name, const QStringList& itemIds, const QString& folder)
{
    if (fetching() || itemIds.isEmpty() || !source(name))
        return false;
    m_source = name;
    m_queue = itemIds;
    m_folder = folder;
    m_paths.clear();
    m_errors.clear();
    m_fetched = 0;
    m_fetchTotal = int(itemIds.size());
    emit fetchingChanged();
    fetchNext();
    return true;
}

void MediaImports::fetchNext()
{
    if (m_queue.isEmpty()) {
        const QStringList paths = m_paths;
        const QStringList errors = m_errors;
        m_fetchTotal = 0;
        m_fetched = 0;
        emit fetchingChanged();
        emit fetched(paths, errors);
        return;
    }
    const QString itemId = m_queue.takeFirst();
    QPointer<MediaImports> self(this);
    source(m_source)->fetch(itemId.toStdString(), m_folder.toStdString(), [self](std::string path, std::string error) {
        QMetaObject::invokeMethod(qApp, [self, path = QString::fromStdString(path), error = QString::fromStdString(error)] {
            if (!self)
                return;
            if (path.isEmpty())
                self->m_errors << error;
            else
                self->m_paths << path;
            ++self->m_fetched;
            emit self->fetchingChanged();
            self->fetchNext();
        });
    });
}

void MediaImports::openPrivacySettings(const QString& name) const
{
    const QString pane = name == QLatin1String("music") ? QStringLiteral("Privacy_Media") : QStringLiteral("Privacy_Photos");
    QDesktopServices::openUrl(QUrl(QStringLiteral("x-apple.systempreferences:com.apple.preference.security?") + pane));
}

// --- Thumbnails ---

namespace {

class ImportThumbnailResponse : public QQuickImageResponse, public QRunnable {
public:
    explicit ImportThumbnailResponse(QString id) : m_id(std::move(id)) { setAutoDelete(false); }

    QQuickTextureFactory* textureFactory() const override { return QQuickTextureFactory::textureFactoryForImage(m_image); }

    void run() override
    {
        // <source>/<url-encoded item id>@<size>
        const qsizetype slash = m_id.indexOf(QLatin1Char('/'));
        const qsizetype at = m_id.lastIndexOf(QLatin1Char('@'));
        if (slash > 0 && at > slash) {
            const auto found = MediaImports::source(m_id.left(slash), false);
            const std::string item = QUrl::fromPercentEncoding(m_id.mid(slash + 1, at - slash - 1).toUtf8()).toStdString();
            if (found) {
                if (auto picture = found->thumbnail(item, std::clamp(m_id.mid(at + 1).toInt(), 16, 1024)))
                    m_image = QImage(picture->rgba.data(), picture->width, picture->height, QImage::Format_RGBA8888_Premultiplied).copy();
            }
        }
        emit finished();
    }

private:
    QString m_id;
    QImage m_image;
};

} // namespace

ImportThumbnailProvider::ImportThumbnailProvider()
{
    m_pool.setMaxThreadCount(3);
}

QQuickImageResponse* ImportThumbnailProvider::requestImageResponse(const QString& id, const QSize&)
{
    auto* response = new ImportThumbnailResponse(id);
    m_pool.start(response);
    return response;
}
