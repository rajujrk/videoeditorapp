#pragma once

#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

// How the editor's panels are laid out: built-in workspaces for kinds of work and the
// person's own, saved by name. A layout is {id, name, builtIn, library, inspector, timeline}:
// the library's and inspector's widths in pixels (0 hides the panel) and the timeline's
// share of the height (0.15–0.7). The one in use is remembered. Kept in
// <app data>/workspaces.json ({"version": 1, "current": id, "custom": [layout]}), or
// TVE_WORKSPACES_FILE.
class Workspaces : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(QVariantList layouts READ layouts NOTIFY changed)
    Q_PROPERTY(QString current READ current NOTIFY changed)
    // The full-screen preview opens as a plain window (TVE_PREVIEW_WINDOWED, for tests: macOS
    // takes a moment to move a window to a full-screen space of its own).
    Q_PROPERTY(bool previewWindowed READ previewWindowed CONSTANT)

public:
    explicit Workspaces(QObject* parent = nullptr);

    QVariantList layouts() const;
    QString current() const { return m_current; }
    bool previewWindowed() const;

    // The layout by id; empty when there is none.
    Q_INVOKABLE QVariantMap layout(const QString& id) const;
    // Remembers which is in use.
    Q_INVOKABLE void setCurrent(const QString& id);
    // Keeps the panels as they are under `name` (the same name replaces it); its id, or empty.
    Q_INVOKABLE QString save(const QString& name, int library, int inspector, double timeline);
    // Deletes one of the person's own (built-in ones stay); the current one becomes Editing.
    Q_INVOKABLE bool remove(const QString& id);

signals:
    void changed();

private:
    void load();
    void write();

    QVariantList m_custom;
    QString m_current = QStringLiteral("editing");
    QString m_file;
};
