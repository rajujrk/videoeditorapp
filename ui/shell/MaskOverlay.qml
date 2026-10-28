import QtQuick
import TechieVideoEditor.Theme

// Drawn over the player: the selected clip's active mask, with handles to move, size and
// rotate it (pen masks: their points), and the colour picker for the chroma key. Positions
// go from the item to the canvas (aspect-fitted), then into the clip's picture through its
// placement (clipPlacement), which is where masks live.
Item {
    id: overlay
    required property var project

    readonly property var clipInfo: project.selectedClipInfo
    readonly property var masks: clipInfo.masks || []
    readonly property int active: project.activeMask
    readonly property var committed: active >= 0 && active < masks.length ? masks[active] : null
    // The mask while a handle is dragged (previewed, committed on release).
    property var draft: null
    readonly property var mask: draft || committed
    readonly property var placement: {
        project.playhead // the placement may be animated
        return clipInfo.id === undefined ? ({}) : project.clipPlacement(clipInfo.id)
    }
    readonly property bool placed: placement.m !== undefined
    readonly property bool editing: placed && mask !== null
    readonly property bool picking: placed && project.pickingKeyColor
    visible: editing || picking

    // The canvas inside the item, fitted keeping its aspect ratio.
    readonly property real canvasScale: Math.min(width / project.canvasWidth, height / project.canvasHeight)
    readonly property rect canvasRect: Qt.rect((width - project.canvasWidth * canvasScale) / 2,
                                               (height - project.canvasHeight * canvasScale) / 2,
                                               project.canvasWidth * canvasScale, project.canvasHeight * canvasScale)
    readonly property real pictureWidth: placement.width || 1
    readonly property real pictureHeight: placement.height || 1

    // Item point → picture fraction.
    function toPicture(x, y) {
        const m = placement.m
        const u = (x - canvasRect.x) / canvasRect.width
        const v = (y - canvasRect.y) / canvasRect.height
        return Qt.point(m[0] * u + m[1] * v + m[2], m[3] * u + m[4] * v + m[5])
    }
    // Picture fraction → item point (the placement's inverse).
    function fromPicture(px, py) {
        const m = placement.m
        const det = m[0] * m[4] - m[1] * m[3]
        const a = px - m[2]
        const b = py - m[5]
        const u = (m[4] * a - m[1] * b) / det
        const v = (-m[3] * a + m[0] * b) / det
        return Qt.point(canvasRect.x + u * canvasRect.width, canvasRect.y + v * canvasRect.height)
    }
    // A point in the shape's own frame (picture pixels, before rotation) → item point.
    function fromShape(a, b) {
        const t = mask.rotation * Math.PI / 180
        const c = Math.cos(t), s = Math.sin(t)
        const x = mask.x * pictureWidth + c * a - s * b
        const y = mask.y * pictureHeight + s * a + c * b
        return fromPicture(x / pictureWidth, y / pictureHeight)
    }
    // An item point → the shape's own frame (picture pixels from its centre).
    function toShape(x, y) {
        const p = toPicture(x, y)
        const dx = (p.x - mask.x) * pictureWidth
        const dy = (p.y - mask.y) * pictureHeight
        const t = mask.rotation * Math.PI / 180
        const c = Math.cos(t), s = Math.sin(t)
        return Qt.point(c * dx + s * dy, -s * dx + c * dy)
    }

    // The outline as item points (closed unless a line).
    function outline() {
        if (!editing)
            return []
        const hw = mask.width * pictureWidth / 2
        const hh = mask.height * pictureHeight / 2
        const r = Math.min(hw, hh)
        const out = []
        if (mask.shape === "pen") {
            for (const p of mask.points)
                out.push(fromPicture(p[0], p[1]))
        } else if (mask.shape === "rectangle") {
            out.push(fromShape(-hw, -hh), fromShape(hw, -hh), fromShape(hw, hh), fromShape(-hw, hh))
        } else if (mask.shape === "ellipse") {
            for (let i = 0; i < 64; ++i) {
                const t = i / 64 * 2 * Math.PI
                out.push(fromShape(hw * Math.cos(t), hh * Math.sin(t)))
            }
        } else if (mask.shape === "line") {
            const reach = 4 * Math.max(pictureWidth, pictureHeight)
            out.push(fromShape(-reach, 0), fromShape(reach, 0))
        } else if (mask.shape === "star") {
            for (let i = 0; i < 10; ++i) {
                const t = i / 10 * 2 * Math.PI
                const radius = i % 2 === 0 ? r : r * 0.45
                out.push(fromShape(radius * Math.sin(t), -radius * Math.cos(t)))
            }
        } else if (mask.shape === "heart") {
            // The classic heart curve (y from -17 at the point to about 12), fitted to the
            // shader's heart: 2r across as size, from 0.5 size below the centre to 0.6 above.
            const size = 2 * r
            const k = 1.104 * size / 29
            for (let i = 0; i < 64; ++i) {
                const t = i / 64 * 2 * Math.PI
                const x = 16 * Math.pow(Math.sin(t), 3)
                const y = 13 * Math.cos(t) - 5 * Math.cos(2 * t) - 2 * Math.cos(3 * t) - Math.cos(4 * t)
                out.push(fromShape(x * k, 0.5 * size - (y + 17) * k))
            }
        }
        return out
    }

    function preview(changes) {
        draft = Object.assign({}, mask, changes)
        project.previewMask(clipInfo.id, active, changes)
    }
    function commit() {
        if (draft === null)
            return
        const d = draft
        draft = null
        project.setMask(clipInfo.id, active, d.shape === "pen"
                        ? { points: d.points }
                        : { x: d.x, y: d.y, width: d.width, height: d.height, rotation: d.rotation })
    }

    onMaskChanged: shapeCanvas.requestPaint()
    onCanvasRectChanged: shapeCanvas.requestPaint()
    onPlacementChanged: shapeCanvas.requestPaint()

    Canvas {
        id: shapeCanvas
        objectName: "maskOutline"
        anchors.fill: parent
        visible: overlay.editing
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            const points = overlay.outline()
            if (points.length < 2)
                return
            ctx.lineWidth = 1.5
            ctx.strokeStyle = Theme.color.accent
            ctx.setLineDash([5, 4])
            ctx.beginPath()
            ctx.moveTo(points[0].x, points[0].y)
            for (let i = 1; i < points.length; ++i)
                ctx.lineTo(points[i].x, points[i].y)
            if (overlay.mask.shape !== "line")
                ctx.closePath()
            ctx.stroke()
        }
        Connections { target: Theme; function onDarkChanged() { shapeCanvas.requestPaint() } }
    }

    // Pen masks: a click on the picture adds a point after the nearest edge.
    MouseArea {
        objectName: "penArea"
        anchors.fill: parent
        enabled: overlay.editing && overlay.mask.shape === "pen" && !overlay.picking
        onClicked: (mouse) => {
            const points = overlay.mask.points
            if (points.length >= 32)
                return
            const p = overlay.toPicture(mouse.x, mouse.y)
            let best = 0, bestDistance = Infinity
            for (let i = 0; i < points.length; ++i) {
                const a = overlay.fromPicture(points[i][0], points[i][1])
                const next = points[(i + 1) % points.length]
                const b = overlay.fromPicture(next[0], next[1])
                const ex = b.x - a.x, ey = b.y - a.y
                const t = Math.max(0, Math.min(1, ((mouse.x - a.x) * ex + (mouse.y - a.y) * ey) / Math.max(ex * ex + ey * ey, 1e-6)))
                const d = Math.hypot(mouse.x - a.x - t * ex, mouse.y - a.y - t * ey)
                if (d < bestDistance) {
                    bestDistance = d
                    best = i
                }
            }
            const added = points.slice()
            added.splice(best + 1, 0, [p.x, p.y])
            overlay.project.setMask(overlay.clipInfo.id, overlay.active, { points: added })
        }
    }

    // Shapes: move from the centre, size from the corner, rotate from the knob above.
    Handle {
        objectName: "maskCentre"
        visible: overlay.editing && overlay.mask.shape !== "pen"
        position: overlay.editing ? overlay.fromPicture(overlay.mask.x, overlay.mask.y) : Qt.point(0, 0)
        onDragged: (x, y) => {
            const p = overlay.toPicture(x, y)
            overlay.preview({ x: p.x, y: p.y })
        }
        onReleased: overlay.commit()
    }
    Handle {
        objectName: "maskSize"
        visible: overlay.editing && overlay.mask.shape !== "pen" && overlay.mask.shape !== "line"
        position: overlay.editing ? overlay.fromShape(overlay.mask.width * overlay.pictureWidth / 2,
                                                      overlay.mask.height * overlay.pictureHeight / 2) : Qt.point(0, 0)
        square: true
        onDragged: (x, y) => {
            const q = overlay.toShape(x, y)
            overlay.preview({ width: Math.min(4, Math.max(0.01, 2 * Math.abs(q.x) / overlay.pictureWidth)),
                              height: Math.min(4, Math.max(0.01, 2 * Math.abs(q.y) / overlay.pictureHeight)) })
        }
        onReleased: overlay.commit()
    }
    Handle {
        objectName: "maskRotate"
        visible: overlay.editing && overlay.mask.shape !== "pen"
        position: {
            if (!overlay.editing)
                return Qt.point(0, 0)
            // Above the shape's top edge (a line: off to one side), a fixed distance on screen.
            const centre = overlay.fromPicture(overlay.mask.x, overlay.mask.y)
            const top = overlay.mask.shape === "line" ? overlay.fromShape(0, -1) : overlay.fromShape(0, -overlay.mask.height * overlay.pictureHeight / 2)
            const dx = top.x - centre.x, dy = top.y - centre.y
            const length = Math.max(Math.hypot(dx, dy), 1e-6)
            const extra = overlay.mask.shape === "line" ? 40 : 24
            const reach = overlay.mask.shape === "line" ? 0 : length
            return Qt.point(centre.x + dx / length * (reach + extra), centre.y + dy / length * (reach + extra))
        }
        onDragged: (x, y) => {
            // In picture pixels, the knob sits at the shape's (0, -1) direction.
            const p = overlay.toPicture(x, y)
            const dx = (p.x - overlay.mask.x) * overlay.pictureWidth
            const dy = (p.y - overlay.mask.y) * overlay.pictureHeight
            overlay.preview({ rotation: Math.atan2(dx, -dy) * 180 / Math.PI })
        }
        onReleased: overlay.commit()
    }

    // Pen points: drag to move, double-click to remove (keeping at least three).
    Repeater {
        model: overlay.editing && overlay.mask.shape === "pen" ? overlay.mask.points.length : 0
        delegate: Handle {
            required property int index
            objectName: "penPoint:" + index
            position: {
                const p = overlay.mask.points[index]
                return p ? overlay.fromPicture(p[0], p[1]) : Qt.point(0, 0)
            }
            onDragged: (x, y) => {
                const p = overlay.toPicture(x, y)
                const points = overlay.mask.points.slice()
                points[index] = [p.x, p.y]
                overlay.preview({ points: points })
            }
            onReleased: overlay.commit()
            onDoubleClicked: {
                const points = overlay.mask.points.slice()
                if (points.length <= 3)
                    return
                points.splice(index, 1)
                overlay.project.setMask(overlay.clipInfo.id, overlay.active, { points: points })
            }
        }
    }

    // Chroma key: the next click on the picture picks the colour to remove.
    MouseArea {
        objectName: "keyPicker"
        anchors.fill: parent
        enabled: overlay.picking
        cursorShape: Qt.CrossCursor
        onClicked: (mouse) => {
            const p = overlay.toPicture(mouse.x, mouse.y)
            if (p.x < 0 || p.x > 1 || p.y < 0 || p.y > 1)
                return // outside the clip
            const color = overlay.project.clipColorAt(overlay.clipInfo.id, p.x, p.y)
            if (color.length === 3)
                overlay.project.setChromaKey(overlay.clipInfo.id, { enabled: true, color: color })
            overlay.project.pickingKeyColor = false
        }
    }

    component Handle: Rectangle {
        id: handle
        property point position
        property bool square: false
        signal dragged(real x, real y)
        signal released()
        signal doubleClicked()
        x: position.x - width / 2
        y: position.y - height / 2
        width: 12
        height: 12
        radius: square ? 2 : width / 2
        color: area.pressed ? Theme.color.accent : "white"
        border.color: Theme.color.accent
        border.width: 2

        MouseArea {
            id: area
            anchors.fill: parent
            anchors.margins: -6 // a bigger target than the dot
            cursorShape: Qt.SizeAllCursor
            preventStealing: true
            // Drags by the pointer's offset from the handle's centre, so grabbing it off-centre does not jump.
            property point grab
            onPressed: (mouse) => {
                const p = mapToItem(overlay, mouse.x, mouse.y)
                grab = Qt.point(p.x - handle.position.x, p.y - handle.position.y)
            }
            onPositionChanged: (mouse) => {
                const p = mapToItem(overlay, mouse.x, mouse.y)
                handle.dragged(p.x - grab.x, p.y - grab.y)
            }
            onReleased: handle.released()
            onDoubleClicked: handle.doubleClicked()
        }
    }
}
