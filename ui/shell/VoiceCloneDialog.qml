import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import TechieVideoEditor.Theme

// Copying a voice, only with the speaker's consent: who it is (and that it is them, 18 or
// older, agreeing), the statement written for them to read, the recording, the check on this
// Mac that it says the statement, and only then the copy at ElevenLabs.
Popup {
    id: dialog
    required property var project
    readonly property var cloning: project.voiceCloning
    readonly property string stage: cloning.stage

    modal: true
    focus: true
    anchors.centerIn: Overlay.overlay
    width: 600
    padding: Theme.space.l
    closePolicy: stage === "recording" || stage === "checking" || stage === "cloning" ? Popup.NoAutoClose
                                                                                         : Popup.CloseOnEscape
    onClosed: if (stage !== "done") cloning.reset()

    Overlay.modal: Rectangle { color: Qt.alpha("#000000", Theme.dark ? 0.55 : 0.35) }
    background: Rectangle {
        radius: Theme.radius.l
        color: Theme.color.surfaceRaised
        border.color: Theme.color.borderStrong
    }

    contentItem: ColumnLayout {
        spacing: Theme.space.m

        Text {
            objectName: "cloneTitle"
            text: dialog.stage === "done" ? "The voice is ready"
                : dialog.stage === "failed" ? "The voice wasn't made"
                : "Copy a voice, with consent"
            font: Theme.type.title
            color: Theme.color.textPrimary
        }

        // Who, and their go-ahead.
        ColumnLayout {
            visible: dialog.stage === "start"
            Layout.fillWidth: true
            spacing: Theme.space.s
            Text {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: "ElevenLabs makes a copy of a voice from a recording, for speech in your videos. Here it is made only "
                      + "from a recording of the speaker reading a consent statement aloud, checked on this Mac first."
                font: Theme.type.body
                color: Theme.color.textSecondary
            }
            GridLayout {
                columns: 2
                Layout.fillWidth: true
                columnSpacing: Theme.space.m
                Text { text: "Speaker's name"; font: Theme.type.label; color: Theme.color.textSecondary }
                TextField { id: speakerField; objectName: "cloneSpeaker"; Layout.fillWidth: true; placeholderText: "As they'll say it" }
                Text { text: "Voice name"; font: Theme.type.label; color: Theme.color.textSecondary }
                TextField { id: nameField; objectName: "cloneVoiceName"; Layout.fillWidth: true; placeholderText: "e.g. My narration voice" }
            }
            CheckBox { id: isSpeaker; objectName: "cloneIsSpeaker"; text: "The speaker is here and will read the statement themselves" }
            CheckBox { id: adult; objectName: "cloneAdult"; text: "The speaker is 18 or older" }
            CheckBox { id: agrees; objectName: "cloneAgrees"; text: "The speaker agrees to the copy, for speech in their own videos" }
            RowLayout {
                visible: dialog.cloning.needsModel
                Layout.fillWidth: true
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: "The recording is checked with a speech model on this Mac (Base · English, about 60 MB)."
                    font: Theme.type.caption
                    color: Theme.color.textSecondary
                }
                TextButton {
                    objectName: "cloneDownloadModel"
                    text: dialog.project.modelDownload.id ? "Downloading…" : "Download"
                    variant: "secondary"
                    enabled: !dialog.project.modelDownload.id
                    onClicked: dialog.project.downloadSpeechModel(dialog.cloning.speechModel)
                }
            }
        }

        // The statement to read.
        Rectangle {
            visible: dialog.stage === "ready" || dialog.stage === "recording" || dialog.stage === "checking"
                     || (dialog.stage === "checked" && !dialog.cloning.check.passed)
            Layout.fillWidth: true
            implicitHeight: statementText.implicitHeight + 2 * Theme.space.m
            radius: Theme.radius.m
            color: Theme.color.surfaceSunken
            border.color: dialog.stage === "recording" ? Theme.color.danger : Theme.color.borderSubtle
            Text {
                id: statementText
                objectName: "cloneStatement"
                anchors { left: parent.left; right: parent.right; top: parent.top; margins: Theme.space.m }
                wrapMode: Text.WordWrap
                text: dialog.cloning.statement
                font.pixelSize: 17
                lineHeight: 1.3
                color: Theme.color.textPrimary
            }
        }
        Text {
            visible: dialog.stage === "ready"
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: dialog.cloning.speaker + " reads all of it aloud, at an easy pace, somewhere quiet."
            font: Theme.type.body
            color: Theme.color.textSecondary
        }
        RowLayout {
            visible: dialog.stage === "recording"
            Layout.fillWidth: true
            Rectangle {
                Layout.preferredWidth: 160
                height: 6
                radius: 3
                color: Theme.color.surfaceSunken
                Rectangle { width: parent.width * Math.min(1, dialog.cloning.level * 1.5); height: parent.height; radius: 3; color: Theme.color.success }
            }
            Text {
                objectName: "cloneSeconds"
                text: "Recording · " + dialog.cloning.seconds.toFixed(0) + " s"
                font: Theme.type.label
                color: Theme.color.danger
            }
        }
        RowLayout {
            visible: dialog.stage === "checking" || dialog.stage === "cloning"
            BusyIndicator { implicitWidth: 20; implicitHeight: 20; running: parent.visible }
            Text {
                text: dialog.stage === "checking" ? "Checking the recording on this Mac…" : (dialog.project.cloud.status || "Copying the voice…")
                font: Theme.type.body
                color: Theme.color.textSecondary
            }
        }

        // The check.
        Text {
            objectName: "cloneCheck"
            visible: dialog.stage === "checked"
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: dialog.cloning.check.passed
                  ? "The recording says the statement (" + Math.round(dialog.cloning.check.match * 100) + "% of its words heard in order). "
                    + "Making the voice sends this recording to ElevenLabs."
                  : (dialog.cloning.check.why || "")
            font: Theme.type.body
            color: dialog.cloning.check.passed ? Theme.color.textPrimary : Theme.color.warning
        }
        Text {
            objectName: "cloneDone"
            visible: dialog.stage === "done"
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: "“" + dialog.cloning.voiceName + "” is in your voices. Its consent recording is kept with it; deleting "
                  + "the voice deletes both."
            font: Theme.type.body
            color: Theme.color.textPrimary
        }
        Text {
            objectName: "cloneError"
            visible: dialog.cloning.error !== ""
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: dialog.cloning.error
            font: Theme.type.body
            color: Theme.color.danger
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: Theme.space.s
            Item { Layout.fillWidth: true }
            TextButton {
                objectName: "cloneCancel"
                text: dialog.stage === "done" ? "Done" : "Cancel"
                variant: dialog.stage === "done" ? "primary" : "ghost"
                enabled: dialog.stage !== "checking" && dialog.stage !== "cloning"
                onClicked: dialog.close()
            }
            TextButton {
                objectName: "cloneContinue"
                visible: dialog.stage === "start"
                text: "Continue"
                variant: "primary"
                enabled: speakerField.text.trim() !== "" && isSpeaker.checked && adult.checked && agrees.checked
                         && !dialog.cloning.needsModel && dialog.project.cloud.hasElevenLabsKey
                onClicked: dialog.cloning.begin(speakerField.text, nameField.text, isSpeaker.checked, adult.checked, agrees.checked)
            }
            TextButton {
                objectName: "cloneRecord"
                visible: dialog.stage === "ready" || (dialog.stage === "checked" && !dialog.cloning.check.passed)
                text: dialog.stage === "ready" ? "Start recording" : "Record again"
                iconName: "record"
                variant: "primary"
                onClicked: dialog.cloning.startRecording()
            }
            TextButton {
                objectName: "cloneStop"
                visible: dialog.stage === "recording"
                text: "Stop"
                variant: "primary"
                onClicked: dialog.cloning.stopRecording()
            }
            TextButton {
                objectName: "cloneMake"
                visible: dialog.stage === "checked" && dialog.cloning.check.passed
                text: "Make the voice"
                variant: "primary"
                onClicked: dialog.cloning.makeVoice()
            }
            TextButton {
                objectName: "cloneStartAgain"
                visible: dialog.stage === "failed"
                text: "Start again"
                variant: "secondary"
                onClicked: dialog.cloning.reset()
            }
        }
    }
}
