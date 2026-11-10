#include "cloud/MockCloudServer.h"

#include "encode/MediaWriter.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QPainter>
#include <QTemporaryDir>
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

// A few seconds of H.264 whose colour drifts, as MP4 bytes.
QByteArray movingColourMp4(double seconds)
{
    QTemporaryDir dir;
    const QString path = dir.filePath(QStringLiteral("mock.mp4"));
    tve::MediaSettings settings;
    settings.container = tve::Container::Mp4;
    settings.width = 320;
    settings.height = 180;
    settings.audio = false;
    auto writer = tve::MediaWriter::open(path.toStdString(), settings);
    if (!writer)
        return {};
    std::vector<std::uint8_t> luma(320 * 180), chroma(320 * 90);
    const int frames = int(seconds * 30);
    for (int f = 0; f < frames; ++f) {
        for (int y = 0; y < 180; ++y)
            for (int x = 0; x < 320; ++x)
                luma[std::size_t(y * 320 + x)] = std::uint8_t(60 + (x + f * 4) % 140);
        for (std::size_t i = 0; i < chroma.size(); i += 2) {
            chroma[i] = std::uint8_t(100 + f % 60);
            chroma[i + 1] = std::uint8_t(160 - f % 60);
        }
        writer->writeVideo(luma.data(), 320, chroma.data(), 320);
    }
    if (!writer->finish())
        return {};
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
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
    else if (request.path.startsWith(QLatin1String("/v1/images/")))
        openAiImages(socket, request);
    else if (request.path.startsWith(QLatin1String("/v1beta/interactions")))
        geminiVideo(socket, request);
    else if (request.path.startsWith(QLatin1String("/v1/music")) || request.path.startsWith(QLatin1String("/v1/sound-generation")))
        elevenLabsSound(socket, request);
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
    if (request.body.contains(QStringLiteral("tools"))) {
        copilot(socket, request);
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
    if (structured && prompt.contains(QLatin1String("<moments>"))) {
        // Ranked: the other way round, titled by number.
        const QRegularExpressionMatch moments =
            QRegularExpression(QStringLiteral("<moments>(.*)</moments>"), QRegularExpression::DotMatchesEverythingOption).match(prompt);
        const QJsonArray given = QJsonDocument::fromJson(moments.captured(1).toUtf8()).array();
        QJsonArray ranked;
        for (qsizetype i = given.size() - 1; i >= 0; --i) {
            const int index = given[i].toObject().value(QStringLiteral("index")).toInt();
            ranked.append(QJsonObject{{QStringLiteral("index"), index}, {QStringLiteral("title"), QStringLiteral("Moment %1").arg(index + 1)}});
        }
        answer = QString::fromUtf8(QJsonDocument(QJsonObject{{QStringLiteral("moments"), ranked}}).toJson(QJsonDocument::Compact));
    } else if (structured) {
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

void MockCloudServer::copilot(QTcpSocket* socket, const Request& request)
{
    const QJsonArray messages = request.body.value(QStringLiteral("messages")).toArray();
    // The latest request in words, and how many answers have followed it.
    QString asked;
    int answers = 0;
    QJsonArray lastResults;
    for (const QJsonValue& value : messages) {
        const QJsonObject m = value.toObject();
        const QJsonValue content = m.value(QStringLiteral("content"));
        if (m.value(QStringLiteral("role")) == QLatin1String("assistant")) {
            ++answers;
            continue;
        }
        QString text;
        if (content.isString()) {
            text = content.toString();
        } else {
            lastResults = {};
            for (const QJsonValue& block : content.toArray()) {
                const QJsonObject b = block.toObject();
                if (b.value(QStringLiteral("type")) == QLatin1String("text"))
                    text += b.value(QStringLiteral("text")).toString();
                else if (b.value(QStringLiteral("type")) == QLatin1String("tool_result"))
                    lastResults.append(b);
            }
        }
        if (!text.isEmpty()) {
            asked = text;
            answers = 0;
            lastResults = {};
        }
    }
    const auto planAfter = [&](const QString& marker) {
        const qsizetype at = asked.indexOf(marker);
        if (at < 0)
            return QJsonArray();
        const QString rest = asked.mid(at + marker.size());
        // The JSON array up to its closing bracket.
        int depth = 0;
        for (qsizetype i = 0; i < rest.size(); ++i) {
            if (rest[i] == QLatin1Char('['))
                ++depth;
            else if (rest[i] == QLatin1Char(']') && --depth == 0)
                return QJsonDocument::fromJson(rest.left(i + 1).toUtf8()).array();
        }
        return QJsonArray();
    };
    QJsonArray calls = answers == 0 ? planAfter(QStringLiteral("#plan ")) : answers == 1 ? planAfter(QStringLiteral("#then ")) : QJsonArray();
    // (Or a plan from TVE_MOCK_COPILOT_PLAN, for requests in plain words: demos and pictures.)
    if (calls.isEmpty() && answers == 0 && !asked.contains(QLatin1String("#")))
        calls = QJsonDocument::fromJson(qgetenv("TVE_MOCK_COPILOT_PLAN")).array();
    QString text;
    if (!calls.isEmpty()) {
        text = QStringLiteral("I'll make those changes.");
    } else if (answers == 0) {
        text = QStringLiteral("What would you like me to change? I need a little more to go on.");
    } else {
        QStringList failures;
        for (const QJsonValue& r : lastResults)
            if (r.toObject().value(QStringLiteral("is_error")).toBool())
                failures << r.toObject().value(QStringLiteral("content")).toString();
        text = failures.isEmpty() ? QStringLiteral("Done: the changes are ready for you to look over.")
                                  : QStringLiteral("One change didn't work: %1").arg(failures.join(QStringLiteral("; ")));
    }
    const bool refuse = asked.contains(QLatin1String("#refuse"));
    const bool invalid = asked.contains(QLatin1String("#invalid")) && answers == 0;

    QList<QByteArray> events;
    events << sse({{QStringLiteral("type"), QStringLiteral("message_start")},
                   {QStringLiteral("message"), QJsonObject{{QStringLiteral("id"), QStringLiteral("msg_mock_copilot")},
                                                           {QStringLiteral("type"), QStringLiteral("message")},
                                                           {QStringLiteral("role"), QStringLiteral("assistant")},
                                                           {QStringLiteral("model"), request.body.value(QStringLiteral("model"))},
                                                           {QStringLiteral("content"), QJsonArray{}},
                                                           {QStringLiteral("usage"), QJsonObject{{QStringLiteral("input_tokens"), 100},
                                                                                                 {QStringLiteral("cache_read_input_tokens"), answers * 50},
                                                                                                 {QStringLiteral("output_tokens"), 1}}}}}});
    int index = 0;
    const auto block = [&](const QJsonObject& start, const QList<QJsonObject>& deltas) {
        events << sse({{QStringLiteral("type"), QStringLiteral("content_block_start")}, {QStringLiteral("index"), index},
                       {QStringLiteral("content_block"), start}});
        for (const QJsonObject& d : deltas)
            events << sse({{QStringLiteral("type"), QStringLiteral("content_block_delta")}, {QStringLiteral("index"), index},
                           {QStringLiteral("delta"), d}});
        events << sse({{QStringLiteral("type"), QStringLiteral("content_block_stop")}, {QStringLiteral("index"), index}});
        ++index;
    };
    QString stop = QStringLiteral("end_turn");
    if (refuse) {
        stop = QStringLiteral("refusal");
    } else {
        // Thinking (empty, as Opus 5.5 returns it by default), signed.
        block({{QStringLiteral("type"), QStringLiteral("thinking")}, {QStringLiteral("thinking"), QString()}},
              {QJsonObject{{QStringLiteral("type"), QStringLiteral("signature_delta")},
                           {QStringLiteral("signature"), QStringLiteral("mock-signature-%1").arg(answers)}}});
        block({{QStringLiteral("type"), QStringLiteral("text")}, {QStringLiteral("text"), QString()}},
              {QJsonObject{{QStringLiteral("type"), QStringLiteral("text_delta")}, {QStringLiteral("text"), text.left(8)}},
               QJsonObject{{QStringLiteral("type"), QStringLiteral("text_delta")}, {QStringLiteral("text"), text.mid(8)}}});
        int n = 0;
        for (const QJsonValue& value : calls) {
            const QJsonObject call = value.toObject();
            const QString input = QString::fromUtf8(QJsonDocument(call.value(QStringLiteral("input")).toObject()).toJson(QJsonDocument::Compact));
            const qsizetype half = input.size() / 2;
            const auto json = [](const QString& part) {
                return QJsonObject{{QStringLiteral("type"), QStringLiteral("input_json_delta")}, {QStringLiteral("partial_json"), part}};
            };
            block({{QStringLiteral("type"), QStringLiteral("tool_use")},
                   {QStringLiteral("id"), QStringLiteral("toolu_mock_%1_%2").arg(answers).arg(n++)},
                   {QStringLiteral("name"), call.value(QStringLiteral("name"))},
                   {QStringLiteral("input"), QJsonObject{}}},
                  {json(input.left(half)), json(input.mid(half))});
            stop = QStringLiteral("tool_use");
        }
        if (invalid) {
            block({{QStringLiteral("type"), QStringLiteral("tool_use")}, {QStringLiteral("id"), QStringLiteral("toolu_mock_bad")},
                   {QStringLiteral("name"), QStringLiteral("split_clip")}, {QStringLiteral("input"), QJsonObject{}}},
                  {QJsonObject{{QStringLiteral("type"), QStringLiteral("input_json_delta")}, {QStringLiteral("partial_json"), QStringLiteral("{\"clip_id\": 2, \"at\": ")}}});
            stop = QStringLiteral("tool_use");
        }
    }
    events << sse({{QStringLiteral("type"), QStringLiteral("message_delta")},
                   {QStringLiteral("delta"), QJsonObject{{QStringLiteral("stop_reason"), stop}}},
                   {QStringLiteral("usage"), QJsonObject{{QStringLiteral("output_tokens"), 40}}}});
    events << sse({{QStringLiteral("type"), QStringLiteral("message_stop")}});

    socket->write("HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\nCache-Control: no-cache\r\nConnection: close\r\n\r\n");
    for (const QByteArray& e : events)
        socket->write(e);
    socket->disconnectFromHost();
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

void MockCloudServer::openAiImages(QTcpSocket* socket, const Request& request)
{
    const auto error = [&](int status, const char* message) {
        respond(socket, status, "application/json",
                QJsonDocument(QJsonObject{{QStringLiteral("error"), QJsonObject{{QStringLiteral("message"), QLatin1String(message)},
                                                                                {QStringLiteral("type"), QStringLiteral("invalid_request_error")}}}})
                    .toJson(QJsonDocument::Compact));
    };
    const QString auth = request.headers.value(QStringLiteral("authorization"));
    if (!auth.startsWith(QLatin1String("Bearer ")) || auth == QLatin1String("Bearer bad-key")) {
        error(401, "Incorrect API key provided.");
        return;
    }
    const QString prompt = request.body.value(QStringLiteral("prompt")).toString();
    if (prompt.contains(QLatin1String("#blocked"))) {
        error(400, "Your request was rejected by the safety system.");
        return;
    }
    const QStringList size = request.body.value(QStringLiteral("size")).toString().split(QLatin1Char('x'));
    const int width = std::clamp(size.value(0).toInt(), 64, 1024);
    const int height = std::clamp(size.value(1).toInt(), 64, 1024);
    const bool transparent = request.body.value(QStringLiteral("background")).toString() == QLatin1String("transparent")
                          && !prompt.contains(QLatin1String("#opaque"));
    QImage picture(width, height, QImage::Format_ARGB32_Premultiplied);
    picture.fill(transparent ? Qt::transparent : QColor(245, 245, 240));
    {
        QPainter painter(&picture);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor::fromHsv(int(qHash(prompt) % 360), 200, 230));
        painter.drawEllipse(QRectF(width * 0.2, height * 0.2, width * 0.6, height * 0.6));
    }
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    picture.save(&buffer, "PNG");
    const QJsonObject answer{{QStringLiteral("created"), 1},
                             {QStringLiteral("data"), QJsonArray{QJsonObject{{QStringLiteral("b64_json"), QString::fromLatin1(png.toBase64())}}}},
                             {QStringLiteral("usage"), QJsonObject{{QStringLiteral("input_tokens"), 50},
                                                                   {QStringLiteral("output_tokens"), 1000},
                                                                   {QStringLiteral("total_tokens"), 1050}}}};
    respond(socket, 200, "application/json", QJsonDocument(answer).toJson(QJsonDocument::Compact));
}

void MockCloudServer::geminiVideo(QTcpSocket* socket, const Request& request)
{
    const auto error = [&](int status, const char* state, const char* message) {
        respond(socket, status, "application/json",
                QJsonDocument(QJsonObject{{QStringLiteral("error"), QJsonObject{{QStringLiteral("code"), status},
                                                                                {QStringLiteral("message"), QLatin1String(message)},
                                                                                {QStringLiteral("status"), QLatin1String(state)}}}})
                    .toJson(QJsonDocument::Compact));
    };
    const QString key = request.headers.value(QStringLiteral("x-goog-api-key"));
    if (key.isEmpty() || key == QLatin1String("bad-key")) {
        error(403, "PERMISSION_DENIED", "API key not valid. Please pass a valid API key.");
        return;
    }
    const QString previous = request.body.value(QStringLiteral("previous_interaction_id")).toString();
    if (!previous.isEmpty() && !m_interactions.contains(previous)) {
        error(404, "NOT_FOUND", "Interaction not found.");
        return;
    }
    const QJsonValue input = request.body.value(QStringLiteral("input"));
    const QString text = input.isString() ? input.toString()
                                          : input.toArray().last().toObject().value(QStringLiteral("text")).toString();
    const QString id = QStringLiteral("v1_mock_%1").arg(m_interactions.size() + 1);
    m_interactions << id;
    QJsonArray output;
    if (!text.contains(QLatin1String("#blocked")))
        output.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("video")},
                                  {QStringLiteral("mime_type"), QStringLiteral("video/mp4")},
                                  {QStringLiteral("data"), QString::fromLatin1(movingColourMp4(previous.isEmpty() ? 3.0 : 2.0).toBase64())}});
    const QJsonObject answer{{QStringLiteral("id"), id},
                             {QStringLiteral("object"), QStringLiteral("interaction")},
                             {QStringLiteral("status"), QStringLiteral("completed")},
                             {QStringLiteral("model"), request.body.value(QStringLiteral("model"))},
                             {QStringLiteral("steps"), QJsonArray{QJsonObject{{QStringLiteral("type"), QStringLiteral("model_output")},
                                                                              {QStringLiteral("content"), output}}}}};
    respond(socket, 200, "application/json", QJsonDocument(answer).toJson(QJsonDocument::Compact));
}

void MockCloudServer::elevenLabsSound(QTcpSocket* socket, const Request& request)
{
    const QString key = request.headers.value(QStringLiteral("xi-api-key"));
    if (key.isEmpty() || key == QLatin1String("bad-key")) {
        respond(socket, 401, "application/json", R"({"detail":{"status":"invalid_api_key","message":"Invalid API key"}})");
        return;
    }
    const bool music = request.path.startsWith(QLatin1String("/v1/music"));
    const QString prompt = request.body.value(music ? QStringLiteral("prompt") : QStringLiteral("text")).toString();
    if (prompt.contains(QLatin1String("#blocked"))) {
        respond(socket, 400, "application/json", R"({"detail":{"status":"bad_prompt","message":"The prompt names an artist."}})");
        return;
    }
    const double seconds = music ? request.body.value(QStringLiteral("music_length_ms")).toDouble(10000) / 1000.0
                                 : request.body.value(QStringLiteral("duration_seconds")).toDouble(2.0);
    respond(socket, 200, "audio/wav", toneWav(seconds));
}
