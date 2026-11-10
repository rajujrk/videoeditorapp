#include "PackStore.h"

#include "audio/SoundPacks.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QStandardPaths>

#include <algorithm>
#include <memory>

namespace {

QString dataPath(const char* variable, const QString& name)
{
    const QByteArray override = qgetenv(variable);
    return !override.isEmpty() ? QString::fromLocal8Bit(override)
                               : QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QLatin1Char('/') + name;
}

QString settingsFile()
{
    return dataPath("TVE_STORE_SETTINGS_FILE", QStringLiteral("store.json"));
}

QString text(const std::string& s)
{
    return QString::fromStdString(s);
}

QStringList kinds(const std::vector<std::string>& list)
{
    QStringList out;
    for (const std::string& kind : list)
        out << text(kind);
    return out;
}

QString sizeText(qint64 bytes)
{
    return bytes >= (1 << 20) ? QStringLiteral("%1 MB").arg(double(bytes) / (1 << 20), 0, 'f', 1)
                              : QStringLiteral("%1 KB").arg(std::max<qint64>(1, bytes / 1024));
}

// A relative URL of a pack path (its parts decoded as written).
QUrl relativeUrl(const std::string& path)
{
    QUrl url;
    url.setPath(text(path), QUrl::DecodedMode);
    return url;
}

// Deletes a folder, but only one strictly inside the packs folder (never an empty or
// relative path, which would mean the current folder).
bool removeInside(const QString& folder)
{
    const QString root = QDir::cleanPath(PackStore::packsDirectory());
    const QString target = QDir::cleanPath(folder);
    if (folder.isEmpty() || !QDir::isAbsolutePath(root) || !QDir::isAbsolutePath(target) || root.size() < 2
        || !target.startsWith(root + QLatin1Char('/')))
        return false;
    return QDir(target).removeRecursively();
}

QString previewFile(const tve::CatalogueEntry& entry)
{
    const QString suffix = QFileInfo(text(entry.preview)).suffix().toLower();
    return PackStore::packsDirectory() + QStringLiteral("/.previews/") + text(entry.id) + QLatin1Char('-')
           + QString::number(entry.version) + QLatin1Char('.') + suffix;
}

} // namespace

struct PackStore::Download {
    tve::CatalogueEntry entry;
    tve::PackManifest manifest;
    QUrl manifestUrl;
    QString staging;
    std::size_t index = 0;
    qint64 total = 0;
    qint64 done = 0;    // bytes of the files finished
    qint64 current = 0; // bytes of the file under way
    std::unique_ptr<QFile> file;
    std::unique_ptr<QCryptographicHash> hash;
    bool update = false;
};

PackStore::PackStore(QObject* parent) : QObject(parent)
{
    // The keys trusted to sign catalogues.
    const QByteArray keysPath = qgetenv("TVE_STORE_TRUSTED_KEYS");
    QFile keys(!keysPath.isEmpty() ? QString::fromLocal8Bit(keysPath) : QStringLiteral(":/effects/store/trusted-keys.json"));
    if (keys.open(QIODevice::ReadOnly))
        for (const QJsonValue& key : QJsonDocument::fromJson(keys.readAll()).object().value(QStringLiteral("keys")).toArray()) {
            const QByteArray der = QByteArray::fromBase64(key.toObject().value(QStringLiteral("publicKey")).toString().toLatin1());
            if (!der.isEmpty())
                m_keys.emplace_back(der.toStdString());
        }
    loadSettings();
    loadInstalled();
}

PackStore::~PackStore()
{
    if (m_reply)
        m_reply->abort();
}

QString PackStore::packsDirectory()
{
    return dataPath("TVE_PACKS_DIR", QStringLiteral("Packs"));
}

QStringList PackStore::installedFolders()
{
    QStringList folders;
    const QDir root(packsDirectory());
    for (const QString& name : root.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name))
        if (!name.startsWith(QLatin1Char('.')) && QFileInfo::exists(root.filePath(name) + QStringLiteral("/pack.json")))
            folders << root.filePath(name);
    return folders;
}

QString PackStore::defaultCatalogueUrl()
{
    const QByteArray override = qgetenv("TVE_STORE_URL");
    return !override.isEmpty() ? QString::fromLocal8Bit(override)
                               : QStringLiteral("https://rajujrk.github.io/videoeditorapp/packs/catalogue.json");
}

void PackStore::setCatalogueUrl(const QString& url)
{
    const QString next = url.trimmed().isEmpty() ? defaultCatalogueUrl() : url.trimmed();
    if (next == m_catalogueUrl)
        return;
    m_catalogueUrl = next;
    m_catalogue.reset();
    saveSettings();
    emit changed();
}

QString PackStore::published() const
{
    return m_catalogue ? text(m_catalogue->published) : QString();
}

QNetworkAccessManager* PackStore::network()
{
    if (!m_network)
        m_network = new QNetworkAccessManager(this);
    return m_network;
}

void PackStore::loadSettings()
{
    m_catalogueUrl = defaultCatalogueUrl();
    QFile file(settingsFile());
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QJsonObject o = QJsonDocument::fromJson(file.readAll()).object();
    if (const QString url = o.value(QStringLiteral("catalogueUrl")).toString(); !url.isEmpty())
        m_catalogueUrl = url;
    const QJsonObject sequences = o.value(QStringLiteral("sequences")).toObject();
    for (auto it = sequences.begin(); it != sequences.end(); ++it)
        m_sequences[it.key()] = qint64(it.value().toDouble());
}

void PackStore::saveSettings() const
{
    QJsonObject sequences;
    for (const auto& [url, sequence] : m_sequences)
        sequences.insert(url, double(sequence));
    QJsonObject o{{QStringLiteral("sequences"), sequences}};
    if (m_catalogueUrl != defaultCatalogueUrl())
        o.insert(QStringLiteral("catalogueUrl"), m_catalogueUrl);
    QDir().mkpath(QFileInfo(settingsFile()).absolutePath());
    QSaveFile file(settingsFile());
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(o).toJson());
        file.commit();
    }
}

void PackStore::loadInstalled()
{
    m_installed.clear();
    for (const QString& folder : installedFolders()) {
        QFile file(folder + QStringLiteral("/pack.json"));
        if (!file.open(QIODevice::ReadOnly))
            continue;
        if (auto manifest = tve::readPackManifest(file.readAll().toStdString()))
            m_installed[text(manifest->id)] = std::move(*manifest);
    }
}

QVariantList PackStore::packs() const
{
    QVariantList list;
    const auto progress = [&](const QString& id) {
        if (!m_download || m_job != id || m_download->total <= 0)
            return 0.0;
        return double(m_download->done + m_download->current) / double(m_download->total);
    };
    if (m_catalogue)
        for (const tve::CatalogueEntry& entry : m_catalogue->packs) {
            const QString id = text(entry.id);
            const auto installed = m_installed.find(id);
            const int installedVersion = installed != m_installed.end() ? installed->second.version : 0;
            const QString state = m_job == id                     ? QStringLiteral("installing")
                                : installedVersion == 0           ? QStringLiteral("available")
                                : installedVersion < entry.version ? QStringLiteral("update")
                                                                  : QStringLiteral("installed");
            const QString preview = entry.preview.empty() ? QString() : previewFile(entry);
            list << QVariantMap{{QStringLiteral("id"), id},
                                {QStringLiteral("title"), text(entry.title)},
                                {QStringLiteral("description"), text(entry.description)},
                                {QStringLiteral("author"), text(entry.author)},
                                {QStringLiteral("license"), text(entry.license)},
                                {QStringLiteral("contents"), kinds(entry.contents)},
                                {QStringLiteral("size"), double(entry.size)},
                                {QStringLiteral("sizeText"), sizeText(entry.size)},
                                {QStringLiteral("version"), entry.version},
                                {QStringLiteral("installedVersion"), installedVersion},
                                {QStringLiteral("state"), state},
                                {QStringLiteral("progress"), progress(id)},
                                {QStringLiteral("preview"), !preview.isEmpty() && QFileInfo::exists(preview)
                                                                ? QUrl::fromLocalFile(preview).toString() : QString()}};
        }
    // Installed packs the catalogue doesn't list (or none was read).
    for (const auto& [id, manifest] : m_installed) {
        if (m_catalogue && std::ranges::any_of(m_catalogue->packs, [&](const tve::CatalogueEntry& e) { return text(e.id) == id; }))
            continue;
        qint64 size = 0;
        for (const tve::PackFile& file : manifest.files)
            size += file.size;
        list << QVariantMap{{QStringLiteral("id"), id},
                            {QStringLiteral("title"), text(manifest.title)},
                            {QStringLiteral("description"), text(manifest.description)},
                            {QStringLiteral("author"), text(manifest.author)},
                            {QStringLiteral("license"), text(manifest.license)},
                            {QStringLiteral("contents"), kinds(tve::packContents(manifest))},
                            {QStringLiteral("size"), double(size)},
                            {QStringLiteral("sizeText"), sizeText(size)},
                            {QStringLiteral("version"), manifest.version},
                            {QStringLiteral("installedVersion"), manifest.version},
                            {QStringLiteral("state"), QStringLiteral("installed")},
                            {QStringLiteral("progress"), 0.0},
                            {QStringLiteral("preview"), QString()}};
    }
    return list;
}

bool PackStore::allowed(const QUrl& url) const
{
    if (!url.isValid())
        return false;
    if (url.scheme() == QLatin1String("https"))
        return true;
    // Plain http only to this Mac (tests, a local copy of the site).
    const QString host = url.host();
    return url.scheme() == QLatin1String("http")
           && (host == QLatin1String("localhost") || host == QLatin1String("127.0.0.1") || host == QLatin1String("::1"));
}

QUrl PackStore::catalogueBase() const
{
    return QUrl(m_catalogueUrl);
}

void PackStore::setStatus(const QString& status)
{
    m_status = status;
    emit changed();
}

void PackStore::fetch(const QUrl& url, qint64 limit, bool job, Done done)
{
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply* reply = network()->get(request);
    if (job)
        m_reply = reply;
    auto data = std::make_shared<QByteArray>();
    auto tooBig = std::make_shared<bool>(false);
    connect(reply, &QNetworkReply::readyRead, this, [reply, data, tooBig, limit] {
        *data += reply->readAll();
        if (data->size() > limit) {
            *tooBig = true;
            reply->abort();
        }
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, data, tooBig, limit, done, url] {
        reply->deleteLater();
        if (reply->isOpen())
            *data += reply->readAll();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (*tooBig || data->size() > limit)
            return done({}, QStringLiteral("%1 is bigger than it may be").arg(url.fileName()));
        if (m_cancelled)
            return done({}, QString());
        if (status == 404)
            return done({}, tr("Nothing at %1").arg(url.toDisplayString()));
        if (reply->error() != QNetworkReply::NoError)
            return done({}, tr("Couldn't reach the store: %1").arg(reply->errorString()));
        if (!allowed(reply->url()))
            return done({}, tr("The store sent us to an address it may not use"));
        done(*data, QString());
    });
}

bool PackStore::refresh()
{
    if (busy())
        return false;
    m_error.clear();
    const QUrl url(m_catalogueUrl);
    if (m_keys.empty()) {
        end(QStringLiteral("refresh"), false,
            tr("No signing key is trusted yet, so no catalogue can be checked (see tools/packs/README.md)"));
        return false;
    }
    if (!allowed(url)) {
        end(QStringLiteral("refresh"), false, tr("The catalogue address must start with https://"));
        return false;
    }
    m_job = QStringLiteral("refresh");
    m_cancelled = false;
    setStatus(tr("Checking the catalogue…"));
    fetch(url, qint64(tve::kMostCatalogueBytes), true, [this, url](const QByteArray& catalogue, const QString& error) {
        if (!error.isEmpty() || m_cancelled)
            return end(QStringLiteral("refresh"), false, error);
        QUrl signatureUrl = url;
        signatureUrl.setPath(url.path() + QStringLiteral(".sig"));
        fetch(signatureUrl, 1024, true, [this, url, catalogue](const QByteArray& signature, const QString& error) {
            if (!error.isEmpty() || m_cancelled)
                return end(QStringLiteral("refresh"), false, error);
            const std::string data = catalogue.toStdString();
            const bool signedByUs = std::ranges::any_of(m_keys, [&](const std::string& key) {
                return tve::verifySignature(data, signature.toStdString(), key);
            });
            if (!signedByUs)
                return end(QStringLiteral("refresh"), false,
                           tr("The catalogue's signature isn't one we trust, so it was not used"));
            std::string why;
            auto read = tve::readCatalogue(data, &why);
            if (!read)
                return end(QStringLiteral("refresh"), false, tr("The catalogue can't be read: %1").arg(text(why)));
            // Never an older catalogue than one already seen (a replayed one could hide updates).
            const QString key = url.toString();
            if (const auto seen = m_sequences.find(key); seen != m_sequences.end() && read->sequence < seen->second)
                return end(QStringLiteral("refresh"), false,
                           tr("The catalogue is older than one seen before, so it was not used"));
            m_sequences[key] = read->sequence;
            saveSettings();
            m_catalogue = std::move(read);
            end(QStringLiteral("refresh"), true, {});
            fetchPreviews();
        });
    });
    return true;
}

void PackStore::fetchPreviews()
{
    if (!m_catalogue)
        return;
    for (const tve::CatalogueEntry& entry : m_catalogue->packs) {
        if (entry.preview.empty())
            continue;
        const QString path = previewFile(entry);
        const QString suffix = QFileInfo(path).suffix();
        if (QFileInfo::exists(path) || (suffix != QLatin1String("png") && suffix != QLatin1String("jpg") && suffix != QLatin1String("jpeg")))
            continue;
        const std::string sha = entry.previewSha256;
        fetch(catalogueBase().resolved(relativeUrl(entry.preview)), 4 << 20, false, [this, path, sha](const QByteArray& data, const QString& error) {
            if (!error.isEmpty() || tve::bytesSha256(data.toStdString()) != sha)
                return;
            QDir().mkpath(QFileInfo(path).absolutePath());
            QSaveFile file(path);
            if (file.open(QIODevice::WriteOnly) && file.write(data) == data.size() && file.commit())
                emit changed();
        });
    }
}

bool PackStore::install(const QString& id)
{
    if (busy()) {
        m_error = tr("Wait for the store to finish what it is doing");
        emit changed();
        return false;
    }
    const tve::CatalogueEntry* entry = nullptr;
    if (m_catalogue)
        for (const tve::CatalogueEntry& e : m_catalogue->packs)
            if (text(e.id) == id)
                entry = &e;
    if (!entry) {
        end(QStringLiteral("install"), false, tr("The catalogue has no pack %1").arg(id));
        return false;
    }
    m_error.clear();
    m_job = id;
    m_cancelled = false;
    m_download = std::make_unique<Download>();
    m_download->entry = *entry;
    m_download->manifestUrl = catalogueBase().resolved(relativeUrl(entry->manifest));
    m_download->update = m_installed.contains(id);
    setStatus(tr("Getting %1…").arg(text(entry->title)));
    fetch(m_download->manifestUrl, 4 << 20, true, [this](const QByteArray& data, const QString& error) {
        if (!error.isEmpty() || m_cancelled)
            return end(QStringLiteral("install"), false, error);
        Download& d = *m_download;
        if (tve::bytesSha256(data.toStdString()) != d.entry.manifestSha256)
            return end(QStringLiteral("install"), false, tr("The pack's manifest is not the one the catalogue lists"));
        std::string why;
        auto manifest = tve::readPackManifest(data.toStdString(), &why);
        if (!manifest)
            return end(QStringLiteral("install"), false, tr("The pack's manifest can't be read: %1").arg(text(why)));
        if (manifest->id != d.entry.id || manifest->version != d.entry.version)
            return end(QStringLiteral("install"), false, tr("The pack's manifest is for another pack or version"));
        d.manifest = std::move(*manifest);
        for (const tve::PackFile& file : d.manifest.files)
            d.total += file.size;
        d.staging = packsDirectory() + QStringLiteral("/.staging/") + m_job;
        removeInside(d.staging);
        if (!QDir().mkpath(d.staging))
            return end(QStringLiteral("install"), false, tr("Can't write to %1").arg(packsDirectory()));
        downloadNext();
    });
    return true;
}

void PackStore::downloadNext()
{
    Download& d = *m_download;
    const QString title = text(d.entry.title);
    // Files the installed version already has, unchanged, are copied from it.
    const auto installed = m_installed.find(m_job);
    while (d.index < d.manifest.files.size()) {
        const tve::PackFile& file = d.manifest.files[d.index];
        const QString target = d.staging + QLatin1Char('/') + text(file.path);
        QDir().mkpath(QFileInfo(target).absolutePath());
        bool reused = false;
        if (installed != m_installed.end()
            && std::ranges::any_of(installed->second.files, [&](const tve::PackFile& f) { return f.path == file.path && f.sha256 == file.sha256; })) {
            const QString source = packsDirectory() + QLatin1Char('/') + m_job + QLatin1Char('/') + text(file.path);
            reused = QFile::copy(source, target) && tve::fileSha256(target.toStdString()) == file.sha256;
            if (!reused)
                QFile::remove(target);
        }
        if (!reused)
            break;
        d.done += file.size;
        ++d.index;
    }
    if (d.index == d.manifest.files.size())
        return finishInstall();

    const tve::PackFile& file = d.manifest.files[d.index];
    setStatus(tr("Downloading %1 — %2 of %3").arg(title).arg(d.index + 1).arg(d.manifest.files.size()));
    d.file = std::make_unique<QFile>(d.staging + QLatin1Char('/') + text(file.path));
    if (!d.file->open(QIODevice::WriteOnly | QIODevice::Truncate))
        return end(QStringLiteral("install"), false, tr("Can't write to %1").arg(packsDirectory()));
    d.hash = std::make_unique<QCryptographicHash>(QCryptographicHash::Sha256);
    d.current = 0;
    QNetworkRequest request(d.manifestUrl.resolved(relativeUrl(file.path)));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply* reply = network()->get(request);
    m_reply = reply;
    const qint64 size = file.size;
    connect(reply, &QNetworkReply::readyRead, this, [this, reply, size] {
        Download& d = *m_download;
        const QByteArray data = reply->readAll();
        d.current += data.size();
        if (d.current > size) {
            reply->abort();
            return;
        }
        d.file->write(data);
        d.hash->addData(data);
        emit changed();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, path = file.path, size, sha = file.sha256] {
        reply->deleteLater();
        Download& d = *m_download;
        const QByteArray rest = reply->isOpen() ? reply->readAll() : QByteArray();
        d.current += rest.size();
        if (d.current <= size) {
            d.file->write(rest);
            d.hash->addData(rest);
        }
        d.file->close();
        if (m_cancelled)
            return end(QStringLiteral("install"), false, {});
        if (d.current > size || (reply->error() == QNetworkReply::NoError && (d.current != size
                                                                                || QString::fromLatin1(d.hash->result().toHex()) != text(sha))))
            return end(QStringLiteral("install"), false, QStringLiteral("%1 is not the file the pack lists").arg(text(path)));
        if (reply->error() != QNetworkReply::NoError)
            return end(QStringLiteral("install"), false, tr("Download failed: %1").arg(reply->errorString()));
        if (!allowed(reply->url()))
            return end(QStringLiteral("install"), false, tr("The store sent us to an address it may not use"));
        d.done += size;
        d.current = 0;
        ++d.index;
        downloadNext();
    });
}

void PackStore::finishInstall()
{
    Download& d = *m_download;
    {
        QSaveFile manifest(d.staging + QStringLiteral("/pack.json"));
        const QByteArray json = QByteArray::fromStdString(tve::packManifestJson(d.manifest));
        if (!manifest.open(QIODevice::WriteOnly) || manifest.write(json) != json.size() || !manifest.commit())
            return end(QStringLiteral("install"), false, tr("Can't write to %1").arg(packsDirectory()));
    }
    if (auto why = tve::checkPackFolder(d.staging.toStdString(), d.manifest))
        return end(QStringLiteral("install"), false, tr("The pack didn't download whole: %1").arg(text(*why)));
    // The checked pack takes the place of the one installed.
    const QString target = packsDirectory() + QLatin1Char('/') + m_job;
    const QString old = packsDirectory() + QStringLiteral("/.old/") + m_job;
    removeInside(old);
    QDir().mkpath(QFileInfo(old).absolutePath());
    if (QFileInfo::exists(target) && !QDir().rename(target, old))
        return end(QStringLiteral("install"), false, tr("Can't replace the installed pack"));
    if (!QDir().rename(d.staging, target)) {
        QDir().rename(old, target);
        return end(QStringLiteral("install"), false, tr("Can't install the pack in %1").arg(packsDirectory()));
    }
    removeInside(old);
    std::vector<std::string> contents = tve::packContents(d.manifest);
    if (const auto before = m_installed.find(m_job); before != m_installed.end())
        for (const std::string& kind : tve::packContents(before->second))
            if (std::ranges::find(contents, kind) == contents.end())
                contents.push_back(kind);
    loadInstalled();
    if (std::ranges::any_of(contents, [](const std::string& k) { return k == "effects" || k == "transitions"; }))
        m_restartNeeded = true;
    const QString message = (d.update ? tr("Updated %1") : tr("Installed %1")).arg(text(d.entry.title));
    emit contentChanged(kinds(contents));
    end(QStringLiteral("install"), true, message);
}

bool PackStore::remove(const QString& id)
{
    const auto installed = m_installed.find(id);
    if (installed == m_installed.end() || m_job == id || !tve::validPackId(id.toStdString()))
        return false;
    const std::vector<std::string> contents = tve::packContents(installed->second);
    if (!removeInside(packsDirectory() + QLatin1Char('/') + id)) {
        m_error = tr("Can't remove the pack from %1").arg(packsDirectory());
        emit changed();
        return false;
    }
    loadInstalled();
    if (std::ranges::any_of(contents, [](const std::string& k) { return k == "effects" || k == "transitions"; }))
        m_restartNeeded = true;
    emit contentChanged(kinds(contents));
    emit changed();
    emit finished(QStringLiteral("remove"), id, true, {});
    return true;
}

void PackStore::cancel()
{
    if (!busy())
        return;
    m_cancelled = true;
    if (m_reply)
        m_reply->abort();
}

void PackStore::end(const QString& action, bool ok, const QString& message)
{
    const QString id = action == QLatin1String("refresh") ? QString() : m_job;
    if (m_download && !ok) {
        if (m_download->file)
            m_download->file->close();
        removeInside(m_download->staging);
    }
    m_download.reset();
    m_job.clear();
    m_reply = nullptr;
    m_cancelled = false;
    m_status.clear();
    m_error = ok ? QString() : message;
    emit changed();
    emit finished(action, id, ok, message);
}
