#pragma once

#include <QObject>
#include <QJsonObject>
#include <QStringList>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <vector>

// The editor's keyboard shortcuts: a registry of actions with their default keys, and the
// user's changes kept in a keymap file (JSON: {"version": 1, "bindings": {id: [keys]}},
// only what differs from the defaults). Keys are Qt's portable text ("Ctrl+G" is ⌘G on a
// Mac). The file is <app data>/keymap.json (next to the colour presets), or TVE_KEYMAP_FILE.
class KeyMap : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    // Action id → its keys (QStringList), for Shortcut.sequences.
    Q_PROPERTY(QVariantMap sequences READ sequences NOTIFY changed)
    // Action id → " (⌘G)" for tooltips, or empty when the action has no key.
    Q_PROPERTY(QVariantMap hints READ hints NOTIFY changed)
    // {id, title, group, sequences, defaults, custom, text} in registry order.
    Q_PROPERTY(QVariantList actions READ actions NOTIFY changed)
    Q_PROPERTY(QString filePath READ filePath CONSTANT)
    // Ready-made sets of keys: {id, title, description}: "techie" (this app's own),
    // "premiere" and "finalcut" (the keys those editors use for the same actions, where the
    // system lets them be had).
    Q_PROPERTY(QVariantList presets READ presets NOTIFY changed)
    // The preset the keys are now, exactly; empty when they have been changed from it.
    Q_PROPERTY(QString preset READ preset NOTIFY changed)
    Q_PROPERTY(QString lastError READ lastError NOTIFY changed)

public:
    explicit KeyMap(QObject* parent = nullptr);

    QVariantMap sequences() const;
    QVariantMap hints() const;
    QVariantList actions() const;
    QString filePath() const { return m_file; }
    QString lastError() const { return m_lastError; }
    QVariantList presets() const;
    QString preset() const;

    // How a key press reads as a shortcut ("Ctrl+Shift+G"); empty for a modifier alone.
    Q_INVOKABLE QString sequenceFor(int key, int modifiers) const;
    // The action's first key as shown on this system ("⌘G"); empty when it has none.
    Q_INVOKABLE QString text(const QString& id) const;
    // Any key as shown on this system.
    Q_INVOKABLE QString keyText(const QString& sequence) const;
    // " (⌘G)" for tooltips, or empty when the action has no key.
    Q_INVOKABLE QString hint(const QString& id) const;
    // The action (other than `exceptId`) that uses `sequence`, or empty.
    Q_INVOKABLE QString conflict(const QString& sequence, const QString& exceptId = {}) const;
    // Gives the action `sequence` as its only key, taking it from any action that had it.
    // Refuses unknown actions, empty or several-chord keys, and keys the system keeps
    // (⌘Q, ⌘W, ⌘H, ⌘M, Esc, Tab).
    Q_INVOKABLE bool assign(const QString& id, const QString& sequence);
    // Leaves the action without a key.
    Q_INVOKABLE bool clear(const QString& id);
    Q_INVOKABLE void reset(const QString& id);
    Q_INVOKABLE void resetAll();
    // Every action given the preset's keys (its own where the preset has none).
    Q_INVOKABLE bool applyPreset(const QString& id);
    // The whole key map — every action's keys — to a file to keep or take to another Mac
    // ({"version": 1, "app": "TechieVideoEditor", "bindings": {id: [keys]}}).
    Q_INVOKABLE bool exportTo(const QUrl& file);
    // A key map file (or a keymap.json) in: its actions take its keys, the rest their
    // defaults; keys the system keeps, unknown actions and doubled keys are left out.
    // Returns how many actions it named, or -1 (with lastError) when it is not one.
    Q_INVOKABLE int importFrom(const QUrl& file);

signals:
    void changed();

private:
    struct Action {
        QString id;
        QString title;
        QString group;
        QStringList defaults;
        QStringList keys;
    };
    Action* find(const QString& id);
    const Action* find(const QString& id) const;
    void load();
    void save();
    // Sets every action from bindings (id → keys; missing: defaults); keys already taken
    // stay with the first action. Returns how many were named.
    int applyBindings(const QJsonObject& bindings);
    // A preset's keys for every action (its own where the preset has none).
    std::vector<QStringList> presetKeys(const QString& id) const;
    void fail(const QString& message);

    std::vector<Action> m_actions;
    QString m_file;
    QString m_lastError;
};
