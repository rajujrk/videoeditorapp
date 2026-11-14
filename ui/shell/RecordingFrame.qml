import QtQuick
import QtQuick.Window
import TechieVideoEditor.Theme

// What is being recorded, marked on the screen: a red frame just outside the area (the whole
// display, a window where it was when recording began, or the chosen part). It takes no clicks
// and is left out of the recording.
Window {
    id: frame
    required property var area // {x, y, width, height}: global points
    readonly property real outside: 4 // the frame's thickness, drawn round the area

    objectName: "recordingFrame"
    title: qsTr("Recording")
    flags: Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.Tool | Qt.NoDropShadowWindowHint
           | Qt.WindowTransparentForInput | Qt.WindowDoesNotAcceptFocus
    color: "transparent"
    // (A display's frame sits just inside it: there is nothing outside to draw on.)
    readonly property bool inside: (area.x || 0) - outside < 0 || (area.y || 0) - outside < 0
    x: (area.x || 0) - (inside ? 0 : outside)
    y: (area.y || 0) - (inside ? 0 : outside)
    width: (area.width || 1280) + (inside ? 0 : 2 * outside)
    height: (area.height || 720) + (inside ? 0 : 2 * outside)
    visible: true

    Rectangle {
        anchors.fill: parent
        color: "transparent"
        border.color: Theme.color.danger
        border.width: frame.outside - 1
        radius: 2
        // A slow pulse: it is live.
        SequentialAnimation on opacity {
            loops: Animation.Infinite
            NumberAnimation { to: 0.55; duration: 900; easing.type: Easing.InOutSine }
            NumberAnimation { to: 1; duration: 900; easing.type: Easing.InOutSine }
        }
    }
}
