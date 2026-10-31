#include "cloud/MockCloudServer.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <QRegularExpression>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUrl>

#include <cmath>
#include <numbers>

namespace {

void respond(QTcpSocket* socket, int status, const QByteArray& type, const QByteArray& body, const QByteArray& extra = {})
{
    const QByteArray reason = status == 200 ? "OK" : status == 401 ? "Unauthorized" : status == 529 ? "Overloaded" : "Error";
    socket->write("HTTP/1.1 " + QByteArray::number(status) + " " + reason + "\r\nContent-Type: " + type
                  + "\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\nConnection: close\r\n" + extra + "\r\n" + body);
    socket->disconnectFromHost();
}

QByteArray sse(const QJsonObject& event)
{
    return "event: " + event.value(QStringLiteral("type")).toString().toUtf8() + "\ndata: "
         + QJsonDocument(event).toJson(QJsonDocument::Compact) + "\n\n";
}

QJsonObject textDelta(const QString& text)
{
    return {{QStringLiteral("type"), QStringLiteral("content_block_delta")},
            {QStringLiteral("index"), 0},
            {QStringLiteral("delta"), QJsonObject{{QStringLiteral("type"), QStringLiteral("text_delta")}, {QStringLiteral("text"), text}}}};
}

// A 16-bit mono WAV of a soft tone.
QByteArray toneWav(double seconds, int rate = 44100)
{
    const int frames = int(seconds * rate);
    QByteArray data;
    data.reserve(frames * 2);
    for (int i = 0; i < frames; ++i) {
        const auto v = qint16(3000 * std::sin(2.0 * std::numbers::pi * 220.0 * i / rate));
        data.append(char(v & 0xff)).append(char((v >> 8) & 0xff));
    }
    const auto le32 = [](quint32 v) {
        const char bytes[4] = {char(v), char(v >> 8), char(v >> 16), char(v >> 24)};
        return QByteArray(bytes, 4);
    };
    const auto le16 = [](quint16 v) {
        const char bytes[2] = {char(v), char(v >> 8)};
        return QByteArray(bytes, 2);
    };
    return "RIFF" + le32(36 + quint32(data.size())) + "WAVEfmt " + le32(16) + le16(1) + le16(1) + le32(quint32(rate))
         + le32(quint32(rate) * 2) + le16(2) + le16(16) + "data" + le32(quint32(data.size())) + data;
}

} // namespace

MockCloudServer* MockCloudServer::instance()
{
    static MockCloudServer* server = new MockCloudServer(qApp);
    return server;
}

MockCloudServer::MockCloudServer(QObject* parent) : QObject(parent), m_server(new QTcpServer(this))
{
    m_server->listen(QHostAddress::LocalHost, 0);
    connect(m_server, &QTcpServer::newConnection, this, [this] {
        while (QTcpSocket* socket = m_server->nextPendingConnection()) {
            connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            auto* buffer = new QByteArray;
            connect(socket, &QObject::destroyed, [buffer] { delete buffer; });
            connect(socket, &QTcpSocket::readyRead, this, [this, socket, buffer] {
                *buffer += socket->readAll();
                const qsizetype headerEnd = buffer->indexOf("\r\n\r\n");
                if (headerEnd < 0)
                    return;
                const QList<QByteArray> lines = buffer->left(headerEnd).split('\n');
                Request request;
                const QList<QByteArray> first = lines.value(0).trimmed().split(' ');
                request.method = QString::fromLatin1(first.value(0));
                request.path = QString::fromLatin1(first.value(1));
                for (qsizetype i = 1; i < lines.size(); ++i) {
                    const qsizetype colon = lines[i].indexOf(':');
                    if (colon > 0)
                        request.headers.insert(QString::fromLatin1(lines[i].left(colon).trimmed().toLower()),
                                               QString::fromUtf8(lines[i].mid(colon + 1).trimmed()));
                }
                const qsizetype length = request.headers.value(QStringLiteral("content-length")).toLongLong();
                if (buffer->size() - (headerEnd + 4) < length)
                    return; // more body to come
                request.body = QJsonDocument::fromJson(buffer->mid(headerEnd + 4, length)).object();
                buffer->clear();
                m_requests.push_back(request);
                serve(socket, request);
            });
        }
    });
}

QString MockCloudServer::baseUrl() const
{
    return QStringLiteral("http://127.0.0.1:%1").arg(m_server->serverPort());
}

void MockCloudServer::serve(QTcpSocket* socket, const Request& request)
{
    if (request.path.startsWith(QLatin1String("/v1/messages")))
        claude(socket, request);
    else if (request.path.startsWith(QLatin1String("/v1/voices")) || request.path.startsWith(QLatin1String("/v1/text-to-speech/")))
        elevenLabs(socket, request);
    else
        respond(socket, 404, "application/json", R"({"error":{"type":"not_found_error","message":"Not found"}})");
}

void MockCloudServer::claude(QTcpSocket* socket, const Request& request)
{
    const auto error = [&](int status, const char* type, const char* message, const QByteArray& extra = {}) {
        respond(socket, status, "application/json",
                QJsonDocument(QJsonObject{{QStringLiteral("type"), QStringLiteral("error")},
                                          {QStringLiteral("error"), QJsonObject{{QStringLiteral("type"), QLatin1String(type)},
                                                                                {QStringLiteral("message"), QLatin1String(message)}}}})
                    .toJson(QJsonDocument::Compact),
                extra);
    };
    const QString key = request.headers.value(QStringLiteral("x-api-key"));
    if (key.isEmpty() || key == QLatin1String("bad-key")) {
        error(401, "authentication_error", "invalid x-api-key");
        return;
    }
    if (request.headers.value(QStringLiteral("anthropic-version")).isEmpty()) {
        error(400, "invalid_request_error", "anthropic-version header is required");
        return;
    }
    const QString model = request.body.value(QStringLiteral("model")).toString();
    const QString prompt = request.body.value(QStringLiteral("messages")).toArray().first().toObject()
                               .value(QStringLiteral("content")).toString();
    if (prompt.contains(QLatin1String("#overload")) && m_overloads[prompt]++ == 0) {
        error(529, "overloaded_error", "Overloaded", "retry-after: 0\r\n");
        return;
    }

    // What to say.
    QString answer;
    const bool structured = request.body.value(QStringLiteral("output_config")).toObject().contains(QStringLiteral("format"));
    if (structured) {
        const QString language = QRegularExpression(QStringLiteral("Target language: ([^\\n]+)")).match(prompt).captured(1).trimmed();
        const QRegularExpressionMatch captions =
            QRegularExpression(QStringLiteral("<captions>(.*)</captions>"), QRegularExpression::DotMatchesEverythingOption).match(prompt);
        QJsonArray translated;
        for (const QJsonValue& value : QJsonDocument::fromJson(captions.captured(1).toUtf8()).array()) {
            const QJsonObject caption = value.toObject();
            translated.append(QJsonObject{{QStringLiteral("index"), caption.value(QStringLiteral("index"))},
                                          {QStringLiteral("text"), QStringLiteral("[%1] %2").arg(language, caption.value(QStringLiteral("text")).toString())}});
        }
        answer = QString::fromUtf8(QJsonDocument(QJsonObject{{QStringLiteral("captions"), translated}}).toJson(QJsonDocument::Compact));
    } else if (prompt.contains(QLatin1String("five titles"))) {
        answer = QStringLiteral("A Day by the Sea\nWaves, Wind and Wonder\nWhat the Tide Brought In\nSalt Air Stories\nThe Long Walk Home");
    } else {
        answer = QStringLiteral("This is a mock answer written from the transcript, in a few sentences. "
                                "It stands in for Claude so the feature can be tried without a key.");
    }
    const bool refuse = prompt.contains(QLatin1String("#refuse"));
    const bool fallback = prompt.contains(QLatin1String("#fallback"));
    const bool slow = prompt.contains(QLatin1String("#slow"));

    QList<QByteArray> events;
    events << sse({{QStringLiteral("type"), QStringLiteral("message_start")},
                   {QStringLiteral("message"), QJsonObject{{QStringLiteral("id"), QStringLiteral("msg_mock")},
                                                           {QStringLiteral("type"), QStringLiteral("message")},
                                                           {QStringLiteral("role"), QStringLiteral("assistant")},
                                                           {QStringLiteral("model"), model},
                                                           {QStringLiteral("content"), QJsonArray{}},
                                                           {QStringLiteral("usage"), QJsonObject{{QStringLiteral("input_tokens"), (prompt.size() + 3) / 4},
                                                                                                 {QStringLiteral("output_tokens"), 1}}}}}});
    events << sse({{QStringLiteral("type"), QStringLiteral("content_block_start")},
                   {QStringLiteral("index"), 0},
                   {QStringLiteral("content_block"), QJsonObject{{QStringLiteral("type"), QStringLiteral("text")}, {QStringLiteral("text"), QString()}}}});
    if (fallback) {
        events << sse(textDelta(QStringLiteral("Declined part ")));
        events << sse({{QStringLiteral("type"), QStringLiteral("content_block_start")},
                       {QStringLiteral("index"), 1},
                       {QStringLiteral("content_block"), QJsonObject{{QStringLiteral("type"), QStringLiteral("fallback")}}}});
    }
    if (!refuse) {
        const int pieces = slow ? 40 : 4;
        const qsizetype step = std::max<qsizetype>(1, (answer.size() + pieces - 1) / pieces);
        for (qsizetype at = 0; at < answer.size(); at += step)
            events << sse(textDelta(answer.mid(at, step)));
    }
    events << sse({{QStringLiteral("type"), QStringLiteral("content_block_stop")}, {QStringLiteral("index"), 0}});
    events << sse({{QStringLiteral("type"), QStringLiteral("message_delta")},
                   {QStringLiteral("delta"), QJsonObject{{QStringLiteral("stop_reason"), refuse ? QStringLiteral("refusal") : QStringLiteral("end_turn")}}},
                   {QStringLiteral("usage"), QJsonObject{{QStringLiteral("output_tokens"), refuse ? 0 : (answer.size() + 3) / 4}}}});
    events << sse({{QStringLiteral("type"), QStringLiteral("message_stop")}});

    socket->write("HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\nCache-Control: no-cache\r\nConnection: close\r\n\r\n");
    // Each event in turn (every 150 ms when slow).
    auto* timer = new QTimer(socket);
    timer->setInterval(slow ? 150 : 1);
    auto remaining = std::make_shared<QList<QByteArray>>(events);
    QPointer<QTcpSocket> guarded(socket);
    connect(timer, &QTimer::timeout, socket, [guarded, timer, remaining] {
        if (!guarded)
            return;
        if (remaining->isEmpty()) {
            timer->stop();
            guarded->disconnectFromHost();
            return;
        }
        guarded->write(remaining->takeFirst());
    });
    timer->start();
}

void MockCloudServer::elevenLabs(QTcpSocket* socket, const Request& request)
{
    const QString key = request.headers.value(QStringLiteral("xi-api-key"));
    if (key.isEmpty() || key == QLatin1String("bad-key")) {
        respond(socket, 401, "application/json", R"({"detail":{"status":"invalid_api_key","message":"Invalid API key"}})");
        return;
    }
    if (request.method == QLatin1String("GET")) {
        const QJsonArray voices{
            QJsonObject{{QStringLiteral("voice_id"), QStringLiteral("mockvoice1")}, {QStringLiteral("name"), QStringLiteral("Avery")},
                        {QStringLiteral("labels"), QJsonObject{{QStringLiteral("gender"), QStringLiteral("female")},
                                                               {QStringLiteral("accent"), QStringLiteral("american")}}}},
            QJsonObject{{QStringLiteral("voice_id"), QStringLiteral("mockvoice2")}, {QStringLiteral("name"), QStringLiteral("Rowan")},
                        {QStringLiteral("labels"), QJsonObject{{QStringLiteral("gender"), QStringLiteral("male")},
                                                               {QStringLiteral("accent"), QStringLiteral("british")}}}}};
        respond(socket, 200, "application/json", QJsonDocument(QJsonObject{{QStringLiteral("voices"), voices}}).toJson(QJsonDocument::Compact));
        return;
    }
    // Speech: 60 ms a character.
    const QString text = request.body.value(QStringLiteral("text")).toString();
    constexpr double kPerCharacter = 0.06;
    QJsonArray characters, starts, ends;
    for (qsizetype i = 0; i < text.size(); ++i) {
        characters.append(QString(text[i]));
        starts.append(double(i) * kPerCharacter);
        ends.append(double(i + 1) * kPerCharacter);
    }
    const QJsonObject alignment{{QStringLiteral("characters"), characters},
                                {QStringLiteral("character_start_times_seconds"), starts},
                                {QStringLiteral("character_end_times_seconds"), ends}};
    const QJsonObject answer{{QStringLiteral("audio_base64"), QString::fromLatin1(toneWav(double(text.size()) * kPerCharacter + 0.2).toBase64())},
                             {QStringLiteral("alignment"), alignment},
                             {QStringLiteral("normalized_alignment"), alignment}};
    respond(socket, 200, "application/json", QJsonDocument(answer).toJson(QJsonDocument::Compact));
}
