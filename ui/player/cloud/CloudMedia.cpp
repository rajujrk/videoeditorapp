#include "cloud/CloudMedia.h"

#include "cloud/CloudKeys.h"
#include "cloud/CloudVoices.h"
#include "decode/AudioDecoder.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeDatabase>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QUrlQuery>

namespace {

QString envOr(const char* variable, const char* fallback)
{
    const QString value = qEnvironmentVariable(variable);
    return value.isEmpty() ? QLatin1String(fallback) : value;
}

bool save(const QString& path, const QByteArray& data)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(data) == data.size() && file.commit();
}

// An audio file's extension from its first bytes.
QString audioExtension(const QByteArray& data)
{
    return data.startsWith("RIFF") ? QStringLiteral(".wav") : QStringLiteral(".mp3");
}

double audioSeconds(const QString& path)
{
    auto decoder = tve::AudioDecoder::open(path.toStdString());
    return decoder ? double(decoder->info().duration) / double(tve::kTicksPerSecond) : 0;
}

QString errorMessage(const QByteArray& body)
{
    const QJsonObject object = QJsonDocument::fromJson(body).object();
    const QJsonValue error = object.value(QStringLiteral("error"));
    if (error.isObject())
        return error.toObject().value(QStringLiteral("message")).toString();
    const QJsonValue detail = object.value(QStringLiteral("detail"));
    if (detail.isObject())
        return detail.toObject().value(QStringLiteral("message")).toString();
    if (detail.isArray())
        return detail.toArray().first().toObject().value(QStringLiteral("msg")).toString();
    return detail.toString();
}

// What a refused request means, in words for the person.
QString describe(const QString& provider, int status, const QByteArray& body, const QNetworkReply* reply)
{
    if (status == 0)
        return QStringLiteral("Could not reach %1: %2").arg(provider, reply ? reply->errorString() : QString());
    if (status == 401 || status == 403)
        return QStringLiteral("The %1 API key was not accepted. Check it in AI settings.").arg(provider);
    if (status == 429)
        return QStringLiteral("%1 is busy or your quota is used up; try again later.").arg(provider);
    const QString message = errorMessage(body);
    if (status == 400 && !message.isEmpty())
        return QStringLiteral("%1 could not make it: %2").arg(provider, message);
    return message.isEmpty() ? QStringLiteral("%1 error %2").arg(provider).arg(status)
                             : QStringLiteral("%1 error %2: %3").arg(provider).arg(status).arg(message);
}

QString dataUrl(const QString& path, QString* mime = nullptr, QByteArray* bytes = nullptr)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    const QByteArray data = file.readAll();
    const QString type = QMimeDatabase().mimeTypeForFileNameAndData(path, data).name();
    if (mime)
        *mime = type;
    if (bytes)
        *bytes = data;
    return QStringLiteral("data:%1;base64,%2").arg(type, QString::fromLatin1(data.toBase64()));
}

} // namespace

MediaTask::MediaTask(QNetworkAccessManager* network, QObject* parent) : QObject(parent), m_network(network) {}

void MediaTask::post(const QNetworkRequest& request, const QByteArray& body, Reader read)
{
    QMetaObject::invokeMethod(this, [this, request, body, read = std::move(read)] {
        if (m_done)
            return;
        m_reply = m_network->post(request, body);
        connect(m_reply, &QNetworkReply::finished, this, [this, read] {
            QNetworkReply* reply = m_reply;
            m_reply = nullptr;
            reply->deleteLater();
            if (m_cancelled)
                return;
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            finish(read(status, reply->readAll(), reply));
        });
    }, Qt::QueuedConnection);
}

void MediaTask::cancel()
{
    if (m_done)
        return;
    m_cancelled = true;
    if (m_reply)
        m_reply->abort();
    MediaResult result;
    result.cancelled = true;
    finish(result);
}

void MediaTask::fail(const QString& error)
{
    QMetaObject::invokeMethod(this, [this, error] {
        MediaResult result;
        result.error = error;
        finish(result);
    }, Qt::QueuedConnection);
}

void MediaTask::finish(MediaResult result)
{
    if (m_done)
        return;
    m_done = true;
    emit finished(result);
}

namespace cloudmedia {

QString openAiBaseUrl()
{
    return envOr("TVE_OPENAI_BASE_URL", "https://api.openai.com");
}

QString geminiBaseUrl()
{
    return envOr("TVE_GEMINI_BASE_URL", "https://generativelanguage.googleapis.com");
}

MediaTask* image(QNetworkAccessManager* network, const QString& prompt, const ImageOptions& options,
                 const QString& basePath, QObject* parent)
{
    auto* task = new MediaTask(network, parent);
    const QString key = cloudkeys::read(QStringLiteral("openai"));
    if (key.isEmpty()) {
        task->fail(QStringLiteral("Add your OpenAI API key in AI settings first."));
        return task;
    }
    QJsonObject body{{QStringLiteral("model"), options.model},
                     {QStringLiteral("prompt"), prompt},
                     {QStringLiteral("size"), options.size},
                     {QStringLiteral("quality"), options.quality},
                     {QStringLiteral("output_format"), QStringLiteral("png")},
                     {QStringLiteral("n"), 1}};
    if (options.transparent)
        body.insert(QStringLiteral("background"), QStringLiteral("transparent"));
    // With pictures to work from it is an edit (they go as data URLs).
    QString endpoint = QStringLiteral("/v1/images/generations");
    if (!options.inputs.isEmpty()) {
        endpoint = QStringLiteral("/v1/images/edits");
        QJsonArray images;
        for (const QString& input : options.inputs) {
            const QString url = dataUrl(input);
            if (url.isEmpty()) {
                task->fail(QStringLiteral("Could not read %1").arg(QFileInfo(input).fileName()));
                return task;
            }
            images.append(QJsonObject{{QStringLiteral("image_url"), url}});
        }
        body.insert(QStringLiteral("images"), images);
    }
    QNetworkRequest request(QUrl(openAiBaseUrl() + endpoint));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Authorization", "Bearer " + key.toUtf8());
    request.setTransferTimeout(300000);
    const QString model = options.model;
    task->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact),
               [basePath, model](int status, const QByteArray& data, const QNetworkReply* reply) {
                   MediaResult result;
                   if (status != 200) {
                       result.error = describe(QStringLiteral("OpenAI"), status, data, reply);
                       return result;
                   }
                   const QJsonObject answer = QJsonDocument::fromJson(data).object();
                   const QByteArray png = QByteArray::fromBase64(answer.value(QStringLiteral("data")).toArray().first()
                                                                     .toObject().value(QStringLiteral("b64_json")).toString().toLatin1());
                   result.path = basePath + QStringLiteral(".png");
                   if (png.isEmpty() || !save(result.path, png)) {
                       result.error = QStringLiteral("OpenAI sent no picture.");
                       return result;
                   }
                   result.ok = true;
                   result.model = model;
                   result.tokens = answer.value(QStringLiteral("usage")).toObject().value(QStringLiteral("total_tokens")).toInteger();
                   return result;
               });
    return task;
}

MediaTask* video(QNetworkAccessManager* network, const QString& prompt, const VideoOptions& options,
                 const QString& basePath, QObject* parent)
{
    auto* task = new MediaTask(network, parent);
    const QString key = cloudkeys::read(QStringLiteral("google"));
    if (key.isEmpty()) {
        task->fail(QStringLiteral("Add your Google AI (Gemini API) key in AI settings first."));
        return task;
    }
    QJsonObject body{{QStringLiteral("model"), QLatin1String(kVideoModel)},
                     {QStringLiteral("response_format"), QJsonObject{{QStringLiteral("type"), QStringLiteral("video")},
                                                                     {QStringLiteral("aspect_ratio"), options.aspect},
                                                                     {QStringLiteral("resolution"), options.resolution}}}};
    QString task_ = QStringLiteral("text_to_video");
    if (options.mode == QLatin1String("extend")) {
        if (options.previous.isEmpty()) {
            task->fail(QStringLiteral("Only videos made here can be extended."));
            return task;
        }
        // A video made earlier goes on from where it ended.
        body.insert(QStringLiteral("previous_interaction_id"), options.previous);
        body.insert(QStringLiteral("input"), prompt.trimmed().isEmpty() ? QStringLiteral("Extend this video") : prompt);
        task_ = QStringLiteral("extend");
    } else {
        // The pictures first (the first and last frames in that order), then the words.
        QJsonArray input;
        for (const QString& path : options.images) {
            QString mime;
            QByteArray bytes;
            if (dataUrl(path, &mime, &bytes).isEmpty()) {
                task->fail(QStringLiteral("Could not read %1").arg(QFileInfo(path).fileName()));
                return task;
            }
            input.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("image")},
                                     {QStringLiteral("data"), QString::fromLatin1(bytes.toBase64())},
                                     {QStringLiteral("mime_type"), mime}});
        }
        input.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("text")}, {QStringLiteral("text"), prompt}});
        body.insert(QStringLiteral("input"), input);
        if (!options.images.isEmpty())
            task_ = QStringLiteral("image_to_video");
    }
    body.insert(QStringLiteral("generation_config"),
                QJsonObject{{QStringLiteral("video_config"), QJsonObject{{QStringLiteral("task"), task_}}}});
    QNetworkRequest request(QUrl(geminiBaseUrl() + QStringLiteral("/v1beta/interactions")));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("x-goog-api-key", key.toUtf8());
    request.setTransferTimeout(900000); // video takes minutes
    task->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact),
               [basePath](int status, const QByteArray& data, const QNetworkReply* reply) {
                   MediaResult result;
                   if (status != 200) {
                       result.error = describe(QStringLiteral("Google"), status, data, reply);
                       return result;
                   }
                   const QJsonObject answer = QJsonDocument::fromJson(data).object();
                   if (const QString state = answer.value(QStringLiteral("status")).toString();
                       !state.isEmpty() && state != QLatin1String("completed")) {
                       result.error = QStringLiteral("Google did not finish the video (%1).").arg(state);
                       return result;
                   }
                   // The video in the model's output.
                   QByteArray video;
                   for (const QJsonValue& step : answer.value(QStringLiteral("steps")).toArray()) {
                       if (step.toObject().value(QStringLiteral("type")).toString() != QLatin1String("model_output"))
                           continue;
                       for (const QJsonValue& part : step.toObject().value(QStringLiteral("content")).toArray())
                           if (part.toObject().value(QStringLiteral("type")).toString() == QLatin1String("video"))
                               video = QByteArray::fromBase64(part.toObject().value(QStringLiteral("data")).toString().toLatin1());
                   }
                   result.path = basePath + QStringLiteral(".mp4");
                   if (video.isEmpty() || !save(result.path, video)) {
                       result.error = QStringLiteral("Google sent no video (it may have been blocked by its safety filters).");
                       return result;
                   }
                   result.ok = true;
                   result.model = answer.value(QStringLiteral("model")).toString(QLatin1String(kVideoModel));
                   result.reference = answer.value(QStringLiteral("id")).toString();
                   return result;
               });
    return task;
}

namespace {

MediaTask* elevenLabsAudio(QNetworkAccessManager* network, const QString& endpoint, const QJsonObject& body,
                           const QString& model, const QString& basePath, QObject* parent)
{
    auto* task = new MediaTask(network, parent);
    const QString key = cloudkeys::read(QStringLiteral("elevenlabs"));
    if (key.isEmpty()) {
        task->fail(QStringLiteral("Add your ElevenLabs API key in AI settings first."));
        return task;
    }
    QUrl url(ElevenLabsVoices::baseUrl() + endpoint);
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("output_format"), QStringLiteral("mp3_44100_128"));
    url.setQuery(query);
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("xi-api-key", key.toUtf8());
    request.setTransferTimeout(600000);
    task->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact),
               [basePath, model](int status, const QByteArray& data, const QNetworkReply* reply) {
                   MediaResult result;
                   if (status != 200) {
                       result.error = describe(QStringLiteral("ElevenLabs"), status, data, reply);
                       return result;
                   }
                   result.path = basePath + audioExtension(data);
                   if (data.isEmpty() || !save(result.path, data)) {
                       result.error = QStringLiteral("ElevenLabs sent no sound.");
                       return result;
                   }
                   result.ok = true;
                   result.model = model;
                   result.seconds = audioSeconds(result.path);
                   return result;
               });
    return task;
}

} // namespace

MediaTask* music(QNetworkAccessManager* network, const QString& prompt, double seconds, bool instrumental,
                 const QString& basePath, QObject* parent)
{
    const QJsonObject body{{QStringLiteral("prompt"), prompt},
                           {QStringLiteral("music_length_ms"), qint64(std::clamp(seconds, 3.0, 600.0) * 1000)},
                           {QStringLiteral("force_instrumental"), instrumental}};
    return elevenLabsAudio(network, QStringLiteral("/v1/music"), body, QStringLiteral("music_v1"), basePath, parent);
}

MediaTask* soundEffect(QNetworkAccessManager* network, const QString& prompt, double seconds, bool loop,
                       const QString& basePath, QObject* parent)
{
    const QJsonObject body{{QStringLiteral("text"), prompt},
                           {QStringLiteral("duration_seconds"), std::clamp(seconds, 0.5, 30.0)},
                           {QStringLiteral("loop"), loop}};
    return elevenLabsAudio(network, QStringLiteral("/v1/sound-generation"), body,
                           QStringLiteral("eleven_text_to_sound_v2"), basePath, parent);
}

} // namespace cloudmedia
