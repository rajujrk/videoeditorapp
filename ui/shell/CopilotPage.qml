import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import TechieVideoEditor.Theme

// The library's Copilot tab: ask Claude (your own key) for changes in words — "cut the
// pauses after 0:30", "a title saying Welcome for the first three seconds", "warmer colour
// on the beach shots", "captions from what's said". It edits a copy and lists what it would
// change; nothing happens to the project until Apply, which is one undo step.
Item {
    id: page
    required property var project
    readonly property var copilot: project.copilot
    readonly property var cloud: project.cloud
    signal settingsRequested()

    function send() {
        if (page.copilot.ask(request.text))
            request.text = ""
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: Theme.space.m
        spacing: Theme.space.s

        RowLayout {
            visible: !page.cloud.hasClaudeKey
            Layout.fillWidth: true
            Text {
                Layout.fillWidth: true
                text: qsTr("Add your Claude key in AI settings to edit with the copilot.")
                font: Theme.type.body
                color: Theme.color.textSecondary
                wrapMode: Text.WordWrap
            }
            TextButton {
                objectName: "copilotSettings"
                text: qsTr("AI settings…")
                variant: "secondary"
                onClicked: page.settingsRequested()
            }
        }

        // The conversation, newest at the bottom.
        ListView {
            id: conversation
            objectName: "copilotConversation"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: Theme.space.s
            model: page.copilot.messages
            onCountChanged: Qt.callLater(() => conversation.positionViewAtEnd())
            ScrollBar.vertical: ScrollBar {}

            header: Text {
                visible: conversation.count === 0
                height: visible ? implicitHeight + Theme.space.m : 0
                width: conversation.width
                wrapMode: Text.WordWrap
                text: qsTr("Ask for changes in your own words: trims and cuts, titles, transitions, effects, colour, captions. You'll see what it would change before anything happens.")
                      + "\n\n" + qsTr("Each request sends Anthropic a description of the project — its clips' names and timing, titles and captions, and what is said in transcribed media — not the pictures or sound.")
                font: Theme.type.body
                color: Theme.color.textSecondary
            }

            delegate: Rectangle {
                id: entry
                required property var modelData
                required property int index
                readonly property string kind: modelData.kind
                objectName: "copilotMessage:" + index
                width: conversation.width
                height: body.implicitHeight + Theme.space.m
                radius: Theme.radius.m
                color: kind === "you" ? Theme.color.accentSubtle
                     : kind === "changes" ? Theme.color.surfaceRaised
                     : "transparent"
                border.color: kind === "changes" ? (modelData.state === "proposed" ? Theme.color.accent : Theme.color.borderSubtle)
                                                 : "transparent"

                ColumnLayout {
                    id: body
                    anchors { left: parent.left; right: parent.right; top: parent.top; margins: Theme.space.s }
                    spacing: Theme.space.xxs
                    Text {
                        visible: entry.kind !== "changes"
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: entry.modelData.text || ""
                        font: Theme.type.body
                        color: entry.kind === "error" ? Theme.color.danger : Theme.color.textPrimary
                    }
                    Text {
                        visible: entry.kind === "changes"
                        objectName: "copilotChangesTitle:" + entry.index
                        Layout.fillWidth: true
                        text: entry.modelData.state === "applied" ? qsTr("Applied")
                            : entry.modelData.state === "discarded" ? qsTr("Not applied")
                            : (entry.modelData.changes || []).length === 1 ? qsTr("1 change, not applied yet") : qsTr("%1 changes, not applied yet").arg(Fmt.num((entry.modelData.changes || []).length))
                        font: Theme.type.label
                        color: entry.modelData.state === "proposed" ? Theme.color.accentText : Theme.color.textSecondary
                    }
                    Repeater {
                        model: entry.kind === "changes" ? entry.modelData.changes : []
                        delegate: Text {
                            required property string modelData
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            text: "• " + modelData
                            font: Theme.type.body
                            color: Theme.color.textSecondary
                        }
                    }
                    RowLayout {
                        visible: entry.kind === "changes" && entry.modelData.state === "proposed"
                        Layout.fillWidth: true
                        Layout.topMargin: Theme.space.xs
                        Item { Layout.fillWidth: true }
                        TextButton {
                            objectName: entry.kind === "changes" ? "copilotDiscard" : ""
                            text: qsTr("Don't apply")
                            variant: "ghost"
                            onClicked: page.copilot.discardChanges()
                        }
                        TextButton {
                            objectName: entry.kind === "changes" ? "copilotApply" : ""
                            text: qsTr("Apply")
                            variant: "primary"
                            tip: qsTr("Make these changes to the project, as one step you can undo")
                            onClicked: page.copilot.applyChanges()
                        }
                    }
                }
            }
        }

        RowLayout {
            visible: page.copilot.busy
            Layout.fillWidth: true
            BusyIndicator { implicitWidth: 20; implicitHeight: 20; running: page.copilot.busy }
            Text {
                objectName: "copilotStatus"
                Layout.fillWidth: true
                text: page.cloud.status || qsTr("Asking Claude…")
                font: Theme.type.caption
                color: Theme.color.textSecondary
                elide: Text.ElideRight
            }
            TextButton { objectName: "copilotStop"; text: qsTr("Stop"); variant: "ghost"; onClicked: page.copilot.cancel() }
        }

        TextArea {
            id: request
            objectName: "copilotRequest"
            Layout.fillWidth: true
            Layout.preferredHeight: 72
            wrapMode: TextEdit.Wrap
            enabled: page.cloud.hasClaudeKey && !page.copilot.busy
            font: Theme.type.body
            color: Theme.color.textPrimary
            placeholderText: qsTr("What should change? (⌘↩ to send)")
            background: Rectangle { radius: Theme.radius.m; color: Theme.color.surfaceSunken; border.color: Theme.color.borderSubtle }
            Keys.onReturnPressed: (event) => {
                if (event.modifiers & Qt.ControlModifier || event.modifiers & Qt.MetaModifier)
                    page.send()
                else
                    event.accepted = false
            }
        }
        RowLayout {
            Layout.fillWidth: true
            TextButton {
                objectName: "copilotStartOver"
                text: qsTr("New conversation")
                variant: "ghost"
                enabled: page.copilot.messages.length > 0 && !page.copilot.busy
                onClicked: page.copilot.startOver()
            }
            Item { Layout.fillWidth: true }
            TextButton {
                objectName: "copilotSend"
                text: qsTr("Ask")
                iconName: "assistant"
                variant: "primary"
                enabled: request.text.trim() !== "" && page.cloud.hasClaudeKey && !page.copilot.busy
                onClicked: page.send()
            }
        }
    }
}
