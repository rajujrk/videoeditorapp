import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import TechieVideoEditor.Theme

// Record the screen and/or a camera (and the microphone): pick what to record, then record
// from the playhead. Stopping puts each recording on a track of its own there — the camera
// small over the screen — as one undo step.
Popup {
    id: panel
    required property var project

    property var screens: []
    property var cameras: []
    property int screenIndex: 0   // -1: no screen
    property int cameraIndex: -1  // -1: no camera
    property bool systemSound: true
    property bool microphone: true

    width: 380
    padding: Theme.space.l
    closePolicy: project.screenRecording ? Popup.NoAutoClose : Popup.CloseOnEscape

    function refresh() {
        screens = project.listScreenTargets()
        cameras = project.listCameras()
        if (screenIndex >= screens.length)
            screenIndex = screens.length > 0 ? 0 : -1
        if (cameraIndex >= cameras.length)
            cameraIndex = -1
        // (A combo box goes back to its first entry when its list changes: set them again.)
        screenChoice.currentIndex = screenIndex + 1
        cameraChoice.currentIndex = cameraIndex + 1
    }
    onAboutToShow: refresh()

    function record() {
        const options = { systemSound: panel.systemSound, microphone: panel.microphone }
        if (screenIndex >= 0 && screenIndex < screens.length)
            options.screen = screens[screenIndex]
        if (cameraIndex >= 0 && cameraIndex < cameras.length)
            options.camera = cameras[cameraIndex].id
        project.startScreenRecording(options)
    }

    background: Rectangle {
        radius: Theme.radius.l
        color: Theme.color.surfaceRaised
        border.color: Theme.color.borderStrong
    }

    contentItem: ColumnLayout {
        spacing: Theme.space.m

        Text { text: "Record screen and camera"; font: Theme.type.title; color: Theme.color.textPrimary }

        // Permissions
        ColumnLayout {
            visible: panel.project.screenAccess !== "granted"
            Layout.fillWidth: true
            Text {
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                text: "To record the screen, allow TechieVideoEditor in System Settings › Privacy & Security › Screen & System Audio Recording."
                font: Theme.type.body
                color: Theme.color.textSecondary
                wrapMode: Text.WordWrap
            }
            TextButton {
                objectName: "allowScreen"
                text: "Allow screen recording"
                variant: "secondary"
                onClicked: { panel.project.requestScreenAccess(); panel.refresh() }
            }
        }

        Text { text: "Screen"; font: Theme.type.label; color: Theme.color.textSecondary }
        ComboBox {
            id: screenChoice
            objectName: "screenChoice"
            Layout.fillWidth: true
            enabled: !panel.project.screenRecording
            model: ["No screen"].concat(panel.screens.map((s) => s.title))
            onActivated: (index) => panel.screenIndex = index - 1
        }
        ToggleSwitch {
            objectName: "systemSound"
            text: "System sound"
            enabled: panel.screenIndex >= 0 && !panel.project.screenRecording
            checked: panel.systemSound
            onToggled: panel.systemSound = checked
        }

        Text { text: "Camera"; font: Theme.type.label; color: Theme.color.textSecondary }
        RowLayout {
            Layout.fillWidth: true
            ComboBox {
                id: cameraChoice
                objectName: "cameraChoice"
                Layout.fillWidth: true
                enabled: !panel.project.screenRecording
                model: ["No camera"].concat(panel.cameras.map((c) => c.name))
                onActivated: (index) => {
                    panel.cameraIndex = index - 1
                    if (panel.cameraIndex >= 0 && panel.project.cameraAccess !== "granted")
                        panel.project.requestCameraAccess()
                }
            }
        }
        Text {
            visible: panel.cameraIndex >= 0 && panel.screenIndex >= 0
            Layout.fillWidth: true
            Layout.preferredWidth: 1
            text: "The camera goes small in the corner over the screen (picture in picture)."
            font: Theme.type.body
            color: Theme.color.textSecondary
            wrapMode: Text.WordWrap
        }
        ToggleSwitch {
            objectName: "recordMicrophone"
            text: "Microphone" + (panel.project.inputDevice ? " (" + panel.project.inputDevice + ")" : "")
            enabled: !panel.project.screenRecording
            checked: panel.microphone
            onToggled: panel.microphone = checked
        }

        RowLayout {
            Layout.fillWidth: true
            Text {
                objectName: "screenRecordingTime"
                Layout.fillWidth: true
                visible: panel.project.screenRecording
                text: "● " + Math.floor(panel.project.screenRecordingTime / 60) + ":"
                      + String(Math.floor(panel.project.screenRecordingTime % 60)).padStart(2, "0")
                font: Theme.type.mono
                color: Theme.color.danger
            }
            Item { Layout.fillWidth: true; visible: !panel.project.screenRecording }
            TextButton {
                objectName: "startScreenRecording"
                visible: !panel.project.screenRecording
                text: "Record"
                variant: "primary"
                enabled: panel.screenIndex >= 0 || panel.cameraIndex >= 0
                onClicked: panel.record()
            }
            TextButton {
                objectName: "stopScreenRecording"
                visible: panel.project.screenRecording
                text: "Stop"
                variant: "primary"
                onClicked: {
                    panel.project.stopScreenRecording()
                    panel.close()
                }
            }
        }
    }
}
