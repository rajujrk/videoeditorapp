#include "Favourites.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>

#include <algorithm>

namespace {

QString keyOf(const QString& kind, const QString& id)
{
    return kind + QLatin1Char(':') + id;
}

} // namespace

Favourites::Favourites(QObject* parent) : QObject(parent)
{
    m_file = qEnvironmentVariable("TVE_FAVOURITES_FILE");
    if (m_file.isEmpty())
        m_file = QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath(QStringLiteral("favourites.json"));
    load();
}

QStringList Favourites::cleaned(const QVariant& tags)
{
    QStringList given = tags.typeId() == QMetaType::QString ? tags.toString().split(QLatin1Char(',')) : tags.toStringList();
    QStringList out;
    for (QString tag : given) {
        tag = tag.simplified().toLower().left(32);
        if (!tag.isEmpty() && !out.contains(tag))
            out << tag;
    }
    return out;
}

bool Favourites::isFavourite(const QString& kind, const QString& id) const
{
    return m_items.contains(keyOf(kind, id));
}

bool Favourites::toggle(const QString& kind, const QString& id)
{
    const QString key = keyOf(kind, id);
    const bool now = !m_items.contains(key);
    if (now)
        m_items.insert(key, QStringList());
    else
        m_items.remove(key);
    save();
    emit changed();
    return now;
}

void Favourites::setTags(const QString& kind, const QString& id, const QVariant& tags)
{
    m_items.insert(keyOf(kind, id), cleaned(tags));
    save();
    emit changed();
}

QStringList Favourites::tagsOf(const QString& kind, const QString& id) const
{
    return m_items.value(keyOf(kind, id)).toStringList();
}

QStringList Favourites::tagsFor(const QString& kind) const
{
    QStringList tags;
    const QString prefix = kind + QLatin1Char(':');
    for (auto it = m_items.begin(); it != m_items.end(); ++it)
        if (it.key().startsWith(prefix))
            for (const QString& tag : it.value().toStringList())
                if (!tags.contains(tag))
                    tags << tag;
    std::ranges::sort(tags);
    return tags;
}

bool Favourites::has(const QString& kind, const QString& id, const QString& tag) const
{
    const auto found = m_items.find(keyOf(kind, id));
    return found != m_items.end() && (tag.isEmpty() || found.value().toStringList().contains(tag));
}

bool Favourites::tagMatches(const QString& kind, const QString& id, const QString& text) const
{
    const QString wanted = text.trimmed().toLower();
    if (wanted.isEmpty())
        return false;
    const QStringList tags = tagsOf(kind, id);
    return std::ranges::any_of(tags, [&](const QString& tag) { return tag.contains(wanted); });
}

void Favourites::load()
{
    QFile file(m_file);
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QJsonObject favourites = QJsonDocument::fromJson(file.readAll()).object().value(QStringLiteral("favourites")).toObject();
    for (auto it = favourites.begin(); it != favourites.end(); ++it)
        if (it.key().contains(QLatin1Char(':')))
            m_items.insert(it.key(), cleaned(it.value().toVariant()));
}

void Favourites::save()
{
    QDir().mkpath(QFileInfo(m_file).path());
    QSaveFile file(m_file);
    if (!file.open(QIODevice::WriteOnly))
        return;
    file.write(QJsonDocument(QJsonObject{{QStringLiteral("version"), 1},
                                         {QStringLiteral("favourites"), QJsonObject::fromVariantMap(m_items)}})
                   .toJson());
    file.commit();
}
