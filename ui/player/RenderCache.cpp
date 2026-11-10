#include "RenderCache.h"

#include "Exporter.h"
#include "io/ProjectSerializer.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>

#include <algorithm>
#include <mutex>

namespace {

// The index of render files: [{key, from, to, used}] (times in ticks, `used` in ms), kept
// beside them. One lock for every reader and writer in the process.
std::mutex& indexLock()
{
    static std::mutex lock;
    return lock;
}

QString indexPath()
{
    return RenderCache::directory() + QStringLiteral("/index.json");
}

QJsonArray readIndex()
{
    QFile file(indexPath());
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(file.readAll()).object().value(QStringLiteral("stretches")).toArray();
}

void writeIndex(const QJsonArray& entries)
{
    QDir().mkpath(RenderCache::directory());
    QSaveFile file(indexPath());
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(QJsonObject{{QStringLiteral("stretches"), entries}}).toJson(QJsonDocument::Compact));
        file.commit();
    }
}

QString fileFor(const QString& key, tve::ProjectColour colour)
{
    // HDR stretches as 10-bit HEVC (its colour kept), the rest as ProRes 422.
    return RenderCache::directory() + QLatin1Char('/') + key
         + (colour == tve::ProjectColour::Sdr ? QStringLiteral(".mov") : QStringLiteral(".mp4"));
}

} // namespace

QString RenderCache::directory()
{
    const QString overridden = qEnvironmentVariable("TVE_RENDER_CACHE_DIR");
    return overridden.isEmpty()
             ? QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/Render Files")
             : overridden;
}

QString RenderCache::keyOf(const tve::Project& project, tve::Ticks from, tve::Ticks to)
{
    const std::string content = tve::renderedContent(project, from, to);
    return QString::fromLatin1(
        QCryptographicHash::hash(QByteArray::fromStdString(content), QCryptographicHash::Sha256).toHex().left(32));
}

std::vector<tve::RenderedStretch> RenderCache::validFor(const tve::Project& project) const
{
    std::lock_guard lock(indexLock());
    std::vector<tve::RenderedStretch> valid;
    const tve::Ticks duration = project.duration();
    QJsonArray entries = readIndex();
    bool touched = false;
    for (qsizetype i = 0; i < entries.size(); ++i) {
        QJsonObject entry = entries[i].toObject();
        const tve::Ticks from = entry.value(QStringLiteral("from")).toInteger();
        const tve::Ticks to = entry.value(QStringLiteral("to")).toInteger();
        if (from < 0 || to <= from || to > duration)
            continue;
        const QString key = entry.value(QStringLiteral("key")).toString();
        const QString path = fileFor(key, project.settings.colour);
        if (key != keyOf(project, from, to) || !QFileInfo::exists(path))
            continue;
        valid.push_back({from, to, path.toStdString()});
        entry[QStringLiteral("used")] = QDateTime::currentMSecsSinceEpoch();
        entries[i] = entry;
        touched = true;
    }
    if (touched)
        writeIndex(entries);
    std::sort(valid.begin(), valid.end(), [](const auto& a, const auto& b) { return a.from < b.from; });
    return valid;
}

bool RenderCache::render(std::shared_ptr<const tve::Project> project, tve::Ticks from, tve::Ticks to,
                         const std::function<bool(double)>& progress, QString* error)
{
    const QString key = keyOf(*project, from, to);
    const QString path = fileFor(key, project->settings.colour);
    QDir().mkpath(directory());
    if (!QFileInfo::exists(path)) {
        Exporter::Options options;
        tve::MediaSettings& settings = options.settings;
        settings.width = project->settings.width;
        settings.height = project->settings.height;
        settings.frameRate = project->settings.frameRate;
        settings.audio = false;
        if (project->settings.colour == tve::ProjectColour::Sdr) {
            settings.container = tve::Container::Mov;
            settings.codec = tve::VideoCodec::ProRes;
            settings.proRes = tve::ProResProfile::Standard;
        } else {
            settings.container = tve::Container::Mp4;
            settings.codec = tve::VideoCodec::Hevc;
            settings.colour = project->settings.colour;
            settings.keyframeInterval = 10; // quick to seek in
            settings.videoBitrate = std::int64_t(project->settings.width) * project->settings.height * 12;
        }
        options.from = from;
        options.to = to;
        const QString partial = path + QStringLiteral(".part") + QLatin1Char('.') + QFileInfo(path).suffix();
        const Exporter::Result result = Exporter::run(project, partial, options, progress);
        if (!result.ok) {
            if (error)
                *error = result.error;
            return false;
        }
        QFile::remove(path);
        if (!QFile::rename(partial, path)) {
            QFile::remove(partial);
            if (error)
                *error = QStringLiteral("cannot keep the render file");
            return false;
        }
    }
    std::lock_guard lock(indexLock());
    QJsonArray entries = readIndex();
    for (qsizetype i = entries.size() - 1; i >= 0; --i)
        if (entries[i].toObject().value(QStringLiteral("key")).toString() == key)
            entries.removeAt(i);
    entries.append(QJsonObject{{QStringLiteral("key"), key},
                               {QStringLiteral("from"), qint64(from)},
                               {QStringLiteral("to"), qint64(to)},
                               {QStringLiteral("used"), QDateTime::currentMSecsSinceEpoch()}});
    // The least recently used go past the limit.
    QFileInfoList files = QDir(directory()).entryInfoList({QStringLiteral("*.mov"), QStringLiteral("*.mp4")}, QDir::Files);
    qint64 total = 0;
    for (const QFileInfo& info : files)
        total += info.size();
    std::vector<std::pair<qint64, QString>> byUse;
    for (const QJsonValue& value : entries)
        byUse.push_back({value.toObject().value(QStringLiteral("used")).toInteger(), value.toObject().value(QStringLiteral("key")).toString()});
    std::sort(byUse.begin(), byUse.end());
    for (const auto& [used, oldKey] : byUse) {
        if (total <= kMaxBytes || oldKey == key)
            break;
        for (const QString& suffix : {QStringLiteral(".mov"), QStringLiteral(".mp4")}) {
            const QFileInfo old(directory() + QLatin1Char('/') + oldKey + suffix);
            if (old.exists()) {
                total -= old.size();
                QFile::remove(old.filePath());
            }
        }
        for (qsizetype i = entries.size() - 1; i >= 0; --i)
            if (entries[i].toObject().value(QStringLiteral("key")).toString() == oldKey)
                entries.removeAt(i);
    }
    writeIndex(entries);
    return true;
}

void RenderCache::clear()
{
    std::lock_guard lock(indexLock());
    QDir dir(directory());
    for (const QFileInfo& info : dir.entryInfoList({QStringLiteral("*.mov"), QStringLiteral("*.mp4"), QStringLiteral("index.json")}, QDir::Files))
        QFile::remove(info.filePath());
}

qint64 RenderCache::size()
{
    qint64 total = 0;
    for (const QFileInfo& info : QDir(directory()).entryInfoList({QStringLiteral("*.mov"), QStringLiteral("*.mp4")}, QDir::Files))
        total += info.size();
    return total;
}
