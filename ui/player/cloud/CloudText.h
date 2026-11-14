#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>

class QNetworkAccessManager;
class QNetworkReply;

// Cloud text generation behind one interface: a provider takes a request and returns a task
// that streams text, can be cancelled, retries what is worth retrying, and ends with the
// text, the stop reason and what it used (tokens and an estimated cost).

struct TextRequest {
    QString system;
    QString prompt;
    QString model;              // e.g. "claude-opus-5-5"
    QString effort = QStringLiteral("medium");
    int maxTokens = 64000;
    QJsonObject schema;         // when set, the reply is JSON matching it
    // Pictures sent before the prompt (encoded files: "image/jpeg", "image/png"…).
    struct Image {
        QString mediaType;
        QByteArray data;
    };
    QList<Image> images;
};

struct TextUsage {
    qint64 inputTokens = 0;
    qint64 outputTokens = 0;
    double cost = 0;            // US dollars, estimated from list prices
    QString model;              // the model that answered (a fallback may have)
};

struct TextResult {
    bool ok = false;
    bool cancelled = false;
    QString text;
    QString error;
    QString stopReason;
    TextUsage usage;
};

class TextTask : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    virtual void cancel() = 0;
    bool done() const { return m_done; }
    const TextResult& result() const { return m_result; }

signals:
    // More text arrived (the text so far).
    void textChanged(const QString& text);
    // Waiting before trying again (attempt, seconds).
    void retrying(int attempt, double seconds);
    void finished(const TextResult& result);

protected:
    void finish(TextResult result);
    bool m_done = false;
    TextResult m_result;
};

// A conversation with tools (the editing copilot): the whole history each turn, appended to,
// never edited (Claude's thinking blocks stay valid only in the conversation that made
// them). `system` and `tools` stay the same for the conversation.
struct AgentRequest {
    QJsonArray system;   // text blocks
    QJsonArray tools;    // client tools (sent with eager input streaming)
    QJsonArray messages;
    QString model;
    QString effort = QStringLiteral("high");
    int maxTokens = 64000;
};

// A tool Claude asked for, its input read strictly from the stream: `invalid` when it
// isn't a JSON object (`raw` holds what came).
struct ToolCall {
    QString id;
    QString name;
    QJsonObject input;
    bool invalid = false;
    QString raw;
};

struct AgentResult {
    TextResult turn;         // its text (after any fallback), stop reason and usage
    QJsonArray content;      // the assistant's content, as it goes back into the history
    QList<ToolCall> toolCalls;
};

class AgentTask : public TextTask {
    Q_OBJECT
public:
    using TextTask::TextTask;
    const AgentResult& agentResult() const { return m_agent; }

protected:
    AgentResult m_agent;
};

class TextProvider {
public:
    virtual ~TextProvider() = default;
    // The task belongs to `parent`; it reports finished() once.
    virtual TextTask* send(const TextRequest& request, QObject* parent) = 0;
};

// The Claude API (Messages, streamed). The key comes from cloudkeys ("anthropic");
// TVE_ANTHROPIC_BASE_URL points it elsewhere (the mock server).
class ClaudeProvider final : public TextProvider {
public:
    explicit ClaudeProvider(QNetworkAccessManager* network) : m_network(network) {}
    TextTask* send(const TextRequest& request, QObject* parent) override;
    // One turn of a conversation with tools: what Claude says and the tools it asks for.
    AgentTask* converse(const AgentRequest& request, QObject* parent);

    // List prices per million tokens (input, output) of the models we use or fall back to.
    static std::pair<double, double> prices(const QString& model);
    static QString baseUrl();

private:
    QNetworkAccessManager* m_network;
};
