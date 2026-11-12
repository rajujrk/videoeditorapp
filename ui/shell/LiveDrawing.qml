import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import TechieVideoEditor.Theme

// Drawing on the screen while it is recorded: a clear window over the recorded area (so the
// recording catches what is drawn) taking the pointer while drawing is on, and a small bar of
// controls of its own, left out of the recording. Pen, highlighter or arrow, five inks, three
// widths; strokes fade away after a few seconds unless kept, Clear wipes them. D turns drawing
// on and off, C clears, Esc stops drawing (the pointer goes back to the screen).
Window {
    id: drawingLayer
    required property var project
    required property var area // {x, y, width, height}: global points
    property bool drawing: true
    property string tool: "pen" // "pen", "highlighter", "arrow"
    property color ink: "#FF3B30"
    property real thickness: 6
    property bool fades: true
    readonly property real fadeAfter: 3.0 // seconds a stroke stays once drawn
    readonly property real fadeTime: 0.6  // then it fades out over this
    property var strokes: [] // {tool, color, width, points: [x0, y0, x1, y1, …], ended (ms; 0 while drawing)}
    property double now: Date.now()

    objectName: "liveDrawing"
    title: qsTr("Drawing on the screen")
    flags: Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.Tool | Qt.NoDropShadowWindowHint
           | (drawing ? 0 : Qt.WindowTransparentForInput)
    color: "transparent"
    x: area.x || 0
    y: area.y || 0
    width: area.width || 1280
    height: area.height || 720
    visible: true

    function clear() {
        strokes = []
        canvas.requestPaint()
    }
    // How visible a stroke is now (1 drawn and resting … 0 gone).
    function opacityOf(stroke) {
        if (!fades || stroke.ended === 0)
            return 1
        const left = stroke.ended / 1000 + fadeAfter + fadeTime - now / 1000
        return Math.max(0, Math.min(1, left / fadeTime))
    }

    // Fading: drop what has gone, repaint what is going.
    FrameAnimation {
        running: drawingLayer.fades && drawingLayer.strokes.some((s) => s.ended > 0)
        onTriggered: {
            drawingLayer.now = Date.now()
            const kept = drawingLayer.strokes.filter((s) => drawingLayer.opacityOf(s) > 0)
            if (kept.length !== drawingLayer.strokes.length)
                drawingLayer.strokes = kept
            canvas.requestPaint()
        }
    }

    Canvas {
        id: canvas
        objectName: "liveDrawingCanvas"
        anchors.fill: parent
        onPaint: {
            const c = getContext("2d")
            c.reset()
            c.lineCap = "round"
            c.lineJoin = "round"
            for (const s of drawingLayer.strokes) {
                const p = s.points
                if (p.length < 2)
                    continue
                const highlighter = s.tool === "highlighter"
                c.globalAlpha = drawingLayer.opacityOf(s) * (highlighter ? 0.35 : 1)
                c.strokeStyle = s.color
                c.fillStyle = s.color
                c.lineWidth = highlighter ? s.width * 3 : s.width
                c.beginPath()
                if (s.tool === "arrow") {
                    // A straight shaft from where it started to where it is, and a head.
                    const x0 = p[0], y0 = p[1], x1 = p[p.length - 2], y1 = p[p.length - 1]
                    c.moveTo(x0, y0)
                    c.lineTo(x1, y1)
                    c.stroke()
                    const angle = Math.atan2(y1 - y0, x1 - x0)
                    const head = Math.max(14, s.width * 3.5)
                    c.beginPath()
                    c.moveTo(x1, y1)
                    c.lineTo(x1 - head * Math.cos(angle - 0.45), y1 - head * Math.sin(angle - 0.45))
                    c.lineTo(x1 - head * Math.cos(angle + 0.45), y1 - head * Math.sin(angle + 0.45))
                    c.closePath()
                    c.fill()
                } else {
                    c.moveTo(p[0], p[1])
                    if (p.length === 2)
                        c.lineTo(p[0] + 0.1, p[1]) // a dot
                    for (let i = 2; i < p.length; i += 2)
                        c.lineTo(p[i], p[i + 1])
                    c.stroke()
                }
            }
        }
    }

    MouseArea {
        objectName: "liveDrawingArea"
        anchors.fill: parent
        enabled: drawingLayer.drawing
        cursorShape: Qt.CrossCursor
        onPressed: (mouse) => {
            drawingLayer.strokes = drawingLayer.strokes.concat([{ tool: drawingLayer.tool, color: String(drawingLayer.ink), width: drawingLayer.thickness,
                                                    points: [mouse.x, mouse.y], ended: 0 }])
            canvas.requestPaint()
        }
        onPositionChanged: (mouse) => {
            const s = drawingLayer.strokes[drawingLayer.strokes.length - 1]
            if (!s || s.ended !== 0)
                return
            s.points.push(mouse.x, mouse.y)
            canvas.requestPaint()
        }
        onReleased: {
            const s = drawingLayer.strokes[drawingLayer.strokes.length - 1]
            if (s && s.ended === 0)
                s.ended = Date.now()
            drawingLayer.now = Date.now()
            drawingLayer.strokesChanged() // (starts the fading)
            canvas.requestPaint()
        }
    }

    Shortcut { objectName: "liveDrawingKey:draw"; sequence: "D"; context: Qt.ApplicationShortcut; enabled: drawingLayer.visible; onActivated: drawingLayer.drawing = !drawingLayer.drawing }
    Shortcut { objectName: "liveDrawingKey:clear"; sequence: "C"; context: Qt.ApplicationShortcut; enabled: drawingLayer.visible && drawingLayer.drawing; onActivated: drawingLayer.clear() }
    Shortcut { objectName: "liveDrawingKey:stop"; sequence: "Escape"; enabled: drawingLayer.drawing; onActivated: drawingLayer.drawing = false }

    // The controls: a bar near the bottom of the area, out of the recording; drag it by its grip.
    property alias controls: bar
    Window {
        id: bar
        objectName: "liveDrawingControls"
        flags: Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.Tool
        color: "transparent"
        width: row.implicitWidth + Theme.space.m * 2
        height: row.implicitHeight + Theme.space.s * 2
        x: drawingLayer.x + (drawingLayer.width - width) / 2
        y: drawingLayer.y + drawingLayer.height - height - 48
        visible: drawingLayer.visible
        Component.onCompleted: drawingLayer.project.keepOutOfRecordings(bar, true)
        Component.onDestruction: drawingLayer.project.keepOutOfRecordings(bar, false)

        Rectangle {
            anchors.fill: parent
            radius: height / 2
            color: Theme.color.surfaceRaised
            border.color: Theme.color.borderStrong
            DragHandler { target: null; onActiveChanged: if (active) bar.startSystemMove() }
            RowLayout {
                id: row
                anchors.centerIn: parent
                spacing: Theme.space.xs
                IconButton {
                    objectName: "liveDrawingToggle"
                    iconName: "draw"
                    tip: drawingLayer.drawing ? qsTr("Stop drawing: the pointer works the screen again (D or Esc)") : qsTr("Draw on the screen (D)")
                    checked: drawingLayer.drawing
                    onClicked: drawingLayer.drawing = !drawingLayer.drawing
                }
                Repeater {
                    model: [{ key: "pen", label: qsTr("Pen") }, { key: "highlighter", label: qsTr("Highlighter") },
                            { key: "arrow", label: qsTr("Arrow") }]
                    delegate: TextButton {
                        required property var modelData
                        objectName: "liveDrawingTool:" + modelData.key
                        text: modelData.label
                        variant: drawingLayer.tool === modelData.key ? "primary" : "ghost"
                        onClicked: { drawingLayer.tool = modelData.key; drawingLayer.drawing = true }
                    }
                }
                Repeater {
                    model: ["#FF3B30", "#FFCC00", "#34C759", "#0A84FF", "#FFFFFF"]
                    delegate: AbstractButton {
                        id: swatch
                        required property string modelData
                        required property int index
                        readonly property bool chosen: Qt.colorEqual(drawingLayer.ink, modelData)
                        objectName: "liveDrawingInk:" + index
                        implicitWidth: 22
                        implicitHeight: 22
                        onClicked: { drawingLayer.ink = modelData; drawingLayer.drawing = true }
                        background: Rectangle {
                            radius: width / 2
                            color: swatch.modelData
                            border.width: swatch.chosen ? 3 : 1
                            border.color: swatch.chosen ? Theme.color.accent : Theme.color.borderStrong
                        }
                    }
                }
                Repeater {
                    model: [3, 6, 12]
                    delegate: AbstractButton {
                        id: widthButton
                        required property int modelData
                        objectName: "liveDrawingWidth:" + modelData
                        implicitWidth: 24
                        implicitHeight: 24
                        onClicked: drawingLayer.thickness = modelData
                        background: Rectangle {
                            radius: 4
                            color: drawingLayer.thickness === widthButton.modelData ? Theme.color.accentSubtle : "transparent"
                            Rectangle {
                                anchors.centerIn: parent
                                width: widthButton.modelData + 2
                                height: width
                                radius: width / 2
                                color: Theme.color.textPrimary
                            }
                        }
                    }
                }
                ToggleSwitch {
                    objectName: "liveDrawingFades"
                    text: qsTr("Fade away")
                    checked: drawingLayer.fades
                    onToggled: drawingLayer.fades = checked
                }
                IconButton {
                    objectName: "liveDrawingClear"
                    iconName: "delete"
                    tip: qsTr("Clear the drawing (C)")
                    onClicked: drawingLayer.clear()
                }
            }
        }
    }
}
