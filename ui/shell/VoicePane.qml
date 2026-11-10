import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import TechieVideoEditor.Theme

// The Audio tab's Voice section: type what to say, choose a voice (the Mac's own, offline,
// or ElevenLabs with a key), listen, and add it at the playhead with captions. Below, the
// voices copied with consent: made here from the speaker's recorded statement, deletable.
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
    // Copied voices (ElevenLabs), and copying one with consent.
    Text { Layout.topMargin: Theme.space.m; text: "Copied voices"; font: Theme.type.label; color: Theme.color.textSecondary }
    Repeater {
        model: pane.project.voiceCloning.clones
        delegate: RowLayout {
            required property var modelData
            required property int index
            objectName: "clonedVoice:" + index
            Layout.fillWidth: true
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0
                Text { text: modelData.name; font: Theme.type.body; color: Theme.color.textPrimary; elide: Text.ElideRight; Layout.fillWidth: true }
                Text {
                    Layout.fillWidth: true
                    text: modelData.consent ? "Consent recorded by " + modelData.consent.speaker + " on " + modelData.consent.date
                                            : "Made outside this app (no consent recording here)"
                    font: Theme.type.caption
                    color: Theme.color.textSecondary
                    elide: Text.ElideRight
                }
            }
            IconButton {
                objectName: "deleteClone:" + index
                iconName: "delete"
                tip: "Delete this voice from your ElevenLabs account"
                onClicked: {
                    deleteConfirm.voice = modelData
                    deleteConfirm.open()
                }
            }
        }
    }
    TextButton {
        objectName: "copyVoice"
        Layout.fillWidth: true
        text: "Copy a voice, with consent…"
        variant: "secondary"
        enabled: pane.cloud.hasElevenLabsKey && pane.cloud.job === ""
        tip: pane.cloud.hasElevenLabsKey ? "Make a copy of a voice from the speaker reading a consent statement"
                                         : "Add an ElevenLabs key in AI settings first"
        onClicked: cloneDialog.open()
    }
    Item { Layout.fillHeight: true }

    VoiceCloneDialog {
        id: cloneDialog
        objectName: "voiceCloneDialog"
        project: pane.project
        onClosed: if (project.voiceCloning.stage === "done") {
            pane.voice = project.voiceCloning.madeVoice
            project.voiceCloning.reset()
        }
    }
    Popup {
        id: deleteConfirm
        objectName: "deleteCloneConfirm"
        property var voice: ({})
        modal: true
        anchors.centerIn: Overlay.overlay
        width: 420
        padding: Theme.space.l
        background: Rectangle { radius: Theme.radius.l; color: Theme.color.surfaceRaised; border.color: Theme.color.borderStrong }
        contentItem: ColumnLayout {
            spacing: Theme.space.m
            Text {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: "Delete “" + (deleteConfirm.voice.name || "") + "” from your ElevenLabs account? Its consent recording "
                      + "here is deleted too. This can't be undone."
                font: Theme.type.body
                color: Theme.color.textPrimary
            }
            RowLayout {
                Item { Layout.fillWidth: true }
                TextButton { text: "Cancel"; variant: "ghost"; onClicked: deleteConfirm.close() }
                TextButton {
                    objectName: "deleteCloneConfirmButton"
                    text: "Delete"
                    variant: "danger"
                    onClicked: {
                        pane.project.voiceCloning.deleteVoice(deleteConfirm.voice.id)
                        deleteConfirm.close()
                    }
                }
            }
        }
    }
}
