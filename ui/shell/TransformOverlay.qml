import QtQuick
import TechieVideoEditor.Theme

// Drawn over the player: the selected clip's frame on the canvas, to place it by hand. Drag
// inside the frame to move it (it snaps to the canvas's centre lines), a corner to scale it,
// the knob above it to rotate it (⇧: in 15° steps). Previewed while dragging, one undo step on
// release. Clicking a picture in the player selects its clip.
Item {
    id: overlay
    required property var project
    property bool blocked: false // another overlay is editing (a mask, a tracked area, a drawing, 360°)

    readonly property var clipInfo: project.selectedClipInfo
    readonly property var placement: {
        project.playhead // (animated placements change with it)
        return clipInfo.id === undefined ? ({}) : project.clipPlacement(clipInfo.id)
    }
    readonly property bool onScreen: clipInfo.id !== undefined && clipInfo.trackKind !== "audio"
                                     && clipInfo.isAdjustment !== true && clipInfo.locked !== true
                                     && project.playhead >= clipInfo.start && project.playhead < clipInfo.start + clipInfo.duration
    readonly property bool showsFrame: placement.m !== undefined && onScreen && !project.playing && !blocked
    readonly property var committed: clipInfo.visual || ({})
    // The placement while a handle is dragged: {x, y, scale, rotation}.
    property var draft: null
    readonly property var shown: draft || ({ x: committed.x || 0, y: committed.y || 0, scale: committed.scale || 1,
                                              rotation: committed.rotation || 0 })
    // Centre lines the move snapped to (drawn while dragging).
    property bool snappedX: false
    property bool snappedY: false

    // The canvas inside the item, fitted keeping its aspect ratio.
    readonly property real canvasScale: Math.min(width / project.canvasWidth, height / project.canvasHeight)
    readonly property rect canvasRect: Qt.rect((width - project.canvasWidth * canvasScale) / 2,
                                               (height - project.canvasHeight * canvasScale) / 2,
                                               project.canvasWidth * canvasScale, project.canvasHeight * canvasScale)

    // The frame's corners and centre as item points, placed as the compositor places the
    // picture (engine/gpu/LayerPlacement.h): its visible part fitted to the canvas, scaled,
    // rotated clockwise about its centre and moved.
    readonly property var frame: {
        if (!showsFrame)
            return null
        const W = project.canvasWidth, H = project.canvasHeight
        const cropW = 1 - (committed.cropLeft || 0) - (committed.cropRight || 0)
        const cropH = 1 - (committed.cropTop || 0) - (committed.cropBottom || 0)
        const visibleW = placement.width * cropW, visibleH = placement.height * cropH
        const fit = Math.min(W / visibleW, H / visibleH)
        const shownW = visibleW * fit * shown.scale, shownH = visibleH * fit * shown.scale
        const cx = W * (0.5 + shown.x), cy = H * (0.5 + shown.y)
        const a = shown.rotation * Math.PI / 180, c = Math.cos(a), s = Math.sin(a)
        const toItem = (px, py) => Qt.point(canvasRect.x + px / W * canvasRect.width, canvasRect.y + py / H * canvasRect.height)
        const corner = (u, v) => {
            const dx = (u - 0.5) * shownW, dy = (v - 0.5) * shownH
            return toItem(cx + c * dx - s * dy, cy + s * dx + c * dy)
        }
        // The rotation knob: above the top edge's middle, a fixed distance out (inside the
        // frame instead when that would leave the player, as for a picture filling it).
        const out = 28 / canvasScale
        let knob = toItem(cx + s * (shownH / 2 + out), cy - c * (shownH / 2 + out))
        if (knob.x < 8 || knob.y < 8 || knob.x > width - 8 || knob.y > height - 8)
            knob = toItem(cx + s * (shownH / 2 - out), cy - c * (shownH / 2 - out))
        return { corners: [corner(0, 0), corner(1, 0), corner(1, 1), corner(0, 1)], centre: toItem(cx, cy),
                 knob: knob, top: corner(0.5, 0) }
    }
    onFrameChanged: outline.requestPaint()

    // Whether an item point is inside the frame (a convex quadrilateral).
    function inside(p) {
        if (!frame)
            return false
        const q = frame.corners
        let sign = 0
        for (let i = 0; i < 4; ++i) {
            const a = q[i], b = q[(i + 1) % 4]
            const cross = (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x)
            if (cross !== 0) {
                if (sign !== 0 && Math.sign(cross) !== sign)
                    return false
                sign = Math.sign(cross)
            }
        }
        return true
    }
    function preview(changes) {
        draft = Object.assign({}, shown, changes)
        project.previewClipVisual(clipInfo.id, changes)
    }
    function commit() {
        if (draft === null)
            return
        const d = draft
        draft = null
        snappedX = snappedY = false
        project.setClipVisual(clipInfo.id, { x: d.x, y: d.y, scale: d.scale, rotation: d.rotation })
    }

    Canvas {
        id: outline
        anchors.fill: parent
        visible: overlay.frame !== null
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            const f = overlay.frame
            if (!f)
                return
            // Centre lines the move snapped to.
            ctx.strokeStyle = Theme.color.warning
            ctx.lineWidth = 1
            const r = overlay.canvasRect
            if (overlay.snappedX) {
                ctx.beginPath(); ctx.moveTo(r.x + r.width / 2, r.y); ctx.lineTo(r.x + r.width / 2, r.y + r.height); ctx.stroke()
            }
            if (overlay.snappedY) {
                ctx.beginPath(); ctx.moveTo(r.x, r.y + r.height / 2); ctx.lineTo(r.x + r.width, r.y + r.height / 2); ctx.stroke()
            }
            // The frame, on a dark line so it shows on any picture.
            for (const pass of [{ colour: "#80000000", width: 3 }, { colour: String(Theme.color.accentText), width: 1.5 }]) {
                ctx.strokeStyle = pass.colour
                ctx.lineWidth = pass.width
                ctx.beginPath()
                ctx.moveTo(f.corners[0].x, f.corners[0].y)
                for (let i = 1; i < 4; ++i)
                    ctx.lineTo(f.corners[i].x, f.corners[i].y)
                ctx.closePath()
                ctx.stroke()
                ctx.beginPath()
                ctx.moveTo(f.top.x, f.top.y)
                ctx.lineTo(f.knob.x, f.knob.y)
                ctx.stroke()
            }
        }
    }

    // Moving the clip, and selecting by clicking a picture.
    MouseArea {
        id: body
        objectName: "transformBody"
        anchors.fill: parent
        enabled: !overlay.blocked
        hoverEnabled: true
        cursorShape: overlay.draft !== null ? Qt.ClosedHandCursor
                   : overlay.frame !== null && overlay.inside(Qt.point(mouseX, mouseY)) ? Qt.SizeAllCursor : Qt.ArrowCursor
        property point press
        property var start: null
        onPressed: (mouse) => {
            // The topmost picture under the pointer is selected (a selected title, which
            // covers the canvas, keeps the selection inside its frame); inside the selected
            // clip's frame a drag moves it. Elsewhere the click goes through.
            const point = Qt.point(mouse.x, mouse.y)
            const r = overlay.canvasRect
            const hit = overlay.project.clipAtCanvas((mouse.x - r.x) / r.width, (mouse.y - r.y) / r.height)
            const keepTitle = overlay.clipInfo.isTitle === true && overlay.inside(point)
            start = null
            if (hit > 0 && hit !== overlay.clipInfo.id && !keepTitle) {
                overlay.project.selectedClip = hit
                return
            }
            if (overlay.frame === null || !overlay.inside(point)) {
                mouse.accepted = false
                return
            }
            press = point
            start = overlay.shown
        }
        onPositionChanged: (mouse) => {
            if (!pressed || start === null)
                return
            let x = start.x + (mouse.x - press.x) / overlay.canvasRect.width
            let y = start.y + (mouse.y - press.y) / overlay.canvasRect.height
            // To the canvas's centre lines, within a few pixels.
            const near = 6 / overlay.canvasRect.width, nearY = 6 / overlay.canvasRect.height
            overlay.snappedX = Math.abs(x) < near
            overlay.snappedY = Math.abs(y) < nearY
            if (overlay.snappedX) x = 0
            if (overlay.snappedY) y = 0
            overlay.preview({ x: Math.max(-1, Math.min(1, x)), y: Math.max(-1, Math.min(1, y)) })
        }
        onReleased: {
            if (start !== null)
                overlay.commit()
            start = null
        }
        onCanceled: { overlay.draft = null; start = null }
    }

    // Corners: scale about the centre.
    Repeater {
        model: overlay.frame ? 4 : 0
        delegate: Rectangle {
            id: handle
            required property int index
            objectName: "transformCorner:" + index
            readonly property point at: overlay.frame ? overlay.frame.corners[index] : Qt.point(0, 0)
            x: at.x - width / 2
            y: at.y - height / 2
            width: 12
            height: 12
            radius: 2
            color: "#FFFFFF"
            border.color: Theme.color.accent
            border.width: 2
            MouseArea {
                anchors.fill: parent
                anchors.margins: -6
                cursorShape: handle.index % 2 === 0 ? Qt.SizeFDiagCursor : Qt.SizeBDiagCursor
                property real startScale: 1
                property real startDistance: 1
                onPressed: (mouse) => {
                    const c = overlay.frame.centre
                    const p = mapToItem(overlay, mouse.x, mouse.y)
                    startScale = overlay.shown.scale
                    startDistance = Math.max(1, Math.hypot(p.x - c.x, p.y - c.y))
                }
                onPositionChanged: (mouse) => {
                    if (!pressed)
                        return
                    const c = overlay.frame.centre
                    const p = mapToItem(overlay, mouse.x, mouse.y)
                    const scale = startScale * Math.hypot(p.x - c.x, p.y - c.y) / startDistance
                    overlay.preview({ scale: Math.max(0.05, Math.min(4, scale)) })
                }
                onReleased: overlay.commit()
                onCanceled: overlay.draft = null
            }
        }
    }

    // The knob: rotate about the centre.
    Rectangle {
        objectName: "transformRotate"
        visible: overlay.frame !== null
        readonly property point at: overlay.frame ? overlay.frame.knob : Qt.point(0, 0)
        x: at.x - width / 2
        y: at.y - height / 2
        width: 14
        height: 14
        radius: 7
        color: Theme.color.accent
        border.color: "#FFFFFF"
        border.width: 2
        MouseArea {
            anchors.fill: parent
            anchors.margins: -6
            cursorShape: Qt.CrossCursor
            property real startRotation: 0
            property real startAngle: 0
            function angleAt(mouse) {
                const c = overlay.frame.centre
                const p = mapToItem(overlay, mouse.x, mouse.y)
                return Math.atan2(p.y - c.y, p.x - c.x) * 180 / Math.PI
            }
            onPressed: (mouse) => {
                startRotation = overlay.shown.rotation
                startAngle = angleAt(mouse)
            }
            onPositionChanged: (mouse) => {
                if (!pressed)
                    return
                let r = startRotation + angleAt(mouse) - startAngle
                r = ((r + 180) % 360 + 360) % 360 - 180
                if (mouse.modifiers & Qt.ShiftModifier)
                    r = Math.round(r / 15) * 15
                overlay.preview({ rotation: r })
            }
            onReleased: overlay.commit()
            onCanceled: overlay.draft = null
        }
    }
}
