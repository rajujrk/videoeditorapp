#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QString>
#include <QTimer>

// The teleprompter's script and how it reads: the text, its size (pixels), how fast it scrolls
// (1 slow … 10 fast), mirrored (for a beam-splitter glass) or not, and whether it starts
// scrolling by itself when a recording starts. Kept for the app, not the project, in
// Application Support/…/teleprompter.json (TVE_TELEPROMPTER_FILE overrides), saved a moment
// after each change.
class Teleprompter : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("The app's teleprompter")
    Q_PROPERTY(QString text READ text WRITE setText NOTIFY changed)
    Q_PROPERTY(int fontSize READ fontSize WRITE setFontSize NOTIFY changed)
    Q_PROPERTY(double speed READ speed WRITE setSpeed NOTIFY changed)
    Q_PROPERTY(bool mirrored READ mirrored WRITE setMirrored NOTIFY changed)
    Q_PROPERTY(bool followRecording READ followRecording WRITE setFollowRecording NOTIFY changed)
    // Pixels a second at this speed and size: about a line every 2.5 s at speed 1, a line
    // every 0.35 s at 10.
    Q_PROPERTY(double pixelsPerSecond READ pixelsPerSecond NOTIFY changed)

public:
    static constexpr int kMinFontSize = 24;
    static constexpr int kMaxFontSize = 120;

    explicit Teleprompter(QObject* parent = nullptr);
    ~Teleprompter() override;

    QString text() const { return m_text; }
    int fontSize() const { return m_fontSize; }
    double speed() const { return m_speed; }
    bool mirrored() const { return m_mirrored; }
    bool followRecording() const { return m_followRecording; }
    double pixelsPerSecond() const;

    void setText(const QString& text);
    void setFontSize(int size);
    void setSpeed(double speed);
    void setMirrored(bool mirrored);
    void setFollowRecording(bool follow);

    static QString settingsFile();
    // Writes pending changes now (normally a moment after the last change).
    Q_INVOKABLE void save();

signals:
    void changed();

private:
    void load();
    void touched();

    QString m_text;
    int m_fontSize = 56;
    double m_speed = 3.0;
    bool m_mirrored = false;
    bool m_followRecording = true;
    QTimer m_saveTimer;
    bool m_dirty = false;
};
