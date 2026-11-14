import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import TechieVideoEditor.Theme

// Record the screen and/or a camera (and the microphone): pick what to record — the entire
// screen, a window, or an area drawn and sized on the display — then record from the playhead,
// a red frame marking what is recorded (left out of the recording). Stopping puts each
// recording on a track of its own there — the camera small over the screen — as one undo step.
Popup {
    id: panel
    required property var project

    property var screens: []
    property var cameras: []
    // What of the screen: "display" (all of one), "window" (one window), "area" (part of a
    // display, chosen and sized on it).
    property string mode: "display"
    property int screenIndex: 0   // into screens; -1: no screen
    // The part of the display chosen for "area" (global points), or null.
    property var area: null
    // The screens the choice lists: displays, or windows.
    readonly property var choices: screens.map((s, i) => ({ index: i, target: s }))
                                          .filter((c) => (c.target.kind === "window") === (mode === "window"))
    readonly property var chosen: screenIndex >= 0 && screenIndex < screens.length ? screens[screenIndex] : null
    property int cameraIndex: -1  // -1: no camera
    property string cameraBackground: "" // "", "blur" or "remove"
    property bool systemSound: true
    property bool microphone: true
    signal teleprompterRequested() // open the teleprompter (its own window)
    // Draw on the screen being recorded: asked for with the recording, or while it runs.
    property bool drawOnScreen: false
    signal drawingRequested(var area)
    // The area being recorded (global points), for the drawing layer.
    property var recordedArea: null

    width: 380
    padding: Theme.space.l
    closePolicy: project.screenRecording ? Popup.NoAutoClose : Popup.CloseOnEscape

    function refresh() {
        screens = project.listScreenTargets()
        cameras = project.listCameras()
        if (screenIndex >= screens.length || (chosen && (chosen.kind === "window") !== (mode === "window")))
            screenIndex = choices.length > 0 ? choices[0].index : -1
        if (cameraIndex >= cameras.length)
            cameraIndex = -1
        // (A combo box goes back to its first entry when its list changes: set them again.)
        syncChoice()
        cameraChoice.currentIndex = cameraIndex + 1
    }
    function syncChoice() {
        screenChoice.currentIndex = screenIndex < 0 ? 0 : choices.findIndex((c) => c.index === screenIndex) + 1
    }
    onAboutToShow: refresh()

    // Another kind of screen recording: the first of its kind chosen (an area is drawn next).
    function setMode(kind) {
        if (mode === kind)
            return
        mode = kind
        area = null
        screenIndex = choices.length > 0 ? choices[0].index : -1
        syncChoice()
        if (kind === "area" && screenIndex >= 0)
            chooseArea()
    }
    function chooseArea() {
        if (!chosen || chosen.kind === "window")
            return
        pickerLoader.active = true
        pickerLoader.item.start(area)
    }

    // The area recorded (global points): the chosen part, else the display or window.
    function areaOf(screen) {
        if (mode === "area" && area)
            return area
        return { x: screen.x || 0, y: screen.y || 0, width: screen.width, height: screen.height }
    }
    function record() {
        const options = { systemSound: panel.systemSound, microphone: panel.microphone }
        if (chosen) {
            options.screen = chosen
            if (mode === "area" && area)
                options.region = { x: area.x - (chosen.x || 0), y: area.y - (chosen.y || 0), width: area.width, height: area.height }
        }
        if (cameraIndex >= 0 && cameraIndex < cameras.length) {
            options.camera = cameras[cameraIndex].id
            options.cameraBackground = cameraBackground
        }
        // The frame marking it goes up first, so the recording knows to leave it out.
        const marked = options.screen ? areaOf(options.screen) : null
        if (marked) {
            frameLoader.area = marked
            frameLoader.active = true
            project.keepOutOfRecordings(frameLoader.item, true)
        }
        if (!project.startScreenRecording(options)) {
            unmark()
            return
        }
        recordedArea = marked
        if (drawOnScreen && recordedArea)
            drawingRequested(recordedArea)
    }
    // The frame taken down (and no longer left out of recordings).
    function unmark() {
        if (frameLoader.item) {
            project.keepOutOfRecordings(frameLoader.item, false)
            frameLoader.item.visible = false
        }
        frameLoader.active = false
    }
    Connections {
        target: panel.project
        function onScreenRecordingChanged() {
            if (!panel.project.screenRecording)
                panel.unmark()
        }
    }
    // The area being chosen, over its display.
    Loader {
        id: pickerLoader
        active: false
        sourceComponent: ScreenAreaPicker {
            display: panel.chosen || ({ x: 0, y: 0, width: 1280, height: 720 })
            onAccepted: (chosenArea) => { panel.area = chosenArea; pickerLoader.active = false }
            onCanceled: pickerLoader.active = false
        }
    }
    readonly property var areaPicker: pickerLoader.item
    // What is being recorded, marked on the screen.
    Loader {
        id: frameLoader
        property var area: ({})
        active: false
        sourceComponent: RecordingFrame { area: frameLoader.area }
    }
    readonly property var recordingFrame: frameLoader.item

    background: Rectangle {
        radius: Theme.radius.l
        color: Theme.color.surfaceRaised
        border.color: Theme.color.borderStrong
    }

    contentItem: ColumnLayout {
        spacing: Theme.space.m

        Text { text: qsTr("Record screen and camera"); font: Theme.type.title; color: Theme.color.textPrimary }

        // Permissions
        ColumnLayout {
            visible: panel.project.screenAccess !== "granted"
            Layout.fillWidth: true
            Text {
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                text: qsTr("To record the screen, allow TechieVideoEditor in System Settings › Privacy & Security › Screen & System Audio Recording.")
                font: Theme.type.body
                color: Theme.color.textSecondary
                wrapMode: Text.WordWrap
            }
            TextButton {
                objectName: "allowScreen"
                text: qsTr("Allow screen recording")
                variant: "secondary"
                onClicked: { panel.project.requestScreenAccess(); panel.refresh() }
            }
        }

        Text { text: qsTr("Screen"); font: Theme.type.label; color: Theme.color.textSecondary }
        // The whole display, one window, or part of a display.
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.space.xs
            Repeater {
                model: [{ id: "display", label: qsTr("Entire screen"), icon: "display" },
                        { id: "window", label: qsTr("A window"), icon: "window" },
                        { id: "area", label: qsTr("An area"), icon: "region" }]
                delegate: TextButton {
                    required property var modelData
                    objectName: "recordMode:" + modelData.id
                    Layout.fillWidth: true
                    text: modelData.label
                    iconName: modelData.icon
                    variant: panel.mode === modelData.id ? "primary" : "secondary"
                    enabled: !panel.project.screenRecording
                    onClicked: panel.setMode(modelData.id)
                }
            }
        }
        ComboBox {
            id: screenChoice
            objectName: "screenChoice"
            Layout.fillWidth: true
            enabled: !panel.project.screenRecording
            model: [qsTr("No screen")].concat(panel.choices.map((c) => c.target.title))
            onActivated: (index) => {
                panel.screenIndex = index === 0 ? -1 : panel.choices[index - 1].index
                if (panel.mode === "area") {
                    panel.area = null
                    if (panel.screenIndex >= 0)
                        panel.chooseArea()
                }
            }
        }
        // The area chosen: its size, and changing it.
        RowLayout {
            visible: panel.mode === "area" && panel.screenIndex >= 0
            Layout.fillWidth: true
            Text {
                objectName: "chosenArea"
                Layout.fillWidth: true
                text: panel.area ? qsTr("%1 × %2 points of the display").arg(Math.round(panel.area.width)).arg(Math.round(panel.area.height))
                                 : qsTr("No area chosen yet")
                font: Theme.type.body
                color: panel.area ? Theme.color.textPrimary : Theme.color.textSecondary
            }
            TextButton {
                objectName: "chooseArea"
                text: panel.area ? qsTr("Change…") : qsTr("Choose…")
                iconName: "region"
                variant: "secondary"
                enabled: !panel.project.screenRecording
                onClicked: panel.chooseArea()
            }
        }
        ToggleSwitch {
            objectName: "systemSound"
            text: qsTr("System sound")
            enabled: panel.screenIndex >= 0 && !panel.project.screenRecording
            checked: panel.systemSound
            onToggled: panel.systemSound = checked
        }

        ToggleSwitch {
            objectName: "drawOnScreen"
            text: qsTr("Draw on the screen while recording")
            enabled: panel.screenIndex >= 0 && !panel.project.screenRecording
            checked: panel.drawOnScreen
            onToggled: panel.drawOnScreen = checked
        }
        Text { text: qsTr("Camera"); font: Theme.type.label; color: Theme.color.textSecondary }
        RowLayout {
            Layout.fillWidth: true
            ComboBox {
                id: cameraChoice
                objectName: "cameraChoice"
                Layout.fillWidth: true
                enabled: !panel.project.screenRecording
                model: [qsTr("No camera")].concat(panel.cameras.map((c) => c.name))
                onActivated: (index) => {
                    panel.cameraIndex = index - 1
                    if (panel.cameraIndex >= 0 && panel.project.cameraAccess !== "granted")
                        panel.project.requestCameraAccess()
                }
            }
        }
        // The camera's background, done afterwards on its clip (Vision, on this Mac).
        RowLayout {
            visible: panel.cameraIndex >= 0
            Layout.fillWidth: true
            Text { text: qsTr("Background"); font: Theme.type.label; color: Theme.color.textSecondary }
            Item { Layout.fillWidth: true }
            Repeater {
                model: [{ id: "", label: qsTr("As it is") }, { id: "blur", label: qsTr("Blurred") },
                        { id: "remove", label: qsTr("Removed") }]
                delegate: TextButton {
                    required property var modelData
                    objectName: "cameraBackground:" + (modelData.id || "keep")
                    text: modelData.label
                    variant: panel.cameraBackground === modelData.id ? "primary" : "secondary"
                    enabled: !panel.project.screenRecording
                    onClicked: panel.cameraBackground = modelData.id
                }
            }
        }
        Text {
            visible: panel.cameraIndex >= 0 && panel.screenIndex >= 0
            Layout.fillWidth: true
            Layout.preferredWidth: 1
            text: qsTr("The camera goes small in the corner over the screen (picture in picture).")
            font: Theme.type.body
            color: Theme.color.textSecondary
            wrapMode: Text.WordWrap
        }
        TextButton {
            objectName: "openTeleprompter"
            text: qsTr("Teleprompter…")
            iconName: "transcript"
            variant: "ghost"
            tip: qsTr("Your script, scrolling while you record, in a window left out of the recording")
            onClicked: panel.teleprompterRequested()
        }
        ToggleSwitch {
            objectName: "recordMicrophone"
            text: panel.project.inputDevice ? qsTr("Microphone (%1)").arg(panel.project.inputDevice) : qsTr("Microphone")
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
                text: qsTr("Record")
                variant: "primary"
                enabled: (panel.screenIndex >= 0 && (panel.mode !== "area" || panel.area !== null)) || (panel.screenIndex < 0 && panel.cameraIndex >= 0)
                onClicked: panel.record()
            }
            TextButton {
                objectName: "drawDuringRecording"
                visible: panel.project.screenRecording && panel.recordedArea !== null
                text: qsTr("Draw")
                iconName: "draw"
                variant: "secondary"
                onClicked: panel.drawingRequested(panel.recordedArea)
            }
            TextButton {
                objectName: "stopScreenRecording"
                visible: panel.project.screenRecording
                text: qsTr("Stop")
                variant: "primary"
                onClicked: {
                    panel.project.stopScreenRecording()
                    panel.close()
                }
            }
        }
    }
}
