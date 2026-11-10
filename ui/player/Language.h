#pragma once

#include <QObject>
#include <QPointer>
#include <QQmlEngine>
#include <QTranslator>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <memory>
#include <string>

// A core or engine message or name (marked TVE_TR("core", …)) in the app's language; text
// that isn't marked comes back as it is.
QString coreText(const std::string& text);
QString coreText(const char* text);
// A name from the built-in catalogues (effects, looks, sounds, stickers; see
// tools/i18n/catalogue_strings.py) in the app's language.
QString catalogueText(const QString& name);
QString catalogueText(const std::string& name);

// The language the app speaks: "auto" (this Mac's preferred languages, the default), "en" or
// "ta" (Tamil). Its translator (:/i18n/tve_<language>.qm) and locale (numbers, dates) are
// installed for the whole app; changing it retranslates what is on screen at once. Kept in
// <app data>/language.json (TVE_LANGUAGE_FILE).
class Language : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY changed)
    // The one in use: "en" or "ta".
    Q_PROPERTY(QString current READ current NOTIFY changed)
    // [{id, label}] — each in its own language.
    Q_PROPERTY(QVariantList languages READ languages NOTIFY changed)

public:
    static Language* instance();
    static Language* create(QQmlEngine* engine, QJSEngine*);

    QString language() const { return m_language; }
    void setLanguage(const QString& language);
    QString current() const { return m_current; }
    QVariantList languages() const;

    // Installs the language (at start-up, before any window).
    void apply();

signals:
    void changed();

private:
    Language();
    void save() const;
    static QString settingsFile();

    QString m_language = QStringLiteral("auto");
    QString m_current = QStringLiteral("en");
    std::unique_ptr<QTranslator> m_translator;
    QList<QPointer<QQmlEngine>> m_engines;
};
