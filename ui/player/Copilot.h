#pragma once

#include "cloud/CloudText.h"
#include "model/Project.h"

#include <QJsonArray>
#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <optional>

class ProjectController;

// The editing copilot: Claude (the person's key, 16a) asked in words, editing with tools —
// split, trim, move and delete clips, titles, transitions, effects, colour and looks,
// captions — on a copy of the project. Nothing changes until the person applies what it
// proposes: then all of it is one undo step ("Copilot: …"), refused if the project was
// edited meanwhile. Each request carries the project as it is then, described compactly; the
// conversation is append-only (its system prompt and tools never change), so Claude's
// thinking and the prompt cache stay valid across turns.
class Copilot : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("The project's copilot")
    // [{kind: "you" | "copilot" | "changes" | "error" | "note", text, changes: [text],
    //   state (for "changes"): "proposed" | "applied" | "discarded"}]
    Q_PROPERTY(QVariantList messages READ messages NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    // What it proposes now, waiting for the go-ahead (one line each).
    Q_PROPERTY(QStringList proposal READ proposal NOTIFY changed)

public:
    explicit Copilot(ProjectController* project);

    QVariantList messages() const { return m_messages; }
    bool busy() const { return m_busy; }
    QStringList proposal() const { return m_proposal; }

    // Asks for something; false (with lastError) if it can't be asked now.
    Q_INVOKABLE bool ask(const QString& request);
    // Applies what it proposes, as one undo step; false (with the controller's lastError)
    // if the project changed since.
    Q_INVOKABLE bool applyChanges();
    Q_INVOKABLE void discardChanges();
    // A new conversation (also when another project is opened).
    Q_INVOKABLE void startOver();
    Q_INVOKABLE void cancel();
    // The project as Claude is told it (also for tests).
    Q_INVOKABLE QString describeProject() const;

    // The tools (with their JSON schemas) and the system prompt, fixed for the app's run.
    static const QJsonArray& tools();
    static const QJsonArray& systemPrompt();

    // Runs one tool on `project` (a copy): its result for Claude, and the change in words
    // when it made one. Public for tests.
    struct Outcome {
        bool ok = false;
        QJsonObject result;
        QString change;
    };
    static Outcome run(const QString& name, const QJsonObject& input, tve::Project& project);
    // Checks `input` against the tool's schema: empty if it fits, else why not.
    static QString check(const QString& name, const QJsonObject& input);

signals:
    void changed();

private:
    void send();
    void rollBack();
    void received(const AgentResult& result);
    void finishTurn(const QString& text);
    void add(const QVariantMap& message);
    void setChangesState(const QString& state);

    ProjectController* m_project;
    QJsonArray m_history;
    std::optional<tve::Project> m_base;    // the project when asked
    std::optional<tve::Project> m_sandbox; // its copy, as the tools left it
    QStringList m_proposal;
    QVariantList m_messages;
    QString m_note; // said to Claude before the next request (what became of its changes)
    int m_rounds = 0;
    bool m_busy = false;
};
