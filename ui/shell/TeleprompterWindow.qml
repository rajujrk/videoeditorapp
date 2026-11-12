import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import TechieVideoEditor.Theme

// The teleprompter: the script in large type scrolling up past a reading line, in a window of
// its own that stays on top and is left out of screen recordings. It starts from the top when
// a recording starts (when set to) and stops with it; Space plays or pauses, ↑ and ↓ change
// the speed, Home goes back to the top. Edit switches to typing the script.
Window {
    id: prompter
    required property var project
    readonly property var settings: project.teleprompter
    property bool playing: false
    property bool editing: false
    readonly property bool recording: project.recording || project.screenRecording
    readonly property real readingLine: script.height * 0.33
    objectName: "teleprompter"
    title: qsTr("Teleprompter")
    width: 760
    height: 460
    minimumWidth: 420
    minimumHeight: 260
    flags: Qt.Window | Qt.WindowStaysOnTopHint
    color: "#0B0C10"
    visible: true

    Component.onCompleted: project.keepOutOfRecordings(prompter, true)
    Component.onDestruction: project.keepOutOfRecordings(prompter, false)
    onRecordingChanged: {
        if (!settings.followRecording || editing)
            return
        if (recording)
            restart()
        playing = recording
    }
    function restart() { scroller.contentY = 0 }
    function faster(step) { settings.speed = Math.round((settings.speed + step) * 2) / 2 }

    Shortcut { objectName: "teleprompterKey:space"; sequence: "Space"; enabled: !prompter.editing; onActivated: prompter.playing = !prompter.playing }
    Shortcut { objectName: "teleprompterKey:up"; sequence: "Up"; enabled: !prompter.editing; onActivated: prompter.faster(0.5) }
    Shortcut { objectName: "teleprompterKey:down"; sequence: "Down"; enabled: !prompter.editing; onActivated: prompter.faster(-0.5) }
    Shortcut { objectName: "teleprompterKey:home"; sequence: "Home"; enabled: !prompter.editing; onActivated: prompter.restart() }

    // Scrolling: the speed's pixels a second, frame by frame, stopping at the end.
    FrameAnimation {
        running: prompter.playing && !prompter.editing && prompter.visible
        onTriggered: {
            const end = Math.max(0, scroller.contentHeight - prompter.readingLine)
            scroller.contentY = Math.min(scroller.contentY + prompter.settings.pixelsPerSecond * frameTime, end)
            if (scroller.contentY >= end)
                prompter.playing = false
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Controls
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: bar.implicitHeight + Theme.space.s * 2
            color: Theme.color.surface
            RowLayout {
                id: bar
                anchors { fill: parent; margins: Theme.space.s }
                spacing: Theme.space.s
                IconButton {
                    objectName: "teleprompterPlay"
                    iconName: prompter.playing ? "pause" : "play"
                    tip: prompter.playing ? qsTr("Pause (Space)") : qsTr("Scroll (Space)")
                    enabled: !prompter.editing
                    onClicked: prompter.playing = !prompter.playing
                }
                IconButton {
                    objectName: "teleprompterRestart"
                    iconName: "skip-back"
                    tip: qsTr("Back to the top (Home)")
                    enabled: !prompter.editing
                    onClicked: prompter.restart()
                }
                Text { text: qsTr("Speed"); font: Theme.type.label; color: Theme.color.textSecondary }
                Slider {
                    objectName: "teleprompterSpeed"
                    Layout.preferredWidth: 110
                    from: 1
                    to: 10
                    stepSize: 0.5
                    value: prompter.settings.speed
                    onMoved: prompter.settings.speed = value
                }
                Text { text: qsTr("Size"); font: Theme.type.label; color: Theme.color.textSecondary }
                Slider {
                    objectName: "teleprompterSize"
                    Layout.preferredWidth: 90
                    from: 24
                    to: 120
                    stepSize: 2
                    value: prompter.settings.fontSize
                    onMoved: prompter.settings.fontSize = value
                }
                Item { Layout.fillWidth: true }
                ToggleSwitch {
                    objectName: "teleprompterMirror"
                    text: qsTr("Mirror")
                    checked: prompter.settings.mirrored
                    onToggled: prompter.settings.mirrored = checked
                }
                ToggleSwitch {
                    objectName: "teleprompterFollow"
                    text: qsTr("With recording")
                    checked: prompter.settings.followRecording
                    onToggled: prompter.settings.followRecording = checked
                    ToolTip.visible: hovered
                    ToolTip.delay: 600
                    ToolTip.text: qsTr("Start from the top when a recording starts, and stop when it stops")
                }
                TextButton {
                    objectName: "teleprompterEdit"
                    text: prompter.editing ? qsTr("Done") : qsTr("Edit")
                    variant: prompter.editing ? "primary" : "secondary"
                    onClicked: {
                        if (!prompter.editing)
                            prompter.playing = false
                        else
                            prompter.settings.text = editor.text
                        prompter.editing = !prompter.editing
                    }
                }
            }
        }

        // The script, scrolling (or being edited)
        Item {
            id: script
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true

            Flickable {
                id: scroller
                objectName: "teleprompterScroll"
                anchors.fill: parent
                visible: !prompter.editing
                contentWidth: width
                contentHeight: words.height + prompter.readingLine + height
                boundsBehavior: Flickable.StopAtBounds
                interactive: !prompter.playing
                transform: Scale {
                    origin.x: scroller.width / 2
                    xScale: prompter.settings.mirrored ? -1 : 1
                }
                Text {
                    id: words
                    objectName: "teleprompterWords"
                    y: prompter.readingLine
                    x: width * 0.08
                    width: scroller.width * 0.84
                    text: prompter.settings.text
                    wrapMode: Text.WordWrap
                    color: "#F4F4F0"
                    lineHeight: 1.3
                    font.family: Theme.type.body.family
                    font.pixelSize: prompter.settings.fontSize
                    font.weight: Font.Medium
                }
            }
            // The reading line: arrows at both edges.
            Repeater {
                model: [false, true]
                delegate: Canvas {
                    required property bool modelData
                    visible: !prompter.editing
                    width: 18
                    height: 24
                    x: modelData ? script.width - width - 6 : 6
                    y: prompter.readingLine + prompter.settings.fontSize * 0.65 - height / 2
                    onPaint: {
                        const c = getContext("2d")
                        c.reset()
                        c.fillStyle = "#F5B83D"
                        c.beginPath()
                        if (modelData) { c.moveTo(width, 0); c.lineTo(0, height / 2); c.lineTo(width, height) }
                        else { c.moveTo(0, 0); c.lineTo(width, height / 2); c.lineTo(0, height) }
                        c.closePath()
                        c.fill()
                    }
                }
            }
            ScrollView {
                anchors { fill: parent; margins: Theme.space.m }
                visible: prompter.editing
                TextArea {
                    id: editor
                    objectName: "teleprompterText"
                    text: prompter.settings.text
                    wrapMode: TextEdit.Wrap
                    color: "#F4F4F0"
                    font.family: Theme.type.body.family
                    font.pixelSize: 20
                    placeholderText: qsTr("Type or paste your script")
                    background: Rectangle { color: "#16181F"; radius: Theme.radius.s }
                }
            }
        }
    }
}
