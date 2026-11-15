#include "MyItems.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>

namespace myitems {

namespace {

QString dataPath(const char* variable, const QString& name)
{
    const QString overridden = qEnvironmentVariable(variable);
    return overridden.isEmpty() ? QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath(name) : overridden;
}

QString stacksFile()
{
    return dataPath("TVE_MY_EFFECTS_FILE", QStringLiteral("my-effect-stacks.json"));
}

QVariantList readStacks()
{
    QFile file(stacksFile());
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(file.readAll()).object().value(QStringLiteral("stacks")).toArray().toVariantList();
}

bool writeStacks(const QVariantList& list)
{
    QDir().mkpath(QFileInfo(stacksFile()).absolutePath());
    QSaveFile file(stacksFile());
    const QJsonObject o{{QStringLiteral("version"), 1}, {QStringLiteral("stacks"), QJsonArray::fromVariantList(list)}};
    return file.open(QIODevice::WriteOnly) && file.write(QJsonDocument(o).toJson()) >= 0 && file.commit();
}

// A copy of a file in `folder` under its own name (made unique); its path, or "".
QString copyInto(const QString& folder, const QString& path, QString* error)
{
    if (!QFileInfo(path).isFile()) {
        *error = QObject::tr("Cannot read %1").arg(path);
        return {};
    }
    if (!QDir().mkpath(folder)) {
        *error = QObject::tr("Could not make %1").arg(folder);
        return {};
    }
    const QFileInfo info(path);
    QString target = QDir(folder).filePath(info.fileName());
    for (int n = 2; QFileInfo::exists(target); ++n)
        target = QDir(folder).filePath(QStringLiteral("%1 %2.%3").arg(info.completeBaseName()).arg(n).arg(info.suffix()));
    if (!QFile::copy(path, target)) {
        *error = QObject::tr("Could not copy %1").arg(info.fileName());
        return {};
    }
    return target;
}

} // namespace

QVariantList stacks()
{
    return readStacks();
}

QVariantMap stack(const QString& id)
{
    for (const QVariant& v : readStacks())
        if (v.toMap().value(QStringLiteral("id")).toString() == id)
            return v.toMap();
    return {};
}

QString saveStack(const QString& title, const QVariantList& effects)
{
    const QString name = title.trimmed();
    if (name.isEmpty() || effects.isEmpty())
        return {};
    QVariantList list = readStacks();
    int next = 1;
    QString id;
    for (int i = 0; i < list.size(); ++i) {
        const QVariantMap m = list[i].toMap();
        next = std::max(next, m.value(QStringLiteral("id")).toString().mid(6).toInt() + 1);
        if (m.value(QStringLiteral("title")).toString() == name)
            id = m.value(QStringLiteral("id")).toString();
    }
    if (id.isEmpty())
        id = QStringLiteral("stack.%1").arg(next);
    const QVariantMap entry{{QStringLiteral("id"), id}, {QStringLiteral("title"), name}, {QStringLiteral("effects"), effects}};
    bool replaced = false;
    for (QVariant& v : list)
        if (v.toMap().value(QStringLiteral("id")).toString() == id) {
            v = entry;
            replaced = true;
        }
    if (!replaced)
        list << entry;
    return writeStacks(list) ? id : QString();
}

bool removeStack(const QString& id)
{
    QVariantList list = readStacks();
    const auto before = list.size();
    list.removeIf([&](const QVariant& v) { return v.toMap().value(QStringLiteral("id")).toString() == id; });
    return list.size() != before && writeStacks(list);
}

QString stickersFolder()
{
    return dataPath("TVE_MY_STICKERS_DIR", QStringLiteral("My Stickers"));
}

bool isPictureFile(const QString& path)
{
    static const QStringList pictures{QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg"), QStringLiteral("webp"),
                                      QStringLiteral("gif")};
    return pictures.contains(QFileInfo(path).suffix().toLower());
}

bool isStickerFile(const QString& path)
{
    return isPictureFile(path) || QFileInfo(path).suffix().toLower() == QLatin1String("json");
}

QString addSticker(const QString& path, QString* error)
{
    if (!isStickerFile(path)) {
        *error = QObject::tr("A sticker is a picture (PNG, JPEG, WebP, GIF) or a Lottie animation (.json)");
        return {};
    }
    const QString copy = copyInto(stickersFolder(), path, error);
    return copy.isEmpty() ? QString() : QStringLiteral("mine/") + QFileInfo(copy).fileName();
}

bool removeSticker(const QString& id)
{
    if (!id.startsWith(QLatin1String("mine/")))
        return false;
    const QString name = id.mid(5);
    if (name.isEmpty() || name.contains(QLatin1Char('/')) || name.contains(QLatin1String("..")))
        return false;
    return QFile::remove(QDir(stickersFolder()).filePath(name));
}

QString lutsFolder()
{
    return dataPath("TVE_MY_LUTS_DIR", QStringLiteral("My LUTs"));
}

QString keepLut(const QString& path, QString* error)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix != QLatin1String("cube") && suffix != QLatin1String("3dl")) {
        *error = QObject::tr("A LUT is a .cube or .3dl file");
        return {};
    }
    return copyInto(lutsFolder(), path, error);
}

} // namespace myitems
