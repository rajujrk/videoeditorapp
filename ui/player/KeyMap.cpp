#include "KeyMap.h"
#include "Language.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeySequence>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUrl>

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

// Ready-made keys: what each changes from this app's own (an empty list: no key). The
// keys those editors use for the same actions on a Mac, where the system lets them be had
// (Premiere's ⌘M for export is the system's Minimize, so export keeps ⌘E).
struct Preset {
    const char* id;
    const char* title;
    const char* description;
    std::vector<std::pair<const char*, QStringList>> keys;
};

const std::vector<Preset>& presetTable()
{
    static const std::vector<Preset> presets{
        {"techie", QT_TRANSLATE_NOOP("KeyMap", "TechieVideoEditor"), QT_TRANSLATE_NOOP("KeyMap", "This app's own keys"), {}},
        {"premiere", QT_TRANSLATE_NOOP("KeyMap", "Like Premiere Pro"),
         QT_TRANSLATE_NOOP("KeyMap", "⌘K splits, C is the blade, Return renders, ⌥⌫ ripple deletes, ⇧M and ⇧⌘M go between markers"),
         {{"split", {QStringLiteral("Ctrl+K")}},
          {"blade", {QStringLiteral("C")}},
          {"render", {QStringLiteral("Return")}},
          {"rippleDelete", {QStringLiteral("Alt+Backspace"), QStringLiteral("Alt+Del")}},
          {"pasteAttributes", {QStringLiteral("Ctrl+Alt+V")}},
          {"nextMarker", {QStringLiteral("Shift+M")}},
          {"previousMarker", {QStringLiteral("Ctrl+Shift+M")}},
          {"shortcuts", {QStringLiteral("Ctrl+Alt+K")}}}},
        {"finalcut", QT_TRANSLATE_NOOP("KeyMap", "Like Final Cut Pro"),
         QT_TRANSLATE_NOOP("KeyMap", "⌘B splits, A selects, ⌫ ripple deletes and ⇧⌫ leaves a gap, ⌥G makes a compound clip, ⌃R renders"),
         {{"split", {QStringLiteral("Ctrl+B")}},
          {"selectTool", {QStringLiteral("A")}},
          {"delete", {QStringLiteral("Shift+Backspace"), QStringLiteral("Shift+Del")}},
          {"rippleDelete", {QStringLiteral("Backspace"), QStringLiteral("Del")}},
          {"compound", {QStringLiteral("Alt+G")}},
          {"render", {QStringLiteral("Meta+R")}},
          {"previousMarker", {QStringLiteral("Meta+;")}},
          {"nextMarker", {QStringLiteral("Meta+'")}},
          {"shortcuts", {QStringLiteral("Ctrl+Alt+K")}}}},
    };
    return presets;
}

} // namespace

KeyMap::KeyMap(QObject* parent) : QObject(parent)
{
    connect(Language::instance(), &Language::changed, this, &KeyMap::changed); // titles in the language
    const auto add = [&](const char* id, const char* title, const char* group, QStringList defaults) {
        m_actions.push_back({QString::fromLatin1(id), QString::fromUtf8(title), QString::fromUtf8(group), defaults, defaults});
    };
    add("play", QT_TRANSLATE_NOOP("KeyMap", "Play / pause"), "Playback", {QStringLiteral("Space")});
    add("frameBack", QT_TRANSLATE_NOOP("KeyMap", "Previous frame"), "Playback", {QStringLiteral("Left")});
    add("frameForward", QT_TRANSLATE_NOOP("KeyMap", "Next frame"), "Playback", {QStringLiteral("Right")});
    add("goToStart", QT_TRANSLATE_NOOP("KeyMap", "Go to start"), "Playback", {QStringLiteral("Home")});
    add("goToEnd", QT_TRANSLATE_NOOP("KeyMap", "Go to end"), "Playback", {QStringLiteral("End")});
    add("previousMarker", QT_TRANSLATE_NOOP("KeyMap", "Previous marker"), "Playback", {QStringLiteral("Alt+Left")});
    add("nextMarker", QT_TRANSLATE_NOOP("KeyMap", "Next marker"), "Playback", {QStringLiteral("Alt+Right")});
    add("render", QT_TRANSLATE_NOOP("KeyMap", "Render the heavy stretches"), "Playback", {QStringLiteral("Ctrl+R")});
    add("shuttleBack", QT_TRANSLATE_NOOP("KeyMap", "Play backwards (again: faster)"), "Playback", {QStringLiteral("J")});
    add("shuttleStop", QT_TRANSLATE_NOOP("KeyMap", "Stop"), "Playback", {QStringLiteral("K")});
    add("shuttleForward", QT_TRANSLATE_NOOP("KeyMap", "Play forwards (again: faster)"), "Playback", {QStringLiteral("L")});
    add("markIn", QT_TRANSLATE_NOOP("KeyMap", "Mark In"), "Playback", {QStringLiteral("I")});
    add("markOut", QT_TRANSLATE_NOOP("KeyMap", "Mark Out"), "Playback", {QStringLiteral("O")});
    add("clearMarks", QT_TRANSLATE_NOOP("KeyMap", "Clear In and Out"), "Playback", {QStringLiteral("Alt+X")});
    add("split", QT_TRANSLATE_NOOP("KeyMap", "Split at the playhead"), "Editing", {QStringLiteral("S")});
    add("blade", QT_TRANSLATE_NOOP("KeyMap", "Blade tool (click clips to cut them)"), "Editing", {QStringLiteral("B")});
    add("selectTool", QT_TRANSLATE_NOOP("KeyMap", "Select tool"), "Editing", {QStringLiteral("V")});
    add("delete", QT_TRANSLATE_NOOP("KeyMap", "Delete the selection"), "Editing", {QStringLiteral("Del"), QStringLiteral("Backspace")});
    add("rippleDelete", QT_TRANSLATE_NOOP("KeyMap", "Ripple delete (close the gap)"), "Editing",
        {QStringLiteral("Shift+Del"), QStringLiteral("Shift+Backspace")});
    add("cut", QT_TRANSLATE_NOOP("KeyMap", "Cut"), "Editing", {QStringLiteral("Ctrl+X")});
    add("copy", QT_TRANSLATE_NOOP("KeyMap", "Copy"), "Editing", {QStringLiteral("Ctrl+C")});
    add("paste", QT_TRANSLATE_NOOP("KeyMap", "Paste at the playhead"), "Editing", {QStringLiteral("Ctrl+V")});
    add("duplicate", QT_TRANSLATE_NOOP("KeyMap", "Duplicate"), "Editing", {QStringLiteral("Ctrl+D")});
    add("pasteAttributes", QT_TRANSLATE_NOOP("KeyMap", "Paste attributes"), "Editing", {QStringLiteral("Ctrl+Shift+V")});
    add("selectAll", QT_TRANSLATE_NOOP("KeyMap", "Select every clip"), "Editing", {QStringLiteral("Ctrl+A")});
    add("undo", QT_TRANSLATE_NOOP("KeyMap", "Undo"), "Editing", {QStringLiteral("Ctrl+Z")});
    add("redo", QT_TRANSLATE_NOOP("KeyMap", "Redo"), "Editing", {QStringLiteral("Ctrl+Shift+Z")});
    add("addMarker", QT_TRANSLATE_NOOP("KeyMap", "Add a marker"), "Structure", {QStringLiteral("M")});
    add("group", QT_TRANSLATE_NOOP("KeyMap", "Group the selected clips"), "Structure", {QStringLiteral("Ctrl+G")});
    add("ungroup", QT_TRANSLATE_NOOP("KeyMap", "Ungroup"), "Structure", {QStringLiteral("Ctrl+Shift+G")});
    add("compound", QT_TRANSLATE_NOOP("KeyMap", "Make or break apart a compound clip"), "Structure", {QStringLiteral("Ctrl+Alt+G")});
    add("angle1", QT_TRANSLATE_NOOP("KeyMap", "Cut to angle 1 (multicam)"), "Structure", {QStringLiteral("1")});
    add("angle2", QT_TRANSLATE_NOOP("KeyMap", "Cut to angle 2 (multicam)"), "Structure", {QStringLiteral("2")});
    add("angle3", QT_TRANSLATE_NOOP("KeyMap", "Cut to angle 3 (multicam)"), "Structure", {QStringLiteral("3")});
    add("angle4", QT_TRANSLATE_NOOP("KeyMap", "Cut to angle 4 (multicam)"), "Structure", {QStringLiteral("4")});
    add("angle5", QT_TRANSLATE_NOOP("KeyMap", "Cut to angle 5 (multicam)"), "Structure", {QStringLiteral("5")});
    add("angle6", QT_TRANSLATE_NOOP("KeyMap", "Cut to angle 6 (multicam)"), "Structure", {QStringLiteral("6")});
    add("angle7", QT_TRANSLATE_NOOP("KeyMap", "Cut to angle 7 (multicam)"), "Structure", {QStringLiteral("7")});
    add("angle8", QT_TRANSLATE_NOOP("KeyMap", "Cut to angle 8 (multicam)"), "Structure", {QStringLiteral("8")});
    add("angle9", QT_TRANSLATE_NOOP("KeyMap", "Cut to angle 9 (multicam)"), "Structure", {QStringLiteral("9")});
    add("save", QT_TRANSLATE_NOOP("KeyMap", "Save the project"), "File", {QStringLiteral("Ctrl+S")});
    add("import", QT_TRANSLATE_NOOP("KeyMap", "Import media"), "File", {QStringLiteral("Ctrl+I")});
    add("export", QT_TRANSLATE_NOOP("KeyMap", "Export"), "File", {QStringLiteral("Ctrl+E")});
    add("shortcuts", QT_TRANSLATE_NOOP("KeyMap", "Keyboard shortcuts"), "File", {QStringLiteral("Ctrl+/")});
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
                            {QStringLiteral("title"), QCoreApplication::translate("KeyMap", action.title.toUtf8().constData())},
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
        fail(tr("Unknown action: %1").arg(id));
        return false;
    }
    const QString key = normalized(sequence);
    if (key.isEmpty()) {
        fail(tr("That is not a single key or key combination"));
        return false;
    }
    if (reservedKeys().contains(key)) {
        fail(tr("%1 belongs to the system").arg(QKeySequence::fromString(key).toString(QKeySequence::NativeText)));
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
    applyBindings(QJsonDocument::fromJson(file.readAll()).object().value(QStringLiteral("bindings")).toObject());
}

int KeyMap::applyBindings(const QJsonObject& bindings)
{
    int named = 0;
    for (Action& action : m_actions)
        action.keys = action.defaults;
    for (auto it = bindings.begin(); it != bindings.end(); ++it) {
        Action* action = find(it.key());
        if (!action)
            continue; // an action this version does not have
        QStringList keys;
        for (const QJsonValue& value : it.value().toArray()) {
            const QString key = normalized(value.toString());
            if (!key.isEmpty() && !reservedKeys().contains(key) && !keys.contains(key))
                keys << key;
        }
        action->keys = keys;
        ++named;
    }
    // A key two actions claim stays with the one named (else the first).
    QStringList taken;
    for (Action& action : m_actions)
        if (bindings.contains(action.id))
            taken << action.keys;
    for (Action& action : m_actions) {
        if (bindings.contains(action.id))
            continue;
        action.keys.removeIf([&](const QString& key) { return taken.contains(key); });
        taken << action.keys;
    }
    QStringList seen;
    for (Action& action : m_actions) {
        action.keys.removeIf([&](const QString& key) { return seen.contains(key); });
        seen << action.keys;
    }
    return named;
}

std::vector<QStringList> KeyMap::presetKeys(const QString& id) const
{
    std::vector<QStringList> keys;
    const auto preset = std::ranges::find_if(presetTable(), [&](const Preset& p) { return id == QLatin1String(p.id); });
    if (preset == presetTable().end())
        return keys;
    for (const Action& action : m_actions) {
        QStringList chosen = action.defaults;
        for (const auto& [actionId, presetKeys] : preset->keys)
            if (action.id == QLatin1String(actionId)) {
                chosen.clear();
                for (const QString& key : presetKeys)
                    chosen << normalized(key);
            }
        keys.push_back(chosen);
    }
    return keys;
}

QVariantList KeyMap::presets() const
{
    QVariantList list;
    for (const Preset& preset : presetTable())
        list << QVariantMap{{QStringLiteral("id"), QString::fromLatin1(preset.id)},
                            {QStringLiteral("title"), QCoreApplication::translate("KeyMap", preset.title)},
                            {QStringLiteral("description"), QCoreApplication::translate("KeyMap", preset.description)}};
    return list;
}

QString KeyMap::preset() const
{
    for (const Preset& preset : presetTable()) {
        const auto keys = presetKeys(QString::fromLatin1(preset.id));
        bool same = keys.size() == m_actions.size();
        for (std::size_t i = 0; same && i < keys.size(); ++i)
            same = keys[i] == m_actions[i].keys;
        if (same)
            return QString::fromLatin1(preset.id);
    }
    return {};
}

bool KeyMap::applyPreset(const QString& id)
{
    const auto keys = presetKeys(id);
    if (keys.empty()) {
        fail(tr("Unknown set of shortcuts: %1").arg(id));
        return false;
    }
    for (std::size_t i = 0; i < m_actions.size(); ++i)
        m_actions[i].keys = keys[i];
    m_lastError.clear();
    save();
    emit changed();
    return true;
}

bool KeyMap::exportTo(const QUrl& file)
{
    const QString path = file.isLocalFile() ? file.toLocalFile() : file.toString();
    QJsonObject bindings;
    for (const Action& action : m_actions)
        bindings.insert(action.id, QJsonArray::fromStringList(action.keys));
    QSaveFile out(path);
    if (path.isEmpty() || !out.open(QIODevice::WriteOnly)) {
        fail(tr("Cannot write %1").arg(QFileInfo(path).fileName()));
        return false;
    }
    out.write(QJsonDocument(QJsonObject{{QStringLiteral("version"), 1},
                                        {QStringLiteral("app"), QStringLiteral("TechieVideoEditor")},
                                        {QStringLiteral("bindings"), bindings}})
                  .toJson());
    if (!out.commit()) {
        fail(tr("Cannot write %1").arg(QFileInfo(path).fileName()));
        return false;
    }
    m_lastError.clear();
    emit changed();
    return true;
}

int KeyMap::importFrom(const QUrl& file)
{
    const QString path = file.isLocalFile() ? file.toLocalFile() : file.toString();
    QFile in(path);
    if (!in.open(QIODevice::ReadOnly)) {
        fail(tr("Cannot read %1").arg(QFileInfo(path).fileName()));
        return -1;
    }
    const QJsonObject root = QJsonDocument::fromJson(in.readAll()).object();
    const QJsonValue bindings = root.value(QStringLiteral("bindings"));
    if (!bindings.isObject() || root.value(QStringLiteral("version")).toInt(1) > 1) {
        fail(tr("%1 is not a TechieVideoEditor shortcuts file").arg(QFileInfo(path).fileName()));
        return -1;
    }
    const int named = applyBindings(bindings.toObject());
    m_lastError.clear();
    save();
    emit changed();
    return named;
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
        fail(tr("Cannot save the shortcuts to %1").arg(m_file));
        return;
    }
    file.write(QJsonDocument(QJsonObject{{QStringLiteral("version"), 1}, {QStringLiteral("bindings"), bindings}}).toJson());
    if (!file.commit())
        fail(tr("Cannot save the shortcuts to %1").arg(m_file));
}

void KeyMap::fail(const QString& message)
{
    m_lastError = message;
    emit changed();
}
