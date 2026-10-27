import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import TechieVideoEditor.Theme

// Record a voice-over: pick the microphone, watch its level, then record from the playhead
// while the timeline plays. Stopping (here, or with Space) puts the take on a free audio
// track. The microphone is open only while this panel is.
Popup {
    id: panel
    required property var project

    // "idle" → "countdown" → "recording" → "idle"
    readonly property string stage: project.recording ? "recording" : countdown.running ? "countdown" : "idle"
    property bool countIn: true
    property int count: 0

    width: 340
    padding: Theme.space.l
    closePolicy: stage === "idle" ? Popup.CloseOnEscape : Popup.NoAutoClose

    onAboutToShow: project.openVoiceOver()
    onClosed: {
        countdown.stop()
        project.closeVoiceOver()
    }

    function record() {
        if (!countIn) {
            project.startRecording()
            return
        }
        count = 3
        countdown.start()
    }

    Timer {
        id: countdown
        interval: 1000
        repeat: true
        onTriggered: {
            panel.count -= 1
            if (panel.count > 0)
                return
            stop()
            panel.project.startRecording()
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
            spacing: Theme.space.s
            Icon { name: "mic"; size: Theme.size.icon; color: Theme.color.textPrimary }
            Text {
                Layout.fillWidth: true
                text: "Record voice-over"
                font: Theme.type.title
                color: Theme.color.textPrimary
            }
            IconButton {
                objectName: "voiceOverClose"
                iconName: "plus"
                rotation: 45
                tip: panel.stage === "idle" ? "Close (Esc)" : "Stop recording first"
                enabled: panel.stage === "idle"
                onClicked: panel.close()
            }
        }

        // No access: say how to give it.
        ColumnLayout {
            visible: panel.project.microphoneAccess !== "granted"
            Layout.fillWidth: true
            spacing: Theme.space.s
            Text {
                objectName: "microphoneAccessMessage"
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: panel.project.microphoneAccess === "denied"
                      ? "TechieVideoEditor isn't allowed to use the microphone. Turn it on in System Settings › Privacy & Security › Microphone, then open this panel again."
                      : "Waiting for permission to use the microphone…"
                font: Theme.type.body
                color: Theme.color.textSecondary
            }
            TextButton {
                visible: panel.project.microphoneAccess === "denied"
                text: "Open Privacy Settings"
                variant: "secondary"
                onClicked: panel.project.openMicrophoneSettings()
            }
        }

        GridLayout {
            visible: panel.project.voiceOverActive
            Layout.fillWidth: true
            columns: 2
            columnSpacing: Theme.space.m
            rowSpacing: Theme.space.s

            Text { text: "Microphone"; font: Theme.type.label; color: Theme.color.textSecondary }
            ComboBox {
                id: devicePicker
                objectName: "inputDevicePicker"
                Layout.fillWidth: true
                enabled: panel.stage === "idle"
                model: panel.project.inputDevices
                currentIndex: Math.max(0, panel.project.inputDevices.indexOf(panel.project.inputDevice))
                onActivated: (index) => panel.project.inputDevice = panel.project.inputDevices[index]
            }

            Text { text: "Level"; font: Theme.type.label; color: Theme.color.textSecondary }
            Rectangle {
                objectName: "inputLevelMeter"
                Layout.fillWidth: true
                implicitHeight: 8
                radius: 4
                color: Theme.color.surfaceSunken
                Rectangle {
                    // Level in dB from -60 to 0, so speech fills a useful part of the bar.
                    readonly property real db: panel.project.inputLevel > 0
                                               ? 20 * Math.log(panel.project.inputLevel) / Math.LN10 : -60
                    width: parent.width * Math.max(0, Math.min(1, (db + 60) / 60))
                    height: parent.height
                    radius: 4
                    color: db > -1 ? Theme.color.danger : db > -9 ? Theme.color.warning : Theme.color.success
                }
            }
        }

        ColumnLayout {
            visible: panel.project.voiceOverActive
            Layout.fillWidth: true
            spacing: Theme.space.xs
            ToggleSwitch {
                objectName: "muteWhileRecording"
                text: "Mute the timeline while recording"
                enabled: panel.stage !== "recording"
                checked: panel.project.muteWhileRecording
                onToggled: panel.project.muteWhileRecording = checked
            }
            ToggleSwitch {
                objectName: "countIn"
                text: "Count down 3 seconds first"
                enabled: panel.stage === "idle"
                checked: panel.countIn
                onToggled: panel.countIn = checked
            }
        }

        Divider { visible: panel.project.voiceOverActive; Layout.fillWidth: true }

        RowLayout {
            visible: panel.project.voiceOverActive
            Layout.fillWidth: true
            spacing: Theme.space.m

            // Round record button: a red dot to start, a red square to stop.
            AbstractButton {
                id: recordButton
                objectName: "recordButton"
                implicitWidth: 48
                implicitHeight: 48
                hoverEnabled: true
                onClicked: panel.stage === "recording" ? panel.project.stopRecording()
                         : panel.stage === "countdown" ? countdown.stop()
                         : panel.record()

                ToolTip {
                    text: panel.stage === "recording" ? "Stop and add to the timeline (Space)"
                        : panel.stage === "countdown" ? "Cancel" : "Record from the playhead"
                    visible: recordButton.hovered
                    delay: 600
                }
                background: Rectangle {
                    radius: width / 2
                    color: recordButton.pressed ? Theme.color.borderStrong
                         : recordButton.hovered ? Theme.color.surfaceHover : Theme.color.surface
                    border.width: 2
                    border.color: Theme.color.borderStrong
                }
                contentItem: Item {
                    Rectangle {
                        anchors.centerIn: parent
                        width: panel.stage === "recording" ? 18 : 22
                        height: width
                        radius: panel.stage === "recording" ? 3 : width / 2
                        color: Theme.color.danger
                        visible: panel.stage !== "countdown"
                    }
                    Text {
                        anchors.centerIn: parent
                        visible: panel.stage === "countdown"
                        text: panel.count
                        font: Theme.type.title
                        color: Theme.color.danger
                    }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                RowLayout {
                    spacing: Theme.space.xs
                    Rectangle {
                        visible: panel.stage === "recording"
                        implicitWidth: 8
                        implicitHeight: 8
                        radius: 4
                        color: Theme.color.danger
                        SequentialAnimation on opacity {
                            running: panel.stage === "recording"
                            loops: Animation.Infinite
                            NumberAnimation { to: 0.25; duration: 600 }
                            NumberAnimation { to: 1; duration: 600 }
                        }
                    }
                    Text {
                        objectName: "recordingStatus"
                        text: panel.stage === "recording" ? "Recording"
                            : panel.stage === "countdown" ? "Starting in " + panel.count + "…" : "Ready"
                        font: Theme.type.heading
                        color: panel.stage === "recording" ? Theme.color.danger : Theme.color.textPrimary
                    }
                }
                Text {
                    objectName: "recordingTime"
                    text: panel.stage === "recording"
                          ? panel.project.timecode(panel.project.recordingTime).substring(0, 8)
                            + " from " + panel.project.timecode(panel.project.recordingStart).substring(0, 8)
                          : "From the playhead at " + panel.project.timecode(panel.project.playhead).substring(0, 8)
                    font: Theme.type.mono
                    color: Theme.color.textSecondary
                }
            }
        }

        Text {
            visible: panel.project.voiceOverActive
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: "The take goes on the first audio track with room for it, or a new Voice-over track."
            font: Theme.type.caption
            color: Theme.color.textSecondary
        }
    }
}
