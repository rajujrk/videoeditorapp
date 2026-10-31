import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import TechieVideoEditor.Theme

// The Audio tab's Voice section: type what to say, choose a voice (the Mac's own, offline,
// or ElevenLabs with a key), listen, and add it at the playhead with captions.
ColumnLayout {
    id: pane
    required property var project
    readonly property var cloud: project.cloud
    readonly property bool speaking: cloud.job === "speak"
    readonly property var voices: cloud.voices
    property string voice: voices.length > 0 ? voices[0].id : ""
    readonly property var voiceInfo: voices.find((v) => v.id === voice) || ({})

    function label(v) {
        return v.name + (v.language ? " · " + v.language : "") + (v.detail ? " · " + v.detail : "")
             + (v.provider === "elevenlabs" ? " (ElevenLabs)" : "")
    }

    spacing: Theme.space.s

    TextArea {
        id: script
        objectName: "voiceText"
        Layout.fillWidth: true
        Layout.preferredHeight: 120
        wrapMode: TextEdit.Wrap
        font: Theme.type.body
        color: Theme.color.textPrimary
        placeholderText: "Type what the voice should say"
        background: Rectangle { radius: Theme.radius.m; color: Theme.color.surfaceSunken; border.color: Theme.color.borderSubtle }
    }

    Text { text: "Voice"; font: Theme.type.label; color: Theme.color.textSecondary }
    ComboBox {
        id: voiceChoice
        objectName: "voiceChoice"
        Layout.fillWidth: true
        model: pane.voices.map((v) => pane.label(v))
        currentIndex: Math.max(0, pane.voices.findIndex((v) => v.id === pane.voice))
        onActivated: (index) => pane.voice = pane.voices[index].id
    }
    Text {
        Layout.fillWidth: true
        visible: !pane.cloud.hasElevenLabsKey
        text: "These are your Mac's own voices. Add an ElevenLabs key in AI settings for cloud voices."
        font: Theme.type.caption
        color: Theme.color.textSecondary
        wrapMode: Text.WordWrap
    }

    RowLayout {
        Layout.fillWidth: true
        Text { text: "Speed"; font: Theme.type.label; color: Theme.color.textSecondary }
        Slider {
            id: speed
            objectName: "voiceSpeed"
            Layout.fillWidth: true
            from: 0.5
            to: 2
            value: 1
            stepSize: 0.05
        }
        Text { text: speed.value.toFixed(2) + "×"; font: Theme.type.label; color: Theme.color.textPrimary }
    }
    ToggleSwitch {
        id: captions
        objectName: "voiceCaptions"
        text: "Captions from the text"
        checked: true
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.space.s
        TextButton {
            objectName: "listenVoice"
            text: "Listen"
            iconName: "play"
            variant: "secondary"
            enabled: script.text.trim() !== "" && pane.voice !== "" && pane.cloud.job === ""
            onClicked: pane.project.speakText(script.text, pane.voice, { speed: speed.value, preview: true })
        }
        TextButton {
            objectName: "addVoice"
            Layout.fillWidth: true
            text: "Add at the playhead"
            variant: "primary"
            enabled: script.text.trim() !== "" && pane.voice !== "" && pane.cloud.job === ""
            onClicked: pane.project.speakText(script.text, pane.voice, { speed: speed.value, captions: captions.checked })
        }
        TextButton {
            objectName: "cancelVoice"
            visible: pane.speaking
            text: "Stop"
            variant: "ghost"
            onClicked: pane.cloud.cancel()
        }
    }
    Text {
        objectName: "voiceStatus"
        Layout.fillWidth: true
        visible: pane.speaking
        text: pane.cloud.status
        font: Theme.type.caption
        color: Theme.color.textSecondary
    }
    Item { Layout.fillHeight: true }
}
