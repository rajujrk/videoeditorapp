#include "cloud/CloudVoices.h"

#include "Language.h"
#include "cloud/CloudKeys.h"
#include "decode/AudioDecoder.h"
#include "encode/WavWriter.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHttpMultiPart>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTimer>
#include <QUrlQuery>

#import <AVFAudio/AVFAudio.h>

#include <memory>
#include <mutex>

void SpeechTask::finish(SpeechResult result)
{
    if (m_done)
        return;
    m_done = true;
    emit finished(result);
}

QList<SpokenWord> spreadWords(const QString& text, double duration)
{
    QList<SpokenWord> words;
    const QStringList parts = text.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
    qsizetype letters = 0;
    for (const QString& part : parts)
        letters += part.size() + 1;
    double at = 0;
    for (const QString& part : parts) {
        const double length = duration * double(part.size() + 1) / double(std::max<qsizetype>(1, letters));
        words.push_back({at, at + length * 0.9, part});
        at += length;
    }
    return words;
}

QList<SpokenWord> wordsFromCharacters(const QStringList& characters, const QList<double>& starts, const QList<double>& ends)
{
    QList<SpokenWord> words;
    SpokenWord word;
    const auto flush = [&] {
        if (!word.text.isEmpty())
            words.push_back(word);
        word = {};
    };
    const qsizetype count = std::min({characters.size(), starts.size(), ends.size()});
    for (qsizetype i = 0; i < count; ++i) {
        const QString& c = characters[i];
        if (c.trimmed().isEmpty()) {
            flush();
            continue;
        }
        if (word.text.isEmpty())
            word.start = starts[i];
        word.text += c;
        word.end = ends[i];
    }
    flush();
    return words;
}

namespace {

double fileDuration(const QString& path)
{
    auto decoder = tve::AudioDecoder::open(path.toStdString());
    return decoder ? double(decoder->info().duration) / double(tve::kTicksPerSecond) : 0;
}

// --- System voices ---

// One utterance written to a WAV: samples as they come, word markers by their sample offset.
struct SystemSpeech {
    std::mutex mutex;
    std::unique_ptr<tve::WavWriter> writer;
    QString path;
    QString error;
    int channels = 0;
    int bytesPerFrame = 0; // of the voice's own sample format, all channels
    double rate = 0;
    qint64 frames = 0;
    QList<QPair<qint64, NSRange>> marks; // frame offset, text range
    bool cancelled = false;
    bool ended = false;
};

class SystemSpeechTask final : public SpeechTask {
public:
    SystemSpeechTask(QString text, QObject* parent) : SpeechTask(parent), m_text(std::move(text)) {}
    ~SystemSpeechTask() override
    {
        if (m_synthesizer) {
            [m_synthesizer stopSpeakingAtBoundary:AVSpeechBoundaryImmediate];
            [m_synthesizer release];
        }
    }

    void start(const QString& voiceId, double speed, const QString& basePath)
    {
        auto speech = std::make_shared<SystemSpeech>();
        speech->path = basePath + QStringLiteral(".wav");
        QDir().mkpath(QFileInfo(speech->path).absolutePath());
        m_speech = speech;
        QPointer<SystemSpeechTask> self(this);
        @autoreleasepool {
            AVSpeechUtterance* utterance = [AVSpeechUtterance speechUtteranceWithString:m_text.toNSString()];
            if (!voiceId.isEmpty())
                if (AVSpeechSynthesisVoice* voice = [AVSpeechSynthesisVoice voiceWithIdentifier:voiceId.toNSString()])
                    utterance.voice = voice;
            // speed 1 is the voice's normal rate; the scale is not linear, so step around it.
            const float base = AVSpeechUtteranceDefaultSpeechRate;
            utterance.rate = float(std::clamp(speed >= 1.0 ? base + (speed - 1.0) * (AVSpeechUtteranceMaximumSpeechRate - base)
                                                           : base * float(speed),
                                              double(AVSpeechUtteranceMinimumSpeechRate), double(AVSpeechUtteranceMaximumSpeechRate)));
            m_synthesizer = [[AVSpeechSynthesizer alloc] init];
            [m_synthesizer writeUtterance:utterance
                toBufferCallback:^(AVAudioBuffer* buffer) {
                    std::lock_guard lock(speech->mutex);
                    if (speech->cancelled || speech->ended)
                        return;
                    AVAudioPCMBuffer* pcm = [buffer isKindOfClass:[AVAudioPCMBuffer class]] ? (AVAudioPCMBuffer*)buffer : nil;
                    if (!pcm || pcm.frameLength == 0) {
                        // The end — unless more comes: a long text arrives in parts, with an
                        // empty buffer between them. So the end is an empty buffer with nothing
                        // after it for a second.
                        const qint64 at = speech->frames;
                        QMetaObject::invokeMethod(qApp, [self, speech, at] {
                            QTimer::singleShot(1000, qApp, [self, speech, at] {
                                QString error;
                                {
                                    std::lock_guard lock(speech->mutex);
                                    if (speech->ended || speech->cancelled || speech->frames != at)
                                        return; // (more came)
                                    speech->ended = true;
                                    error = speech->error;
                                    if (speech->writer && !speech->writer->finish())
                                        error = tr("The voice could not be written.");
                                    if (!speech->writer && error.isEmpty())
                                        error = tr("The voice made no sound.");
                                }
                                if (self)
                                    self->ended(*speech, error);
                            });
                        });
                        return;
                    }
                    const AVAudioFormat* format = pcm.format;
                    const int channels = int(format.channelCount);
                    if (!speech->writer) {
                        speech->channels = channels;
                        speech->rate = format.sampleRate;
                        speech->bytesPerFrame = int(format.streamDescription->mBytesPerFrame) * (format.isInterleaved ? 1 : channels);
                        std::string why;
                        speech->writer = tve::WavWriter::open(speech->path.toStdString(), int(format.sampleRate), channels, &why);
                        if (!speech->writer)
                            speech->error = coreText(why);
                    }
                    if (!speech->writer)
                        return;
                    const AVAudioFrameCount frames = pcm.frameLength;
                    std::vector<float> interleaved(std::size_t(frames) * std::size_t(channels));
                    for (int c = 0; c < channels; ++c)
                        for (AVAudioFrameCount f = 0; f < frames; ++f) {
                            float v = 0;
                            if (pcm.floatChannelData)
                                v = format.isInterleaved ? pcm.floatChannelData[0][f * channels + c] : pcm.floatChannelData[c][f];
                            else if (pcm.int16ChannelData)
                                v = float(format.isInterleaved ? pcm.int16ChannelData[0][f * channels + c] : pcm.int16ChannelData[c][f]) / 32768.0f;
                            else if (pcm.int32ChannelData)
                                v = float(format.isInterleaved ? pcm.int32ChannelData[0][f * channels + c] : pcm.int32ChannelData[c][f]) / 2147483648.0f;
                            interleaved[std::size_t(f) * std::size_t(channels) + std::size_t(c)] = v;
                        }
                    speech->writer->write(interleaved.data(), frames);
                    speech->frames += frames;
                }
                toMarkerCallback:^(NSArray<AVSpeechSynthesisMarker*>* markers) {
                    std::lock_guard lock(speech->mutex);
                    for (AVSpeechSynthesisMarker* marker in markers)
                        if (marker.mark == AVSpeechSynthesisMarkerMarkWord)
                            speech->marks.push_back({qint64(marker.byteSampleOffset), marker.textRange});
                }];
        }
    }

    void cancel() override
    {
        if (done())
            return;
        if (m_speech) {
            std::lock_guard lock(m_speech->mutex);
            m_speech->cancelled = true;
        }
        [m_synthesizer stopSpeakingAtBoundary:AVSpeechBoundaryImmediate];
        if (m_speech)
            QFile::remove(m_speech->path);
        SpeechResult result;
        result.cancelled = true;
        finish(result);
    }

private:
    void ended(const SystemSpeech& speech, const QString& error)
    {
        SpeechResult result;
        if (!error.isEmpty()) {
            result.error = error;
            QFile::remove(speech.path);
            finish(result);
            return;
        }
        result.ok = true;
        result.path = speech.path;
        result.duration = speech.rate > 0 ? double(speech.frames) / speech.rate : fileDuration(speech.path);
        // Markers give byte offsets into the sound as the voice made it (frames on systems
        // where they would not fit as bytes).
        qint64 last = 0;
        for (const auto& mark : speech.marks)
            last = std::max(last, mark.first);
        const double perFrame = last >= speech.frames ? double(std::max(1, speech.bytesPerFrame)) : 1.0;

        const QString text = m_text;
        for (qsizetype i = 0; i < speech.marks.size(); ++i) {
            const auto& [offset, range] = speech.marks[i];
            SpokenWord word;
            word.text = text.mid(qsizetype(range.location), qsizetype(range.length)).trimmed();
            word.start = double(offset) / perFrame / speech.rate;
            word.end = i + 1 < speech.marks.size() ? double(speech.marks[i + 1].first) / perFrame / speech.rate : result.duration;
            if (!word.text.isEmpty())
                result.words.push_back(word);
        }
        result.timed = !result.words.isEmpty();
        if (!result.timed)
            result.words = spreadWords(text, result.duration);
        // A word ends when the next starts, less a breath, never past the sound.
        for (SpokenWord& w : result.words) {
            w.end = std::min(w.end, result.duration);
            if (w.end - w.start > 0.05)
                w.end -= 0.02;
        }
        finish(result);
    }

    QString m_text;
    std::shared_ptr<SystemSpeech> m_speech;
    AVSpeechSynthesizer* m_synthesizer = nil;
};

// --- ElevenLabs ---

class ElevenLabsTask final : public SpeechTask {
public:
    using SpeechTask::SpeechTask;

    void start(QNetworkAccessManager* network, const QString& text, const QString& voiceId, const QString& model,
               double speed, const QString& basePath)
    {
        const QString key = cloudkeys::read(QStringLiteral("elevenlabs"));
        if (key.isEmpty()) {
            fail(tr("Add your ElevenLabs API key in AI settings first."));
            return;
        }
        m_text = text;
        m_basePath = basePath;
        QUrl url(ElevenLabsVoices::baseUrl() + QStringLiteral("/v1/text-to-speech/%1/with-timestamps").arg(voiceId));
        QUrlQuery query;
        query.addQueryItem(QStringLiteral("output_format"), QStringLiteral("mp3_44100_128"));
        url.setQuery(query);
        QNetworkRequest http(url);
        http.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
        http.setRawHeader("xi-api-key", key.toUtf8());
        http.setTransferTimeout(180000);
        const QJsonObject body{{QStringLiteral("text"), text},
                               {QStringLiteral("model_id"), model},
                               {QStringLiteral("voice_settings"),
                                QJsonObject{{QStringLiteral("stability"), 0.5},
                                            {QStringLiteral("similarity_boost"), 0.75},
                                            {QStringLiteral("speed"), std::clamp(speed, 0.7, 1.2)}}}};
        m_reply = network->post(http, QJsonDocument(body).toJson(QJsonDocument::Compact));
        connect(m_reply, &QNetworkReply::finished, this, [this] { ended(); });
    }

    void cancel() override
    {
        if (done())
            return;
        m_cancelled = true;
        if (m_reply)
            m_reply->abort();
        SpeechResult result;
        result.cancelled = true;
        finish(result);
    }

private:
    void fail(const QString& error)
    {
        SpeechResult result;
        result.error = error;
        QMetaObject::invokeMethod(this, [this, result] { finish(result); }, Qt::QueuedConnection);
    }

    void ended()
    {
        QNetworkReply* reply = m_reply;
        m_reply = nullptr;
        reply->deleteLater();
        if (m_cancelled)
            return;
        const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray data = reply->readAll();
        SpeechResult result;
        if (code != 200) {
            result.error = code == 401 ? tr("The ElevenLabs API key was not accepted. Check it in AI settings.")
                         : code == 429 ? tr("ElevenLabs is busy or your quota is used up; try again later.")
                         : code == 0   ? tr("Could not reach ElevenLabs: %1").arg(reply->errorString())
                                       : tr("ElevenLabs error %1").arg(code);
            const QJsonValue detail = QJsonDocument::fromJson(data).object().value(QStringLiteral("detail"));
            const QString message = detail.isObject() ? detail.toObject().value(QStringLiteral("message")).toString()
                                                      : detail.isString() ? detail.toString() : QString();
            if (!message.isEmpty() && code != 401)
                result.error += QStringLiteral(": ") + message;
            finish(result);
            return;
        }
        const QJsonObject answer = QJsonDocument::fromJson(data).object();
        const QByteArray audio = QByteArray::fromBase64(answer.value(QStringLiteral("audio_base64")).toString().toLatin1());
        if (audio.isEmpty()) {
            result.error = tr("ElevenLabs sent no sound.");
            finish(result);
            return;
        }
        // Named by what the bytes are (MP3 as asked; WAV from the mock server).
        const QString path = m_basePath + (audio.startsWith("RIFF") ? QStringLiteral(".wav") : QStringLiteral(".mp3"));
        QDir().mkpath(QFileInfo(path).absolutePath());
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly) || file.write(audio) != audio.size() || !file.commit()) {
            result.error = tr("The voice could not be saved.");
            finish(result);
            return;
        }
        result.ok = true;
        result.path = path;
        result.duration = fileDuration(path);
        result.characters = m_text.size();
        const QJsonObject alignment = answer.value(QStringLiteral("alignment")).toObject();
        QStringList characters;
        QList<double> starts, ends;
        for (const QJsonValue& c : alignment.value(QStringLiteral("characters")).toArray())
            characters << c.toString();
        for (const QJsonValue& t : alignment.value(QStringLiteral("character_start_times_seconds")).toArray())
            starts << t.toDouble();
        for (const QJsonValue& t : alignment.value(QStringLiteral("character_end_times_seconds")).toArray())
            ends << t.toDouble();
        result.words = wordsFromCharacters(characters, starts, ends);
        result.timed = !result.words.isEmpty();
        if (!result.timed)
            result.words = spreadWords(m_text, result.duration);
        finish(result);
    }

    QPointer<QNetworkReply> m_reply;
    QString m_text;
    QString m_basePath;
    bool m_cancelled = false;
};

} // namespace

QList<VoiceInfo> SystemVoices::voices() const
{
    QList<VoiceInfo> list;
    @autoreleasepool {
        for (AVSpeechSynthesisVoice* voice in [AVSpeechSynthesisVoice speechVoices]) {
            // The novelty voices (Bells, Bubbles…) are left out.
            if (voice.voiceTraits & AVSpeechSynthesisVoiceTraitIsNoveltyVoice)
                continue;
            const QString detail = voice.quality == AVSpeechSynthesisVoiceQualityPremium ? QStringLiteral("Premium")
                                 : voice.quality == AVSpeechSynthesisVoiceQualityEnhanced ? QStringLiteral("Enhanced")
                                                                                         : QString();
            list.push_back({QString::fromNSString(voice.identifier), QString::fromNSString(voice.name),
                            QString::fromNSString(voice.language), detail});
        }
    }
    std::ranges::sort(list, [](const VoiceInfo& a, const VoiceInfo& b) {
        return a.language != b.language ? a.language < b.language : a.name < b.name;
    });
    return list;
}

SpeechTask* SystemVoices::speak(const QString& text, const QString& voiceId, double speed, const QString& basePath, QObject* parent)
{
    auto* task = new SystemSpeechTask(text, parent);
    QMetaObject::invokeMethod(task, [task, voiceId, speed, basePath] { if (!task->done()) task->start(voiceId, speed, basePath); },
                              Qt::QueuedConnection);
    return task;
}

QString ElevenLabsVoices::baseUrl()
{
    const QString override = qEnvironmentVariable("TVE_ELEVENLABS_BASE_URL");
    return override.isEmpty() ? QStringLiteral("https://api.elevenlabs.io") : override;
}

void ElevenLabsVoices::refresh(std::function<void(QString)> done)
{
    const QString key = cloudkeys::read(QStringLiteral("elevenlabs"));
    if (key.isEmpty()) {
        m_voices.clear();
        done(QCoreApplication::translate("CloudVoices", "Add your ElevenLabs API key in AI settings first."));
        return;
    }
    QNetworkRequest http(QUrl(baseUrl() + QStringLiteral("/v1/voices")));
    http.setRawHeader("xi-api-key", key.toUtf8());
    http.setTransferTimeout(60000);
    QNetworkReply* reply = m_network->get(http);
    QObject::connect(reply, &QNetworkReply::finished, reply, [this, reply, done = std::move(done)] {
        reply->deleteLater();
        const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (code != 200) {
            m_voices.clear(); // a refused key has no voices
            done(code == 401 ? QCoreApplication::translate("CloudVoices", "The ElevenLabs API key was not accepted.")
                             : QCoreApplication::translate("CloudVoices", "Could not list ElevenLabs voices (%1).").arg(code ? QString::number(code) : reply->errorString()));
            return;
        }
        m_voices.clear();
        for (const QJsonValue& value : QJsonDocument::fromJson(reply->readAll()).object().value(QStringLiteral("voices")).toArray()) {
            const QJsonObject voice = value.toObject();
            const QJsonObject labels = voice.value(QStringLiteral("labels")).toObject();
            QStringList detail;
            for (const char* label : {"gender", "age", "accent"})
                if (const QString v = labels.value(QLatin1String(label)).toString(); !v.isEmpty())
                    detail << v;
            m_voices.push_back({voice.value(QStringLiteral("voice_id")).toString(), voice.value(QStringLiteral("name")).toString(),
                                labels.value(QStringLiteral("language")).toString(), detail.join(QStringLiteral(", ")),
                                voice.value(QStringLiteral("category")).toString() == QLatin1String("cloned")});
        }
        done({});
    });
}

namespace {

// What an ElevenLabs error says, in words for the person.
QString elevenLabsError(int code, const QByteArray& body, const QString& network)
{
    const QJsonValue detail = QJsonDocument::fromJson(body).object().value(QStringLiteral("detail"));
    QString message = detail.isObject() ? detail.toObject().value(QStringLiteral("message")).toString()
                    : detail.isArray()  ? detail.toArray().first().toObject().value(QStringLiteral("msg")).toString()
                                        : QString();
    if (code == 401)
        return QCoreApplication::translate("CloudVoices", "The ElevenLabs API key was not accepted.");
    if (code == 0)
        return QCoreApplication::translate("CloudVoices", "Could not reach ElevenLabs: %1").arg(network);
    return message.isEmpty() ? QCoreApplication::translate("CloudVoices", "ElevenLabs error %1").arg(code) : QCoreApplication::translate("CloudVoices", "ElevenLabs: %1").arg(message);
}

} // namespace

void ElevenLabsVoices::clone(const QString& name, const QString& description, const QString& recording,
                             std::function<void(QString, QString)> done)
{
    const QString key = cloudkeys::read(QStringLiteral("elevenlabs"));
    if (key.isEmpty()) {
        done({}, QCoreApplication::translate("CloudVoices", "Add your ElevenLabs API key in AI settings first."));
        return;
    }
    auto* file = new QFile(recording);
    if (!file->open(QIODevice::ReadOnly)) {
        delete file;
        done({}, QCoreApplication::translate("CloudVoices", "The recording can't be read."));
        return;
    }
    auto* form = new QHttpMultiPart(QHttpMultiPart::FormDataType);
    const auto field = [form](const char* fieldName, const QString& value) {
        QHttpPart part;
        part.setHeader(QNetworkRequest::ContentDispositionHeader, QStringLiteral("form-data; name=\"%1\"").arg(QLatin1String(fieldName)));
        part.setBody(value.toUtf8());
        form->append(part);
    };
    field("name", name);
    field("description", description);
    field("remove_background_noise", QStringLiteral("false"));
    QHttpPart audio;
    audio.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("audio/wav"));
    audio.setHeader(QNetworkRequest::ContentDispositionHeader, QStringLiteral("form-data; name=\"files\"; filename=\"consent.wav\""));
    audio.setBodyDevice(file);
    file->setParent(form);
    form->append(audio);

    QNetworkRequest http(QUrl(baseUrl() + QStringLiteral("/v1/voices/add")));
    http.setRawHeader("xi-api-key", key.toUtf8());
    http.setTransferTimeout(180000);
    QNetworkReply* reply = m_network->post(http, form);
    form->setParent(reply);
    QObject::connect(reply, &QNetworkReply::finished, reply, [reply, done = std::move(done)] {
        reply->deleteLater();
        const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray body = reply->readAll();
        const QString id = QJsonDocument::fromJson(body).object().value(QStringLiteral("voice_id")).toString();
        if (code != 200 || id.isEmpty()) {
            done({}, elevenLabsError(code, body, reply->errorString()));
            return;
        }
        done(id, {});
    });
}

void ElevenLabsVoices::remove(const QString& voiceId, std::function<void(QString)> done)
{
    const QString key = cloudkeys::read(QStringLiteral("elevenlabs"));
    if (key.isEmpty()) {
        done(QCoreApplication::translate("CloudVoices", "Add your ElevenLabs API key in AI settings first."));
        return;
    }
    QNetworkRequest http(QUrl(baseUrl() + QStringLiteral("/v1/voices/") + QString::fromLatin1(QUrl::toPercentEncoding(voiceId))));
    http.setRawHeader("xi-api-key", key.toUtf8());
    http.setTransferTimeout(60000);
    QNetworkReply* reply = m_network->deleteResource(http);
    QObject::connect(reply, &QNetworkReply::finished, reply, [this, reply, voiceId, done = std::move(done)] {
        reply->deleteLater();
        const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (code != 200) {
            done(elevenLabsError(code, reply->readAll(), reply->errorString()));
            return;
        }
        m_voices.removeIf([&](const VoiceInfo& v) { return v.id == voiceId; });
        done({});
    });
}

SpeechTask* ElevenLabsVoices::speak(const QString& text, const QString& voiceId, double speed, const QString& basePath, QObject* parent)
{
    auto* task = new ElevenLabsTask(parent);
    QNetworkAccessManager* network = m_network;
    const QString model = m_model;
    QMetaObject::invokeMethod(task, [task, network, text, voiceId, model, speed, basePath] {
        if (!task->done())
            task->start(network, text, voiceId, model, speed, basePath);
    }, Qt::QueuedConnection);
    return task;
}
