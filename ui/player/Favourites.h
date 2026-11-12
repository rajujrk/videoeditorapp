#pragma once

#include <QObject>
#include <QStringList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

// The library items the person keeps to hand — effects, transitions, titles, stickers and
// looks — with tags of their own to find them by. Each is "<kind>:<id>" ("effect:glow") with
// its tags (lower case, trimmed, each once). Kept in <app data>/favourites.json
// ({"version": 1, "favourites": {key: [tags]}}), or TVE_FAVOURITES_FILE; saved at once.
class Favourites : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    // key → its tags (QStringList); a key is there while the item is a favourite.
    Q_PROPERTY(QVariantMap items READ items NOTIFY changed)
    Q_PROPERTY(QString filePath READ filePath CONSTANT)

public:
    explicit Favourites(QObject* parent = nullptr);

    QVariantMap items() const { return m_items; }
    QString filePath() const { return m_file; }

    Q_INVOKABLE bool isFavourite(const QString& kind, const QString& id) const;
    // Makes it a favourite, or no longer one (its tags go with it); returns which it is now.
    Q_INVOKABLE bool toggle(const QString& kind, const QString& id);
    // Its tags (a list, or text with commas); makes it a favourite.
    Q_INVOKABLE void setTags(const QString& kind, const QString& id, const QVariant& tags);
    Q_INVOKABLE QStringList tagsOf(const QString& kind, const QString& id) const;
    // Every tag on a favourite of this kind, sorted.
    Q_INVOKABLE QStringList tagsFor(const QString& kind) const;
    // Whether it is a favourite with `tag` (any favourite when `tag` is empty).
    Q_INVOKABLE bool has(const QString& kind, const QString& id, const QString& tag = {}) const;
    // Whether any of its tags contains `text` (for the library's search).
    Q_INVOKABLE bool tagMatches(const QString& kind, const QString& id, const QString& text) const;

signals:
    void changed();

private:
    static QStringList cleaned(const QVariant& tags);
    void load();
    void save();

    QVariantMap m_items;
    QString m_file;
};
