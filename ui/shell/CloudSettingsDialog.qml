import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import TechieVideoEditor.Theme

// AI settings: the person's own keys for Claude (writing and translation) and ElevenLabs
// (cloud voices), kept in their Keychain; the models used; and what this session has used.
Popup {
    id: dialog
    required property var project
    readonly property var cloud: project.cloud

    modal: true
    focus: true
    anchors.centerIn: Overlay.overlay
    width: 560
    padding: Theme.space.l
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    onAboutToShow: { claudeKey.text = ""; elevenLabsKey.text = ""; message.text = "" }

    function money(dollars) {
        return dollars < 0.01 && dollars > 0 ? "under $0.01" : "about $" + dollars.toFixed(2)
    }
    function save(provider, field) {
        if (dialog.cloud.setKey(provider, field.text)) {
            field.text = ""
            message.text = "Saved in your Keychain."
        } else {
            message.text = dialog.cloud.lastError()
        }
    }

    background: Rectangle {
        radius: Theme.radius.l
        color: Theme.color.surfaceRaised
        border.color: Theme.color.borderStrong
    }

    contentItem: ColumnLayout {
        spacing: Theme.space.m

        RowLayout {
            Layout.fillWidth: true
            Text { Layout.fillWidth: true; text: "AI settings"; font: Theme.type.title; color: Theme.color.textPrimary }
            IconButton { objectName: "closeCloudSettings"; iconName: "close"; tip: "Close"; onClicked: dialog.close() }
        }
        Text {
            Layout.fillWidth: true
            Layout.preferredWidth: 1
            text: "Writing, translation and cloud voices use your own accounts. Your keys stay in your Mac's Keychain; "
                  + "the text you use them on is sent to Anthropic or ElevenLabs, and their charges are yours."
                  + (dialog.cloud.mock ? " (Mock services: nothing leaves this Mac.)" : "")
            font: Theme.type.body
            color: Theme.color.textSecondary
            wrapMode: Text.WordWrap
        }

        // Claude
        Text { text: "Claude (writing and translation)"; font: Theme.type.heading; color: Theme.color.textPrimary }
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.space.s
            TextField {
                id: claudeKey
                objectName: "claudeKeyField"
                Layout.fillWidth: true
                echoMode: TextInput.Password
                placeholderText: dialog.cloud.hasClaudeKey ? "Key saved · paste a new one to replace it" : "Paste your Claude API key"
                onAccepted: dialog.save("anthropic", claudeKey)
            }
            TextButton {
                objectName: "saveClaudeKey"
                text: "Save"
                variant: "primary"
                enabled: claudeKey.text.trim() !== ""
                onClicked: dialog.save("anthropic", claudeKey)
            }
            TextButton {
                objectName: "removeClaudeKey"
                visible: dialog.cloud.hasClaudeKey
                text: "Remove"
                variant: "ghost"
                onClicked: { dialog.cloud.removeKey("anthropic"); message.text = "Removed." }
            }
        }
        Repeater {
            model: dialog.cloud.claudeModels
            delegate: AbstractButton {
                id: claudeChoice
                required property var modelData
                objectName: "claudeModel:" + modelData.id
                Layout.fillWidth: true
                checked: dialog.cloud.claudeModel === modelData.id
                hoverEnabled: true
                implicitHeight: 32
                onClicked: dialog.cloud.claudeModel = modelData.id
                background: Rectangle {
                    radius: Theme.radius.s
                    color: claudeChoice.hovered ? Theme.color.surfaceHover : "transparent"
                }
                contentItem: RowLayout {
                    spacing: Theme.space.s
                    Rectangle {
                        Layout.leftMargin: Theme.space.xs
                        implicitWidth: 16
                        implicitHeight: 16
                        radius: 8
                        color: "transparent"
                        border.width: 2
                        border.color: claudeChoice.checked ? Theme.color.accent : Theme.color.borderStrong
                        Rectangle {
                            anchors.centerIn: parent
                            visible: claudeChoice.checked
                            width: 8
                            height: 8
                            radius: 4
                            color: Theme.color.accent
                        }
                    }
                    Text { text: claudeChoice.modelData.label; font: Theme.type.label; color: Theme.color.textPrimary }
                    Text {
                        Layout.fillWidth: true
                        text: claudeChoice.modelData.detail
                        font: Theme.type.caption
                        color: Theme.color.textSecondary
                        elide: Text.ElideRight
                    }
                }
            }
        }

        // ElevenLabs
        Text { text: "ElevenLabs (cloud voices)"; font: Theme.type.heading; color: Theme.color.textPrimary }
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.space.s
            TextField {
                id: elevenLabsKey
                objectName: "elevenLabsKeyField"
                Layout.fillWidth: true
                echoMode: TextInput.Password
                placeholderText: dialog.cloud.hasElevenLabsKey ? "Key saved · paste a new one to replace it" : "Paste your ElevenLabs API key"
                onAccepted: dialog.save("elevenlabs", elevenLabsKey)
            }
            TextButton {
                objectName: "saveElevenLabsKey"
                text: "Save"
                variant: "primary"
                enabled: elevenLabsKey.text.trim() !== ""
                onClicked: dialog.save("elevenlabs", elevenLabsKey)
            }
            TextButton {
                objectName: "removeElevenLabsKey"
                visible: dialog.cloud.hasElevenLabsKey
                text: "Remove"
                variant: "ghost"
                onClicked: { dialog.cloud.removeKey("elevenlabs"); message.text = "Removed." }
            }
        }
        Repeater {
            model: dialog.cloud.elevenLabsModels
            delegate: AbstractButton {
                id: elevenLabsChoice
                required property var modelData
                objectName: "elevenLabsModel:" + modelData.id
                Layout.fillWidth: true
                checked: dialog.cloud.elevenLabsModel === modelData.id
                hoverEnabled: true
                implicitHeight: 32
                onClicked: dialog.cloud.elevenLabsModel = modelData.id
                background: Rectangle {
                    radius: Theme.radius.s
                    color: elevenLabsChoice.hovered ? Theme.color.surfaceHover : "transparent"
                }
                contentItem: RowLayout {
                    spacing: Theme.space.s
                    Rectangle {
                        Layout.leftMargin: Theme.space.xs
                        implicitWidth: 16
                        implicitHeight: 16
                        radius: 8
                        color: "transparent"
                        border.width: 2
                        border.color: elevenLabsChoice.checked ? Theme.color.accent : Theme.color.borderStrong
                        Rectangle {
                            anchors.centerIn: parent
                            visible: elevenLabsChoice.checked
                            width: 8
                            height: 8
                            radius: 4
                            color: Theme.color.accent
                        }
                    }
                    Text { text: elevenLabsChoice.modelData.label; font: Theme.type.label; color: Theme.color.textPrimary }
                    Text {
                        Layout.fillWidth: true
                        text: elevenLabsChoice.modelData.detail
                        font: Theme.type.caption
                        color: Theme.color.textSecondary
                        elide: Text.ElideRight
                    }
                }
            }
        }
        Text {
            Layout.fillWidth: true
            Layout.preferredWidth: 1
            visible: dialog.cloud.voicesError !== ""
            text: dialog.cloud.voicesError
            font: Theme.type.caption
            color: Theme.color.danger
            wrapMode: Text.WordWrap
        }
        Text {
            Layout.fillWidth: true
            Layout.preferredWidth: 1
            text: "Whether you may use ElevenLabs audio commercially depends on your ElevenLabs plan."
            font: Theme.type.caption
            color: Theme.color.textSecondary
            wrapMode: Text.WordWrap
        }

        Text {
            id: message
            objectName: "cloudSettingsMessage"
            visible: text !== ""
            font: Theme.type.label
            color: Theme.color.textSecondary
        }

        // This session's use
        Divider { Layout.fillWidth: true; implicitHeight: 1 }
        RowLayout {
            Layout.fillWidth: true
            Text {
                objectName: "cloudUsage"
                Layout.fillWidth: true
                text: "This session: " + dialog.cloud.usage.inputTokens.toLocaleString(Qt.locale(), "f", 0) + " tokens in, "
                      + dialog.cloud.usage.outputTokens.toLocaleString(Qt.locale(), "f", 0) + " out ("
                      + dialog.money(dialog.cloud.usage.cost) + ") · "
                      + dialog.cloud.usage.characters.toLocaleString(Qt.locale(), "f", 0) + " characters spoken"
                font: Theme.type.caption
                color: Theme.color.textSecondary
                wrapMode: Text.WordWrap
            }
            TextButton { text: "Reset"; variant: "ghost"; onClicked: dialog.cloud.resetUsage() }
        }
    }
}
