#pragma once

#include "model/Consent.h"

#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <memory>
#include <thread>

class ProjectController;

// A copy of a voice (ElevenLabs instant voice cloning) only with the speaker's consent,
// recorded here: they say they are the speaker and at least 18 and agree, read a consent
// statement written for them now (their name, what the copy is for, today's date, four code
// words picked afresh, a short passage), and the recording is transcribed on this Mac and
// checked against it. Only a recording that says it goes to ElevenLabs, and it is the only
// audio the copy is made from. A record of the consent (the statement, the recording, what
// was heard) is kept with the voice; cloned voices are listed, and deleting one deletes it
// from the account and the record with it.
//
// Test hooks: TVE_CONSENT_SEED fixes the code words; TVE_CONSENT_RECORDING=say (or
// say-wrong) makes the take the Mac's voice reading the statement (or something else)
// instead of the microphone. TVE_CONSENT_DIR holds the records (else app data).
class VoiceCloning : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("The project's voice cloning")
    // "start" → "ready" (the statement to read) → "recording" → "checking" → "checked" (passed
    // or not: check) → "cloning" → "done"; "failed" with error at any step.
    Q_PROPERTY(QString stage READ stage NOTIFY changed)
    Q_PROPERTY(QString statement READ statement NOTIFY changed)
    Q_PROPERTY(QString speaker READ speaker NOTIFY changed)
    Q_PROPERTY(QString voiceName READ voiceName NOTIFY changed)
    // {passed, match (0..1), why, heard}
    Q_PROPERTY(QVariantMap check READ check NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(double seconds READ seconds NOTIFY levelChanged)
    Q_PROPERTY(double level READ level NOTIFY levelChanged)
    // A transcription model is needed to check the recording (the smallest English one is
    // suggested: speechModel).
    Q_PROPERTY(bool needsModel READ needsModel NOTIFY changed)
    Q_PROPERTY(QString speechModel READ speechModel CONSTANT)
    // The cloned voices in the ElevenLabs account: [{id ("elevenlabs:…"), name, consent
    // ({speaker, date, created} when made here, else null)}].
    Q_PROPERTY(QVariantList clones READ clones NOTIFY clonesChanged)
    // The voice just made ("elevenlabs:…"), to use at once.
    Q_PROPERTY(QString madeVoice READ madeVoice NOTIFY changed)

public:
    explicit VoiceCloning(ProjectController* project);
    ~VoiceCloning() override;

    QString stage() const { return m_stage; }
    QString statement() const { return QString::fromStdString(m_statement.text); }
    QString speaker() const { return QString::fromStdString(m_statement.speaker); }
    QString voiceName() const { return m_voiceName; }
    QVariantMap check() const { return m_check; }
    QString error() const { return m_error; }
    double seconds() const;
    double level() const;
    bool needsModel() const;
    QString speechModel() const;
    QVariantList clones() const;
    QString madeVoice() const { return m_madeVoice; }

    // Writes the statement for `speaker`, once they confirm they are the speaker, at least 18,
    // and agree. False (with error) otherwise.
    Q_INVOKABLE bool begin(const QString& speaker, const QString& voiceName, bool isSpeaker, bool adult, bool agrees);
    Q_INVOKABLE bool startRecording();
    // Ends the take, then transcribes and checks it.
    Q_INVOKABLE bool stopRecording();
    // Sends the checked recording to ElevenLabs for the copy.
    Q_INVOKABLE bool makeVoice();
    // Back to the start (the take and statement thrown away).
    Q_INVOKABLE void reset();
    // Deletes a cloned voice ("elevenlabs:…" or its bare id) from the account, and its
    // consent record. Done when clonesChanged comes (or error says why not).
    Q_INVOKABLE bool deleteVoice(const QString& voiceId);

    // Where consent records are kept.
    static QString consentDirectory();

signals:
    void changed();
    void levelChanged();
    void clonesChanged();

private:
    void setStage(const QString& stage);
    void fail(const QString& error);
    void checkTake(const QString& path, tve::Ticks duration);
    void sayTake(bool wrong);
    void keepRecord(const QString& voiceId);

    ProjectController* m_project;
    QString m_stage = QStringLiteral("start");
    tve::ConsentStatement m_statement;
    QString m_voiceName;
    QVariantMap m_check;
    QString m_error;
    QString m_takePath;
    QString m_madeVoice;
    QTimer m_meter;
    std::thread m_worker;
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
};
