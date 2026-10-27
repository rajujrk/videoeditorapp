#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantMap>

#include <atomic>
#include <map>
#include <thread>

// Proxies of media files, kept in a cache folder (Caches/…/Proxies; TVE_PROXY_DIR
// overrides) under a name made from the source's path, size and modification time, so a
// changed source gets a new one. Makes them in the background, one at a time.
class ProxyManager : public QObject {
    Q_OBJECT

public:
    enum class State { None, Queued, Making, Ready, Failed };

    explicit ProxyManager(QObject* parent = nullptr);
    ~ProxyManager() override;

    QString directory() const { return m_directory; }
    void setDirectory(const QString& directory);

    // The proxy file for `source` if it is ready, else empty.
    QString readyProxy(const QString& source) const;
    State state(const QString& source) const;
    double progress(const QString& source) const; // 0–1 while making
    // {source path: {state: "queued"|"making"|"ready"|"failed", progress, error}} for every
    // source asked about (and every ready one among `sources`).
    QVariantMap states(const QStringList& sources) const;

    // Queues a proxy for `source` unless it is ready, queued or being made.
    void request(const QString& source);
    // Stops making it and deletes it.
    void remove(const QString& source);

signals:
    // A proxy's state or progress changed; `ready` when one has just become usable.
    void changed(const QString& source, bool ready);

private:
    struct Job {
        State state = State::None;
        double progress = 0;
        QString error;
    };

    QString proxyPath(const QString& source) const;
    void startNext();

    QString m_directory;
    std::map<QString, Job> m_jobs;
    QStringList m_queue;
    QString m_current;
    std::thread m_worker;
    std::atomic<bool> m_cancel{false};
};
