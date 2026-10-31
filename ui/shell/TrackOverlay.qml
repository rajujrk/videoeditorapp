import QtQuick
import TechieVideoEditor.Theme

// Drawn over the player while choosing what to track: drag a box around it. The box goes
// from the item to the canvas (aspect-fitted), then into the clip's picture through its
// placement (clipPlacement) — as shown, steadied or not; the app takes it from there. The
// box stays shown until it is tracked.
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
    visible: placed && (picking || area !== null)

    readonly property real canvasScale: Math.min(width / project.canvasWidth, height / project.canvasHeight)
    readonly property rect canvasRect: Qt.rect((width - project.canvasWidth * canvasScale) / 2,
                                               (height - project.canvasHeight * canvasScale) / 2,
                                               project.canvasWidth * canvasScale, project.canvasHeight * canvasScale)

    // Item point → picture fraction, and back (the placement's inverse).
    function toPicture(x, y) {
        const m = placement.m
        const u = (x - canvasRect.x) / canvasRect.width
        const v = (y - canvasRect.y) / canvasRect.height
        return Qt.point(m[0] * u + m[1] * v + m[2], m[3] * u + m[4] * v + m[5])
    }
    function fromPicture(px, py) {
        const m = placement.m
        const det = m[0] * m[4] - m[1] * m[3]
        const a = px - m[2]
        const b = py - m[5]
        return Qt.point(canvasRect.x + ((m[4] * a - m[1] * b) / det) * canvasRect.width,
                        canvasRect.y + ((-m[3] * a + m[0] * b) / det) * canvasRect.height)
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
        visible: width > 0 && height > 0
        color: Qt.alpha(Theme.color.accent, 0.15)
        border.width: 2
        border.color: Theme.color.accent
    }

    Text {
        visible: overlay.picking && !overlay.dragging
        anchors { horizontalCenter: parent.horizontalCenter; top: parent.top; topMargin: Theme.space.m }
        text: "Drag a box around what to follow"
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
            overlay.project.setTrackArea(overlay.clipInfo.id, {
                x: (a.x + b.x) / 2, y: (a.y + b.y) / 2, width: Math.abs(b.x - a.x), height: Math.abs(b.y - a.y)
            })
        }
    }
}
