import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import TechieVideoEditor.Theme

// Drawn over the player while drawing (the Draw button): brush strokes, lines, arrows,
// rectangles and ellipses, in canvas fractions (aspect-fitted), each added to the selected
// drawing or, with none selected, to a new one at the playhead (one undo step each). A strip
// along the top picks the tool, colour, width and fill; Done or Esc stops drawing.
Item {
    id: overlay
    required property var project
    visible: project.drawingMode

    property string tool: "brush"
    property var stroke: [1, 1, 1, 1]
    property real strokeWidth: 0.01 // of the canvas's shorter side
    property bool filled: false
    readonly property bool closedShape: tool === "rectangle" || tool === "ellipse"
    readonly property var tools: [{ id: "brush", label: qsTr("Brush") }, { id: "line", label: qsTr("Line") }, { id: "arrow", label: qsTr("Arrow") },
                                  { id: "rectangle", label: qsTr("Rectangle") }, { id: "ellipse", label: qsTr("Ellipse") }]
    readonly property var colours: [[1, 1, 1, 1], [0, 0, 0, 1], [1, 0.84, 0.2, 1], [1, 0.55, 0.1, 1],
                                    [0.92, 0.25, 0.3, 1], [0.35, 0.55, 1, 1], [0.3, 0.8, 0.55, 1]]
    readonly property var widths: [{ label: qsTr("Thin"), value: 0.005 }, { label: qsTr("Medium"), value: 0.01 }, { label: qsTr("Thick"), value: 0.022 }]

    readonly property real canvasScale: Math.min(width / project.canvasWidth, height / project.canvasHeight)
    readonly property rect canvasRect: Qt.rect((width - project.canvasWidth * canvasScale) / 2,
                                               (height - project.canvasHeight * canvasScale) / 2,
                                               project.canvasWidth * canvasScale, project.canvasHeight * canvasScale)
    function toCanvas(x, y) {
        return [(x - canvasRect.x) / canvasRect.width, (y - canvasRect.y) / canvasRect.height]
    }
    function fromCanvas(p) {
        return Qt.point(canvasRect.x + p[0] * canvasRect.width, canvasRect.y + p[1] * canvasRect.height)
    }

    // The path being drawn (canvas fractions).
    property var points: []

    // Adds the path drawn to the selected drawing, or makes a drawing of it.
    function finish() {
        const drawn = points
        points = []
        preview.requestPaint()
        if (drawn.length < 2)
            return
        const path = {
            kind: tool,
            points: tool === "brush" ? drawn : [drawn[0], drawn[drawn.length - 1]],
            stroke: stroke,
            width: strokeWidth,
            fill: closedShape && filled ? [stroke[0], stroke[1], stroke[2], 0.35] : [1, 1, 1, 0]
        }
        const info = project.selectedClipInfo
        if (info.isDrawing === true)
            project.addDrawnPath(info.id, path)
        else
            project.addDrawing(path)
    }

    Keys.onEscapePressed: project.drawingMode = false
    onVisibleChanged: if (visible) forceActiveFocus()

    // What is being drawn, until it is added (then the player shows it).
    Canvas {
        id: preview
        anchors.fill: parent
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            const pts = overlay.points
            if (pts.length < 2)
                return
            const c = overlay.stroke
            ctx.strokeStyle = Qt.rgba(c[0], c[1], c[2], c[3])
            ctx.fillStyle = Qt.rgba(c[0], c[1], c[2], 0.35)
            ctx.lineWidth = Math.max(1, overlay.strokeWidth * Math.min(overlay.canvasRect.width, overlay.canvasRect.height))
            ctx.lineCap = "round"
            ctx.lineJoin = "round"
            const a = overlay.fromCanvas(pts[0])
            const b = overlay.fromCanvas(pts[pts.length - 1])
            ctx.beginPath()
            if (overlay.tool === "brush") {
                ctx.moveTo(a.x, a.y)
                for (let i = 1; i < pts.length; ++i) {
                    const p = overlay.fromCanvas(pts[i])
                    ctx.lineTo(p.x, p.y)
                }
            } else if (overlay.tool === "rectangle") {
                ctx.rect(Math.min(a.x, b.x), Math.min(a.y, b.y), Math.abs(b.x - a.x), Math.abs(b.y - a.y))
            } else if (overlay.tool === "ellipse") {
                ctx.ellipse(Math.min(a.x, b.x), Math.min(a.y, b.y), Math.abs(b.x - a.x), Math.abs(b.y - a.y))
            } else {
                ctx.moveTo(a.x, a.y)
                ctx.lineTo(b.x, b.y)
                if (overlay.tool === "arrow") {
                    const angle = Math.atan2(b.y - a.y, b.x - a.x)
                    const head = ctx.lineWidth * 4
                    ctx.moveTo(b.x - head * Math.cos(angle - 0.5), b.y - head * Math.sin(angle - 0.5))
                    ctx.lineTo(b.x, b.y)
                    ctx.lineTo(b.x - head * Math.cos(angle + 0.5), b.y - head * Math.sin(angle + 0.5))
                }
            }
            if (overlay.closedShape && overlay.filled)
                ctx.fill()
            ctx.stroke()
        }
    }

    MouseArea {
        objectName: "drawingArea"
        anchors.fill: parent
        cursorShape: Qt.CrossCursor
        preventStealing: true
        onPressed: (mouse) => {
            overlay.forceActiveFocus()
            overlay.points = [overlay.toCanvas(mouse.x, mouse.y)]
        }
        onPositionChanged: (mouse) => {
            if (!pressed)
                return
            const p = overlay.toCanvas(mouse.x, mouse.y)
            const next = overlay.points.slice()
            if (overlay.tool === "brush") {
                // A point at least a little way from the last, up to the most a path holds.
                const last = next[next.length - 1]
                if (Math.hypot(p[0] - last[0], p[1] - last[1]) < 0.004 || next.length >= 1990)
                    return
                next.push(p)
            } else {
                next.splice(1, next.length - 1, p)
            }
            overlay.points = next
            preview.requestPaint()
        }
        onReleased: overlay.finish()
    }

    component Chip: AbstractButton {
        id: chip
        property string label
        hoverEnabled: true
        leftPadding: Theme.space.s
        rightPadding: Theme.space.s
        implicitHeight: 24
        background: Rectangle {
            radius: height / 2
            color: chip.checked ? Theme.color.accentSubtle : chip.hovered ? Theme.color.surfaceHover : "transparent"
            border.color: chip.checked ? Theme.color.accent : Theme.color.borderSubtle
        }
        contentItem: Text {
            text: chip.label
            font: Theme.type.caption
            color: chip.checked ? Theme.color.accentText : Theme.color.textSecondary
            verticalAlignment: Text.AlignVCenter
        }
    }

    // Tool, colour, width and fill.
    Rectangle {
        objectName: "drawingTools"
        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.topMargin: Theme.space.s
        width: Math.min(parent.width - Theme.space.m * 2, toolRow.implicitWidth + Theme.space.s * 2)
        height: toolRow.implicitHeight + Theme.space.xs * 2
        radius: Theme.radius.m
        color: Theme.color.surfaceRaised
        border.color: Theme.color.borderStrong
        clip: true

        // Clicks on the strip don't draw.
        MouseArea { anchors.fill: parent }

        RowLayout {
            id: toolRow
            anchors.centerIn: parent
            spacing: Theme.space.xs
            Repeater {
                model: overlay.tools
                delegate: Chip {
                    required property var modelData
                    objectName: "drawTool:" + modelData.id
                    label: modelData.label
                    checked: overlay.tool === modelData.id
                    onClicked: overlay.tool = modelData.id
                }
            }
            Divider { implicitWidth: 1; implicitHeight: 16 }
            Repeater {
                model: overlay.colours
                delegate: Rectangle {
                    required property var modelData
                    required property int index
                    objectName: "drawColour:" + index
                    readonly property bool chosen: Math.abs(overlay.stroke[0] - modelData[0]) < 0.01
                                                   && Math.abs(overlay.stroke[1] - modelData[1]) < 0.01
                                                   && Math.abs(overlay.stroke[2] - modelData[2]) < 0.01
                    width: 18
                    height: 18
                    radius: width / 2
                    color: Qt.rgba(modelData[0], modelData[1], modelData[2], 1)
                    border.width: chosen ? 2 : 1
                    border.color: chosen ? Theme.color.accent : Theme.color.borderStrong
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: overlay.stroke = parent.modelData
                    }
                }
            }
            Divider { implicitWidth: 1; implicitHeight: 16 }
            Repeater {
                model: overlay.widths
                delegate: Chip {
                    required property var modelData
                    objectName: "drawWidth:" + modelData.label
                    label: modelData.label
                    checked: Math.abs(overlay.strokeWidth - modelData.value) < 1e-6
                    onClicked: overlay.strokeWidth = modelData.value
                }
            }
            Chip {
                objectName: "drawFill"
                visible: overlay.closedShape
                label: qsTr("Fill")
                checked: overlay.filled
                onClicked: overlay.filled = !overlay.filled
            }
            Divider { implicitWidth: 1; implicitHeight: 16 }
            TextButton {
                objectName: "drawDone"
                text: qsTr("Done")
                variant: "primary"
                onClicked: overlay.project.drawingMode = false
            }
        }
    }
}
