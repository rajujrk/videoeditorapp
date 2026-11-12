#include "Teleprompter.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>

#include <algorithm>
#include <cmath>

namespace {
constexpr qsizetype kMaxText = 200'000;
} // namespace

Teleprompter::Teleprompter(QObject* parent) : QObject(parent)
{
    m_text = tr("Type or paste your script here.\n\nIt scrolls while you record, at the speed you set. "
                "Keep your eyes on the line by the arrows.");
    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(500);
    connect(&m_saveTimer, &QTimer::timeout, this, &Teleprompter::save);
    load();
}

Teleprompter::~Teleprompter()
{
    save();
}

QString Teleprompter::settingsFile()
{
    const QString overridden = qEnvironmentVariable("TVE_TELEPROMPTER_FILE");
    return overridden.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/teleprompter.json")
                                : overridden;
}

double Teleprompter::pixelsPerSecond() const
{
    // Line height ≈ 1.3 × the font size; speed 1 → a line in 2.5 s, speed 10 → in 0.35 s,
    // evenly in between on a log scale.
    const double secondsPerLine = 2.5 * std::pow(0.35 / 2.5, (m_speed - 1.0) / 9.0);
    return m_fontSize * 1.3 / secondsPerLine;
}

void Teleprompter::setText(const QString& text)
{
    const QString clipped = text.left(kMaxText);
    if (clipped == m_text)
        return;
    m_text = clipped;
    touched();
}

void Teleprompter::setFontSize(int size)
{
    size = std::clamp(size, kMinFontSize, kMaxFontSize);
    if (size == m_fontSize)
        return;
    m_fontSize = size;
    touched();
}

void Teleprompter::setSpeed(double speed)
{
    speed = std::clamp(std::isfinite(speed) ? speed : 3.0, 1.0, 10.0);
    if (speed == m_speed)
        return;
    m_speed = speed;
    touched();
}

void Teleprompter::setMirrored(bool mirrored)
{
    if (mirrored == m_mirrored)
        return;
    m_mirrored = mirrored;
    touched();
}

void Teleprompter::setFollowRecording(bool follow)
{
    if (follow == m_followRecording)
        return;
    m_followRecording = follow;
    touched();
}

void Teleprompter::touched()
{
    m_dirty = true;
    m_saveTimer.start();
    emit changed();
}

void Teleprompter::load()
{
    QFile file(settingsFile());
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QJsonObject o = QJsonDocument::fromJson(file.readAll()).object();
    if (o.contains(QStringLiteral("text")))
        m_text = o.value(QStringLiteral("text")).toString().left(kMaxText);
    m_fontSize = std::clamp(o.value(QStringLiteral("fontSize")).toInt(m_fontSize), kMinFontSize, kMaxFontSize);
    m_speed = std::clamp(o.value(QStringLiteral("speed")).toDouble(m_speed), 1.0, 10.0);
    m_mirrored = o.value(QStringLiteral("mirrored")).toBool(m_mirrored);
    m_followRecording = o.value(QStringLiteral("followRecording")).toBool(m_followRecording);
}

void Teleprompter::save()
{
    m_saveTimer.stop();
    if (!m_dirty)
        return;
    QDir().mkpath(QFileInfo(settingsFile()).absolutePath());
    QSaveFile file(settingsFile());
    if (!file.open(QIODevice::WriteOnly))
        return;
    const QJsonObject o{{QStringLiteral("text"), m_text},
                        {QStringLiteral("fontSize"), m_fontSize},
                        {QStringLiteral("speed"), m_speed},
                        {QStringLiteral("mirrored"), m_mirrored},
                        {QStringLiteral("followRecording"), m_followRecording}};
    file.write(QJsonDocument(o).toJson());
    if (file.commit())
        m_dirty = false;
}
