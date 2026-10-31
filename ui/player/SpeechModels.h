#pragma once

#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <memory>
#include <optional>
#include <vector>

class QCryptographicHash;
class QFile;
class QNetworkAccessManager;
class QNetworkReply;

// The speech models the app can use — speech recognition (Whisper, MIT, from the official
// whisper.cpp model repository) and speech enhancement (DeepFilterNet3, MIT / Apache-2.0,
// from its GitHub repository) — which of them are on this computer, and downloading one:
// streamed to a ".part" file, checked against its known size and SHA-256, then moved into
// place (an archive is unpacked into a folder of the model's id). Models live in
// <app data>/Models (TVE_MODEL_DIR moves them; TVE_MODEL_BASE_URL is where they all come
// from, for tests).
class SpeechModels : public QObject {
    Q_OBJECT

public:
    struct Model {
        QString id;    // e.g. "base.en-q5_1"
        QString title; // e.g. "Base · English"
        QString file;  // e.g. "ggml-base.en-q5_1.bin"
        qint64 size = 0;
        QString sha256;
        bool englishOnly = false;
        QString note; // "fast", "more accurate"…
        QString kind = QStringLiteral("transcribe"); // or "enhance"
        QString url;          // where it comes from ("": the whisper.cpp repository)
        QStringList contents; // an archive (.tar.gz): the files it must hold, unpacked
    };

    explicit SpeechModels(QObject* parent = nullptr);
    ~SpeechModels() override;

    static const std::vector<Model>& catalog();
    static const Model* find(const QString& id);

    QString directory() const { return m_directory; }
    // Where the model is (its folder, for an archive), if it is here (and its size is right).
    std::optional<QString> path(const QString& id) const;
    // {id, title, size, englishOnly, note, downloaded} for each model of a kind.
    QVariantList list(const QString& kind = QStringLiteral("transcribe")) const;

    // Starts downloading; false (with `error`) if it cannot start (unknown model, already
    // downloading). finished() says how it ended.
    bool download(const QString& id, QString* error);
    void cancel();
    // {id, received, total, progress} while downloading, else empty.
    QVariantMap progress() const;

signals:
    void changed();
    // ok, or why not (empty when cancelled).
    void finished(const QString& id, bool ok, const QString& error);

private:
    void end(bool ok, const QString& error);
    // Unpacks a downloaded archive into <directory>/<id>; why not, or empty.
    QString unpack(const Model& model, const QString& archive);

    QString m_directory;
    QString m_baseUrl;
    QNetworkAccessManager* m_network = nullptr;
    QPointer<QNetworkReply> m_reply;
    std::unique_ptr<QFile> m_file;
    std::unique_ptr<QCryptographicHash> m_hash;
    QString m_downloading;
    qint64 m_received = 0;
    bool m_cancelled = false;
};
