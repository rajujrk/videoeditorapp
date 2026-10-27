import QtQuick
import TechieVideoEditor.Theme

// A keyframe's timing curve: time runs left to right, progress bottom to top, from the
// keyframe (bottom left) to the next one (top right). Drag a handle to reshape it; letting
// go commits. Progress may overshoot (−0.5 to 1.5 is in view).
Item {
    id: editor
    property var curve: [0, 0, 1, 1] // x1, y1, x2, y2
    property bool hold: false        // draws a step; nothing to drag
    signal committed(var curve)

    implicitHeight: 150
    // The curve being shown: follows `curve`, or the drag in progress.
    property var live: curve
    onCurveChanged: { live = curve; canvas.requestPaint() }
    onLiveChanged: canvas.requestPaint()
    onHoldChanged: canvas.requestPaint()

    readonly property real pad: 12
    readonly property real yMin: -0.5
    readonly property real yMax: 1.5
    function px(x) { return pad + x * (width - 2 * pad) }
    function py(y) { return pad + (yMax - y) / (yMax - yMin) * (height - 2 * pad) }
    function fromPx(px0, py0) {
        return [Math.max(0, Math.min(1, (px0 - pad) / (width - 2 * pad))),
                Math.max(yMin, Math.min(yMax, yMax - (py0 - pad) / (height - 2 * pad) * (yMax - yMin)))]
    }

    Rectangle {
        anchors.fill: parent
        radius: Theme.radius.m
        color: Theme.color.surfaceSunken
        border.color: Theme.color.borderSubtle
    }

    Canvas {
        id: canvas
        anchors.fill: parent
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            const c = editor.live
            // The unit square: from this keyframe's value to the next one's.
            ctx.strokeStyle = Theme.color.borderStrong
            ctx.lineWidth = 1
            ctx.strokeRect(editor.px(0), editor.py(1), editor.px(1) - editor.px(0), editor.py(0) - editor.py(1))
            if (editor.hold) {
                ctx.strokeStyle = Theme.color.accentText
                ctx.lineWidth = 2
                ctx.beginPath()
                ctx.moveTo(editor.px(0), editor.py(0))
                ctx.lineTo(editor.px(1), editor.py(0))
                ctx.lineTo(editor.px(1), editor.py(1))
                ctx.stroke()
                return
            }
            // Handle arms
            ctx.strokeStyle = Theme.color.textDisabled
            ctx.beginPath()
            ctx.moveTo(editor.px(0), editor.py(0))
            ctx.lineTo(editor.px(c[0]), editor.py(c[1]))
            ctx.moveTo(editor.px(1), editor.py(1))
            ctx.lineTo(editor.px(c[2]), editor.py(c[3]))
            ctx.stroke()
            // The curve
            ctx.strokeStyle = Theme.color.accentText
            ctx.lineWidth = 2
            ctx.beginPath()
            ctx.moveTo(editor.px(0), editor.py(0))
            ctx.bezierCurveTo(editor.px(c[0]), editor.py(c[1]), editor.px(c[2]), editor.py(c[3]),
                              editor.px(1), editor.py(1))
            ctx.stroke()
        }
    }

    Repeater {
        model: editor.hold ? [] : [0, 1]
        delegate: Rectangle {
            id: handle
            required property int modelData
            objectName: "curveHandle:" + modelData
            readonly property int ix: modelData * 2
            width: 12
            height: 12
            radius: 6
            x: editor.px(editor.live[ix]) - width / 2
            y: editor.py(editor.live[ix + 1]) - height / 2
            color: drag.pressed ? Theme.color.accent : Theme.color.surface
            border.color: Theme.color.accentText
            border.width: 2

            MouseArea {
                id: drag
                anchors.fill: parent
                anchors.margins: -6
                cursorShape: Qt.SizeAllCursor
                preventStealing: true
                onPositionChanged: (mouse) => {
                    if (!pressed)
                        return
                    const p = mapToItem(editor, mouse.x, mouse.y)
                    const point = editor.fromPx(p.x, p.y)
                    const next = editor.live.slice()
                    next[handle.ix] = point[0]
                    next[handle.ix + 1] = point[1]
                    editor.live = next
                }
                onReleased: editor.committed(editor.live)
            }
        }
    }
}
