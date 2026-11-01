#pragma once

#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <functional>

class QNetworkAccessManager;
class QNetworkReply;
class QNetworkRequest;

// Generated media behind one task type: images and stickers (OpenAI GPT Image), video
// (Google Gemini Omni Flash) and music and sound effects (ElevenLabs). Each request writes
// a file and says what made it. Keys come from cloudkeys ("openai", "google",
// "elevenlabs"); TVE_OPENAI_BASE_URL, TVE_GEMINI_BASE_URL and TVE_ELEVENLABS_BASE_URL point
// them elsewhere (the mock server).

struct MediaResult {
    bool ok = false;
    bool cancelled = false;
    QString error;
    QString path;        // the file made
    QString model;
    QString reference;   // the provider's id for it (Gemini's interaction, to extend)
    qint64 tokens = 0;   // OpenAI image tokens
    double seconds = 0;  // music and sound
};

class MediaTask : public QObject {
    Q_OBJECT
public:
    // `read` turns the finished reply (status, body) into a result.
    using Reader = std::function<MediaResult(int status, const QByteArray& body, const QNetworkReply* reply)>;
    MediaTask(QNetworkAccessManager* network, QObject* parent);
    // Sends once the caller has connected (a queued start); `body` is JSON.
    void post(const QNetworkRequest& request, const QByteArray& body, Reader read);
    void cancel();
    // Fails at once (a missing key, say), still through finished().
    void fail(const QString& error);
    bool done() const { return m_done; }

signals:
    void finished(const MediaResult& result);

private:
    void finish(MediaResult result);

    QNetworkAccessManager* m_network;
    QPointer<QNetworkReply> m_reply;
    bool m_done = false;
    bool m_cancelled = false;
};

struct ImageOptions {
    QString model = QStringLiteral("gpt-image-2.5-flare");
    QString size = QStringLiteral("1024x1024");
    QString quality = QStringLiteral("medium");
    bool transparent = false;
    QStringList inputs; // image files to work from (image to image)
};

struct VideoOptions {
    QString mode = QStringLiteral("text"); // "text", "image", "frames", "extend"
    QStringList images;                    // one (image) or two (frames: first, last)
    QString previous;                      // the interaction to extend
    QString aspect = QStringLiteral("16:9");
    QString resolution = QStringLiteral("720p");
};

namespace cloudmedia {

constexpr const char* kVideoModel = "gemini-omni-1.1-flash";

MediaTask* image(QNetworkAccessManager* network, const QString& prompt, const ImageOptions& options,
                 const QString& basePath, QObject* parent);
MediaTask* video(QNetworkAccessManager* network, const QString& prompt, const VideoOptions& options,
                 const QString& basePath, QObject* parent);
// Music (3–600 s) or a sound effect (0.5–30 s).
MediaTask* music(QNetworkAccessManager* network, const QString& prompt, double seconds, bool instrumental,
                 const QString& basePath, QObject* parent);
MediaTask* soundEffect(QNetworkAccessManager* network, const QString& prompt, double seconds, bool loop,
                       const QString& basePath, QObject* parent);

QString openAiBaseUrl();
QString geminiBaseUrl();

} // namespace cloudmedia
