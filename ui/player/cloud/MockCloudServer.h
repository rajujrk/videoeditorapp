#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QMap>
#include <QObject>
#include <QString>

class QTcpServer;
class QTcpSocket;

// A stand-in for the Claude and ElevenLabs APIs on 127.0.0.1, for trying the AI features
// and testing them without keys or the network (TVE_CLOUD_MOCK=1 starts it and points the
// providers at it). The real adapters talk to it over HTTP: streamed Claude replies, JSON
// for structured requests (translations: "[<language>] " before each caption), a voice
// list, and speech as a WAV with each character's time. A key "bad-key" is refused. Words in
// a prompt steer it: "#overload" (busy once, then fine), "#refuse" (declined), "#fallback"
// (a decline mid-answer, then another model's answer), "#slow" (streams slowly). It also
// makes pictures (OpenAI Images: a disc, on transparency when asked, unless "#opaque"),
// video (Gemini interactions: a few seconds of moving colour; an extension needs the id of
// one it made) and sound (ElevenLabs music and effects: a tone as long as asked); "#blocked"
// is refused. ElevenLabs voice cloning (voices/add, a multipart form) adds a "cloned" voice
// to the list; DELETE removes it. With tools (the copilot) it follows a plan in the request: "#plan [{name,
// input}, …]" are its first tool calls (after a thinking block and a line of text), "#then
// […]" the next round's; then a closing sentence (naming a failed call). "#invalid" streams
// a tool input that isn't JSON; "#refuse" declines. A request without "#" takes its first
// calls from TVE_MOCK_COPILOT_PLAN (the same JSON), if set.
class MockCloudServer : public QObject {
    Q_OBJECT
public:
    // The one server of the process, started the first time.
    static MockCloudServer* instance();
    QString baseUrl() const;

    struct Request {
        QString method;
        QString path;
        QMap<QString, QString> headers; // lower-case names
        QJsonObject body; // JSON; or a multipart form's fields, its files as [{name, filename, type, size}] under "files"
    };
    // The requests so far, oldest first.
    const QList<Request>& requests() const { return m_requests; }
    void clearRequests() { m_requests.clear(); }

private:
    explicit MockCloudServer(QObject* parent = nullptr);
    void serve(QTcpSocket* socket, const Request& request);
    void claude(QTcpSocket* socket, const Request& request);
    void copilot(QTcpSocket* socket, const Request& request);
    void elevenLabs(QTcpSocket* socket, const Request& request);
    void openAiImages(QTcpSocket* socket, const Request& request);
    void geminiVideo(QTcpSocket* socket, const Request& request);
    void elevenLabsSound(QTcpSocket* socket, const Request& request);

    QTcpServer* m_server = nullptr;
    QList<Request> m_requests;
    QMap<QString, int> m_overloads; // prompt → times refused as busy
    QStringList m_interactions;     // video interactions made
    QList<QJsonObject> m_clones;    // voices copied (ElevenLabs voices/add)
};
