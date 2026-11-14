#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QString>
#include <QtQml/qqmlregistration.h>

// How the person likes to edit, the same in every project: the transition ⌘T adds and its
// length, deleting closing the gap on every track (auto ripple), the player following the
// pointer over the timeline (skimming), playing round and round (loop), and the canvas's
// border in the player with pictures snapping to it. Kept in <app data>/editing.json
// (TVE_EDITING_FILE).
class EditingPreferences : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(QString defaultTransition READ defaultTransition WRITE setDefaultTransition NOTIFY changed)
    Q_PROPERTY(double defaultTransitionLength READ defaultTransitionLength WRITE setDefaultTransitionLength NOTIFY changed)
    Q_PROPERTY(bool autoRipple READ autoRipple WRITE setAutoRipple NOTIFY changed)
    Q_PROPERTY(bool skimming READ skimming WRITE setSkimming NOTIFY changed)
    Q_PROPERTY(bool loopPlayback READ loopPlayback WRITE setLoopPlayback NOTIFY changed)
    Q_PROPERTY(bool canvasBorder READ canvasBorder WRITE setCanvasBorder NOTIFY changed)
    Q_PROPERTY(bool snapToBorder READ snapToBorder WRITE setSnapToBorder NOTIFY changed)

public:
    static EditingPreferences* instance();
    static EditingPreferences* create(QQmlEngine*, QJSEngine*);

    // Lengths a default transition can have (seconds).
    static constexpr double kShortest = 0.1;
    static constexpr double kLongest = 10.0;

    QString defaultTransition() const { return m_defaultTransition; }
    void setDefaultTransition(const QString& id);
    double defaultTransitionLength() const { return m_defaultTransitionLength; }
    void setDefaultTransitionLength(double seconds);
    bool autoRipple() const { return m_autoRipple; }
    void setAutoRipple(bool on);
    bool skimming() const { return m_skimming; }
    void setSkimming(bool on);
    bool loopPlayback() const { return m_loopPlayback; }
    void setLoopPlayback(bool on);
    bool canvasBorder() const { return m_canvasBorder; }
    void setCanvasBorder(bool on);
    bool snapToBorder() const { return m_snapToBorder; }
    void setSnapToBorder(bool on);

    // Everything back as it comes (for tests).
    Q_INVOKABLE void reset();

signals:
    void changed();

private:
    EditingPreferences();
    void load();
    void save() const;
    template <typename T>
    void set(T& field, T value);

    QString m_file;
    QString m_defaultTransition = QStringLiteral("tve.transition.dissolve");
    double m_defaultTransitionLength = 1.0;
    bool m_autoRipple = false;
    bool m_skimming = false;
    bool m_loopPlayback = false;
    bool m_canvasBorder = true;
    bool m_snapToBorder = true;
};
