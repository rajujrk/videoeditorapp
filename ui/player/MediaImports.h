#pragma once

#include "imports/ImportSources.h"

#include <QObject>
#include <QQuickAsyncImageProvider>
#include <QStringList>
#include <QThreadPool>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <memory>

// Importing from the Photos library ("photos"), the Music library ("music") and devices
// ("devices"), for QML. TVE_PHOTOS_SOURCE, TVE_MUSIC_SOURCE and TVE_DEVICE_SOURCE name test
// folders to use instead (their sub-folders are the albums, playlists and devices). Fetched
// items land in the folder the controller gives; fetched() hands their paths back.
class MediaImports : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("The project's imports")
    Q_PROPERTY(bool fetching READ fetching NOTIFY fetchingChanged)
    // Items fetched so far and in all, while fetching.
    Q_PROPERTY(int fetchedCount READ fetchedCount NOTIFY fetchingChanged)
    Q_PROPERTY(int fetchTotal READ fetchTotal NOTIFY fetchingChanged)

public:
    explicit MediaImports(QObject* parent = nullptr);
    ~MediaImports() override;

    // The source by name, made the first time (on the main thread); nullptr for none. The
    // thumbnail provider uses the ones made already.
    static std::shared_ptr<tve::ImportSource> source(const QString& name, bool make = true);

    // "granted", "denied" or "undetermined".
    Q_INVOKABLE QString access(const QString& source) const;
    // Asks; changed() follows the answer.
    Q_INVOKABLE void requestAccess(const QString& source);
    // [{id, title, count}]
    Q_INVOKABLE QVariantList collections(const QString& source) const;
    // [{id, title, detail, kind, duration, width, height, created (ms since 1970), thumbnail}]
    // with thumbnail an image URL ("" when there is none).
    Q_INVOKABLE QVariantList items(const QString& source, const QString& collection) const;
    // Fetches items into `folder`, one after another; false if a fetch is running already.
    bool fetch(const QString& source, const QStringList& itemIds, const QString& folder);
    // Opens the Privacy & Security pane for the source in System Settings.
    Q_INVOKABLE void openPrivacySettings(const QString& source) const;

    bool fetching() const { return m_fetchTotal > 0; }
    int fetchedCount() const { return m_fetched; }
    int fetchTotal() const { return m_fetchTotal; }

signals:
    // Access answered, or devices came or went.
    void changed();
    void fetchingChanged();
    // A fetch ended: the files it made (or found), and what could not be fetched.
    void fetched(const QStringList& paths, const QStringList& errors);

private:
    void fetchNext();

    QString m_source;
    QStringList m_queue;
    QString m_folder;
    QStringList m_paths;
    QStringList m_errors;
    int m_fetched = 0;
    int m_fetchTotal = 0;
};

// Thumbnails of import items: image://tveimport/<source>/<url-encoded item id>@<size>.
class ImportThumbnailProvider : public QQuickAsyncImageProvider {
public:
    static constexpr const char* kId = "tveimport";

    ImportThumbnailProvider();
    QQuickImageResponse* requestImageResponse(const QString& id, const QSize& requestedSize) override;

private:
    QThreadPool m_pool;
};
