#include "Language.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QSaveFile>
#include <QStandardPaths>

namespace {

// The languages the app speaks: their codes, their names in themselves, and their locales.
struct Spoken {
    const char* code;
    const char* name;
    const char* locale;
};
constexpr Spoken kSpoken[] = {
    {"en", "English", "en_US"},     {"ta", "தமிழ்", "ta_IN"},       {"fr", "Français", "fr_FR"},
    {"de", "Deutsch", "de_DE"},     {"es", "Español", "es_ES"},     {"hi", "हिन्दी", "hi_IN"},
    {"ja", "日本語", "ja_JP"},       {"ko", "한국어", "ko_KR"},       {"th", "ไทย", "th_TH"},
    {"vi", "Tiếng Việt", "vi_VN"},  {"zh_CN", "简体中文", "zh_CN"},
};
const QStringList kLanguages = [] {
    QStringList codes;
    for (const Spoken& s : kSpoken)
        codes << QString::fromLatin1(s.code);
    return codes;
}();
const Spoken* spoken(const QString& code)
{
    for (const Spoken& s : kSpoken)
        if (code == QLatin1String(s.code))
            return &s;
    return nullptr;
}

} // namespace

QString coreText(const char* text)
{
    return QCoreApplication::translate("core", text);
}

QString coreText(const std::string& text)
{
    return coreText(text.c_str());
}

QString catalogueText(const QString& name)
{
    return QCoreApplication::translate("catalogue", name.toUtf8().constData());
}

QString catalogueText(const std::string& name)
{
    return QCoreApplication::translate("catalogue", name.c_str());
}

Language* Language::instance()
{
    static Language* language = new Language;
    return language;
}

Language* Language::create(QQmlEngine* engine, QJSEngine*)
{
    Language* language = instance();
    QJSEngine::setObjectOwnership(language, QJSEngine::CppOwnership);
    if (!language->m_engines.contains(engine))
        language->m_engines << engine;
    return language;
}

Language::Language()
{
    QFile file(settingsFile());
    if (file.open(QIODevice::ReadOnly)) {
        const QString saved = QJsonDocument::fromJson(file.readAll()).object().value(QStringLiteral("language")).toString();
        if (saved == QLatin1String("auto") || kLanguages.contains(saved))
            m_language = saved;
    }
}

QString Language::settingsFile()
{
    const QString overridden = qEnvironmentVariable("TVE_LANGUAGE_FILE");
    return overridden.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/language.json")
                                : overridden;
}

void Language::save() const
{
    QDir().mkpath(QFileInfo(settingsFile()).absolutePath());
    QSaveFile file(settingsFile());
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(QJsonObject{{QStringLiteral("language"), m_language}}).toJson());
        file.commit();
    }
}

QVariantList Language::languages() const
{
    QVariantList list{QVariantMap{{QStringLiteral("id"), QStringLiteral("auto")}, {QStringLiteral("label"), tr("Same as this Mac")}}};
    for (const Spoken& s : kSpoken)
        list << QVariantMap{{QStringLiteral("id"), QString::fromLatin1(s.code)}, {QStringLiteral("label"), QString::fromUtf8(s.name)}};
    return list;
}

void Language::setLanguage(const QString& language)
{
    if (language == m_language || (language != QLatin1String("auto") && !kLanguages.contains(language)))
        return;
    m_language = language;
    save();
    apply();
}

void Language::apply()
{
    // Auto: the first of this Mac's languages the app speaks.
    QString wanted = m_language;
    if (wanted == QLatin1String("auto")) {
        wanted = QStringLiteral("en");
        for (const QString& name : QLocale::system().uiLanguages()) {
            const QString code = name.section(QLatin1Char('-'), 0, 0).section(QLatin1Char('_'), 0, 0).toLower();
            // (Chinese: the simplified one, for zh-Hans and plain zh.)
            const QString mapped = code == QLatin1String("zh") ? QStringLiteral("zh_CN") : code;
            if (kLanguages.contains(mapped)) {
                wanted = mapped;
                break;
            }
        }
    }
    if (m_translator) {
        QCoreApplication::removeTranslator(m_translator.get());
        m_translator.reset();
    }
    if (wanted != QLatin1String("en")) {
        auto translator = std::make_unique<QTranslator>();
        if (translator->load(QStringLiteral(":/i18n/tve_%1.qm").arg(wanted))) {
            QCoreApplication::installTranslator(translator.get());
            m_translator = std::move(translator);
        } else {
            wanted = QStringLiteral("en");
        }
    }
    // Numbers and dates the language's way (without thousands separators in what the editor
    // shows: sizes, frame counts and the like read better whole).
    const Spoken* language = spoken(wanted);
    QLocale locale(QString::fromLatin1(language ? language->locale : "en_US"));
    locale.setNumberOptions(QLocale::OmitGroupSeparator);
    QLocale::setDefault(locale);
    m_current = wanted;
    for (const QPointer<QQmlEngine>& engine : m_engines)
        if (engine) {
            engine->setUiLanguage(wanted);
            engine->retranslate();
        }
    emit changed();
}
