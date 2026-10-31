#pragma once

#include "audio/SoundPacks.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <memory>
#include <optional>
#include <vector>

namespace tve {
class SoundPreview;
}

// The Audio tab's sounds: the built-in music and effects (rendered from their recipes into a
// cache the first time they are used), the sound packs installed in the packs folder, and
// My music (audio files in folders the person chose). Previews play on their own device.
// Folders: <app data>/Sounds for the cache (TVE_SOUND_CACHE_DIR), <app data>/Sound Packs
// (TVE_SOUND_PACK_DIR), and the chosen music folders in <app data>/music-folders.json
// (TVE_MUSIC_FOLDERS_FILE). TVE_AUDIO=null previews without a device.
class SoundLibrary : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("The project's sound library")
    // [{id, kind "music"/"sfx", title, category, duration, pack}] (pack: "" for built-in).
    Q_PROPERTY(QVariantList sounds READ sounds NOTIFY changed)
    // [{id "file:<path>", path, title, folder, duration}]
    Q_PROPERTY(QVariantList myMusic READ myMusic NOTIFY changed)
    Q_PROPERTY(QStringList musicFolders READ musicFolders NOTIFY changed)
    // Sounds left out of packs, and why.
    Q_PROPERTY(QStringList packProblems READ packProblems NOTIFY changed)
    // The id previewing ("" when none), and how far it has got (0..1).
    Q_PROPERTY(QString previewing READ previewing NOTIFY previewChanged)
    Q_PROPERTY(double previewProgress READ previewProgress NOTIFY previewProgressChanged)

public:
    explicit SoundLibrary(QObject* parent = nullptr);
    ~SoundLibrary() override;

    QVariantList sounds() const { return m_sounds; }
    QVariantList myMusic() const { return m_myMusic; }
    QStringList musicFolders() const { return m_folders; }
    QStringList packProblems() const { return m_packProblems; }
    QString previewing() const { return m_previewing; }
    double previewProgress() const { return m_previewProgress; }

    // Plays a sound's preview; the same id again stops it. False (with `error`) if it cannot.
    Q_INVOKABLE bool preview(const QString& id);
    Q_INVOKABLE void stopPreview();
    Q_INVOKABLE bool addMusicFolder(const QUrl& folder);
    Q_INVOKABLE void removeMusicFolder(const QString& folder);
    // Checks a pack and copies it into the packs folder.
    Q_INVOKABLE bool importPack(const QUrl& folder);
    // Reads the packs and the music folders again.
    Q_INVOKABLE void refresh();
    Q_INVOKABLE QString lastError() const { return m_error; }

    // The file to put on the timeline for a sound (rendering a built-in one into the cache),
    // and its title; nullopt (with `error`) if there is none.
    struct File {
        QString path;
        QString title;
        QString kind;
    };
    std::optional<File> fileFor(const QString& id, QString* error);

    QString cacheDirectory() const;
    QString packsDirectory() const;

signals:
    void changed();
    void previewChanged();
    void previewProgressChanged();

private:
    void loadFolders();
    void saveFolders() const;
    void scanMusic();
    // Interleaved stereo 48 kHz for a preview (at most `seconds` of it).
    std::optional<std::vector<float>> previewSamples(const QString& id, double seconds, QString* error);

    QVariantList m_sounds;
    QVariantList m_myMusic;
    QStringList m_folders;
    QStringList m_packProblems;
    std::vector<tve::SoundPack> m_packs;
    std::unique_ptr<tve::SoundPreview> m_preview;
    QString m_previewing;
    double m_previewProgress = 0;
    QTimer m_previewClock;
    QString m_error;
};
