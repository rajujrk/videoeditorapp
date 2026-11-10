#pragma once

#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <functional>

class QNetworkAccessManager;
class QNetworkReply;

// Text to speech behind one interface: the voices a provider has, and speaking a text into an
// audio file with each word's time (for captions).

struct VoiceInfo {
    QString id;
    QString name;
    QString language; // e.g. "en-GB"; empty when the voice speaks many
    QString detail;   // "Enhanced", "Premade"…
    bool cloned = false; // a copy of someone's voice (ElevenLabs' "cloned" category)
};

struct SpokenWord {
    double start = 0; // seconds into the audio
    double end = 0;
    QString text;
};

struct SpeechResult {
    bool ok = false;
    bool cancelled = false;
    QString error;
    QString path;            // the audio file
    double duration = 0;     // seconds
    QList<SpokenWord> words; // in order
    bool timed = false;      // the words' times came from the voice (else spread over the text)
    qint64 characters = 0;   // billed characters (cloud voices)
};

class SpeechTask : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    virtual void cancel() = 0;
    bool done() const { return m_done; }

signals:
    void finished(const SpeechResult& result);

protected:
    void finish(SpeechResult result);
    bool m_done = false;
};

class VoiceProvider {
public:
    virtual ~VoiceProvider() = default;
    virtual QList<VoiceInfo> voices() const = 0;
    // Fetches the list again where it comes from the cloud; `done` gets an error or empty.
    virtual void refresh(std::function<void(QString error)> done) { done({}); }
    // Speaks into `basePath` plus the extension the audio needs; `speed` 0.5..2.
    virtual SpeechTask* speak(const QString& text, const QString& voiceId, double speed, const QString& basePath,
                              QObject* parent) = 0;
};

// macOS voices, offline (AVSpeechSynthesizer): a WAV, with word times from its markers.
class SystemVoices final : public VoiceProvider {
public:
    QList<VoiceInfo> voices() const override;
    SpeechTask* speak(const QString& text, const QString& voiceId, double speed, const QString& basePath,
                      QObject* parent) override;
};

// ElevenLabs (text to speech with timestamps). The key comes from cloudkeys ("elevenlabs");
// TVE_ELEVENLABS_BASE_URL points it elsewhere (the mock server).
class ElevenLabsVoices final : public VoiceProvider {
public:
    explicit ElevenLabsVoices(QNetworkAccessManager* network) : m_network(network) {}
    QList<VoiceInfo> voices() const override { return m_voices; }
    void refresh(std::function<void(QString error)> done) override;
    SpeechTask* speak(const QString& text, const QString& voiceId, double speed, const QString& basePath,
                      QObject* parent) override;
    // Instant voice cloning: a copy of the voice in `recording` (a WAV), named `name`, in the
    // person's ElevenLabs account. `done` gets the new voice's id, or an error.
    void clone(const QString& name, const QString& description, const QString& recording,
               std::function<void(QString voiceId, QString error)> done);
    // Deletes a voice from the account; `done` gets an error or empty.
    void remove(const QString& voiceId, std::function<void(QString error)> done);
    void setModel(const QString& model) { m_model = model; }
    QString model() const { return m_model; }

    static QString baseUrl();

private:
    QNetworkAccessManager* m_network;
    QList<VoiceInfo> m_voices;
    QString m_model = QStringLiteral("eleven_multilingual_v2");
};

// Words spread over a duration by their length (when a voice gives no times).
QList<SpokenWord> spreadWords(const QString& text, double duration);
// Words from per-character times (ElevenLabs' alignment).
QList<SpokenWord> wordsFromCharacters(const QStringList& characters, const QList<double>& starts, const QList<double>& ends);
