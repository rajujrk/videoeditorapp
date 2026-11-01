#pragma once

#include "audio/plugins/AudioPlugin.h"
#include "model/Project.h"

#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariantList>

#include <functional>
#include <map>
#include <memory>

// The audio plug-ins being looked at in the app: one instance for each effect whose settings
// the inspector shows or whose own window is open, keyed by where it is ("clip:<id>/<index>"
// or "track:<id>/<index>"), kept in step with the project (its saved state, then its
// values). Changes made in a plug-in's own window come back as edited(): its whole state,
// once they pause or the window closes. (The engine plays its own instances.)
class AudioPluginHost : public QObject {
    Q_OBJECT
public:
    explicit AudioPluginHost(QObject* parent = nullptr);
    ~AudioPluginHost() override;

    // The instance for the effect at `key`, opened (or reopened when the effect changed) with
    // its state and values; nullptr with `error` when the plug-in can't be opened.
    tve::AudioPlugin* instance(const QString& key, const tve::ClipEffect& effect, QString* error = nullptr);

    // Its parameters for the inspector: [{id, title, type "float"/"int", min 0, max 1,
    // default (its value now), steps, unit, keyframable}].
    QVariantList parameters(const QString& key, const tve::ClipEffect& effect);
    QString valueText(const QString& key, const tve::ClipEffect& effect, const QString& param, double value);

    // Shows its own window (titled `title`; some plug-ins give their view a moment later);
    // false with `error` if the plug-in can't be opened.
    bool openWindow(const QString& key, const tve::ClipEffect& effect, const QString& title, QString* error = nullptr);
    bool isWindowOpen(const QString& key) const;
    void closeWindow(const QString& key);
    void closeAll();
    // As if its own window had set a parameter (for tests and scripting).
    bool editLive(const QString& key, const tve::ClipEffect& effect, const QString& param, double value);

    // Drops instances whose effect is gone (`find` gives the effect at a key, or null) and
    // brings the others up to date.
    void sync(const std::function<const tve::ClipEffect*(const QString& key)>& find);

signals:
    // Its own window changed it: its whole state now, base64.
    void edited(const QString& key, const QString& state);

private:
    struct Session {
        std::unique_ptr<tve::AudioPlugin> plugin;
        std::string effectId;
        std::string state;  // as loaded or last sent (base64)
        std::map<std::string, std::vector<float>> values;
        void* window = nullptr; // NSWindow*
        void* delegate = nullptr;
        std::string openedState;
        QTimer* pause = nullptr;
    };
    void bring(Session& session, const tve::ClipEffect& effect);
    void send(const QString& key);
    void dropWindow(Session& session);

    std::map<QString, Session> m_sessions;
};
