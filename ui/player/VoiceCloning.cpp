#include "VoiceCloning.h"

#include "ProjectController.h"
#include "SpeechModels.h"

#include "audio/AudioRecorder.h"
#include "cloud/CloudServices.h"
#include "cloud/CloudVoices.h"
#include "decode/AudioDecoder.h"
#include "speech/Transcriber.h"

#include <QCoreApplication>
#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>

#include <random>

using namespace tve;

namespace {

constexpr const char* kSuggestedModel = "base.en-q5_1";

QString bareId(const QString& voiceId)
{
    return voiceId.startsWith(QLatin1String("elevenlabs:")) ? voiceId.mid(11) : voiceId;
}

// A record's folder: a voice id only ever names one folder inside the consent directory.
QString recordFolder(const QString& voiceId)
{
    const QString id = bareId(voiceId);
    if (id.isEmpty() || id.contains(QLatin1Char('/')) || id.contains(QLatin1String("..")) || id.startsWith(QLatin1Char('.')))
        return {};
    return VoiceCloning::consentDirectory() + QLatin1Char('/') + id;
}

QJsonObject readRecord(const QString& voiceId)
{
    const QString folder = recordFolder(voiceId);
    QFile file(folder + QStringLiteral("/record.json"));
    if (folder.isEmpty() || !file.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}

} // namespace

VoiceCloning::VoiceCloning(ProjectController* project) : QObject(project), m_project(project)
{
    m_meter.setInterval(100);
    connect(&m_meter, &QTimer::timeout, this, &VoiceCloning::levelChanged);
    connect(m_project->m_cloud, &CloudServices::voicesChanged, this, &VoiceCloning::clonesChanged);
}

VoiceCloning::~VoiceCloning()
{
    *m_alive = false;
    if (m_worker.joinable())
        m_worker.join();
}

QString VoiceCloning::consentDirectory()
{
    const QString overridden = qEnvironmentVariable("TVE_CONSENT_DIR");
    return overridden.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/Voice consent")
                                : overridden;
}

double VoiceCloning::seconds() const
{
    if (m_stage != QLatin1String("recording") || !m_project->m_recorder)
        return 0.0;
    return toSeconds(m_project->m_recorder->recordedDuration());
}

double VoiceCloning::level() const
{
    return m_stage == QLatin1String("recording") && m_project->m_recorder ? double(m_project->m_recorder->peak()) : 0.0;
}

bool VoiceCloning::needsModel() const
{
    for (const SpeechModels::Model& model : SpeechModels::catalog())
        if (model.kind == QLatin1String("transcribe") && m_project->m_speechModels.path(model.id))
            return false;
    return true;
}

QString VoiceCloning::speechModel() const
{
    return QString::fromLatin1(kSuggestedModel);
}

QVariantList VoiceCloning::clones() const
{
    QVariantList list;
    for (const QVariant& value : m_project->m_cloud->voices()) {
        const QVariantMap voice = value.toMap();
        if (!voice.value(QStringLiteral("cloned")).toBool())
            continue;
        const QJsonObject record = readRecord(voice.value(QStringLiteral("id")).toString());
        list << QVariantMap{{QStringLiteral("id"), voice.value(QStringLiteral("id"))},
                            {QStringLiteral("name"), voice.value(QStringLiteral("name"))},
                            {QStringLiteral("consent"),
                             record.isEmpty() ? QVariant()
                                              : QVariant(QVariantMap{{QStringLiteral("speaker"), record.value(QStringLiteral("speaker")).toString()},
                                                                     {QStringLiteral("date"), record.value(QStringLiteral("date")).toString()},
                                                                     {QStringLiteral("created"), record.value(QStringLiteral("created")).toString()}})}};
    }
    return list;
}

void VoiceCloning::setStage(const QString& stage)
{
    m_stage = stage;
    if (stage == QLatin1String("recording"))
        m_meter.start();
    else
        m_meter.stop();
    emit changed();
    emit levelChanged();
}

void VoiceCloning::fail(const QString& error)
{
    m_error = error;
    setStage(QStringLiteral("failed"));
}

bool VoiceCloning::begin(const QString& speaker, const QString& voiceName, bool isSpeaker, bool adult, bool agrees)
{
    m_error.clear();
    const QString name = speaker.simplified();
    if (name.isEmpty() || consentWords(name.toStdString()).empty()) {
        m_error = QStringLiteral("Write the speaker's name: they say it in the statement.");
        emit changed();
        return false;
    }
    if (!isSpeaker || !adult || !agrees) {
        m_error = QStringLiteral("A voice is copied only by the person it belongs to, 18 or older, who agrees to it.");
        emit changed();
        return false;
    }
    if (!m_project->m_cloud->hasElevenLabsKey()) {
        m_error = QStringLiteral("Add your ElevenLabs API key in AI settings first.");
        emit changed();
        return false;
    }
    const QString seed = qEnvironmentVariable("TVE_CONSENT_SEED");
    std::mt19937 random(seed.isEmpty() ? std::random_device{}() : seed.toUInt());
    const QDate today = QDate::currentDate();
    m_statement = consentStatement(name.toStdString(), spokenDate(today.year(), today.month(), today.day()), random);
    m_voiceName = voiceName.simplified().isEmpty() ? name : voiceName.simplified();
    m_check.clear();
    m_madeVoice.clear();
    setStage(QStringLiteral("ready"));
    return true;
}

bool VoiceCloning::startRecording()
{
    if (m_stage != QLatin1String("ready") && m_stage != QLatin1String("checked"))
        return false;
    m_check.clear();
    QDir().mkpath(consentDirectory() + QStringLiteral("/takes"));
    m_takePath = consentDirectory() + QStringLiteral("/takes/") + QUuid::createUuid().toString(QUuid::WithoutBraces) + QStringLiteral(".wav");
    if (qEnvironmentVariable("TVE_CONSENT_RECORDING").startsWith(QLatin1String("say"))) {
        setStage(QStringLiteral("recording"));
        return true;
    }
    if (!m_project->openVoiceOver() || !m_project->m_recorder) {
        m_error = QStringLiteral("The microphone isn't available: allow it in System Settings › Privacy & Security › Microphone.");
        emit changed();
        return false;
    }
    std::string error;
    if (!m_project->m_recorder->start(m_takePath.toStdString(), &error)) {
        m_error = QStringLiteral("Could not start recording: %1").arg(QString::fromStdString(error));
        emit changed();
        return false;
    }
    setStage(QStringLiteral("recording"));
    return true;
}

bool VoiceCloning::stopRecording()
{
    if (m_stage != QLatin1String("recording"))
        return false;
    const QString hook = qEnvironmentVariable("TVE_CONSENT_RECORDING");
    if (hook.startsWith(QLatin1String("say"))) {
        setStage(QStringLiteral("checking"));
        sayTake(hook == QLatin1String("say-wrong"));
        return true;
    }
    std::string error;
    const auto take = m_project->m_recorder->stop(&error);
    if (!take) {
        fail(QStringLiteral("Recording failed: %1").arg(QString::fromStdString(error)));
        return false;
    }
    setStage(QStringLiteral("checking"));
    checkTake(QString::fromStdString(take->path), take->duration);
    return true;
}

// (Test hook) The Mac's own voice reads the statement, or something else, into the take.
void VoiceCloning::sayTake(bool wrong)
{
    auto voices = std::make_shared<SystemVoices>();
    QString voice;
    for (const VoiceInfo& v : voices->voices())
        if (v.language.startsWith(QLatin1String("en")) && voice.isEmpty())
            voice = v.id;
    const QString text = wrong ? QStringLiteral("Hello there. This is a different recording, about the weather and the sea, with "
                                                "none of the words that were asked for. The tide came in slowly, and the gulls "
                                                "called over the harbour while the boats rocked in the grey morning light. "
                                                "Later the clouds lifted, the market opened along the quay, and people came "
                                                "down to buy fish, bread and flowers before the rain returned in the afternoon.")
                               : statement();
    const QString base = m_takePath.chopped(4);
    SpeechTask* task = voices->speak(text, voice, 1.0, base, this);
    connect(task, &SpeechTask::finished, this, [this, voices, task](const SpeechResult& result) {
        task->deleteLater();
        if (!result.ok) {
            fail(result.error);
            return;
        }
        m_takePath = result.path;
        checkTake(result.path, fromSeconds(result.duration));
    });
}

void VoiceCloning::checkTake(const QString& path, Ticks duration)
{
    std::optional<QString> model;
    for (const SpeechModels::Model& m : SpeechModels::catalog()) {
        if (m.kind != QLatin1String("transcribe"))
            continue;
        if (auto found = m_project->m_speechModels.path(m.id); found && (!model || m.englishOnly))
            model = found;
    }
    if (!model) {
        fail(QStringLiteral("Download a speech model first: the recording is checked on this Mac."));
        return;
    }
    if (m_worker.joinable())
        m_worker.join();
    const ConsentStatement statement = m_statement;
    m_worker = std::thread([this, alive = m_alive, path = path.toStdString(), model = model->toStdString(), statement, duration] {
        std::string error;
        TranscribeOptions options;
        options.language = "en";
        const auto transcript = transcribe(path, model, options, {}, &error);
        std::string heard;
        if (transcript)
            for (const TranscriptWord& w : transcript->words)
                heard += (heard.empty() ? "" : " ") + w.text;
        const ConsentCheck check = checkConsent(statement, heard, duration);
        QMetaObject::invokeMethod(qApp, [this, alive, transcript = transcript.has_value(), error, check, heard] {
            if (!*alive)
                return;
            if (!transcript) {
                fail(QStringLiteral("The recording couldn't be checked: %1").arg(QString::fromStdString(error)));
                return;
            }
            QStringList missing;
            for (const std::string& w : check.missing)
                missing << QString::fromStdString(w);
            m_check = {{QStringLiteral("passed"), check.passed},
                       {QStringLiteral("match"), check.match},
                       {QStringLiteral("why"), QString::fromStdString(check.why)},
                       {QStringLiteral("missing"), missing},
                       {QStringLiteral("heard"), QString::fromStdString(heard).trimmed()}};
            setStage(QStringLiteral("checked"));
        }, Qt::QueuedConnection);
    });
}

bool VoiceCloning::makeVoice()
{
    if (m_stage != QLatin1String("checked") || !m_check.value(QStringLiteral("passed")).toBool())
        return false;
    const QString description = QStringLiteral("The voice of %1, copied in TechieVideoEditor from a consent statement they "
                                               "recorded and read on %2.")
                                    .arg(speaker(), QString::fromStdString(m_statement.date));
    QPointer<VoiceCloning> self(this);
    if (!m_project->m_cloud->cloneVoice(m_voiceName, description, m_takePath, [self](QString voiceId, QString error) {
            if (!self)
                return;
            if (!error.isEmpty()) {
                self->fail(error);
                return;
            }
            self->keepRecord(voiceId);
            self->m_madeVoice = QStringLiteral("elevenlabs:") + voiceId;
            self->setStage(QStringLiteral("done"));
        })) {
        m_error = QStringLiteral("Another AI job is running; try again when it's done.");
        emit changed();
        return false;
    }
    setStage(QStringLiteral("cloning"));
    return true;
}

// The consent kept with the voice: the statement, the recording and what was heard.
void VoiceCloning::keepRecord(const QString& voiceId)
{
    const QString folder = recordFolder(voiceId);
    if (folder.isEmpty() || !QDir().mkpath(folder))
        return;
    QFile::remove(folder + QStringLiteral("/consent.wav"));
    if (!QFile::rename(m_takePath, folder + QStringLiteral("/consent.wav")))
        QFile::copy(m_takePath, folder + QStringLiteral("/consent.wav"));
    const QJsonObject record{{QStringLiteral("voiceId"), voiceId},
                             {QStringLiteral("voiceName"), m_voiceName},
                             {QStringLiteral("provider"), QStringLiteral("ElevenLabs")},
                             {QStringLiteral("speaker"), speaker()},
                             {QStringLiteral("date"), QString::fromStdString(m_statement.date)},
                             {QStringLiteral("created"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
                             {QStringLiteral("statement"), statement()},
                             {QStringLiteral("heard"), m_check.value(QStringLiteral("heard")).toString()},
                             {QStringLiteral("match"), m_check.value(QStringLiteral("match")).toDouble()}};
    QSaveFile file(folder + QStringLiteral("/record.json"));
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(record).toJson());
        file.commit();
    }
}

void VoiceCloning::reset()
{
    if (m_stage == QLatin1String("recording") && m_project->m_recorder && m_project->m_recorder->recording())
        m_project->m_recorder->stop();
    // A take that wasn't used goes (only ever a file in the takes folder).
    if (!m_takePath.isEmpty() && m_takePath.startsWith(consentDirectory() + QStringLiteral("/takes/")))
        QFile::remove(m_takePath);
    m_takePath.clear();
    m_statement = {};
    m_voiceName.clear();
    m_check.clear();
    m_error.clear();
    m_madeVoice.clear();
    setStage(QStringLiteral("start"));
}

bool VoiceCloning::deleteVoice(const QString& voiceId)
{
    const QString id = bareId(voiceId);
    if (id.isEmpty())
        return false;
    QPointer<VoiceCloning> self(this);
    m_project->m_cloud->deleteVoice(id, [self, id](QString error) {
        if (!self)
            return;
        if (!error.isEmpty()) {
            self->m_error = error;
            emit self->changed();
            return;
        }
        // Its consent record goes with it: one folder, named by the id, inside the consent folder.
        const QString folder = recordFolder(id);
        if (!folder.isEmpty() && QDir(folder).exists() && QFileInfo(folder).dir().absolutePath() == QDir(consentDirectory()).absolutePath())
            QDir(folder).removeRecursively();
        emit self->clonesChanged();
    });
    return true;
}
