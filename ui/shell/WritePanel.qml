import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import TechieVideoEditor.Theme

// Write with AI: titles, a description or a clean script from the transcript, written by
// Claude as it streams in; titles can go on the timeline one by one.
Popup {
    id: panel
    required property var project
    readonly property var cloud: project.cloud
    readonly property bool writing: cloud.job === "write"

    property string kind: "titles"
    readonly property var kinds: [{ id: "titles", label: "Titles" }, { id: "description", label: "Description" },
                                  { id: "script", label: "Script" }]
    readonly property var tones: ["Any tone", "Friendly", "Professional", "Playful", "Bold", "Calm"]
    // The answer's lines (titles).
    readonly property var lines: draft.text.split("\n").map((l) => l.trim()).filter((l) => l !== "")

    function write() {
        panel.project.writeCopy(kind, tone.currentIndex > 0 ? tones[tone.currentIndex] : "", notes.text)
    }

    parent: Overlay.overlay
    anchors.centerIn: parent
    modal: true
    width: 640
    height: Math.min(620, parent ? parent.height - Theme.space.xl * 2 : 620)
    padding: Theme.space.l
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    background: Rectangle {
        radius: Theme.radius.l
        color: Theme.color.surfaceRaised
        border.color: Theme.color.borderStrong
    }

    contentItem: ColumnLayout {
        spacing: Theme.space.m

        RowLayout {
            Layout.fillWidth: true
            Text { Layout.fillWidth: true; text: "Write with AI"; font: Theme.type.title; color: Theme.color.textPrimary }
            IconButton { objectName: "closeWrite"; iconName: "close"; tip: "Close"; onClicked: panel.close() }
        }

        Row {
            spacing: Theme.space.xxs
            Repeater {
                model: panel.kinds
                delegate: AbstractButton {
                    id: kindButton
                    required property var modelData
                    objectName: "writeKind:" + modelData.id
                    checked: panel.kind === modelData.id
                    enabled: !panel.writing
                    hoverEnabled: true
                    implicitWidth: kindLabel.implicitWidth + Theme.space.l * 2
                    implicitHeight: 28
                    onClicked: panel.kind = modelData.id
                    background: Rectangle {
                        radius: Theme.radius.s
                        color: kindButton.checked ? Theme.color.accentSubtle : kindButton.hovered ? Theme.color.surfaceHover : "transparent"
                        border.color: kindButton.checked ? Theme.color.accent : Theme.color.borderSubtle
                    }
                    contentItem: Text {
                        id: kindLabel
                        text: kindButton.modelData.label
                        font: Theme.type.label
                        color: kindButton.checked ? Theme.color.accentText : Theme.color.textSecondary
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.space.s
            ComboBox { id: tone; objectName: "writeTone"; model: panel.tones; enabled: !panel.writing }
            TextField {
                id: notes
                objectName: "writeNotes"
                Layout.fillWidth: true
                enabled: !panel.writing
                placeholderText: "Anything else: the audience, words to use…"
                color: Theme.color.textPrimary
                placeholderTextColor: Theme.color.textDisabled
                selectByMouse: true
                background: Rectangle {
                    implicitHeight: 32
                    radius: Theme.radius.s
                    color: Theme.color.surfaceSunken
                    border.color: parent.activeFocus ? Theme.color.accent : Theme.color.borderSubtle
                }
                onAccepted: if (writeButton.enabled) panel.write()
            }
            TextButton {
                id: writeButton
                objectName: "writeButton"
                visible: !panel.writing
                text: panel.lines.length > 0 ? "Write again" : "Write"
                variant: "primary"
                enabled: panel.cloud.hasClaudeKey && panel.cloud.job === ""
                onClicked: panel.write()
            }
            TextButton {
                objectName: "cancelWrite"
                visible: panel.writing
                text: "Stop"
                variant: "secondary"
                onClicked: panel.cloud.cancel()
            }
        }

        Text {
            objectName: "writeStatus"
            Layout.fillWidth: true
            text: !panel.cloud.hasClaudeKey ? "Add your Claude API key in AI settings (the speech bubble at the top) to write."
                : panel.cloud.status
            visible: text !== ""
            font: Theme.type.caption
            color: Theme.color.textSecondary
            wrapMode: Text.WordWrap
        }

        // The answer, editable once written.
        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: panel.kind !== "titles" || panel.writing
            TextArea {
                id: draft
                objectName: "writeDraft"
                text: panel.cloud.draft
                readOnly: panel.writing
                wrapMode: TextEdit.Wrap
                selectByMouse: true
                font: Theme.type.body
                color: Theme.color.textPrimary
                placeholderText: "What Claude writes appears here."
                background: Rectangle { radius: Theme.radius.m; color: Theme.color.surfaceSunken; border.color: Theme.color.borderSubtle }
            }
        }
        ListView {
            objectName: "writeTitles"
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: panel.kind === "titles" && !panel.writing
            clip: true
            spacing: Theme.space.xxs
            model: panel.lines
            delegate: Rectangle {
                id: titleRow
                required property string modelData
                required property int index
                width: ListView.view.width
                height: 40
                radius: Theme.radius.m
                color: Theme.color.surfaceSunken
                border.color: Theme.color.borderSubtle
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: Theme.space.m
                    anchors.rightMargin: Theme.space.xs
                    Text { Layout.fillWidth: true; text: titleRow.modelData; font: Theme.type.body; color: Theme.color.textPrimary; elide: Text.ElideRight }
                    TextButton {
                        objectName: "addWrittenTitle:" + titleRow.index
                        text: "Add as title"
                        variant: "ghost"
                        tip: "A title clip at the playhead"
                        onClicked: panel.project.addTitle({ text: titleRow.modelData })
                    }
                }
            }
            Text {
                anchors.centerIn: parent
                visible: parent.count === 0
                text: "Titles appear here, ready to add."
                font: Theme.type.body
                color: Theme.color.textSecondary
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Text {
                objectName: "writeUsage"
                Layout.fillWidth: true
                text: "This session: about $" + panel.cloud.usage.cost.toFixed(2) + " · "
                      + panel.cloud.claudeModels.find((m) => m.id === panel.cloud.claudeModel).label
                font: Theme.type.caption
                color: Theme.color.textSecondary
            }
            TextButton {
                objectName: "copyDraft"
                text: "Copy"
                variant: "secondary"
                enabled: draft.text !== "" && !panel.writing
                onClicked: { draft.selectAll(); draft.copy(); draft.deselect() }
            }
        }
    }
}
