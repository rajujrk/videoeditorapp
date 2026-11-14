import QtQuick
import QtQuick.Window
import TechieVideoEditor.Theme

// The seconds before a recording starts, counted down large in the middle of what will be
// recorded; it is gone before the first frame. Takes no clicks (Esc, or Cancel in the
// recorder, stops it).
Window {
    id: countdown
    required property var area // {x, y, width, height}: global points
    property int remaining: 0 // seconds left
    property int total: 3

    objectName: "recordCountdown"
    title: qsTr("Recording starts")
    flags: Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.Tool | Qt.NoDropShadowWindowHint
           | Qt.WindowTransparentForInput | Qt.WindowDoesNotAcceptFocus
    color: "transparent"
    readonly property real side: 220
    x: (area.x || 0) + ((area.width || 1280) - side) / 2
    y: (area.y || 0) + ((area.height || 720) - side) / 2
    width: side
    height: side
    visible: true

    Rectangle {
        anchors.fill: parent
        radius: width / 2
        color: "#B3000000"
        // The second running out, round the edge.
        Canvas {
            id: ring
            objectName: "countdownRing"
            anchors.fill: parent
            property real progress: 0 // of this second, 0..1
            onProgressChanged: requestPaint()
            onPaint: {
                const ctx = getContext("2d")
                ctx.reset()
                ctx.lineWidth = 6
                ctx.strokeStyle = "#40FFFFFF"
                ctx.beginPath()
                ctx.arc(width / 2, height / 2, width / 2 - 8, 0, 2 * Math.PI)
                ctx.stroke()
                ctx.strokeStyle = String(Theme.color.danger)
                ctx.lineCap = "round"
                ctx.beginPath()
                ctx.arc(width / 2, height / 2, width / 2 - 8, -Math.PI / 2, -Math.PI / 2 + 2 * Math.PI * (1 - progress))
                ctx.stroke()
            }
            NumberAnimation on progress {
                id: tick
                from: 0
                to: 1
                duration: 1000
                running: countdown.remaining > 0
                loops: Animation.Infinite
            }
        }
        Text {
            objectName: "countdownNumber"
            anchors.centerIn: parent
            text: String(countdown.remaining)
            font.pixelSize: 110
            font.weight: Font.DemiBold
            font.family: Theme.type.title.family
            color: "white" // always on the dark disc, in both themes
            // Each new second lands with a small pop.
            onTextChanged: pop.restart()
            NumberAnimation on scale { id: pop; from: 1.25; to: 1; duration: 260; easing.type: Easing.OutCubic }
        }
    }
}
