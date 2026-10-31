#include "cloud/CloudServices.h"

#include "cloud/CloudKeys.h"
#include "cloud/MockCloudServer.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QSaveFile>
#include <QStandardPaths>

namespace {

QString settingsFile()
{
    const QString override = qEnvironmentVariable("TVE_CLOUD_SETTINGS_FILE");
    return !override.isEmpty() ? override
                               : QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/cloud-settings.json");
}

const QStringList kClaudeModels{QStringLiteral("claude-opus-5-5"), QStringLiteral("claude-sonnet-5-5")};
const QStringList kElevenLabsModels{QStringLiteral("eleven_multilingual_v2"), QStringLiteral("eleven_flash_v2_5")};

} // namespace

CloudServices::CloudServices(QObject* parent)
    : QObject(parent), m_network(new QNetworkAccessManager(this)), m_claude(std::make_unique<ClaudeProvider>(m_network)),
      m_system(std::make_unique<SystemVoices>()), m_elevenLabs(std::make_unique<ElevenLabsVoices>(m_network))
{
    m_mock = qEnvironmentVariable("TVE_CLOUD_MOCK") == QLatin1String("1");
    if (m_mock) {
        const QString url = MockCloudServer::instance()->baseUrl();
        qputenv("TVE_ANTHROPIC_BASE_URL", url.toUtf8());
        qputenv("TVE_ELEVENLABS_BASE_URL", url.toUtf8());
    }
    for (const char* provider : {"anthropic", "elevenlabs"})
        m_hasKey.insert(QLatin1String(provider), cloudkeys::has(QLatin1String(provider)));
    loadSettings();
}

CloudServices::~CloudServices()
{
    // Jobs end with this; their callbacks are not called.
    if (m_textTask)
        m_textTask->disconnect(this);
    if (m_speechTask)
        m_speechTask->disconnect(this);
}

QVariantList CloudServices::claudeModels() const
{
    return {QVariantMap{{QStringLiteral("id"), kClaudeModels[0]}, {QStringLiteral("label"), QStringLiteral("Claude Opus 5.5")},
                        {QStringLiteral("detail"), QStringLiteral("Best writing and translation · $4 / $20 per million tokens")}},
            QVariantMap{{QStringLiteral("id"), kClaudeModels[1]}, {QStringLiteral("label"), QStringLiteral("Claude Sonnet 5.5")},
                        {QStringLiteral("detail"), QStringLiteral("Faster, half the price · $2 / $10 per million tokens")}}};
}

QVariantList CloudServices::elevenLabsModels() const
{
    return {QVariantMap{{QStringLiteral("id"), kElevenLabsModels[0]}, {QStringLiteral("label"), QStringLiteral("Multilingual v2")},
                        {QStringLiteral("detail"), QStringLiteral("Most natural, 29 languages")}},
            QVariantMap{{QStringLiteral("id"), kElevenLabsModels[1]}, {QStringLiteral("label"), QStringLiteral("Flash v2.5")},
                        {QStringLiteral("detail"), QStringLiteral("Fast and cheaper per character")}}};
}

void CloudServices::setClaudeModel(const QString& model)
{
    if (!kClaudeModels.contains(model) || model == m_claudeModel)
        return;
    m_claudeModel = model;
    saveSettings();
    emit settingsChanged();
}

void CloudServices::setElevenLabsModel(const QString& model)
{
    if (!kElevenLabsModels.contains(model) || model == m_elevenLabs->model())
        return;
    m_elevenLabs->setModel(model);
    saveSettings();
    emit settingsChanged();
}

void CloudServices::loadSettings()
{
    QFile file(settingsFile());
    if (!file.open(QIODevice::ReadOnly))
        return;
    const QJsonObject settings = QJsonDocument::fromJson(file.readAll()).object();
    if (const QString model = settings.value(QStringLiteral("claudeModel")).toString(); kClaudeModels.contains(model))
        m_claudeModel = model;
    if (const QString model = settings.value(QStringLiteral("elevenLabsModel")).toString(); kElevenLabsModels.contains(model))
        m_elevenLabs->setModel(model);
}

void CloudServices::saveSettings() const
{
    QDir().mkpath(QFileInfo(settingsFile()).absolutePath());
    QSaveFile file(settingsFile());
    if (!file.open(QIODevice::WriteOnly))
        return;
    file.write(QJsonDocument(QJsonObject{{QStringLiteral("claudeModel"), m_claudeModel},
                                         {QStringLiteral("elevenLabsModel"), m_elevenLabs->model()}})
                   .toJson());
    file.commit();
}

QVariantMap CloudServices::usage() const
{
    return {{QStringLiteral("inputTokens"), double(m_inputTokens)},
            {QStringLiteral("outputTokens"), double(m_outputTokens)},
            {QStringLiteral("cost"), m_cost},
            {QStringLiteral("characters"), double(m_characters)}};
}

void CloudServices::resetUsage()
{
    m_inputTokens = m_outputTokens = m_characters = 0;
    m_cost = 0;
    emit usageChanged();
}

bool CloudServices::setKey(const QString& provider, const QString& key)
{
    if (provider != QLatin1String("anthropic") && provider != QLatin1String("elevenlabs")) {
        m_error = QStringLiteral("Unknown provider %1").arg(provider);
        return false;
    }
    if (!cloudkeys::store(provider, key, &m_error))
        return false;
    m_hasKey[provider] = true;
    emit keysChanged();
    if (provider == QLatin1String("elevenlabs") && m_voicesListed)
        refreshVoices();
    return true;
}

void CloudServices::removeKey(const QString& provider)
{
    cloudkeys::remove(provider);
    m_hasKey[provider] = false;
    emit keysChanged();
    if (provider == QLatin1String("elevenlabs") && m_voicesListed)
        refreshVoices();
}

QVariantList CloudServices::voices() const
{
    if (!m_voicesListed) {
        // Listed now without telling (this is a read); the cloud voices follow.
        auto* self = const_cast<CloudServices*>(this);
        self->m_voicesListed = true;
        self->m_systemVoices = m_system->voices();
        self->rebuildVoices();
        if (hasElevenLabsKey())
            QMetaObject::invokeMethod(self, &CloudServices::refreshVoices, Qt::QueuedConnection);
    }
    return m_voices;
}

void CloudServices::rebuildVoices()
{
    m_voices.clear();
    for (const VoiceInfo& voice : m_systemVoices)
        m_voices << QVariantMap{{QStringLiteral("id"), QStringLiteral("system:") + voice.id},
                                {QStringLiteral("name"), voice.name},
                                {QStringLiteral("language"), voice.language},
                                {QStringLiteral("detail"), voice.detail},
                                {QStringLiteral("provider"), QStringLiteral("system")}};
    for (const VoiceInfo& voice : m_elevenLabs->voices())
        m_voices << QVariantMap{{QStringLiteral("id"), QStringLiteral("elevenlabs:") + voice.id},
                                {QStringLiteral("name"), voice.name},
                                {QStringLiteral("language"), voice.language},
                                {QStringLiteral("detail"), voice.detail},
                                {QStringLiteral("provider"), QStringLiteral("elevenlabs")}};
}

void CloudServices::refreshVoices()
{
    if (!m_voicesListed) {
        voices(); // lists them, then comes back here for the cloud ones
        emit voicesChanged();
        return;
    }
    m_voicesError.clear();
    rebuildVoices();
    emit voicesChanged();
    if (!hasElevenLabsKey())
        return;
    QPointer<CloudServices> self(this);
    m_elevenLabs->refresh([self](QString error) {
        if (!self)
            return;
        self->m_voicesError = error;
        self->rebuildVoices();
        emit self->voicesChanged();
    });
}

void CloudServices::beginJob(const QString& job, const QString& status)
{
    m_job = job;
    m_status = status;
    m_progress = 0;
    m_draft.clear();
    emit jobChanged();
    emit draftChanged();
}

void CloudServices::setProgress(double progress, const QString& status)
{
    m_progress = progress;
    m_status = status;
    emit jobChanged();
}

void CloudServices::endJob(const QString& status)
{
    m_job.clear();
    m_status = status;
    m_progress = 0;
    emit jobChanged();
}

bool CloudServices::generate(const QString& job, const TextRequest& request, std::function<void(const TextResult&)> done)
{
    // A translation sends batches one after another within its own job: the next once the
    // last has finished.
    if (busy() && (m_job != job || (m_textTask && !m_textTask->done())))
        return false;
    if (!busy())
        beginJob(job, QStringLiteral("Asking Claude…"));
    TextTask* task = m_claude->send(request, this);
    m_textTask = task;
    connect(task, &TextTask::textChanged, this, [this](const QString& text) {
        m_draft = text;
        emit draftChanged();
    });
    connect(task, &TextTask::retrying, this, [this](int attempt, double seconds) {
        m_status = QStringLiteral("Claude is busy; trying again in %1 s (attempt %2 of 3)…").arg(qRound(seconds)).arg(attempt);
        emit jobChanged();
    });
    connect(task, &TextTask::finished, this, [this, task, done = std::move(done)](const TextResult& result) {
        task->deleteLater();
        m_inputTokens += result.usage.inputTokens;
        m_outputTokens += result.usage.outputTokens;
        m_cost += result.usage.cost;
        emit usageChanged();
        done(result);
    });
    return true;
}

bool CloudServices::speak(const QString& text, const QString& voiceId, double speed, const QString& basePath,
                          std::function<void(const SpeechResult&)> done)
{
    if (busy())
        return false;
    const bool cloud = voiceId.startsWith(QLatin1String("elevenlabs:"));
    VoiceProvider* provider = cloud ? static_cast<VoiceProvider*>(m_elevenLabs.get()) : m_system.get();
    const QString id = voiceId.mid(voiceId.indexOf(QLatin1Char(':')) + 1);
    beginJob(QStringLiteral("speak"), cloud ? QStringLiteral("ElevenLabs is speaking…") : QStringLiteral("Speaking…"));
    SpeechTask* task = provider->speak(text, id, speed, basePath, this);
    m_speechTask = task;
    connect(task, &SpeechTask::finished, this, [this, task, done = std::move(done)](const SpeechResult& result) {
        task->deleteLater();
        if (result.ok && result.characters > 0) {
            m_characters += result.characters;
            emit usageChanged();
        }
        done(result);
    });
    return true;
}

void CloudServices::cancel()
{
    if (m_textTask)
        m_textTask->cancel();
    if (m_speechTask)
        m_speechTask->cancel();
}

QVariantList CloudServices::mockRequests() const
{
    QVariantList list;
    if (!m_mock)
        return list;
    for (const auto& request : MockCloudServer::instance()->requests()) {
        QVariantMap headers;
        for (auto it = request.headers.begin(); it != request.headers.end(); ++it)
            headers.insert(it.key(), it.value());
        list << QVariantMap{{QStringLiteral("method"), request.method},
                            {QStringLiteral("path"), request.path},
                            {QStringLiteral("headers"), headers},
                            {QStringLiteral("body"), request.body.toVariantMap()}};
    }
    return list;
}
