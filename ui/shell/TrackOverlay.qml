import QtQuick
import TechieVideoEditor.Theme

// Drawn over the player while choosing what to track (project.trackAreaKind): drag a box
// around an object or a flat rectangle; for a plane, drag a box and then move each of its four
// corners onto the surface's (they may make any four-sided shape, in perspective); for a point,
// click it. What is drawn goes from the item to the canvas (aspect-fitted), then into the
// clip's picture through its placement (clipPlacement) — as shown, steadied or not; the app
// takes it from there. It stays shown until it is tracked.
Item {
    id: overlay
    required property var project

    readonly property var clipInfo: project.selectedClipInfo
    readonly property var placement: {
        project.playhead // the placement may be animated
        return clipInfo.id === undefined ? ({}) : project.clipPlacement(clipInfo.id)
    }
    readonly property bool placed: placement.m !== undefined
    readonly property var area: project.trackArea.clipId === clipInfo.id ? project.trackArea : null
    readonly property bool picking: placed && project.pickingTrackArea
    readonly property string kind: project.trackAreaKind
    visible: placed && (picking || area !== null)
    // A plane's corners as placed (item points), or none.
    readonly property var cornerPoints: {
        if (!area || !placed || !area.corners)
            return []
        const c = area.corners
        return [0, 1, 2, 3].map((i) => fromPicture(c[i * 2], c[i * 2 + 1]))
    }

    readonly property real canvasScale: Math.min(width / project.canvasWidth, height / project.canvasHeight)
    readonly property rect canvasRect: Qt.rect((width - project.canvasWidth * canvasScale) / 2,
                                               (height - project.canvasHeight * canvasScale) / 2,
                                               project.canvasWidth * canvasScale, project.canvasHeight * canvasScale)

    // Item point → picture fraction, and back (the placement's inverse).
    function toPicture(x, y) {
        const m = placement.m
        const u = (x - canvasRect.x) / canvasRect.width
        const v = (y - canvasRect.y) / canvasRect.height
        const w = m[6] * u + m[7] * v + m[8]
        return Qt.point((m[0] * u + m[1] * v + m[2]) / w, (m[3] * u + m[4] * v + m[5]) / w)
    }
    function fromPicture(px, py) {
        const m = placement.toCanvas
        const w = m[6] * px + m[7] * py + m[8]
        return Qt.point(canvasRect.x + ((m[0] * px + m[1] * py + m[2]) / w) * canvasRect.width,
                        canvasRect.y + ((m[3] * px + m[4] * py + m[5]) / w) * canvasRect.height)
    }

    // The box being dragged (item points), or the one drawn.
    property point dragFrom
    property point dragTo
    property bool dragging: false
    readonly property rect shown: {
        if (dragging)
            return Qt.rect(Math.min(dragFrom.x, dragTo.x), Math.min(dragFrom.y, dragTo.y),
                           Math.abs(dragTo.x - dragFrom.x), Math.abs(dragTo.y - dragFrom.y))
        if (!area || !placed)
            return Qt.rect(0, 0, 0, 0)
        const a = fromPicture(area.x - area.width / 2, area.y - area.height / 2)
        const b = fromPicture(area.x + area.width / 2, area.y + area.height / 2)
        return Qt.rect(Math.min(a.x, b.x), Math.min(a.y, b.y), Math.abs(b.x - a.x), Math.abs(b.y - a.y))
    }

    Rectangle {
        objectName: "trackBox"
        x: overlay.shown.x
        y: overlay.shown.y
        width: overlay.shown.width
        height: overlay.shown.height
        visible: width > 0 && height > 0 && overlay.cornerPoints.length === 0 && !(overlay.area && overlay.area.point && !overlay.dragging)
        color: Qt.alpha(Theme.color.accent, 0.15)
        border.width: 2
        border.color: Theme.color.accent
    }

    // A plane: the four corners joined, each a handle to move onto the surface's corner.
    Canvas {
        id: planeOutline
        objectName: "trackPlane"
        anchors.fill: parent
        visible: overlay.cornerPoints.length === 4
        property var points: overlay.cornerPoints
        onPointsChanged: requestPaint()
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            const p = points
            if (p.length !== 4)
                return
            ctx.beginPath()
            ctx.moveTo(p[0].x, p[0].y)
            for (let i = 1; i < 4; ++i)
                ctx.lineTo(p[i].x, p[i].y)
            ctx.closePath()
            ctx.fillStyle = Qt.alpha(Theme.color.accent, 0.15)
            ctx.fill()
            ctx.lineWidth = 2
            ctx.strokeStyle = String(Theme.color.accent)
            ctx.stroke()
        }
    }
    Repeater {
        model: overlay.cornerPoints.length === 4 ? 4 : 0
        delegate: Rectangle {
            id: handle
            required property int index
            objectName: "trackCorner:" + index
            readonly property point at: overlay.cornerPoints[index] || Qt.point(0, 0)
            x: at.x - width / 2
            y: at.y - height / 2
            width: 16
            height: 16
            radius: 8
            color: cornerDrag.pressed ? Theme.color.accent : "white"
            border.color: Theme.color.accent
            border.width: 2
            Behavior on scale { NumberAnimation { duration: Theme.motion.fast } }
            scale: cornerDrag.containsMouse || cornerDrag.pressed ? 1.25 : 1
            MouseArea {
                id: cornerDrag
                anchors.fill: parent
                anchors.margins: -6
                hoverEnabled: true
                cursorShape: Qt.SizeAllCursor
                preventStealing: true
                onPositionChanged: (mouse) => {
                    if (!pressed)
                        return
                    const p = mapToItem(overlay, mouse.x, mouse.y)
                    const moved = overlay.toPicture(p.x, p.y)
                    const corners = overlay.area.corners.slice()
                    corners[handle.index * 2] = moved.x
                    corners[handle.index * 2 + 1] = moved.y
                    overlay.project.setTrackArea(overlay.clipInfo.id, { corners: corners })
                }
            }
        }
    }
    // A point: cross hairs on it.
    Item {
        objectName: "trackPoint"
        readonly property var at: overlay.area && overlay.area.point ? overlay.fromPicture(overlay.area.x, overlay.area.y) : null
        visible: at !== null && !overlay.dragging
        x: at ? at.x : 0
        y: at ? at.y : 0
        Rectangle { x: -12; y: -1; width: 24; height: 2; color: Theme.color.accent }
        Rectangle { x: -1; y: -12; width: 2; height: 24; color: Theme.color.accent }
        Rectangle { x: -7; y: -7; width: 14; height: 14; radius: 7; color: "transparent"; border.color: "white"; border.width: 1.5 }
    }

    Text {
        visible: overlay.picking && !overlay.dragging
        anchors { horizontalCenter: parent.horizontalCenter; top: parent.top; topMargin: Theme.space.m }
        text: overlay.kind === "point" ? qsTr("Click the point to follow")
            : overlay.kind === "plane" ? qsTr("Drag a box over the flat surface; then move each corner onto its corners")
            : qsTr("Drag a box around what to follow")
        font: Theme.type.label
        color: "white"
        style: Text.Outline
        styleColor: "black"
    }

    MouseArea {
        objectName: "trackArea"
        anchors.fill: parent
        enabled: overlay.picking
        cursorShape: Qt.CrossCursor
        onPressed: (mouse) => {
            overlay.dragFrom = Qt.point(mouse.x, mouse.y)
            overlay.dragTo = overlay.dragFrom
            overlay.dragging = true
        }
        onPositionChanged: (mouse) => overlay.dragTo = Qt.point(mouse.x, mouse.y)
        onReleased: {
            overlay.dragging = false
            const a = overlay.toPicture(overlay.dragFrom.x, overlay.dragFrom.y)
            const b = overlay.toPicture(overlay.dragTo.x, overlay.dragTo.y)
            if (overlay.kind === "point") {
                overlay.project.setTrackArea(overlay.clipInfo.id, { x: b.x, y: b.y, point: true })
                return
            }
            if (overlay.kind === "plane") {
                // The box's corners to start from (top left, top right, bottom right, bottom left).
                const l = Math.min(a.x, b.x), r = Math.max(a.x, b.x), t = Math.min(a.y, b.y), d = Math.max(a.y, b.y)
                overlay.project.setTrackArea(overlay.clipInfo.id, { corners: [l, t, r, t, r, d, l, d] })
                return
            }
            overlay.project.setTrackArea(overlay.clipInfo.id, {
                x: (a.x + b.x) / 2, y: (a.y + b.y) / 2, width: Math.abs(b.x - a.x), height: Math.abs(b.y - a.y)
            })
        }
    }
}
