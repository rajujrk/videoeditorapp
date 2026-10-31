#pragma once

#include <QObject>
#include <QStringList>
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
    Q_PROPERTY(QString lastError READ lastError NOTIFY changed)

public:
    explicit KeyMap(QObject* parent = nullptr);

    QVariantMap sequences() const;
    QVariantMap hints() const;
    QVariantList actions() const;
    QString filePath() const { return m_file; }
    QString lastError() const { return m_lastError; }

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
    void fail(const QString& message);

    std::vector<Action> m_actions;
    QString m_file;
    QString m_lastError;
};
