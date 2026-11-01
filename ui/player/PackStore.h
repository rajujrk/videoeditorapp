#pragma once

#include "packs/Packs.h"

#include <QByteArray>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class QNetworkAccessManager;
class QNetworkReply;

// The pack store: content packs (sounds, templates, stickers, effects, transitions) from a
// static host. It reads the host's catalogue — only if its signature is by a trusted key
// (:/effects/store/trusted-keys.json, or the file TVE_STORE_TRUSTED_KEYS) and it is no
// older than the last one seen from that address — and installs, updates and removes packs:
// each pack's manifest is checked against the catalogue, each file against the manifest
// (size and SHA-256) as it downloads into a staging folder, and only a whole, checked pack
// takes the place of the one installed; files an update leaves unchanged are copied from it.
// Packs live in <app data>/Packs/<id> (TVE_PACKS_DIR); the catalogue address and what was
// seen in <app data>/store.json (TVE_STORE_SETTINGS_FILE). Addresses are https (http only
// to this Mac); a pack's files are fetched beside its manifest, never from elsewhere.
class PackStore : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("The project's pack store")
    Q_PROPERTY(QString catalogueUrl READ catalogueUrl WRITE setCatalogueUrl NOTIFY changed)
    Q_PROPERTY(QString defaultCatalogueUrl READ defaultCatalogueUrl CONSTANT)
    // [{id, title, description, author, license, contents, size, version, installedVersion,
    //   state "available"/"installed"/"update"/"installing", progress, preview (a file URL)}]
    Q_PROPERTY(QVariantList packs READ packs NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString lastError READ lastError NOTIFY changed)
    // A catalogue was read this session (when, as the host published it).
    Q_PROPERTY(bool checked READ checked NOTIFY changed)
    Q_PROPERTY(QString published READ published NOTIFY changed)
    Q_PROPERTY(bool hasTrustedKey READ hasTrustedKey CONSTANT)
    // Effects or transitions came or went: they change when the app next starts.
    Q_PROPERTY(bool restartNeeded READ restartNeeded NOTIFY changed)

public:
    explicit PackStore(QObject* parent = nullptr);
    ~PackStore() override;

    static QString packsDirectory();
    // The folders of the installed packs, in id order.
    static QStringList installedFolders();
    static QString defaultCatalogueUrl();

    QString catalogueUrl() const { return m_catalogueUrl; }
    void setCatalogueUrl(const QString& url);
    QVariantList packs() const;
    bool busy() const { return !m_job.isEmpty(); }
    QString status() const { return m_status; }
    QString lastError() const { return m_error; }
    bool checked() const { return m_catalogue.has_value(); }
    QString published() const;
    bool hasTrustedKey() const { return !m_keys.empty(); }
    bool restartNeeded() const { return m_restartNeeded; }

    // Reads the catalogue (finished("refresh", …) says how it went).
    Q_INVOKABLE bool refresh();
    // Installs a pack from the catalogue, or updates it (finished("install", id, …)).
    Q_INVOKABLE bool install(const QString& id);
    Q_INVOKABLE bool remove(const QString& id);
    Q_INVOKABLE void cancel();

signals:
    void changed();
    // Packs holding these kinds of content were installed, updated or removed.
    void contentChanged(const QStringList& kinds);
    void finished(const QString& action, const QString& id, bool ok, const QString& message);

private:
    using Done = std::function<void(const QByteArray& data, const QString& error)>;
    struct Download;

    void loadSettings();
    void saveSettings() const;
    void loadInstalled();
    bool allowed(const QUrl& url) const;
    QUrl catalogueBase() const;
    // Fetches a small file whole (at most `limit` bytes); `job` replies can be cancelled.
    void fetch(const QUrl& url, qint64 limit, bool job, Done done);
    void fetchPreviews();
    void downloadNext();
    void finishInstall();
    void end(const QString& action, bool ok, const QString& message);
    void setStatus(const QString& status);

    QNetworkAccessManager* network();

    QNetworkAccessManager* m_network = nullptr;
    QString m_catalogueUrl;
    std::map<QString, qint64> m_sequences; // the newest catalogue seen, by address
    std::vector<std::string> m_keys;
    std::optional<tve::Catalogue> m_catalogue;
    std::map<QString, tve::PackManifest> m_installed;
    QString m_job; // "refresh", or the id being installed
    QPointer<QNetworkReply> m_reply;
    bool m_cancelled = false;
    std::unique_ptr<Download> m_download;
    QString m_status;
    QString m_error;
    bool m_restartNeeded = false;
};
