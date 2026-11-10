#include "cloud/CloudText.h"

#include "cloud/CloudKeys.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

#include <cmath>
#include <optional>

namespace {

constexpr int kMostRetries = 3;
constexpr const char* kApiVersion = "2026-10-21";
// A declined request runs again on the model Anthropic recommends for that kind of decline.
constexpr const char* kFallbackBeta = "server-side-fallback-2026-07-01";

bool retryable(int status)
{
    return status == 408 || status == 409 || status == 429 || status == 500 || status == 502 || status == 503
        || status == 504 || status == 529;
}

// What an error response says, in words for the person.
QString describeError(int status, const QByteArray& body)
{
    const QJsonObject error = QJsonDocument::fromJson(body).object().value(QStringLiteral("error")).toObject();
    const QString type = error.value(QStringLiteral("type")).toString();
    const QString message = error.value(QStringLiteral("message")).toString();
    if (status == 401 || type == QLatin1String("authentication_error"))
        return QStringLiteral("The Claude API key was not accepted. Check it in AI settings.");
    if (status == 402 || type == QLatin1String("billing_error"))
        return QStringLiteral("Claude API billing needs attention in your Anthropic Console.");
    if (status == 403 || type == QLatin1String("permission_error"))
        return QStringLiteral("This Claude API key is not allowed to do that.");
    if (status == 429 || type == QLatin1String("rate_limit_error"))
        return QStringLiteral("Claude's rate limit was reached; try again shortly.");
    if (status == 529 || type == QLatin1String("overloaded_error"))
        return QStringLiteral("Claude is busy right now; try again shortly.");
    if (status == 413)
        return QStringLiteral("That is too much text to send at once.");
    if (!message.isEmpty())
        return QStringLiteral("Claude API: %1").arg(message);
    return status > 0 ? QStringLiteral("Claude API error %1").arg(status) : QStringLiteral("Could not reach the Claude API.");
}

class ClaudeTask final : public AgentTask {
public:
    ClaudeTask(QNetworkAccessManager* network, TextRequest request, QObject* parent)
        : AgentTask(parent), m_network(network), m_request(std::move(request))
    {
        m_retryTimer.setSingleShot(true);
        connect(&m_retryTimer, &QTimer::timeout, this, [this] { start(); });
    }
    ClaudeTask(QNetworkAccessManager* network, AgentRequest agent, QObject* parent)
        : AgentTask(parent), m_network(network), m_agentRequest(std::move(agent))
    {
        m_request.model = m_agentRequest->model;
        m_retryTimer.setSingleShot(true);
        connect(&m_retryTimer, &QTimer::timeout, this, [this] { start(); });
    }

    void start()
    {
        if (m_done)
            return;
        const QString key = cloudkeys::read(QStringLiteral("anthropic"));
        if (key.isEmpty()) {
            fail(QStringLiteral("Add your Claude API key in AI settings first."));
            return;
        }
        m_buffer.clear();
        m_text.clear();
        m_body.clear();
        m_streamed = false;
        m_blocks.clear();
        m_partialJson.clear();
        m_calls.clear();
        m_lastFallback = -1;

        QNetworkRequest http(QUrl(ClaudeProvider::baseUrl() + QStringLiteral("/v1/messages")));
        http.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
        http.setRawHeader("x-api-key", key.toUtf8());
        http.setRawHeader("anthropic-version", kApiVersion);
        http.setRawHeader("anthropic-beta", kFallbackBeta);
        http.setRawHeader("accept", "text/event-stream");
        http.setTransferTimeout(180000); // silence this long means the connection is gone

        const QJsonObject body = m_agentRequest ? agentBody() : textBody();
        m_reply = m_network->post(http, QJsonDocument(body).toJson(QJsonDocument::Compact));
        connect(m_reply, &QNetworkReply::readyRead, this, [this] { read(); });
        connect(m_reply, &QNetworkReply::finished, this, [this] { ended(); });
    }

    QJsonObject textBody() const
    {
        QJsonObject body{{QStringLiteral("model"), m_request.model},
                         {QStringLiteral("max_tokens"), m_request.maxTokens},
                         {QStringLiteral("stream"), true},
                         {QStringLiteral("fallbacks"), QStringLiteral("default")},
                         {QStringLiteral("messages"),
                          QJsonArray{QJsonObject{{QStringLiteral("role"), QStringLiteral("user")},
                                                 {QStringLiteral("content"), m_request.prompt}}}}};
        if (!m_request.system.isEmpty())
            body.insert(QStringLiteral("system"), m_request.system);
        QJsonObject output{{QStringLiteral("effort"), m_request.effort}};
        if (!m_request.schema.isEmpty())
            output.insert(QStringLiteral("format"), QJsonObject{{QStringLiteral("type"), QStringLiteral("json_schema")},
                                                                {QStringLiteral("schema"), m_request.schema}});
        body.insert(QStringLiteral("output_config"), output);
        return body;
    }

    // A conversation's turn: its fixed system prompt and tools, the history, adaptive
    // thinking, and the cache following the conversation as it grows.
    QJsonObject agentBody() const
    {
        const AgentRequest& r = *m_agentRequest;
        QJsonArray tools;
        for (const QJsonValue& tool : r.tools) {
            QJsonObject t = tool.toObject();
            t.insert(QStringLiteral("eager_input_streaming"), true); // inputs stream as written; checked here
            tools.append(t);
        }
        return {{QStringLiteral("model"), r.model},
                {QStringLiteral("max_tokens"), r.maxTokens},
                {QStringLiteral("stream"), true},
                {QStringLiteral("fallbacks"), QStringLiteral("default")},
                {QStringLiteral("system"), r.system},
                {QStringLiteral("tools"), tools},
                {QStringLiteral("messages"), r.messages},
                {QStringLiteral("thinking"), QJsonObject{{QStringLiteral("type"), QStringLiteral("adaptive")}}},
                {QStringLiteral("output_config"), QJsonObject{{QStringLiteral("effort"), r.effort}}},
                {QStringLiteral("cache_control"), QJsonObject{{QStringLiteral("type"), QStringLiteral("ephemeral")}}}};
    }

    void cancel() override
    {
        if (m_done)
            return;
        m_retryTimer.stop();
        m_cancelled = true;
        if (m_reply)
            m_reply->abort();
        else
            end();
    }

private:
    int status() const { return m_reply ? m_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() : 0; }

    void read()
    {
        const QByteArray data = m_reply->readAll();
        if (status() != 200) {
            m_body += data; // an error's JSON
            return;
        }
        m_buffer += data;
        // Server-sent events: blocks separated by a blank line, "data:" lines holding JSON.
        for (qsizetype end; (end = m_buffer.indexOf("\n\n")) >= 0;) {
            const QByteArray block = m_buffer.left(end);
            m_buffer.remove(0, end + 2);
            QByteArray json;
            for (const QByteArray& line : block.split('\n'))
                if (line.startsWith("data:"))
                    json += line.mid(5).trimmed();
            if (!json.isEmpty())
                event(QJsonDocument::fromJson(json).object());
        }
    }

    void event(const QJsonObject& e)
    {
        const QString type = e.value(QStringLiteral("type")).toString();
        if (type == QLatin1String("message_start")) {
            const QJsonObject message = e.value(QStringLiteral("message")).toObject();
            m_result.usage.model = message.value(QStringLiteral("model")).toString(m_request.model);
            const QJsonObject usage = message.value(QStringLiteral("usage")).toObject();
            m_result.usage.inputTokens = usage.value(QStringLiteral("input_tokens")).toInteger()
                                       + usage.value(QStringLiteral("cache_read_input_tokens")).toInteger()
                                       + usage.value(QStringLiteral("cache_creation_input_tokens")).toInteger();
        } else if (type == QLatin1String("content_block_start")) {
            const int index = e.value(QStringLiteral("index")).toInt();
            const QJsonObject block = e.value(QStringLiteral("content_block")).toObject();
            m_blocks[index] = block;
            const QString kind = block.value(QStringLiteral("type")).toString();
            if (kind == QLatin1String("tool_use"))
                m_partialJson[index].clear();
            // Another model took over after a decline: its answer replaces what came before.
            if (kind == QLatin1String("fallback")) {
                m_lastFallback = index;
                m_text.clear();
                emit textChanged(m_text);
            }
        } else if (type == QLatin1String("content_block_delta")) {
            const int index = e.value(QStringLiteral("index")).toInt();
            const QJsonObject delta = e.value(QStringLiteral("delta")).toObject();
            const QString kind = delta.value(QStringLiteral("type")).toString();
            QJsonObject& block = m_blocks[index];
            const auto append = [&](const char* field, const QString& more) {
                block.insert(QLatin1String(field), block.value(QLatin1String(field)).toString() + more);
            };
            if (kind == QLatin1String("text_delta")) {
                m_streamed = true;
                const QString more = delta.value(QStringLiteral("text")).toString();
                append("text", more);
                m_text += more;
                emit textChanged(m_text);
            } else if (kind == QLatin1String("thinking_delta")) {
                m_streamed = true;
                append("thinking", delta.value(QStringLiteral("thinking")).toString());
            } else if (kind == QLatin1String("signature_delta")) {
                block.insert(QStringLiteral("signature"), delta.value(QStringLiteral("signature")).toString());
            } else if (kind == QLatin1String("input_json_delta")) {
                m_streamed = true;
                m_partialJson[index] += delta.value(QStringLiteral("partial_json")).toString();
            }
        } else if (type == QLatin1String("content_block_stop")) {
            const int index = e.value(QStringLiteral("index")).toInt();
            if (m_partialJson.contains(index))
                readToolInput(index);
        } else if (type == QLatin1String("message_delta")) {
            m_result.stopReason = e.value(QStringLiteral("delta")).toObject().value(QStringLiteral("stop_reason")).toString();
            const QJsonObject usage = e.value(QStringLiteral("usage")).toObject();
            if (usage.contains(QStringLiteral("output_tokens")))
                m_result.usage.outputTokens = usage.value(QStringLiteral("output_tokens")).toInteger();
            if (usage.contains(QStringLiteral("input_tokens")))
                m_result.usage.inputTokens = usage.value(QStringLiteral("input_tokens")).toInteger()
                                           + usage.value(QStringLiteral("cache_read_input_tokens")).toInteger()
                                           + usage.value(QStringLiteral("cache_creation_input_tokens")).toInteger();
        } else if (type == QLatin1String("error")) {
            const QJsonObject error = e.value(QStringLiteral("error")).toObject();
            m_streamError = error.value(QStringLiteral("type")).toString();
            m_streamMessage = error.value(QStringLiteral("message")).toString();
        }
    }

    void ended()
    {
        QNetworkReply* reply = m_reply;
        m_reply = nullptr;
        reply->deleteLater();
        if (m_cancelled) {
            end();
            return;
        }
        const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const bool networkError = reply->error() != QNetworkReply::NoError && code == 0;
        const bool overloadedMidStream = m_streamError == QLatin1String("overloaded_error")
                                      || m_streamError == QLatin1String("api_error");
        // Retry only what failed before any text arrived (a retry would repeat it otherwise).
        if ((retryable(code) || networkError || (overloadedMidStream && !m_streamed)) && m_attempt < kMostRetries) {
            ++m_attempt;
            double wait = std::pow(2.0, m_attempt - 1); // 1, 2, 4 seconds
            const QByteArray after = reply->rawHeader("retry-after");
            if (!after.isEmpty())
                wait = std::max(0.0, after.toDouble());
            m_streamError.clear();
            emit retrying(m_attempt, wait);
            m_retryTimer.start(int(wait * 1000));
            return;
        }
        if (code != 200) {
            fail(networkError ? QStringLiteral("Could not reach the Claude API: %1").arg(reply->errorString())
                              : describeError(code, m_body));
            return;
        }
        if (!m_streamError.isEmpty()) {
            fail(describeError(m_streamError == QLatin1String("overloaded_error") ? 529 : 0,
                               QJsonDocument(QJsonObject{{QStringLiteral("error"),
                                                          QJsonObject{{QStringLiteral("type"), m_streamError},
                                                                      {QStringLiteral("message"), m_streamMessage}}}})
                                   .toJson()));
            return;
        }
        if (m_result.stopReason == QLatin1String("refusal")) {
            fail(QStringLiteral("Claude declined this request."));
            return;
        }
        if (m_result.stopReason == QLatin1String("max_tokens")) {
            fail(QStringLiteral("The answer was too long and was cut off."));
            return;
        }
        m_result.ok = true;
        end();
    }

    // A tool's streamed input, read strictly: a JSON object, or the call is marked invalid
    // (eager streaming means the server no longer checks it).
    void readToolInput(int index)
    {
        QJsonObject& block = m_blocks[index];
        const QString raw = m_partialJson.take(index);
        QJsonParseError error;
        const QJsonDocument doc = QJsonDocument::fromJson(raw.trimmed().isEmpty() ? QByteArray("{}") : raw.toUtf8(), &error);
        ToolCall call{block.value(QStringLiteral("id")).toString(), block.value(QStringLiteral("name")).toString(), {}, false, raw};
        if (error.error != QJsonParseError::NoError || !doc.isObject()) {
            call.invalid = true;
            block.insert(QStringLiteral("input"), QJsonObject{});
        } else {
            call.input = doc.object();
            block.insert(QStringLiteral("input"), call.input);
        }
        m_calls[index] = call;
    }

    // The assistant's content in order, as it goes back: after a fallback, the declined
    // part's thinking and tool calls are left out (its text stays).
    void collect()
    {
        static const QStringList beforeFallback{QStringLiteral("thinking"), QStringLiteral("redacted_thinking"),
                                                QStringLiteral("tool_use")};
        m_agent.content = {};
        m_agent.toolCalls.clear();
        for (auto it = m_blocks.cbegin(); it != m_blocks.cend(); ++it) {
            const QString kind = it.value().value(QStringLiteral("type")).toString();
            if (it.key() < m_lastFallback && beforeFallback.contains(kind))
                continue;
            m_agent.content.append(it.value());
            if (kind == QLatin1String("tool_use") && m_calls.contains(it.key()))
                m_agent.toolCalls.append(m_calls.value(it.key()));
        }
    }

    void fail(const QString& error)
    {
        m_result.ok = false;
        m_result.error = error;
        end();
    }

    void end()
    {
        m_result.cancelled = m_cancelled;
        m_result.text = m_text;
        collect();
        const auto [in, out] = ClaudeProvider::prices(m_result.usage.model.isEmpty() ? m_request.model : m_result.usage.model);
        m_result.usage.cost = (double(m_result.usage.inputTokens) * in + double(m_result.usage.outputTokens) * out) / 1e6;
        m_agent.turn = m_result;
        finish(m_result);
    }

    QNetworkAccessManager* m_network;
    TextRequest m_request;
    std::optional<AgentRequest> m_agentRequest;
    QMap<int, QJsonObject> m_blocks; // the content blocks by index, as they stream
    QMap<int, QString> m_partialJson; // tool inputs still streaming
    QMap<int, ToolCall> m_calls;
    int m_lastFallback = -1;
    QPointer<QNetworkReply> m_reply;
    QTimer m_retryTimer;
    QByteArray m_buffer;
    QByteArray m_body;
    QString m_text;
    QString m_streamError;
    QString m_streamMessage;
    bool m_streamed = false;
    bool m_cancelled = false;
    int m_attempt = 0;
};

} // namespace

void TextTask::finish(TextResult result)
{
    if (m_done)
        return;
    m_done = true;
    m_result = std::move(result);
    emit finished(m_result);
}

QString ClaudeProvider::baseUrl()
{
    const QString override = qEnvironmentVariable("TVE_ANTHROPIC_BASE_URL");
    return override.isEmpty() ? QStringLiteral("https://api.anthropic.com") : override;
}

std::pair<double, double> ClaudeProvider::prices(const QString& model)
{
    if (model.startsWith(QLatin1String("claude-opus-5-5")))
        return {4.0, 20.0};
    if (model.startsWith(QLatin1String("claude-sonnet-5")))
        return {2.0, 10.0};
    if (model.startsWith(QLatin1String("claude-haiku-5-5")))
        return {0.10, 0.50};
    // Claude Opus 5 and 4.8 (fallbacks)
    return {5.0, 25.0};
}

AgentTask* ClaudeProvider::converse(const AgentRequest& request, QObject* parent)
{
    auto* task = new ClaudeTask(m_network, request, parent);
    QMetaObject::invokeMethod(task, [task] { task->start(); }, Qt::QueuedConnection);
    return task;
}

TextTask* ClaudeProvider::send(const TextRequest& request, QObject* parent)
{
    auto* task = new ClaudeTask(m_network, request, parent);
    // Started once the caller has connected to it.
    QMetaObject::invokeMethod(task, [task] { task->start(); }, Qt::QueuedConnection);
    return task;
}
