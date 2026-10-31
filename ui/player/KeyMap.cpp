#include "KeyMap.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeySequence>
#include <QSaveFile>
#include <QStandardPaths>

#include <algorithm>

namespace {

// The same key, however it was written ("ctrl+g", "Ctrl+G").
QString normalized(const QString& sequence)
{
    const QKeySequence keys = QKeySequence::fromString(sequence, QKeySequence::PortableText);
    return keys.count() == 1 ? keys.toString(QKeySequence::PortableText) : QString();
}

const QStringList& reservedKeys()
{
    static const QStringList keys{QStringLiteral("Ctrl+Q"), QStringLiteral("Ctrl+W"), QStringLiteral("Ctrl+H"),
                                  QStringLiteral("Ctrl+M"), QStringLiteral("Esc"), QStringLiteral("Tab")};
    return keys;
}

} // namespace

KeyMap::KeyMap(QObject* parent) : QObject(parent)
{
    const auto add = [&](const char* id, const char* title, const char* group, QStringList defaults) {
        m_actions.push_back({QString::fromLatin1(id), QString::fromUtf8(title), QString::fromUtf8(group), defaults, defaults});
    };
    add("play", "Play / pause", "Playback", {QStringLiteral("Space")});
    add("frameBack", "Previous frame", "Playback", {QStringLiteral("Left")});
    add("frameForward", "Next frame", "Playback", {QStringLiteral("Right")});
    add("goToStart", "Go to start", "Playback", {QStringLiteral("Home")});
    add("goToEnd", "Go to end", "Playback", {QStringLiteral("End")});
    add("previousMarker", "Previous marker", "Playback", {QStringLiteral("Alt+Left")});
    add("nextMarker", "Next marker", "Playback", {QStringLiteral("Alt+Right")});
    add("split", "Split at the playhead", "Editing", {QStringLiteral("S")});
    add("delete", "Delete the selection", "Editing", {QStringLiteral("Del"), QStringLiteral("Backspace")});
    add("undo", "Undo", "Editing", {QStringLiteral("Ctrl+Z")});
    add("redo", "Redo", "Editing", {QStringLiteral("Ctrl+Shift+Z")});
    add("addMarker", "Add a marker", "Structure", {QStringLiteral("M")});
    add("group", "Group the selected clips", "Structure", {QStringLiteral("Ctrl+G")});
    add("ungroup", "Ungroup", "Structure", {QStringLiteral("Ctrl+Shift+G")});
    add("compound", "Make or break apart a compound clip", "Structure", {QStringLiteral("Ctrl+Alt+G")});
    add("save", "Save the project", "File", {QStringLiteral("Ctrl+S")});
    add("import", "Import media", "File", {QStringLiteral("Ctrl+I")});
    add("export", "Export", "File", {QStringLiteral("Ctrl+E")});
    add("shortcuts", "Keyboard shortcuts", "File", {QStringLiteral("Ctrl+/")});
    for (Action& action : m_actions) {
        for (QString& key : action.defaults)
            key = normalized(key);
        action.keys = action.defaults;
    }

    m_file = qEnvironmentVariable("TVE_KEYMAP_FILE");
    if (m_file.isEmpty())
        m_file = QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath(QStringLiteral("keymap.json"));
    load();
}

KeyMap::Action* KeyMap::find(const QString& id)
{
    const auto found = std::ranges::find(m_actions, id, &Action::id);
    return found == m_actions.end() ? nullptr : &*found;
}

const KeyMap::Action* KeyMap::find(const QString& id) const
{
    const auto found = std::ranges::find(m_actions, id, &Action::id);
    return found == m_actions.end() ? nullptr : &*found;
}

QVariantMap KeyMap::sequences() const
{
    QVariantMap map;
    for (const Action& action : m_actions)
        map.insert(action.id, action.keys);
    return map;
}

QVariantMap KeyMap::hints() const
{
    QVariantMap map;
    for (const Action& action : m_actions)
        map.insert(action.id, hint(action.id));
    return map;
}

QVariantList KeyMap::actions() const
{
    QVariantList list;
    for (const Action& action : m_actions) {
        list << QVariantMap{{QStringLiteral("id"), action.id},
                            {QStringLiteral("title"), action.title},
                            {QStringLiteral("group"), action.group},
                            {QStringLiteral("sequences"), action.keys},
                            {QStringLiteral("defaults"), action.defaults},
                            {QStringLiteral("custom"), action.keys != action.defaults},
                            {QStringLiteral("text"), text(action.id)}};
    }
    return list;
}

QString KeyMap::sequenceFor(int key, int modifiers) const
{
    switch (key) {
    case Qt::Key_Shift:
    case Qt::Key_Control:
    case Qt::Key_Meta:
    case Qt::Key_Alt:
    case Qt::Key_AltGr:
    case Qt::Key_CapsLock:
    case Qt::Key_unknown:
    case 0:
        return {};
    default:
        break;
    }
    const auto mods = Qt::KeyboardModifiers(modifiers) & ~Qt::KeypadModifier;
    return QKeySequence(QKeyCombination(mods, Qt::Key(key))).toString(QKeySequence::PortableText);
}

QString KeyMap::text(const QString& id) const
{
    const Action* action = find(id);
    if (!action || action->keys.isEmpty())
        return {};
    return QKeySequence::fromString(action->keys.first(), QKeySequence::PortableText).toString(QKeySequence::NativeText);
}

QString KeyMap::keyText(const QString& sequence) const
{
    return QKeySequence::fromString(sequence, QKeySequence::PortableText).toString(QKeySequence::NativeText);
}

QString KeyMap::hint(const QString& id) const
{
    const QString key = text(id);
    return key.isEmpty() ? QString() : QStringLiteral(" (") + key + QLatin1Char(')');
}

QString KeyMap::conflict(const QString& sequence, const QString& exceptId) const
{
    const QString key = normalized(sequence);
    if (key.isEmpty())
        return {};
    for (const Action& action : m_actions)
        if (action.id != exceptId && action.keys.contains(key))
            return action.id;
    return {};
}

bool KeyMap::assign(const QString& id, const QString& sequence)
{
    Action* action = find(id);
    if (!action) {
        fail(QStringLiteral("Unknown action: ") + id);
        return false;
    }
    const QString key = normalized(sequence);
    if (key.isEmpty()) {
        fail(QStringLiteral("That is not a single key or key combination"));
        return false;
    }
    if (reservedKeys().contains(key)) {
        fail(QStringLiteral("%1 belongs to the system").arg(QKeySequence::fromString(key).toString(QKeySequence::NativeText)));
        return false;
    }
    for (Action& other : m_actions)
        other.keys.removeAll(key);
    action->keys = {key};
    m_lastError.clear();
    save();
    emit changed();
    return true;
}

bool KeyMap::clear(const QString& id)
{
    Action* action = find(id);
    if (!action)
        return false;
    action->keys.clear();
    save();
    emit changed();
    return true;
}

void KeyMap::reset(const QString& id)
{
    Action* action = find(id);
    if (!action)
        return;
    // Its defaults come back, taken from any action that had been given them.
    for (Action& other : m_actions)
        if (&other != action)
            for (const QString& key : action->defaults)
                other.keys.removeAll(key);
    action->keys = action->defaults;
    save();
    emit changed();
}

void KeyMap::resetAll()
{
    for (Action& action : m_actions)
        action.keys = action.defaults;
    save();
    emit changed();
}

void KeyMap::load()
{
    QFile file(m_file);
    if (!file.open(QIODevice::ReadOnly))
        return; // nothing changed yet
    const QJsonObject bindings = QJsonDocument::fromJson(file.readAll()).object().value(QStringLiteral("bindings")).toObject();
    for (auto it = bindings.begin(); it != bindings.end(); ++it) {
        Action* action = find(it.key());
        if (!action)
            continue; // an action this version does not have
        QStringList keys;
        for (const QJsonValue& value : it.value().toArray()) {
            const QString key = normalized(value.toString());
            if (!key.isEmpty() && !reservedKeys().contains(key))
                keys << key;
        }
        action->keys = keys;
    }
    // A key two actions claim stays with the first.
    QStringList taken;
    for (Action& action : m_actions) {
        action.keys.removeIf([&](const QString& key) { return taken.contains(key); });
        taken << action.keys;
    }
}

void KeyMap::save()
{
    QJsonObject bindings;
    for (const Action& action : m_actions)
        if (action.keys != action.defaults)
            bindings.insert(action.id, QJsonArray::fromStringList(action.keys));
    if (bindings.isEmpty()) {
        QFile::remove(m_file);
        return;
    }
    QDir().mkpath(QFileInfo(m_file).path());
    QSaveFile file(m_file);
    if (!file.open(QIODevice::WriteOnly)) {
        fail(QStringLiteral("Cannot save the shortcuts to ") + m_file);
        return;
    }
    file.write(QJsonDocument(QJsonObject{{QStringLiteral("version"), 1}, {QStringLiteral("bindings"), bindings}}).toJson());
    if (!file.commit())
        fail(QStringLiteral("Cannot save the shortcuts to ") + m_file);
}

void KeyMap::fail(const QString& message)
{
    m_lastError = message;
    emit changed();
}
