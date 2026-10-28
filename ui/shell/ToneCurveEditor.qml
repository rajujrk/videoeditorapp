import QtQuick
import TechieVideoEditor.Theme

// A tone curve: input left to right, output bottom to top, both 0–1 (the diagonal leaves it
// unchanged). Drag a point; double-click empty space to add one, or an inner point to remove
// it. The ends stay at the left and right edges. Each change commits.
Item {
    id: editor
    property var project             // for sampling the curve exactly as the grade does
    property var points: []          // [{x, y}]; empty: the diagonal
    property color lineColor: Theme.color.accentText
    signal committed(var points)

    implicitHeight: width
    property var live: points.length ? points : [{ x: 0, y: 0 }, { x: 1, y: 1 }]
    onPointsChanged: live = points.length ? points : [{ x: 0, y: 0 }, { x: 1, y: 1 }]
    onLiveChanged: canvas.requestPaint()
    onLineColorChanged: canvas.requestPaint()

    readonly property real pad: 10
    function px(x) { return pad + x * (width - 2 * pad) }
    function py(y) { return pad + (1 - y) * (height - 2 * pad) }
    function fromPx(mx, my) {
        return { x: Math.max(0, Math.min(1, (mx - pad) / (width - 2 * pad))),
                 y: Math.max(0, Math.min(1, 1 - (my - pad) / (height - 2 * pad))) }
    }
    function copy() { return live.map((p) => ({ x: p.x, y: p.y })) }

    Rectangle {
        anchors.fill: parent
        radius: Theme.radius.m
        color: Theme.color.surfaceSunken
        border.color: Theme.color.borderSubtle
    }

    Canvas {
        // Theme colours are read while painting: repaint when they change.
        Connections { target: Theme; function onDarkChanged() { canvas.requestPaint() } }
        id: canvas
        anchors.fill: parent
        onWidthChanged: requestPaint()
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            ctx.strokeStyle = Theme.color.borderStrong
            ctx.lineWidth = 1
            for (let i = 1; i < 4; ++i) { // quarter grid
                ctx.beginPath(); ctx.moveTo(editor.px(i / 4), editor.py(0)); ctx.lineTo(editor.px(i / 4), editor.py(1)); ctx.stroke()
                ctx.beginPath(); ctx.moveTo(editor.px(0), editor.py(i / 4)); ctx.lineTo(editor.px(1), editor.py(i / 4)); ctx.stroke()
            }
            ctx.setLineDash([4, 4])
            ctx.beginPath(); ctx.moveTo(editor.px(0), editor.py(0)); ctx.lineTo(editor.px(1), editor.py(1)); ctx.stroke()
            ctx.setLineDash([])
            if (!editor.project)
                return
            const samples = editor.project.sampleColorCurve(editor.live, 96)
            ctx.strokeStyle = editor.lineColor
            ctx.lineWidth = 2
            ctx.beginPath()
            for (let i = 0; i < samples.length; ++i) {
                const x = editor.px(i / (samples.length - 1)), y = editor.py(samples[i])
                i === 0 ? ctx.moveTo(x, y) : ctx.lineTo(x, y)
            }
            ctx.stroke()
        }
    }

    MouseArea {
        anchors.fill: parent
        onDoubleClicked: (mouse) => {
            const p = editor.fromPx(mouse.x, mouse.y)
            const next = editor.copy()
            if (next.length >= 16 || next.some((q) => Math.abs(q.x - p.x) < 0.02))
                return
            next.push(p)
            next.sort((a, b) => a.x - b.x)
            editor.committed(next)
        }
    }

    Repeater {
        model: editor.live.length
        delegate: Rectangle {
            id: handle
            required property int index
            readonly property var point: editor.live[index] || ({ x: 0, y: 0 })
            readonly property bool edge: index === 0 || index === editor.live.length - 1
            objectName: "curvePoint:" + index
            width: 12
            height: 12
            radius: 6
            x: editor.px(point.x) - width / 2
            y: editor.py(point.y) - height / 2
            color: drag.pressed ? editor.lineColor : Theme.color.surface
            border.color: editor.lineColor
            border.width: 2

            MouseArea {
                id: drag
                anchors.fill: parent
                anchors.margins: -6
                preventStealing: true
                property bool moved: false
                onPressed: moved = false
                onPositionChanged: (mouse) => {
                    if (!pressed)
                        return
                    moved = true
                    const pos = mapToItem(editor, mouse.x, mouse.y)
                    const p = editor.fromPx(pos.x, pos.y)
                    const next = editor.copy()
                    const i = handle.index
                    next[i].y = p.y
                    if (!handle.edge)
                        next[i].x = Math.max(next[i - 1].x + 0.01, Math.min(next[i + 1].x - 0.01, p.x))
                    editor.live = next
                }
                onReleased: if (moved) editor.committed(editor.live)
                onDoubleClicked: {
                    if (handle.edge)
                        return
                    editor.committed(editor.copy().filter((p, i) => i !== handle.index))
                }
            }
        }
    }
}
