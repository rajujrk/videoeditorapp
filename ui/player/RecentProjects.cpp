#include "RecentProjects.h"

#include "StillRenderer.h"

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
#include <QUrl>

#include <algorithm>

namespace {

QString key(const QString& path)
{
    return QFileInfo(path).absoluteFilePath();
}

QString text(const char* s)
{
    return QString::fromLatin1(s);
}

} // namespace

RecentProjects::RecentProjects(QObject* parent) : QObject(parent)
{
    m_file = qEnvironmentVariable("TVE_RECENT_PROJECTS_FILE");
    if (m_file.isEmpty())
        m_file = QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath(QStringLiteral("recent-projects.json"));
    m_pool.setMaxThreadCount(1);
    reload();
}

RecentProjects::~RecentProjects()
{
    m_pool.clear();
    m_pool.waitForDone();
}

void RecentProjects::reload()
{
    m_entries.clear();
    QFile file(m_file);
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QJsonArray projects = QJsonDocument::fromJson(file.readAll()).object().value(text("projects")).toArray();
    QStringList seen;
    for (const QJsonValue& value : projects) {
        const QJsonObject entry = value.toObject();
        const QString path = entry.value(text("path")).toString();
        if (path.isEmpty() || seen.contains(key(path)))
            continue;
        seen << key(path);
        m_entries << entry.toVariantMap();
        if (m_entries.size() >= kMaximum)
            break;
    }
}

void RecentProjects::save() const
{
    QDir().mkpath(QFileInfo(m_file).path());
    QJsonArray projects;
    for (const QVariant& entry : m_entries)
        projects << QJsonObject::fromVariantMap(entry.toMap());
    QSaveFile out(m_file);
    if (out.open(QIODevice::WriteOnly)) {
        out.write(QJsonDocument(QJsonObject{{text("version"), 1}, {text("projects"), projects}}).toJson());
        out.commit();
    }
}

QString RecentProjects::coverPath(const QString& path) const
{
    const QByteArray hash = QCryptographicHash::hash(key(path).toUtf8(), QCryptographicHash::Sha1).toHex();
    return QFileInfo(m_file).dir().filePath(QStringLiteral("Recent Covers/%1.png").arg(QString::fromLatin1(hash)));
}

QVariantList RecentProjects::list() const
{
    QVariantList out;
    for (const QVariant& value : m_entries) {
        QVariantMap entry = value.toMap();
        const QString path = entry.value(text("path")).toString();
        const QFileInfo info(path);
        const QFileInfo cover(coverPath(path));
        entry.insert(text("missing"), !info.exists());
        entry.insert(text("folder"), info.path());
        QString coverUrl;
        if (cover.exists()) {
            QUrl url = QUrl::fromLocalFile(cover.filePath());
            url.setQuery(QStringLiteral("v=%1").arg(cover.lastModified().toMSecsSinceEpoch())); // a new one is reloaded
            coverUrl = url.toString();
        }
        entry.insert(text("cover"), coverUrl);
        out << entry;
    }
    return out;
}

void RecentProjects::note(const QString& path, const tve::Project& project, bool freshCover)
{
    reload(); // (another window or copy of the app may have changed it since)
    const QString absolute = key(path);
    for (qsizetype i = m_entries.size() - 1; i >= 0; --i)
        if (key(m_entries.at(i).toMap().value(text("path")).toString()) == absolute)
            m_entries.removeAt(i);
    const tve::Ticks duration = project.duration();
    m_entries.prepend(QVariantMap{
        {text("path"), absolute},
        {text("name"), QFileInfo(absolute).completeBaseName()},
        {text("lastOpened"), double(QDateTime::currentMSecsSinceEpoch())},
        {text("width"), project.settings.width},
        {text("height"), project.settings.height},
        {text("duration"), tve::toSeconds(duration)},
    });
    while (m_entries.size() > kMaximum) {
        QFile::remove(coverPath(m_entries.last().toMap().value(text("path")).toString()));
        m_entries.removeLast();
    }
    save();
    emit changed();

    const QFileInfo cover(coverPath(absolute));
    if (duration <= 0) {
        QFile::remove(cover.filePath()); // nothing to show (any old cover is out of date)
        return;
    }
    if (freshCover || !cover.exists() || cover.lastModified() < QFileInfo(absolute).lastModified())
        renderCover(absolute, std::make_shared<const tve::Project>(project));
}

void RecentProjects::renderCover(const QString& path, std::shared_ptr<const tve::Project> project)
{
    const QString out = coverPath(path);
    m_pool.start([this, out, project = std::move(project)] {
        const int w = std::max(1, project->settings.width), h = std::max(1, project->settings.height);
        const double scale = 384.0 / std::max(w, h);
        const QSize size(std::max(2, int(std::lround(w * scale)) & ~1), std::max(2, int(std::lround(h * scale)) & ~1));
        StillRenderer renderer;
        if (!renderer.isValid())
            return;
        const QImage frame = renderer.render(project, project->duration() / 3, size);
        if (frame.isNull() || !QDir().mkpath(QFileInfo(out).path()))
            return;
        QSaveFile file(out);
        if (!file.open(QIODevice::WriteOnly) || !frame.save(&file, "PNG") || !file.commit())
            return;
        QMetaObject::invokeMethod(this, &RecentProjects::changed, Qt::QueuedConnection);
    });
}

bool RecentProjects::remove(const QString& path)
{
    reload();
    const QString absolute = key(path);
    bool removed = false;
    for (qsizetype i = m_entries.size() - 1; i >= 0; --i) {
        if (key(m_entries.at(i).toMap().value(text("path")).toString()) == absolute) {
            m_entries.removeAt(i);
            removed = true;
        }
    }
    if (!removed)
        return false;
    QFile::remove(coverPath(absolute));
    save();
    emit changed();
    return true;
}
