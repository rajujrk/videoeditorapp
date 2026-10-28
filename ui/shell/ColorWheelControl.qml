import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import TechieVideoEditor.Theme

// One colour wheel: drag the puck towards a hue to push the colour that way (further out is
// stronger; double-click the wheel to centre it), and the slider below for all channels at
// once. Hue runs counter-clockwise from red on the right, as the grade reads it.
ColumnLayout {
    id: control
    property string title
    property var wheel: ({ x: 0, y: 0, master: 0 })
    signal moved(var wheel)     // while dragging (preview)
    signal committed(var wheel) // on release (one undo step)

    spacing: Theme.space.xxs
    property var live: wheel
    onWheelChanged: live = wheel

    Text {
        Layout.alignment: Qt.AlignHCenter
        text: control.title
        font: Theme.type.label
        color: Theme.color.textSecondary
    }

    Item {
        id: disk
        objectName: control.objectName + ":disk"
        Layout.alignment: Qt.AlignHCenter
        implicitWidth: 104
        implicitHeight: 104
        readonly property real radius: width / 2 - 4

        Canvas {
            id: canvas
            // Theme colours are read while painting: repaint when they change.
            Connections { target: Theme; function onDarkChanged() { canvas.requestPaint() } }
            anchors.fill: parent
            onPaint: {
                const ctx = getContext("2d")
                ctx.reset()
                const cx = width / 2, cy = height / 2, r = disk.radius
                for (let i = 0; i < 72; ++i) { // hue wedges
                    const a0 = i / 72 * 2 * Math.PI, a1 = (i + 1.2) / 72 * 2 * Math.PI
                    ctx.fillStyle = Qt.hsva(i / 72, 0.75, 0.9, 1)
                    ctx.beginPath()
                    ctx.moveTo(cx, cy)
                    ctx.arc(cx, cy, r, -a1, -a0) // screen y runs down: negate for counter-clockwise
                    ctx.closePath()
                    ctx.fill()
                }
                const fade = ctx.createRadialGradient(cx, cy, 0, cx, cy, r)
                fade.addColorStop(0, Theme.color.surfaceSunken)
                fade.addColorStop(1, Qt.alpha(Theme.color.surfaceSunken, 0))
                ctx.fillStyle = fade
                ctx.beginPath()
                ctx.arc(cx, cy, r, 0, 2 * Math.PI)
                ctx.fill()
            }
        }

        Rectangle {
            objectName: control.objectName + ":puck"
            width: 14
            height: 14
            radius: 7
            x: disk.width / 2 + control.live.x * disk.radius - width / 2
            y: disk.height / 2 - control.live.y * disk.radius - height / 2
            color: "transparent"
            border.width: 2
            border.color: Theme.color.textPrimary
        }

        MouseArea {
            anchors.fill: parent
            preventStealing: true
            function wheelAt(mx, my) {
                let x = (mx - disk.width / 2) / disk.radius
                let y = -(my - disk.height / 2) / disk.radius
                const d = Math.hypot(x, y)
                if (d > 1) { x /= d; y /= d }
                return { x: x, y: y, master: control.live.master }
            }
            onPressed: (mouse) => { control.live = wheelAt(mouse.x, mouse.y); control.moved(control.live) }
            onPositionChanged: (mouse) => { if (pressed) { control.live = wheelAt(mouse.x, mouse.y); control.moved(control.live) } }
            onReleased: control.committed(control.live)
            onDoubleClicked: {
                control.live = { x: 0, y: 0, master: control.live.master }
                control.committed(control.live)
            }
        }
    }

    Slider {
        objectName: control.objectName + ":master"
        Layout.fillWidth: true
        from: -1
        to: 1
        value: control.wheel.master
        onMoved: { control.live = { x: control.live.x, y: control.live.y, master: value }; control.moved(control.live) }
        onPressedChanged: if (!pressed && value !== control.wheel.master) control.committed(control.live)
    }
}
