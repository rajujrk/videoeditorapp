import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import TechieVideoEditor.Theme

// AI settings: the person's own keys for Claude (writing and translation), ElevenLabs
// (voices, music and sound effects), OpenAI (pictures and stickers) and Google (video), kept
// in their Keychain; the models used; and what this session has used.
Popup {
    id: dialog
    required property var project
    readonly property var cloud: project.cloud

    modal: true
    focus: true
    anchors.centerIn: Overlay.overlay
    width: 600
    height: Math.min(implicitHeight, (Overlay.overlay ? Overlay.overlay.height : 800) - Theme.space.xl * 2)
    padding: Theme.space.l
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    signal keyCleared()
    onAboutToShow: { message.text = ""; keyCleared() }

    function money(dollars) {
        return dollars < 0.01 && dollars > 0 ? "under $0.01" : "about $" + dollars.toFixed(2)
    }
    function count(n) { return n.toLocaleString(Qt.locale(), "f", 0) }

    // A key: a field that hides what is typed, Save and Remove. `name` makes the object names
    // ("claude" → claudeKeyField, saveClaudeKey, removeClaudeKey).
    component KeyRow: RowLayout {
        id: row
        property string provider
        property string name
        property string label
        property bool has
        Layout.fillWidth: true
        spacing: Theme.space.s
        function save() {
            if (dialog.cloud.setKey(row.provider, field.text)) {
                field.text = ""
                message.text = "Saved in your Keychain."
            } else {
                message.text = dialog.cloud.lastError()
            }
        }
        Connections { target: dialog; function onKeyCleared() { field.text = "" } }
        TextField {
            id: field
            objectName: row.name + "KeyField"
            Layout.fillWidth: true
            echoMode: TextInput.Password
            color: Theme.color.textPrimary
            placeholderTextColor: Theme.color.textDisabled
            selectByMouse: true
            background: Rectangle {
                implicitHeight: 32
                radius: Theme.radius.s
                color: Theme.color.surfaceSunken
                border.color: parent.activeFocus ? Theme.color.accent : Theme.color.borderSubtle
            }
            placeholderText: row.has ? "Key saved · paste a new one to replace it" : "Paste your " + row.label + " API key"
            onAccepted: row.save()
        }
        TextButton {
            objectName: "save" + row.name.charAt(0).toUpperCase() + row.name.slice(1) + "Key"
            text: "Save"
            variant: "primary"
            enabled: field.text.trim() !== ""
            onClicked: row.save()
        }
        TextButton {
            objectName: "remove" + row.name.charAt(0).toUpperCase() + row.name.slice(1) + "Key"
            visible: row.has
            text: "Remove"
            variant: "ghost"
            onClicked: { dialog.cloud.removeKey(row.provider); message.text = "Removed." }
        }
    }

    // Model choices as rows with a dot.
    component Choices: ColumnLayout {
        id: choices
        property string name
        property var models: []
        property string current
        signal chosen(string id)
        Layout.fillWidth: true
        spacing: 0
        Repeater {
            model: choices.models
            delegate: AbstractButton {
                id: choice
                required property var modelData
                objectName: choices.name + "Model:" + modelData.id
                Layout.fillWidth: true
                checked: choices.current === modelData.id
                hoverEnabled: true
                implicitHeight: 30
                onClicked: choices.chosen(modelData.id)
                background: Rectangle { radius: Theme.radius.s; color: choice.hovered ? Theme.color.surfaceHover : "transparent" }
                contentItem: RowLayout {
                    spacing: Theme.space.s
                    Rectangle {
                        Layout.leftMargin: Theme.space.xs
                        implicitWidth: 16
                        implicitHeight: 16
                        radius: 8
                        color: "transparent"
                        border.width: 2
                        border.color: choice.checked ? Theme.color.accent : Theme.color.borderStrong
                        Rectangle {
                            anchors.centerIn: parent
                            visible: choice.checked
                            width: 8
                            height: 8
                            radius: 4
                            color: Theme.color.accent
                        }
                    }
                    Text { text: choice.modelData.label; font: Theme.type.label; color: Theme.color.textPrimary }
                    Text {
                        Layout.fillWidth: true
                        text: choice.modelData.detail
                        font: Theme.type.caption
                        color: Theme.color.textSecondary
                        elide: Text.ElideRight
                    }
                }
            }
        }
    }

    component Heading: Text {
        font: Theme.type.heading
        color: Theme.color.textPrimary
        Layout.topMargin: Theme.space.xs
    }
    component Note: Text {
        Layout.fillWidth: true
        Layout.preferredWidth: 1
        font: Theme.type.caption
        color: Theme.color.textSecondary
        wrapMode: Text.WordWrap
    }

    background: Rectangle {
        radius: Theme.radius.l
        color: Theme.color.surfaceRaised
        border.color: Theme.color.borderStrong
    }

    contentItem: ColumnLayout {
        spacing: Theme.space.s

        RowLayout {
            Layout.fillWidth: true
            Text { Layout.fillWidth: true; text: "AI settings"; font: Theme.type.title; color: Theme.color.textPrimary }
            IconButton { objectName: "closeCloudSettings"; iconName: "close"; tip: "Close"; onClicked: dialog.close() }
        }
        Text {
            Layout.fillWidth: true
            Layout.preferredWidth: 1
            text: "These features use your own accounts. Your keys stay in your Mac's Keychain; what you use them on is "
                  + "sent to that company, and their charges are yours." + (dialog.cloud.mock ? " (Mock services: nothing leaves this Mac.)" : "")
            font: Theme.type.body
            color: Theme.color.textSecondary
            wrapMode: Text.WordWrap
        }

        ScrollView {
            id: scroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredHeight: sections.implicitHeight
            contentWidth: availableWidth
            clip: true

            ColumnLayout {
                id: sections
                width: scroll.availableWidth
                spacing: Theme.space.s

                Heading { text: "Claude · writing and translation" }
                KeyRow { provider: "anthropic"; name: "claude"; label: "Claude"; has: dialog.cloud.hasClaudeKey }
                Choices {
                    name: "claude"
                    models: dialog.cloud.claudeModels
                    current: dialog.cloud.claudeModel
                    onChosen: (id) => dialog.cloud.claudeModel = id
                }

                Heading { text: "ElevenLabs · voices, music and sound effects" }
                KeyRow { provider: "elevenlabs"; name: "elevenLabs"; label: "ElevenLabs"; has: dialog.cloud.hasElevenLabsKey }
                Choices {
                    name: "elevenLabs"
                    models: dialog.cloud.elevenLabsModels
                    current: dialog.cloud.elevenLabsModel
                    onChosen: (id) => dialog.cloud.elevenLabsModel = id
                }
                Note {
                    visible: dialog.cloud.voicesError !== ""
                    text: dialog.cloud.voicesError
                    color: Theme.color.danger
                }
                Note { text: "How you may use ElevenLabs audio depends on your ElevenLabs plan (music: not for film, TV, radio or studio games below Enterprise Music)." }

                Heading { text: "OpenAI · pictures and stickers" }
                KeyRow { provider: "openai"; name: "openAi"; label: "OpenAI"; has: dialog.cloud.hasOpenAiKey }
                Choices {
                    name: "image"
                    models: dialog.cloud.imageModels
                    current: dialog.cloud.imageModel
                    onChosen: (id) => dialog.cloud.imageModel = id
                }

                Heading { text: "Google AI · video" }
                KeyRow { provider: "google"; name: "google"; label: "Gemini API"; has: dialog.cloud.hasGoogleKey }
                Note { text: "Gemini Omni Flash makes the video with your Gemini API key (only paid use is allowed in the EEA, Switzerland and the UK)." }
            }
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
                text: {
                    const u = dialog.cloud.usage
                    const plural = (n, one, many) => dialog.count(n) + " " + (n === 1 ? one : many)
                    const parts = []
                    if (u.inputTokens > 0 || u.outputTokens > 0)
                        parts.push(dialog.count(u.inputTokens) + " tokens in, " + dialog.count(u.outputTokens) + " out ("
                                   + dialog.money(u.cost) + ")")
                    if (u.characters > 0)
                        parts.push(plural(u.characters, "character", "characters") + " spoken")
                    if (u.images > 0)
                        parts.push(plural(u.images, "picture", "pictures"))
                    if (u.videos > 0)
                        parts.push(plural(u.videos, "video", "videos"))
                    if (u.soundSeconds > 0)
                        parts.push(Math.round(u.soundSeconds) + " s of music and sound")
                    return "This session: " + (parts.length > 0 ? parts.join(" · ") : "nothing used yet")
                }
                font: Theme.type.caption
                color: Theme.color.textSecondary
                wrapMode: Text.WordWrap
            }
            TextButton { text: "Reset"; variant: "ghost"; onClicked: dialog.cloud.resetUsage() }
        }
    }
}
