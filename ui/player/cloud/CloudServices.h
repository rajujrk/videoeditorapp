#pragma once

#include "cloud/CloudMedia.h"
#include "cloud/CloudText.h"
#include "cloud/CloudVoices.h"

#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <functional>
#include <memory>

class QNetworkAccessManager;

// The AI features' shared state for QML: the person's keys (in the Keychain), the models
// used, the one cloud job that runs at a time (its streamed text, status and cancel), what
// this session has used, and the voices. Settings are kept in <app data>/cloud-settings.json
// (TVE_CLOUD_SETTINGS_FILE). TVE_CLOUD_MOCK=1 points everything at MockCloudServer.
class CloudServices : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("The project's AI services")
    Q_PROPERTY(bool hasClaudeKey READ hasClaudeKey NOTIFY keysChanged)
    Q_PROPERTY(bool hasElevenLabsKey READ hasElevenLabsKey NOTIFY keysChanged)
    Q_PROPERTY(bool hasOpenAiKey READ hasOpenAiKey NOTIFY keysChanged)
    Q_PROPERTY(bool hasGoogleKey READ hasGoogleKey NOTIFY keysChanged)
    Q_PROPERTY(QVariantList imageModels READ imageModels CONSTANT)
    Q_PROPERTY(QString imageModel READ imageModel WRITE setImageModel NOTIFY settingsChanged)
    // Seconds the job has been running (generation can take minutes).
    Q_PROPERTY(int elapsed READ elapsed NOTIFY elapsedChanged)
    // [{id, label, detail}]
    Q_PROPERTY(QVariantList claudeModels READ claudeModels CONSTANT)
    Q_PROPERTY(QString claudeModel READ claudeModel WRITE setClaudeModel NOTIFY settingsChanged)
    Q_PROPERTY(QVariantList elevenLabsModels READ elevenLabsModels CONSTANT)
    Q_PROPERTY(QString elevenLabsModel READ elevenLabsModel WRITE setElevenLabsModel NOTIFY settingsChanged)
    // The job running: "" (none), "write", "translate", "speak", "image", "sticker", "video",
    // "music" or "sfx"; its text so far; what it is
    // doing in words; how far (0..1, translations).
    Q_PROPERTY(QString job READ job NOTIFY jobChanged)
    Q_PROPERTY(QString draft READ draft NOTIFY draftChanged)
    Q_PROPERTY(QString status READ status NOTIFY jobChanged)
    Q_PROPERTY(double progress READ progress NOTIFY jobChanged)
    // This session: Claude tokens in and out, their estimated cost (US$), ElevenLabs characters,
    // pictures made and their tokens, videos, seconds of music and sound effects made.
    Q_PROPERTY(QVariantMap usage READ usage NOTIFY usageChanged)
    // [{id ("system:…" / "elevenlabs:…"), name, language, detail, provider}]
    Q_PROPERTY(QVariantList voices READ voices NOTIFY voicesChanged)
    Q_PROPERTY(QString voicesError READ voicesError NOTIFY voicesChanged)
    Q_PROPERTY(bool mock READ mock CONSTANT)

public:
    explicit CloudServices(QObject* parent = nullptr);
    ~CloudServices() override;

    bool hasClaudeKey() const { return m_hasKey.value(QStringLiteral("anthropic")); }
    bool hasElevenLabsKey() const { return m_hasKey.value(QStringLiteral("elevenlabs")); }
    bool hasOpenAiKey() const { return m_hasKey.value(QStringLiteral("openai")); }
    bool hasGoogleKey() const { return m_hasKey.value(QStringLiteral("google")); }
    QVariantList imageModels() const;
    QString imageModel() const { return m_imageModel; }
    void setImageModel(const QString& model);
    int elapsed() const { return m_elapsed; }
    QVariantList claudeModels() const;
    QString claudeModel() const { return m_claudeModel; }
    void setClaudeModel(const QString& model);
    QVariantList elevenLabsModels() const;
    QString elevenLabsModel() const { return m_elevenLabs->model(); }
    void setElevenLabsModel(const QString& model);
    QString job() const { return m_job; }
    QString draft() const { return m_draft; }
    QString status() const { return m_status; }
    double progress() const { return m_progress; }
    QVariantMap usage() const;
    // (Listed the first time they are asked for: listing the Mac's voices takes a while.)
    QVariantList voices() const;
    QString voicesError() const { return m_voicesError; }
    bool mock() const { return m_mock; }

    // "anthropic", "elevenlabs", "openai" or "google". False (with lastError) if it cannot be kept.
    Q_INVOKABLE bool setKey(const QString& provider, const QString& key);
    Q_INVOKABLE void removeKey(const QString& provider);
    Q_INVOKABLE QString lastError() const { return m_error; }
    Q_INVOKABLE void refreshVoices();
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void resetUsage();
    // Requests the mock server has had: [{method, path, headers, body}] (mock only).
    Q_INVOKABLE QVariantList mockRequests() const;

    // --- for the controller ---
    bool busy() const { return !m_job.isEmpty(); }
    // Starts a job; false if one runs. `done` is called once (not when this is destroyed).
    bool generate(const QString& job, const TextRequest& request, std::function<void(const TextResult&)> done);
    // One turn of the copilot's conversation (job "copilot"; its turns follow one another in
    // the one job until endJob, or the turn fails or is cancelled).
    bool converse(const AgentRequest& request, std::function<void(const AgentResult&)> done);
    bool speak(const QString& text, const QString& voiceId, double speed, const QString& basePath,
               std::function<void(const SpeechResult&)> done);
    // Generates media: `start` makes the task (with this object's network).
    using MediaStart = std::function<MediaTask*(QNetworkAccessManager*, QObject*)>;
    bool generateMedia(const QString& job, const QString& status, MediaStart start,
                       std::function<void(const MediaResult&)> done);
    void setProgress(double progress, const QString& status);
    void endJob(const QString& status = {});

signals:
    void keysChanged();
    void settingsChanged();
    void jobChanged();
    void draftChanged();
    void usageChanged();
    void elapsedChanged();
    void voicesChanged();
    // A job ended: ok, or why not (empty when cancelled).
    void jobFinished(const QString& job, bool ok, const QString& error);

private:
    void loadSettings();
    void saveSettings() const;
    void beginJob(const QString& job, const QString& status);
    void rebuildVoices();

    QNetworkAccessManager* m_network;
    std::unique_ptr<ClaudeProvider> m_claude;
    std::unique_ptr<SystemVoices> m_system;
    std::unique_ptr<ElevenLabsVoices> m_elevenLabs;
    QMap<QString, bool> m_hasKey;
    QString m_claudeModel = QStringLiteral("claude-opus-5-5");
    QString m_job;
    QString m_draft;
    QString m_status;
    double m_progress = 0;
    QPointer<TextTask> m_textTask;
    QPointer<SpeechTask> m_speechTask;
    QPointer<MediaTask> m_mediaTask;
    QString m_imageModel = QStringLiteral("gpt-image-2.5-flare");
    QTimer m_clock;
    int m_elapsed = 0;
    qint64 m_images = 0;
    qint64 m_imageTokens = 0;
    qint64 m_videos = 0;
    double m_soundSeconds = 0;
    qint64 m_inputTokens = 0;
    qint64 m_outputTokens = 0;
    double m_cost = 0;
    qint64 m_characters = 0;
    QVariantList m_voices;
    bool m_voicesListed = false;
    QList<VoiceInfo> m_systemVoices;
    QString m_voicesError;
    QString m_error;
    bool m_mock = false;
};
