#include "TemplateLibrary.h"

#include "StillRenderer.h"
#include "TitleRenderer.h"
#include "audio/SoundPacks.h"
#include "audio/Synth.h"
#include "decode/AudioDecoder.h"
#include "io/ProjectSerializer.h"
#include "model/Template.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QMutex>
#include <QPainter>
#include <QRegularExpression>
#include <QRunnable>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUrl>

#include <nlohmann/json.hpp>

#include <cmath>
#include <functional>
#include <set>

using namespace tve;

namespace {

constexpr const char* kUserPrefix = "user.";

QString dataPath(const char* variable, const QString& name)
{
    const QByteArray override = qgetenv(variable);
    return !override.isEmpty() ? QString::fromLocal8Bit(override)
                               : QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QLatin1Char('/') + name;
}

QString favoritesFile()
{
    return dataPath("TVE_TEMPLATE_FAVORITES_FILE", QStringLiteral("template-favorites.json"));
}

Ticks ticksOf(double seconds)
{
    return Ticks(std::llround(seconds * double(kTicksPerSecond)));
}

QJsonObject readJson(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(file.readAll()).object() : QJsonObject();
}

// The built-in recipes, by id (from the app's resources).
const QMap<QString, QJsonObject>& recipes()
{
    static const QMap<QString, QJsonObject> all = [] {
        QMap<QString, QJsonObject> map;
        QDirIterator files(QStringLiteral(":/templates"), {QStringLiteral("template.json")}, QDir::Files, QDirIterator::Subdirectories);
        while (files.hasNext()) {
            const QJsonObject recipe = readJson(files.next());
            if (!recipe.value(QStringLiteral("id")).toString().isEmpty())
                map.insert(recipe.value(QStringLiteral("id")).toString(), recipe);
        }
        return map;
    }();
    return all;
}

// A user template's folder from its id.
QString userFolder(const QString& id)
{
    return TemplateLibrary::userDirectory() + QLatin1Char('/') + id.mid(int(qstrlen(kUserPrefix))) + QStringLiteral(".tvet");
}

// A slot's placeholder: a soft gradient with the slot's number and name, drawn once per
// size.
QString placeholder(const QString& folder, int index, const QString& label, int width, int height, int hueOffset = 0)
{
    // "<folder>/<width>x<height>/<n> <label>.png" (the name clips show).
    QString fileName = label;
    fileName.replace(QRegularExpression(QStringLiteral("[/:\\\\]")), QStringLiteral("-"));
    const QString path = QStringLiteral("%1/%2x%3/%4 %5.png").arg(folder).arg(width).arg(height).arg(index + 1).arg(fileName);
    if (QFileInfo::exists(path))
        return path;
    // Drawn small and saved at that size (it only stands in), keeping the canvas's shape.
    const double scale = 480.0 / std::max(width, height);
    const int w = std::max(16, int(width * scale)), h = std::max(16, int(height * scale));
    QImage image(w, h, QImage::Format_RGB32);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    const int hue = (index * 47 + 200 + hueOffset) % 360;
    QLinearGradient fill(0, 0, w, h);
    fill.setColorAt(0, QColor::fromHsv(hue, 90, 120));
    fill.setColorAt(1, QColor::fromHsv((hue + 40) % 360, 120, 70));
    painter.fillRect(image.rect(), fill);
    painter.setPen(QColor(255, 255, 255, 60));
    painter.setBrush(Qt::NoBrush);
    const double unit = std::min(w, h);
    painter.drawRoundedRect(QRectF(unit * 0.06, unit * 0.06, w - unit * 0.12, h - unit * 0.12), unit * 0.04, unit * 0.04);
    QFont number;
    number.setPixelSize(int(unit * 0.32));
    number.setBold(true);
    painter.setFont(number);
    painter.setPen(QColor(255, 255, 255, 210));
    painter.drawText(QRectF(0, h * 0.5 - unit * 0.3, w, unit * 0.4), Qt::AlignCenter, QString::number(index + 1));
    QFont name;
    name.setPixelSize(int(unit * 0.07));
    painter.setFont(name);
    painter.setPen(QColor(255, 255, 255, 180));
    painter.drawText(QRectF(0, h * 0.5 + unit * 0.12, w, unit * 0.12), Qt::AlignCenter, label);
    painter.end();
    QDir().mkpath(QFileInfo(path).absolutePath());
    image.save(path, "PNG");
    return path;
}

MediaItem stillItem(Project& project, const QString& path)
{
    const QImage image(path);
    return MediaItem{.id = project.allocateId(), .path = path.toStdString(), .duration = kStillDuration, .hasVideo = true,
                     .hasAudio = false, .width = image.width(), .height = image.height(), .still = true, .placeholder = true};
}

std::optional<MediaItem> soundItem(Project& project, const QString& path)
{
    auto decoder = AudioDecoder::open(path.toStdString());
    if (!decoder)
        return std::nullopt;
    return MediaItem{.id = project.allocateId(), .path = path.toStdString(), .duration = decoder->info().duration,
                     .hasVideo = false, .hasAudio = true, .audioSampleRate = decoder->info().sourceSampleRate,
                     .audioChannels = decoder->info().sourceChannels};
}

// A built-in recipe made into a project.
std::optional<Project> buildRecipe(const QJsonObject& recipe, const TemplateShape& shape, QString* error, bool withMusic)
{
    const QString id = recipe.value(QStringLiteral("id")).toString();
    const QJsonObject music = recipe.value(QStringLiteral("music")).toObject();
    const QString soundId = music.value(QStringLiteral("sound")).toString();
    const SoundRecipe* sound = findSound(soundId.toStdString());
    if (!sound) {
        if (error)
            *error = QStringLiteral("The template's music is missing.");
        return std::nullopt;
    }
    const double tempo = QJsonDocument::fromJson(QByteArray::fromStdString(sound->json)).object().value(QStringLiteral("tempo")).toDouble(120);
    const double beat = 60.0 / std::max(tempo, 1.0);

    Project project;
    project.settings.width = shape.width;
    project.settings.height = shape.height;
    const auto addTrack = [&](TrackKind kind, const char* name) {
        project.tracks.push_back(Track{.id = project.allocateId(), .kind = kind, .name = name});
        return project.tracks.size() - 1;
    };
    const std::size_t musicTrack = addTrack(TrackKind::Audio, "Music");
    const std::size_t mainTrack = addTrack(TrackKind::Video, "Video 1");
    const std::size_t titleTrack = addTrack(TrackKind::Video, "Titles");

    TemplateInfo info{.id = id.toStdString(), .name = recipe.value(QStringLiteral("name")).toString().toStdString()};
    const QString folder = TemplateLibrary::cacheDirectory() + QLatin1Char('/') + QString(id).replace(QLatin1Char('.'), QLatin1Char('_'));
    const QJsonObject transition = recipe.value(QStringLiteral("transition")).toObject();
    Ticks at = 0;
    const QJsonArray slotRecipes = recipe.value(QStringLiteral("slots")).toArray();
    for (qsizetype i = 0; i < slotRecipes.size(); ++i) {
        const QJsonObject slot = slotRecipes[i].toObject();
        const QString label = slot.value(QStringLiteral("label")).toString();
        // Each template's placeholders in colours of its own.
        const int hueOffset = int(qHash(id) % 360);
        MediaItem media = stillItem(project, placeholder(folder, int(i), label, shape.width, shape.height, hueOffset));
        Clip clip{.id = project.allocateId(), .mediaId = media.id, .start = at,
                  .duration = ticksOf(slot.value(QStringLiteral("beats")).toDouble(4) * beat)};
        if (i > 0 && !transition.isEmpty())
            clip.transitionIn = ClipTransition{.transitionId = transition.value(QStringLiteral("id")).toString().toStdString(),
                                               .duration = std::clamp(ticksOf(transition.value(QStringLiteral("beats")).toDouble(1) * beat),
                                                                      kMinTransitionDuration, kMaxTransitionDuration)};
        project.media.push_back(media);
        info.mediaSlots.push_back({.clipId = clip.id, .label = label.toStdString(),
                                   .kind = slot.value(QStringLiteral("kind")).toString(QStringLiteral("any")).toStdString()});
        at += clip.duration;
        project.tracks[mainTrack].insertClip(std::move(clip));
    }
    const Ticks length = at;
    for (const QJsonValue& value : recipe.value(QStringLiteral("texts")).toArray()) {
        const QJsonObject text = value.toObject();
        const QVariantMap style = text.value(QStringLiteral("style")).toObject().toVariantMap();
        Clip clip{.id = project.allocateId(), .start = ticksOf(text.value(QStringLiteral("fromBeat")).toDouble() * beat),
                  .duration = ticksOf(text.value(QStringLiteral("beats")).toDouble(4) * beat)};
        clip.duration = std::min(clip.duration, length - clip.start);
        if (clip.duration <= 0)
            continue;
        clip.text = textFrom(style);
        clip.visual.transform.x = style.value(QStringLiteral("x")).toDouble();
        clip.visual.transform.y = style.value(QStringLiteral("y")).toDouble();
        info.texts.push_back({.clipId = clip.id, .label = text.value(QStringLiteral("label")).toString().toStdString()});
        if (!project.tracks[titleTrack].overlaps(clip.start, clip.duration))
            project.tracks[titleTrack].insertClip(std::move(clip));
    }
    if (withMusic) {
        const QString path = TemplateLibrary::builtInMusic(soundId, nullptr, error);
        if (path.isEmpty())
            return std::nullopt;
        auto item = soundItem(project, path);
        if (item) {
            Clip clip{.id = project.allocateId(), .mediaId = item->id, .start = 0, .duration = std::min(length, item->duration)};
            clip.gainDb = float(music.value(QStringLiteral("gainDb")).toDouble());
            clip.fadeOut = std::min(clip.duration / 2, ticksOf(music.value(QStringLiteral("fadeOutBeats")).toDouble(4) * beat));
            info.musicClipId = clip.id;
            project.media.push_back(*item);
            project.tracks[musicTrack].insertClip(std::move(clip));
        }
    }
    project.templateInfo = std::move(info);
    return project;
}

// One of the person's templates made into a project.
std::optional<Project> buildUser(const QString& id, const TemplateShape* shape, QString* error)
{
    const QString folder = userFolder(id);
    const QJsonObject file = readJson(folder + QStringLiteral("/template.json"));
    if (file.value(QStringLiteral("format")).toString() != QLatin1String("tve-template")) {
        if (error)
            *error = QStringLiteral("The template could not be read.");
        return std::nullopt;
    }
    try {
        Project project = projectFromJson(nlohmann::json::parse(
            QJsonDocument(file.value(QStringLiteral("project")).toObject()).toJson(QJsonDocument::Compact).toStdString()));
        // Its media is kept beside it.
        for (MediaItem& media : project.media)
            if (!QDir::isAbsolutePath(QString::fromStdString(media.path)))
                media.path = (folder + QLatin1Char('/') + QString::fromStdString(media.path)).toStdString();
        if (shape) {
            project.settings.width = shape->width;
            project.settings.height = shape->height;
        }
        if (project.templateInfo)
            project.templateInfo->id = id.toStdString();
        return project;
    } catch (const std::exception& e) {
        if (error)
            *error = QStringLiteral("The template could not be read: %1").arg(QString::fromUtf8(e.what()));
        return std::nullopt;
    }
}

QVariantMap summary(const Project& project, const QString& id, const QString& name, const QString& category,
                    const QString& description, const QString& shape, bool builtIn)
{
    return {{QStringLiteral("id"), id},
            {QStringLiteral("name"), name},
            {QStringLiteral("category"), category},
            {QStringLiteral("description"), description},
            {QStringLiteral("slots"), project.templateInfo ? int(project.templateInfo->mediaSlots.size()) : 0},
            {QStringLiteral("texts"), project.templateInfo ? int(project.templateInfo->texts.size()) : 0},
            {QStringLiteral("seconds"), double(project.duration()) / double(kTicksPerSecond)},
            {QStringLiteral("shape"), shape},
            {QStringLiteral("builtIn"), builtIn}};
}

QMutex& previewMutex()
{
    static QMutex mutex;
    return mutex;
}

} // namespace

TemplateLibrary::TemplateLibrary(QObject* parent) : QObject(parent)
{
    const QJsonArray favorites = readJson(favoritesFile()).value(QStringLiteral("favorites")).toArray();
    for (const QJsonValue& favorite : favorites)
        m_favorites << favorite.toString();
}

QVariantList TemplateLibrary::templates() const
{
    if (!m_listed)
        const_cast<TemplateLibrary*>(this)->list();
    return m_list;
}

QString TemplateLibrary::builtInMusic(const QString& soundId, double* tempo, QString* error)
{
    const SoundRecipe* sound = findSound(soundId.toStdString());
    if (!sound) {
        if (error)
            *error = QStringLiteral("Unknown music %1").arg(soundId);
        return {};
    }
    if (tempo)
        *tempo = QJsonDocument::fromJson(QByteArray::fromStdString(sound->json)).object().value(QStringLiteral("tempo")).toDouble(120);
    // Rendered once: "<cache>/music/<id>-<recipe hash>/<Title>.wav" (the name its clip shows).
    const QString path = cacheDirectory() + QStringLiteral("/music/") + QString::fromStdString(sound->id) + QLatin1Char('-')
                       + QString::fromLatin1(QCryptographicHash::hash(QByteArray::fromStdString(sound->json), QCryptographicHash::Sha256).toHex().left(12))
                       + QLatin1Char('/') + QString::fromStdString(sound->title) + QStringLiteral(".wav");
    if (!QFileInfo::exists(path)) {
        std::string why;
        auto samples = renderSound(*sound, &why);
        if (!samples || !writeSoundFile(path.toStdString(), *samples, &why)) {
            if (error)
                *error = QString::fromStdString(why);
            return {};
        }
    }
    return path;
}

QString TemplateLibrary::userDirectory()
{
    return dataPath("TVE_TEMPLATE_DIR", QStringLiteral("Templates"));
}

QString TemplateLibrary::cacheDirectory()
{
    return dataPath("TVE_TEMPLATE_CACHE_DIR", QStringLiteral("Template cache"));
}

QVariantList TemplateLibrary::categories() const
{
    QVariantList list;
    const QList<QPair<QString, QString>> names{
        {QString(), QStringLiteral("All")},           {QStringLiteral("favorites"), QStringLiteral("Favourites")},
        {QStringLiteral("travel"), QStringLiteral("Travel")}, {QStringLiteral("celebration"), QStringLiteral("Celebration")},
        {QStringLiteral("business"), QStringLiteral("Business")}, {QStringLiteral("social"), QStringLiteral("Social")},
        {QStringLiteral("memories"), QStringLiteral("Memories")}, {QStringLiteral("food"), QStringLiteral("Food")},
        {QStringLiteral("sports"), QStringLiteral("Sports")},  {QStringLiteral("minimal"), QStringLiteral("Minimal")},
        {QStringLiteral("mine"), QStringLiteral("Mine")}};
    for (const auto& [id, label] : names)
        list << QVariantMap{{QStringLiteral("id"), id}, {QStringLiteral("label"), label}};
    return list;
}

QVariantList TemplateLibrary::shapes() const
{
    QVariantList list;
    for (const TemplateShape& shape : templateShapes())
        list << QVariantMap{{QStringLiteral("id"), QString::fromStdString(shape.id)},
                            {QStringLiteral("width"), shape.width},
                            {QStringLiteral("height"), shape.height}};
    return list;
}

void TemplateLibrary::refresh()
{
    list();
    emit changed();
}

void TemplateLibrary::list()
{
    m_listed = true;
    m_list.clear();
    for (auto it = recipes().begin(); it != recipes().end(); ++it) {
        const QJsonObject& recipe = it.value();
        const auto project = previewProject(it.key());
        if (!project)
            continue;
        QVariantMap entry = summary(*project, it.key(), recipe.value(QStringLiteral("name")).toString(),
                                    recipe.value(QStringLiteral("category")).toString(),
                                    recipe.value(QStringLiteral("description")).toString(),
                                    recipe.value(QStringLiteral("shape")).toString(QStringLiteral("16:9")), true);
        entry.insert(QStringLiteral("favorite"), m_favorites.contains(it.key()));
        m_list << entry;
    }
    const QDir folder(userDirectory());
    for (const QFileInfo& entry : folder.entryInfoList({QStringLiteral("*.tvet")}, QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        const QString id = QLatin1String(kUserPrefix) + entry.completeBaseName();
        const QJsonObject file = readJson(entry.absoluteFilePath() + QStringLiteral("/template.json"));
        const auto project = buildUser(id, nullptr, nullptr);
        if (!project)
            continue;
        QVariantMap item = summary(*project, id, file.value(QStringLiteral("name")).toString(),
                                   file.value(QStringLiteral("category")).toString(),
                                   file.value(QStringLiteral("description")).toString(),
                                   QString::fromStdString(nearestShape(project->settings.width, project->settings.height).id), false);
        item.insert(QStringLiteral("favorite"), m_favorites.contains(id));
        m_list << item;
    }
}

void TemplateLibrary::toggleFavorite(const QString& id)
{
    if (!m_favorites.removeOne(id))
        m_favorites << id;
    QDir().mkpath(QFileInfo(favoritesFile()).absolutePath());
    QSaveFile file(favoritesFile());
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(QJsonObject{{QStringLiteral("favorites"), QJsonArray::fromStringList(m_favorites)}}).toJson());
        file.commit();
    }
    if (!m_listed)
        list();
    for (QVariant& entry : m_list) {
        QVariantMap map = entry.toMap();
        map.insert(QStringLiteral("favorite"), m_favorites.contains(map.value(QStringLiteral("id")).toString()));
        entry = map;
    }
    emit changed();
}

bool TemplateLibrary::remove(const QString& id)
{
    if (!id.startsWith(QLatin1String(kUserPrefix))) {
        m_error = QStringLiteral("Built-in templates cannot be deleted.");
        return false;
    }
    if (!QDir(userFolder(id)).removeRecursively()) {
        m_error = QStringLiteral("The template could not be deleted.");
        return false;
    }
    m_favorites.removeAll(id);
    refresh();
    return true;
}

std::optional<Project> TemplateLibrary::build(const QString& id, const QString& shapeId, QString* error, bool withMusic)
{
    std::optional<TemplateShape> shape;
    if (!shapeId.isEmpty()) {
        shape = templateShape(shapeId.toStdString());
        if (!shape) {
            if (error)
                *error = QStringLiteral("Unknown shape %1").arg(shapeId);
            return std::nullopt;
        }
    }
    if (id.startsWith(QLatin1String(kUserPrefix)))
        return buildUser(id, shape ? &*shape : nullptr, error);
    const auto recipe = recipes().find(id);
    if (recipe == recipes().end()) {
        if (error)
            *error = QStringLiteral("Unknown template %1").arg(id);
        return std::nullopt;
    }
    if (!shape)
        shape = templateShape(recipe->value(QStringLiteral("shape")).toString(QStringLiteral("16:9")).toStdString());
    return buildRecipe(*recipe, *shape, error, withMusic);
}

std::shared_ptr<const Project> TemplateLibrary::previewProject(const QString& id)
{
    static QMap<QString, std::shared_ptr<const Project>> cache;
    QMutexLocker lock(&previewMutex());
    if (const auto found = cache.value(id); found && !id.startsWith(QLatin1String(kUserPrefix)))
        return found;
    auto project = build(id, {}, nullptr, false);
    if (!project)
        return nullptr;
    auto shared = std::make_shared<const Project>(std::move(*project));
    cache.insert(id, shared);
    return shared;
}

QString TemplateLibrary::save(const Project& source, const QString& name, const QString& category, const std::vector<Id>& slotClips,
                              const std::vector<Id>& texts, Id music, QString* error)
{
    const QString title = name.trimmed().isEmpty() ? QStringLiteral("My template") : name.trimmed();
    if (slotClips.empty()) {
        if (error)
            *error = QStringLiteral("Choose at least one clip to be replaceable.");
        return {};
    }
    QString base = title;
    base.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9 _-]")), QStringLiteral("_"));
    QString folder = userDirectory() + QLatin1Char('/') + base + QStringLiteral(".tvet");
    for (int n = 2; QFileInfo::exists(folder); ++n)
        folder = userDirectory() + QLatin1Char('/') + base + QStringLiteral(" %1.tvet").arg(n);
    const QString media = folder + QStringLiteral("/media");
    QDir().mkpath(media);

    Project project = source;
    TemplateInfo info{.name = title.toStdString()};
    // The slots become placeholders, in order on the timeline.
    std::vector<std::pair<Ticks, Id>> ordered;
    for (Id id : slotClips)
        if (auto location = project.locateClip(id); location && !location->clip->generated() && !location->clip->compound())
            ordered.push_back({location->clip->start, id});
    std::ranges::sort(ordered);
    if (ordered.empty()) {
        if (error)
            *error = QStringLiteral("None of the chosen clips can be replaced.");
        QDir(folder).removeRecursively();
        return {};
    }
    for (std::size_t i = 0; i < ordered.size(); ++i) {
        Clip& clip = *project.locateClip(ordered[i].second)->clip;
        const QString label = QStringLiteral("Slot %1").arg(i + 1);
        const QString path = placeholder(media, int(i), label, project.settings.width, project.settings.height);
        MediaItem item = stillItem(project, path);
        clip.mediaId = item.id;
        clip.sourceIn = 0;
        clip.speed = 1.0;
        clip.speedRamp = {};
        clip.reversed = false;
        clip.visual.crop = {};
        clip.enhancedAudio.reset();
        clip.processedVideo.reset();
        project.media.push_back(item);
        info.mediaSlots.push_back({.clipId = clip.id, .label = label.toStdString()});
    }
    for (Id id : texts)
        if (auto location = project.locateClip(id); location && location->clip->text)
            info.texts.push_back({.clipId = id, .label = location->clip->text->text.substr(0, 30)});
    if (music != kNoId && project.locateClip(music))
        info.musicClipId = music;
    // The rest of what it uses is copied beside it; what nothing uses is left out.
    std::set<Id> used;
    const std::function<void(const std::vector<Track>&)> collect = [&](const std::vector<Track>& tracks) {
        for (const Track& track : tracks)
            for (const Clip& clip : track.clips)
                if (clip.mediaId != kNoId)
                    used.insert(clip.mediaId);
    };
    collect(project.tracks);
    for (const Sequence& sequence : project.sequences)
        collect(sequence.tracks);
    std::erase_if(project.media, [&](const MediaItem& item) { return !used.contains(item.id); });
    for (MediaItem& item : project.media) {
        const QString from = QString::fromStdString(item.path);
        QString name = QFileInfo(from).fileName();
        if (from.startsWith(media)) {
            item.path = (QStringLiteral("media/") + name).toStdString();
            continue;
        }
        for (int n = 2; QFileInfo::exists(media + QLatin1Char('/') + name); ++n)
            name = QFileInfo(from).completeBaseName() + QStringLiteral(" %1.").arg(n) + QFileInfo(from).suffix();
        if (!QFile::copy(from, media + QLatin1Char('/') + name)) {
            if (error)
                *error = QStringLiteral("%1 could not be copied into the template.").arg(QFileInfo(from).fileName());
            QDir(folder).removeRecursively();
            return {};
        }
        item.path = (QStringLiteral("media/") + name).toStdString();
        item.proxyPath.clear();
    }
    project.templateInfo = std::move(info);
    const QJsonObject file{{QStringLiteral("format"), QStringLiteral("tve-template")},
                           {QStringLiteral("version"), 1},
                           {QStringLiteral("name"), title},
                           {QStringLiteral("category"), category.isEmpty() ? QStringLiteral("mine") : category},
                           {QStringLiteral("description"), QStringLiteral("Made from a project")},
                           {QStringLiteral("project"), QJsonDocument::fromJson(QByteArray::fromStdString(projectToJson(project).dump())).object()}};
    QSaveFile out(folder + QStringLiteral("/template.json"));
    if (!out.open(QIODevice::WriteOnly) || out.write(QJsonDocument(file).toJson()) < 0 || !out.commit()) {
        if (error)
            *error = QStringLiteral("The template could not be written.");
        QDir(folder).removeRecursively();
        return {};
    }
    refresh();
    return QLatin1String(kUserPrefix) + QFileInfo(folder).completeBaseName();
}

// --- Previews ---

namespace {

class TemplatePreviewResponse : public QQuickImageResponse, public QRunnable {
public:
    explicit TemplatePreviewResponse(QString id) : m_id(std::move(id)) { setAutoDelete(false); }
    QQuickTextureFactory* textureFactory() const override { return QQuickTextureFactory::textureFactoryForImage(m_image); }

    void run() override
    {
        // <url-encoded id>@<seconds>@<height>
        const qsizetype heightAt = m_id.lastIndexOf(QLatin1Char('@'));
        const qsizetype timeAt = m_id.lastIndexOf(QLatin1Char('@'), heightAt - 1);
        if (heightAt > 0 && timeAt > 0) {
            const QString id = QUrl::fromPercentEncoding(m_id.left(timeAt).toUtf8());
            if (const auto project = TemplateLibrary::previewProject(id)) {
                const int height = std::clamp(m_id.mid(heightAt + 1).toInt(), 16, 720);
                const int width = std::max(16, int(height * double(project->settings.width) / project->settings.height));
                // One renderer per thread (the pool has one).
                thread_local StillRenderer renderer;
                if (renderer.isValid())
                    m_image = renderer.render(project, ticksOf(m_id.mid(timeAt + 1, heightAt - timeAt - 1).toDouble()), QSize(width, height));
            }
        }
        emit finished();
    }

private:
    QString m_id;
    QImage m_image;
};

} // namespace

TemplatePreviewProvider::TemplatePreviewProvider()
{
    m_pool.setMaxThreadCount(1);
}

QQuickImageResponse* TemplatePreviewProvider::requestImageResponse(const QString& id, const QSize&)
{
    auto* response = new TemplatePreviewResponse(id);
    m_pool.start(response);
    return response;
}
