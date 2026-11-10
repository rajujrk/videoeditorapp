#include "SoundLibrary.h"
#include "Language.h"
#include "PackStore.h"

#include "audio/AudioOutput.h"
#include "audio/SoundPreview.h"
#include "audio/Synth.h"
#include "decode/AudioDecoder.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>

#include <algorithm>

using namespace tve;

namespace {

constexpr int kRate = 48000;
constexpr double kPreviewSeconds = 30;
constexpr int kMostMusicFiles = 500;
const QString kFilePrefix = QStringLiteral("file:");

QString dataDirectory(const char* variable, const QString& name)
{
    const QByteArray override = qgetenv(variable);
    return !override.isEmpty() ? QString::fromLocal8Bit(override)
                               : QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QLatin1Char('/') + name;
}

QString foldersFile()
{
    return dataDirectory("TVE_MUSIC_FOLDERS_FILE", QStringLiteral("music-folders.json"));
}

bool isSoundFile(const QString& path)
{
    static const QStringList kinds{QStringLiteral("mp3"), QStringLiteral("m4a"), QStringLiteral("aac"),
                                   QStringLiteral("wav"), QStringLiteral("aif"), QStringLiteral("aiff"),
                                   QStringLiteral("flac"), QStringLiteral("caf"), QStringLiteral("ogg")};
    return kinds.contains(QFileInfo(path).suffix().toLower());
}

// Up to `seconds` of a file, as the engine's stereo 48 kHz.
std::optional<std::vector<float>> decodeFile(const QString& path, double seconds, QString* error)
{
    std::string why;
    auto decoder = AudioDecoder::open(path.toStdString(), &why);
    if (!decoder) {
        if (error)
            *error = coreText(why);
        return std::nullopt;
    }
    std::vector<float> samples(std::size_t(seconds * kRate) * 2);
    const int frames = decoder->read(samples.data(), int(samples.size() / 2));
    samples.resize(std::size_t(std::max(frames, 0)) * 2);
    return samples;
}

// Fades out the last half second of a preview cut short.
void fadeOut(std::vector<float>& samples)
{
    const std::size_t frames = samples.size() / 2;
    const std::size_t fade = std::min<std::size_t>(frames, kRate / 2);
    for (std::size_t f = frames - fade; f < frames; ++f) {
        const float gain = float(frames - f) / float(fade);
        samples[f * 2] *= gain;
        samples[f * 2 + 1] *= gain;
    }
}

AudioOutput::Backend previewBackend()
{
    const QByteArray mode = qgetenv("TVE_AUDIO");
    return mode == "null"   ? AudioOutput::Backend::Null
         : mode == "none" ? AudioOutput::Backend::None
                          : AudioOutput::Backend::System;
}

} // namespace

SoundLibrary::SoundLibrary(QObject* parent)
    : QObject(parent), m_preview(std::make_unique<SoundPreview>(previewBackend()))
{
    m_previewClock.setInterval(66);
    connect(&m_previewClock, &QTimer::timeout, this, [this] {
        m_previewProgress = m_preview->progress();
        emit previewProgressChanged();
        if (!m_preview->playing())
            stopPreview();
    });
    loadFolders();
    refresh();
}

SoundLibrary::~SoundLibrary() = default;

QString SoundLibrary::cacheDirectory() const
{
    return dataDirectory("TVE_SOUND_CACHE_DIR", QStringLiteral("Sounds"));
}

QString SoundLibrary::packsDirectory() const
{
    return dataDirectory("TVE_SOUND_PACK_DIR", QStringLiteral("Sound Packs"));
}

void SoundLibrary::refresh()
{
    m_sounds.clear();
    for (const SoundRecipe& recipe : builtInSounds())
        m_sounds << QVariantMap{{QStringLiteral("id"), QString::fromStdString(recipe.id)},
                                {QStringLiteral("kind"), QString::fromStdString(recipe.kind)},
                                {QStringLiteral("title"), catalogueText(recipe.title)},
                                {QStringLiteral("category"), QString::fromStdString(recipe.category)},
                                {QStringLiteral("duration"), soundDuration(recipe)},
                                {QStringLiteral("pack"), QString()}};
    m_packs = loadSoundPacks(packsDirectory().toStdString());
    // Installed content packs' sounds.
    for (const QString& pack : PackStore::installedFolders())
        if (auto sounds = loadSoundPack((pack + QStringLiteral("/sounds")).toStdString()))
            m_packs.push_back(std::move(*sounds));
    m_packProblems.clear();
    for (const SoundPack& pack : m_packs) {
        const QString packTitle = QString::fromStdString(pack.title.empty() ? pack.id : pack.title);
        for (const PackSound& sound : pack.sounds)
            m_sounds << QVariantMap{{QStringLiteral("id"), QString::fromStdString(pack.id + "/" + sound.id)},
                                    {QStringLiteral("kind"), QString::fromStdString(sound.kind)},
                                    {QStringLiteral("title"), catalogueText(sound.title)},
                                    {QStringLiteral("category"), QString::fromStdString(sound.category)},
                                    {QStringLiteral("duration"), sound.duration},
                                    {QStringLiteral("pack"), packTitle}};
        for (const std::string& problem : pack.problems)
            m_packProblems << packTitle + QStringLiteral(": ") + QString::fromStdString(problem);
    }
    scanMusic();
    emit changed();
}

void SoundLibrary::loadFolders()
{
    QFile file(foldersFile());
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QJsonArray folders = QJsonDocument::fromJson(file.readAll()).object().value(QStringLiteral("folders")).toArray();
    for (const QJsonValue& folder : folders)
        if (folder.isString() && !m_folders.contains(folder.toString()))
            m_folders << folder.toString();
}

void SoundLibrary::saveFolders() const
{
    QDir().mkpath(QFileInfo(foldersFile()).absolutePath());
    QSaveFile file(foldersFile());
    if (!file.open(QIODevice::WriteOnly))
        return;
    file.write(QJsonDocument(QJsonObject{{QStringLiteral("folders"), QJsonArray::fromStringList(m_folders)}}).toJson());
    file.commit();
}

void SoundLibrary::scanMusic()
{
    m_myMusic.clear();
    for (const QString& folder : m_folders) {
        QStringList paths;
        QDirIterator files(folder, QDir::Files, QDirIterator::Subdirectories);
        while (files.hasNext() && paths.size() < kMostMusicFiles) {
            const QString path = files.next();
            if (isSoundFile(path))
                paths << path;
        }
        std::ranges::sort(paths, [](const QString& a, const QString& b) {
            return QFileInfo(a).completeBaseName().compare(QFileInfo(b).completeBaseName(), Qt::CaseInsensitive) < 0;
        });
        for (const QString& path : paths) {
            auto decoder = AudioDecoder::open(path.toStdString());
            if (!decoder)
                continue;
            m_myMusic << QVariantMap{{QStringLiteral("id"), kFilePrefix + path},
                                     {QStringLiteral("path"), path},
                                     {QStringLiteral("title"), QFileInfo(path).completeBaseName()},
                                     {QStringLiteral("folder"), folder},
                                     {QStringLiteral("duration"), double(decoder->info().duration) / double(kTicksPerSecond)}};
        }
    }
}

bool SoundLibrary::addMusicFolder(const QUrl& folder)
{
    const QString path = QDir::cleanPath(folder.isLocalFile() ? folder.toLocalFile() : folder.toString());
    if (!QFileInfo(path).isDir()) {
        m_error = QStringLiteral("%1 is not a folder").arg(path);
        return false;
    }
    if (!m_folders.contains(path)) {
        m_folders << path;
        saveFolders();
        scanMusic();
        emit changed();
    }
    return true;
}

void SoundLibrary::removeMusicFolder(const QString& folder)
{
    if (m_folders.removeAll(folder) == 0)
        return;
    saveFolders();
    scanMusic();
    emit changed();
}

bool SoundLibrary::importPack(const QUrl& folder)
{
    const QString source = QDir::cleanPath(folder.isLocalFile() ? folder.toLocalFile() : folder.toString());
    std::string why;
    const auto pack = loadSoundPack(source.toStdString(), &why);
    if (!pack) {
        m_error = coreText(why);
        return false;
    }
    if (pack->sounds.empty()) {
        m_error = tr("The pack has no sounds that check out.");
        return false;
    }
    // Copied whole into a folder named after its id (replacing an older copy).
    QString name = QString::fromStdString(pack->id);
    name.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9._-]")), QStringLiteral("_"));
    const QString target = packsDirectory() + QLatin1Char('/') + name;
    if (QDir::cleanPath(target) != source) {
        QDir(target).removeRecursively();
        QDirIterator files(source, QDir::Files, QDirIterator::Subdirectories);
        while (files.hasNext()) {
            const QString file = files.next();
            const QString copy = target + QLatin1Char('/') + QDir(source).relativeFilePath(file);
            QDir().mkpath(QFileInfo(copy).absolutePath());
            if (!QFile::copy(file, copy)) {
                m_error = tr("The pack could not be copied.");
                QDir(target).removeRecursively();
                return false;
            }
        }
    }
    refresh();
    return true;
}

std::optional<SoundLibrary::File> SoundLibrary::fileFor(const QString& id, QString* error)
{
    if (id.startsWith(kFilePrefix)) {
        const QString path = id.mid(kFilePrefix.size());
        if (!QFileInfo::exists(path)) {
            if (error)
                *error = QStringLiteral("%1 is no longer there").arg(path);
            return std::nullopt;
        }
        return File{path, QFileInfo(path).completeBaseName(), QStringLiteral("music")};
    }
    if (const SoundRecipe* recipe = findSound(id.toStdString())) {
        // Cached by the recipe's contents, so a changed recipe renders afresh; the file has
        // the sound's title, which names its clips.
        const QByteArray digest = QCryptographicHash::hash(QByteArray::fromStdString(recipe->json), QCryptographicHash::Sha256);
        QString title = QString::fromStdString(recipe->title);
        title.replace(QRegularExpression(QStringLiteral("[/:\\\\]")), QStringLiteral("-"));
        const QString path = cacheDirectory() + QLatin1Char('/') + id + QLatin1Char('-')
                           + QString::fromLatin1(digest.toHex().left(12)) + QLatin1Char('/') + title + QStringLiteral(".wav");
        if (!QFileInfo::exists(path)) {
            std::string why;
            auto samples = renderSound(*recipe, &why);
            if (!samples || !writeSoundFile(path.toStdString(), *samples, &why)) {
                if (error)
                    *error = coreText(why);
                return std::nullopt;
            }
        }
        return File{path, QString::fromStdString(recipe->title), QString::fromStdString(recipe->kind)};
    }
    for (const SoundPack& pack : m_packs)
        for (const PackSound& sound : pack.sounds)
            if (QString::fromStdString(pack.id + "/" + sound.id) == id)
                return File{QString::fromStdString(sound.path), QString::fromStdString(sound.title),
                            QString::fromStdString(sound.kind)};
    if (error)
        *error = tr("Unknown sound %1").arg(id);
    return std::nullopt;
}

std::optional<std::vector<float>> SoundLibrary::previewSamples(const QString& id, double seconds, QString* error)
{
    std::optional<std::vector<float>> samples;
    if (const SoundRecipe* recipe = findSound(id.toStdString())) {
        std::string why;
        samples = renderSound(*recipe, &why);
        if (!samples && error)
            *error = coreText(why);
    } else {
        QString path;
        for (const SoundPack& pack : m_packs)
            for (const PackSound& sound : pack.sounds)
                if (QString::fromStdString(pack.id + "/" + sound.id) == id)
                    path = QString::fromStdString(sound.preview.empty() ? sound.path : sound.preview);
        if (path.isEmpty()) {
            const auto file = fileFor(id, error);
            if (!file)
                return std::nullopt;
            path = file->path;
        }
        samples = decodeFile(path, seconds + 1, error);
    }
    if (samples && samples->size() > std::size_t(seconds * kRate) * 2) {
        samples->resize(std::size_t(seconds * kRate) * 2);
        fadeOut(*samples);
    }
    return samples;
}

bool SoundLibrary::preview(const QString& id)
{
    if (id == m_previewing) {
        stopPreview();
        return true;
    }
    stopPreview();
    QString why;
    auto samples = previewSamples(id, kPreviewSeconds, &why);
    if (!samples || samples->empty() || !m_preview->play(std::move(*samples))) {
        m_error = why.isEmpty() ? tr("The sound could not be played.") : why;
        return false;
    }
    m_previewing = id;
    m_previewProgress = 0;
    m_previewClock.start();
    emit previewChanged();
    emit previewProgressChanged();
    return true;
}

void SoundLibrary::stopPreview()
{
    m_previewClock.stop();
    m_preview->stop();
    if (m_previewing.isEmpty())
        return;
    m_previewing.clear();
    m_previewProgress = 0;
    emit previewChanged();
    emit previewProgressChanged();
}
