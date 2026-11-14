#include "EditingPreferences.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>

#include <algorithm>
#include <cmath>

EditingPreferences* EditingPreferences::instance()
{
    static EditingPreferences* preferences = new EditingPreferences;
    return preferences;
}

EditingPreferences* EditingPreferences::create(QQmlEngine*, QJSEngine*)
{
    EditingPreferences* preferences = instance();
    QJSEngine::setObjectOwnership(preferences, QJSEngine::CppOwnership);
    return preferences;
}

EditingPreferences::EditingPreferences()
{
    m_file = qEnvironmentVariable("TVE_EDITING_FILE");
    if (m_file.isEmpty())
        m_file = QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath(QStringLiteral("editing.json"));
    load();
}

void EditingPreferences::load()
{
    QFile file(m_file);
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QJsonObject o = QJsonDocument::fromJson(file.readAll()).object();
    if (o.value(QStringLiteral("version")).toInt() != 1)
        return;
    if (const QString id = o.value(QStringLiteral("defaultTransition")).toString(); !id.isEmpty())
        m_defaultTransition = id;
    const double length = o.value(QStringLiteral("defaultTransitionLength")).toDouble(m_defaultTransitionLength);
    if (std::isfinite(length))
        m_defaultTransitionLength = std::clamp(length, kShortest, kLongest);
    m_autoRipple = o.value(QStringLiteral("autoRipple")).toBool(m_autoRipple);
    m_skimming = o.value(QStringLiteral("skimming")).toBool(m_skimming);
    m_loopPlayback = o.value(QStringLiteral("loopPlayback")).toBool(m_loopPlayback);
    m_canvasBorder = o.value(QStringLiteral("canvasBorder")).toBool(m_canvasBorder);
    m_snapToBorder = o.value(QStringLiteral("snapToBorder")).toBool(m_snapToBorder);
}

void EditingPreferences::save() const
{
    QDir().mkpath(QFileInfo(m_file).absolutePath());
    QSaveFile file(m_file);
    if (!file.open(QIODevice::WriteOnly))
        return;
    const QJsonObject o{{QStringLiteral("version"), 1},
                        {QStringLiteral("defaultTransition"), m_defaultTransition},
                        {QStringLiteral("defaultTransitionLength"), m_defaultTransitionLength},
                        {QStringLiteral("autoRipple"), m_autoRipple},
                        {QStringLiteral("skimming"), m_skimming},
                        {QStringLiteral("loopPlayback"), m_loopPlayback},
                        {QStringLiteral("canvasBorder"), m_canvasBorder},
                        {QStringLiteral("snapToBorder"), m_snapToBorder}};
    file.write(QJsonDocument(o).toJson());
    file.commit();
}

template <typename T>
void EditingPreferences::set(T& field, T value)
{
    if (field == value)
        return;
    field = value;
    save();
    emit changed();
}

void EditingPreferences::setDefaultTransition(const QString& id)
{
    if (!id.isEmpty())
        set(m_defaultTransition, id);
}

void EditingPreferences::setDefaultTransitionLength(double seconds)
{
    if (std::isfinite(seconds))
        set(m_defaultTransitionLength, std::clamp(seconds, kShortest, kLongest));
}

void EditingPreferences::setAutoRipple(bool on) { set(m_autoRipple, on); }
void EditingPreferences::setSkimming(bool on) { set(m_skimming, on); }
void EditingPreferences::setLoopPlayback(bool on) { set(m_loopPlayback, on); }
void EditingPreferences::setCanvasBorder(bool on) { set(m_canvasBorder, on); }
void EditingPreferences::setSnapToBorder(bool on) { set(m_snapToBorder, on); }

void EditingPreferences::reset()
{
    m_defaultTransition = QStringLiteral("tve.transition.dissolve");
    m_defaultTransitionLength = 1.0;
    m_autoRipple = m_skimming = m_loopPlayback = false;
    m_canvasBorder = m_snapToBorder = true;
    save();
    emit changed();
}
