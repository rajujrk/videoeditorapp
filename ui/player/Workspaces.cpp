#include "Workspaces.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>

#include <algorithm>

namespace {

struct BuiltIn {
    const char* id;
    const char* name;
    int library;
    int inspector;
    double timeline;
};

// Editing as it opens; a wide media library to sort clips; the inspector wide for colour
// (its scopes and wheels); a tall timeline for sound; the picture as large as it goes.
constexpr BuiltIn kBuiltIn[] = {
    {"editing", QT_TRANSLATE_NOOP("Workspaces", "Editing"), 340, 320, 0.36},
    {"organising", QT_TRANSLATE_NOOP("Workspaces", "Organising"), 620, 0, 0.3},
    {"colour", QT_TRANSLATE_NOOP("Workspaces", "Colour"), 0, 460, 0.28},
    {"audio", QT_TRANSLATE_NOOP("Workspaces", "Audio"), 300, 320, 0.55},
    {"preview", QT_TRANSLATE_NOOP("Workspaces", "Big preview"), 0, 0, 0.2},
};

QVariantMap builtIn(const BuiltIn& b)
{
    return {{QStringLiteral("id"), QString::fromLatin1(b.id)},
            {QStringLiteral("name"), QCoreApplication::translate("Workspaces", b.name)},
            {QStringLiteral("builtIn"), true},
            {QStringLiteral("library"), b.library},
            {QStringLiteral("inspector"), b.inspector},
            {QStringLiteral("timeline"), b.timeline}};
}

} // namespace

Workspaces::Workspaces(QObject* parent) : QObject(parent)
{
    m_file = qEnvironmentVariable("TVE_WORKSPACES_FILE");
    if (m_file.isEmpty())
        m_file = QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath(QStringLiteral("workspaces.json"));
    load();
}

bool Workspaces::previewWindowed() const
{
    return qEnvironmentVariableIntValue("TVE_PREVIEW_WINDOWED") != 0;
}

QVariantList Workspaces::layouts() const
{
    QVariantList list;
    for (const BuiltIn& b : kBuiltIn)
        list << builtIn(b);
    return list + m_custom;
}

QVariantMap Workspaces::layout(const QString& id) const
{
    for (const QVariant& entry : layouts())
        if (entry.toMap().value(QStringLiteral("id")).toString() == id)
            return entry.toMap();
    return {};
}

void Workspaces::setCurrent(const QString& id)
{
    if (layout(id).isEmpty() || id == m_current)
        return;
    m_current = id;
    write();
    emit changed();
}

QString Workspaces::save(const QString& name, int library, int inspector, double timeline)
{
    const QString trimmed = name.simplified().left(40);
    if (trimmed.isEmpty())
        return {};
    QVariantMap entry{{QStringLiteral("name"), trimmed},
                      {QStringLiteral("builtIn"), false},
                      {QStringLiteral("library"), std::clamp(library, 0, 4000)},
                      {QStringLiteral("inspector"), std::clamp(inspector, 0, 4000)},
                      {QStringLiteral("timeline"), std::clamp(timeline, 0.15, 0.7)}};
    // The same name: that one, kept.
    for (QVariant& existing : m_custom) {
        QVariantMap map = existing.toMap();
        if (map.value(QStringLiteral("name")).toString().compare(trimmed, Qt::CaseInsensitive) == 0) {
            entry.insert(QStringLiteral("id"), map.value(QStringLiteral("id")));
            existing = entry;
            m_current = entry.value(QStringLiteral("id")).toString();
            write();
            emit changed();
            return m_current;
        }
    }
    int n = 1;
    while (!layout(QStringLiteral("custom-%1").arg(n)).isEmpty())
        ++n;
    entry.insert(QStringLiteral("id"), QStringLiteral("custom-%1").arg(n));
    m_custom << entry;
    m_current = entry.value(QStringLiteral("id")).toString();
    write();
    emit changed();
    return m_current;
}

bool Workspaces::remove(const QString& id)
{
    const auto before = m_custom.size();
    m_custom.removeIf([&](const QVariant& v) { return v.toMap().value(QStringLiteral("id")).toString() == id; });
    if (m_custom.size() == before)
        return false;
    if (m_current == id)
        m_current = QStringLiteral("editing");
    write();
    emit changed();
    return true;
}

void Workspaces::load()
{
    QFile file(m_file);
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    for (const QJsonValue& value : root.value(QStringLiteral("custom")).toArray()) {
        QVariantMap map = value.toObject().toVariantMap();
        if (!map.value(QStringLiteral("id")).toString().startsWith(QLatin1String("custom-"))
            || map.value(QStringLiteral("name")).toString().isEmpty())
            continue;
        map.insert(QStringLiteral("builtIn"), false);
        map.insert(QStringLiteral("timeline"), std::clamp(map.value(QStringLiteral("timeline")).toDouble(), 0.15, 0.7));
        m_custom << map;
    }
    const QString current = root.value(QStringLiteral("current")).toString();
    if (!layout(current).isEmpty())
        m_current = current;
}

void Workspaces::write()
{
    QJsonArray custom;
    for (const QVariant& entry : m_custom)
        custom.append(QJsonObject::fromVariantMap(entry.toMap()));
    QDir().mkpath(QFileInfo(m_file).path());
    QSaveFile file(m_file);
    if (!file.open(QIODevice::WriteOnly))
        return;
    file.write(QJsonDocument(QJsonObject{{QStringLiteral("version"), 1},
                                         {QStringLiteral("current"), m_current},
                                         {QStringLiteral("custom"), custom}})
                   .toJson());
    file.commit();
}
