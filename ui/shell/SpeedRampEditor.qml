import QtQuick
import TechieVideoEditor.Theme

// A clip's speed through its length: left to right is the clip, up is faster (0.1× to 10×,
// logarithmic; the dashed line is 1×). Drag a point up or down, and sideways between its
// neighbours; double-click the graph to add one; click one to select it. Each drag or
// addition commits.
Item {
    id: editor
    property var points: []     // [{at, speed}]
    property real playheadAt: -1 // the playhead as a fraction of the clip, or -1
    property int selected: -1
    signal committed(var points)

    implicitHeight: 150
    property var live: points
    onPointsChanged: {
        live = points
        if (selected >= points.length)
            selected = -1
        canvas.requestPaint()
    }
    onLiveChanged: canvas.requestPaint()
    onPlayheadAtChanged: canvas.requestPaint()

    readonly property real pad: 14
    readonly property real padLeft: 36 // room for the speed labels
    function px(at) { return padLeft + at * (width - padLeft - pad) }
    function py(speed) { return pad + (1 - (Math.log10(speed) + 1) / 2) * (height - 2 * pad) }
    function atFromX(x) { return Math.max(0, Math.min(1, (x - padLeft) / (width - padLeft - pad))) }
    function speedFromY(y) {
        const f = 1 - (y - pad) / (height - 2 * pad)
        return Math.max(0.1, Math.min(10, Math.pow(10, f * 2 - 1)))
    }
    // Speed of the ramp at fraction `at` (straight lines between points).
    function speedAt(at) {
        const p = live
        for (let i = 1; i < p.length; ++i) {
            if (at <= p[i].at) {
                const w = p[i].at - p[i - 1].at
                return w > 0 ? p[i - 1].speed + (p[i].speed - p[i - 1].speed) * (at - p[i - 1].at) / w : p[i].speed
            }
        }
        return p.length ? p[p.length - 1].speed : 1
    }
    function copy() { return live.map((p) => ({ at: p.at, speed: p.speed })) }

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
            // 1× guide
            ctx.strokeStyle = Theme.color.borderStrong
            ctx.lineWidth = 1
            ctx.setLineDash([4, 4])
            ctx.beginPath()
            ctx.moveTo(editor.px(0), editor.py(1))
            ctx.lineTo(editor.px(1), editor.py(1))
            ctx.stroke()
            ctx.setLineDash([])
            if (editor.playheadAt >= 0 && editor.playheadAt <= 1) {
                ctx.strokeStyle = Theme.color.playhead
                ctx.beginPath()
                ctx.moveTo(editor.px(editor.playheadAt), editor.pad / 2)
                ctx.lineTo(editor.px(editor.playheadAt), editor.height - editor.pad / 2)
                ctx.stroke()
            }
            const p = editor.live
            if (p.length < 2)
                return
            // Area under the speed line, then the line.
            ctx.fillStyle = Qt.alpha(Theme.color.accent, 0.18)
            ctx.beginPath()
            ctx.moveTo(editor.px(0), editor.py(0.1))
            for (const point of p)
                ctx.lineTo(editor.px(point.at), editor.py(point.speed))
            ctx.lineTo(editor.px(1), editor.py(0.1))
            ctx.closePath()
            ctx.fill()
            ctx.strokeStyle = Theme.color.accentText
            ctx.lineWidth = 2
            ctx.beginPath()
            for (let i = 0; i < p.length; ++i)
                i === 0 ? ctx.moveTo(editor.px(p[i].at), editor.py(p[i].speed))
                        : ctx.lineTo(editor.px(p[i].at), editor.py(p[i].speed))
            ctx.stroke()
        }
    }

    Text { x: 6; y: editor.py(10) - height / 2; text: "10×"; font: Theme.type.caption; color: Theme.color.textDisabled }
    Text { x: 6; y: editor.py(1) - height / 2; text: "1×"; font: Theme.type.caption; color: Theme.color.textDisabled }
    Text { x: 6; y: editor.py(0.1) - height / 2; text: "0.1×"; font: Theme.type.caption; color: Theme.color.textDisabled }

    // Double-click empty graph to add a point on the line.
    MouseArea {
        anchors.fill: parent
        onDoubleClicked: (mouse) => {
            const at = editor.atFromX(mouse.x)
            const next = editor.copy()
            if (next.some((p) => Math.abs(p.at - at) < 0.01))
                return
            next.push({ at: at, speed: editor.speedAt(at) })
            next.sort((a, b) => a.at - b.at)
            editor.selected = next.findIndex((p) => p.at === at)
            editor.committed(next)
        }
    }

    Repeater {
        model: editor.live.length
        delegate: Rectangle {
            id: handle
            required property int index
            readonly property var point: editor.live[index] || ({ at: 0, speed: 1 })
            readonly property bool edge: index === 0 || index === editor.live.length - 1
            objectName: "rampPoint:" + index
            width: 12
            height: 12
            radius: 6
            x: editor.px(point.at) - width / 2
            y: editor.py(point.speed) - height / 2
            color: editor.selected === index || drag.pressed ? Theme.color.accent : Theme.color.surface
            border.color: Theme.color.accentText
            border.width: 2

            MouseArea {
                id: drag
                anchors.fill: parent
                anchors.margins: -6
                cursorShape: handle.edge ? Qt.SizeVerCursor : Qt.SizeAllCursor
                preventStealing: true
                property bool moved: false
                onPressed: { editor.selected = handle.index; moved = false }
                onPositionChanged: (mouse) => {
                    if (!pressed)
                        return
                    moved = true
                    const pos = mapToItem(editor, mouse.x, mouse.y)
                    const next = editor.copy()
                    const i = handle.index
                    next[i].speed = editor.speedFromY(pos.y)
                    if (!handle.edge) { // ends stay at the clip's ends
                        const lo = next[i - 1].at + 0.01
                        const hi = next[i + 1].at - 0.01
                        next[i].at = Math.max(lo, Math.min(hi, editor.atFromX(pos.x)))
                    }
                    editor.live = next
                }
                onReleased: if (moved) editor.committed(editor.live)
            }
        }
    }
}
