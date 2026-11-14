import QtQuick
import TechieVideoEditor.Theme
import TechieVideoEditor.Player

// Drawn over the player: the selected clip's frame on the canvas, to place it by hand. Drag
// inside the frame to move it (it snaps to the canvas's centre lines and its border's edges), a
// corner to scale it (snapping where an edge meets the border),
// the knob above it to rotate it (⇧: in 15° steps), the cross (the anchor point) with ⌥ to
// choose what it scales and turns about. With corner pinning on (project.pinningCorners), the corners move
// one by one, stretching the picture in perspective. Previewed while dragging, one undo step on
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
    // The placement while a handle is dragged: {x, y, scale, rotation, anchorX, anchorY, pin
    // (eight corner offsets: top left x, y, top right, bottom right, bottom left)}.
    property var draft: null
    readonly property var pinKeys: ["pinTopLeftX", "pinTopLeftY", "pinTopRightX", "pinTopRightY",
                                    "pinBottomRightX", "pinBottomRightY", "pinBottomLeftX", "pinBottomLeftY"]
    readonly property var shown: draft || ({ x: committed.x || 0, y: committed.y || 0, scale: committed.scale || 1,
                                              rotation: committed.rotation || 0, anchorX: committed.anchorX || 0,
                                              anchorY: committed.anchorY || 0,
                                              pin: pinKeys.map((k) => committed[k] || 0) })
    // Centre lines the move snapped to (drawn while dragging).
    property bool snappedX: false
    property bool snappedY: false

    // The canvas inside the item, fitted keeping its aspect ratio.
    readonly property real canvasScale: Math.min(width / project.canvasWidth, height / project.canvasHeight)
    readonly property rect canvasRect: Qt.rect((width - project.canvasWidth * canvasScale) / 2,
                                               (height - project.canvasHeight * canvasScale) / 2,
                                               project.canvasWidth * canvasScale, project.canvasHeight * canvasScale)

    // The frame's corners, centre and anchor point as item points, placed as the compositor
    // places the picture (engine/gpu/LayerPlacement.h): its visible part fitted to the canvas,
    // scaled and rotated clockwise about the anchor point, moved, its corners then pinned.
    readonly property var frame: showsFrame ? frameFor(shown) : null
    // The frame for a placement (shown, or one being tried while dragging).
    function frameFor(st) {
        const W = project.canvasWidth, H = project.canvasHeight
        const cropW = 1 - (committed.cropLeft || 0) - (committed.cropRight || 0)
        const cropH = 1 - (committed.cropTop || 0) - (committed.cropBottom || 0)
        const visibleW = placement.width * cropW, visibleH = placement.height * cropH
        const fit = Math.min(W / visibleW, H / visibleH)
        const fitW = visibleW * fit, fitH = visibleH * fit, k = st.scale
        const cx = W * (0.5 + st.x), cy = H * (0.5 + st.y)
        const ax = st.anchorX * fitW, ay = st.anchorY * fitH
        const a = st.rotation * Math.PI / 180, c = Math.cos(a), s = Math.sin(a)
        const toItem = (px, py) => Qt.point(canvasRect.x + px / W * canvasRect.width, canvasRect.y + py / H * canvasRect.height)
        // A point of the fitted picture (pixels from its centre) on the canvas.
        const place = (qx, qy) => [cx + ax + k * (c * (qx - ax) - s * (qy - ay)), cy + ay + k * (s * (qx - ax) + c * (qy - ay))]
        const pin = st.pin
        const corner = (u, v, i) => {
            const p = place((u - 0.5) * fitW, (v - 0.5) * fitH)
            return toItem(p[0] + pin[2 * i] * W, p[1] + pin[2 * i + 1] * H)
        }
        const middle = place(0, 0)
        // The rotation knob: above the top edge's middle, a fixed distance out (inside the
        // frame instead when that would leave the player, as for a picture filling it).
        const out = 28 / canvasScale, half = fitH * k / 2
        let knob = toItem(middle[0] + s * (half + out), middle[1] - c * (half + out))
        if (knob.x < 8 || knob.y < 8 || knob.x > width - 8 || knob.y > height - 8)
            knob = toItem(middle[0] + s * (half - out), middle[1] - c * (half - out))
        const top = place(0, -fitH / 2)
        return { corners: [corner(0, 0, 0), corner(1, 0, 1), corner(1, 1, 2), corner(0, 1, 3)],
                 centre: toItem(middle[0], middle[1]), pivot: toItem(cx + ax, cy + ay), knob: knob,
                 top: toItem(top[0], top[1]), fitW: fitW, fitH: fitH }
    }
    // The frame's extent (item points): left, top, right, bottom.
    function boundsOf(f) {
        const xs = f.corners.map((p) => p.x), ys = f.corners.map((p) => p.y)
        return { left: Math.min(...xs), top: Math.min(...ys), right: Math.max(...xs), bottom: Math.max(...ys) }
    }

    // The canvas's border: pictures snap to it while moved or scaled, and each edge a picture
    // reaches flashes and shakes with a tap on the trackpad.
    readonly property bool snapsToBorder: EditingPreferences.snapToBorder
    readonly property real snapDistance: 6 // item points
    property var touching: ({ left: false, top: false, right: false, bottom: false })
    property int bumps: 0 // edges reached while dragging (for tests)
    signal edgeReached(string edge)
    // The border's edges a frame lies on.
    function edgesOf(f) {
        const b = boundsOf(f), r = canvasRect, near = 0.75
        return { left: Math.abs(b.left - r.x) < near, top: Math.abs(b.top - r.y) < near,
                 right: Math.abs(b.right - (r.x + r.width)) < near, bottom: Math.abs(b.bottom - (r.y + r.height)) < near }
    }
    function feelEdges(f) {
        const now = edgesOf(f)
        const reached = ["left", "top", "right", "bottom"].filter((e) => now[e] && !touching[e])
        touching = now
        if (reached.length > 0) {
            bumps += reached.length
            project.alignmentFeedback()
            reached.forEach((e) => edgeReached(e))
        }
    }
    // A move pulled onto the border's edges (within a few points): the x, y to use.
    function snapMove(x, y) {
        if (!snapsToBorder)
            return [x, y]
        const b = boundsOf(frameFor(Object.assign({}, shown, draft || {}, { x: x, y: y }))), r = canvasRect
        const pull = (lo, hi, min, max) => Math.abs(lo - min) < snapDistance ? min - lo
                                         : Math.abs(hi - max) < snapDistance ? max - hi : 0
        return [x + pull(b.left, b.right, r.x, r.x + r.width) / r.width,
                y + pull(b.top, b.bottom, r.y, r.y + r.height) / r.height]
    }
    // A scale pulled to where an edge meets the border (scaling goes about the anchor point,
    // so each edge moves in proportion): the scale to use. Not for pinned corners.
    function snapScale(k) {
        if (!snapsToBorder || shown.pin.some((v) => v !== 0))
            return k
        const f = frameFor(Object.assign({}, shown, { scale: 1 })), b = boundsOf(f), p = f.pivot, r = canvasRect
        let best = k, gap = snapDistance
        for (const [edge, pivot, border] of [[b.left, p.x, r.x], [b.right, p.x, r.x + r.width],
                                             [b.top, p.y, r.y], [b.bottom, p.y, r.y + r.height]]) {
            const d = edge - pivot
            if (Math.abs(d) < 1e-6)
                continue
            const at = (border - pivot) / d
            if (at > 0 && Math.abs(at - k) * Math.abs(d) < gap) {
                gap = Math.abs(at - k) * Math.abs(d)
                best = at
            }
        }
        return best
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
        // (An edge the picture lay on as the drag began is not reached again.)
        if (draft === null && frame !== null)
            touching = edgesOf(frame)
        draft = Object.assign({}, shown, changes)
        feelEdges(frameFor(draft))
        const values = Object.assign({}, changes)
        if (changes.pin !== undefined) {
            delete values.pin
            pinKeys.forEach((key, i) => values[key] = changes.pin[i])
        }
        project.previewClipVisual(clipInfo.id, values)
    }
    function commit() {
        if (draft === null)
            return
        const d = draft
        draft = null
        snappedX = snappedY = false
        touching = { left: false, top: false, right: false, bottom: false }
        const values = { x: d.x, y: d.y, scale: d.scale, rotation: d.rotation, anchorX: d.anchorX, anchorY: d.anchorY }
        pinKeys.forEach((key, i) => values[key] = d.pin[i])
        project.setClipVisual(clipInfo.id, values)
    }

    // The canvas's border: a thin line round what is exported, its edges lighting up and
    // shaking as a picture reaches them.
    Item {
        id: frameBorder
        objectName: "canvasBorder"
        visible: EditingPreferences.canvasBorder && overlay.project.canvasWidth > 0 && overlay.width > 0
        x: overlay.canvasRect.x
        y: overlay.canvasRect.y
        width: overlay.canvasRect.width
        height: overlay.canvasRect.height
        Repeater {
            model: ["left", "top", "right", "bottom"]
            delegate: Rectangle {
                id: edge
                required property string modelData
                objectName: "canvasBorder:" + modelData
                readonly property bool across: modelData === "top" || modelData === "bottom"
                readonly property bool lit: overlay.touching[modelData] === true
                readonly property real thickness: lit ? 2 : 1
                property real shake: 0
                x: (modelData === "right" ? frameBorder.width - thickness / 2 : across ? 0 : -thickness / 2) + (across ? 0 : shake)
                y: (modelData === "bottom" ? frameBorder.height - thickness / 2 : across ? -thickness / 2 : 0) + (across ? shake : 0)
                width: across ? frameBorder.width : thickness
                height: across ? thickness : frameBorder.height
                color: lit ? Theme.color.accent : Qt.alpha("#FFFFFF", 0.35)
                Behavior on color { ColorAnimation { duration: 120 } }
                SequentialAnimation {
                    id: shaking
                    NumberAnimation { target: edge; property: "shake"; to: 2.5; duration: 30 }
                    NumberAnimation { target: edge; property: "shake"; to: -2.5; duration: 50 }
                    NumberAnimation { target: edge; property: "shake"; to: 1.5; duration: 40 }
                    NumberAnimation { target: edge; property: "shake"; to: 0; duration: 40 }
                }
                Connections {
                    target: overlay
                    function onEdgeReached(which) { if (which === edge.modelData) shaking.restart() }
                }
            }
        }
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
            // Else to the border's edges.
            const pulled = overlay.snapMove(x, y)
            if (!overlay.snappedX) x = pulled[0]
            if (!overlay.snappedY) y = pulled[1]
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
                cursorShape: overlay.project.pinningCorners ? Qt.CrossCursor
                           : handle.index % 2 === 0 ? Qt.SizeFDiagCursor : Qt.SizeBDiagCursor
                property real startScale: 1
                property real startDistance: 1
                property point startPoint
                property var startPin: []
                onPressed: (mouse) => {
                    const c = overlay.frame.pivot
                    const p = mapToItem(overlay, mouse.x, mouse.y)
                    startPoint = p
                    startPin = overlay.shown.pin.slice()
                    startScale = overlay.shown.scale
                    startDistance = Math.max(1, Math.hypot(p.x - c.x, p.y - c.y))
                }
                onPositionChanged: (mouse) => { if (pressed) follow(mouse) }
                // (On release too: the last move may not have arrived as a move.)
                function follow(mouse) {
                    const p = mapToItem(overlay, mouse.x, mouse.y)
                    if (overlay.project.pinningCorners) {
                        // This corner alone, by as much as the pointer moved (canvas fractions).
                        const pin = startPin.slice()
                        const clamp = (v) => Math.max(-2, Math.min(2, v))
                        pin[2 * handle.index] = clamp(startPin[2 * handle.index] + (p.x - startPoint.x) / overlay.canvasRect.width)
                        pin[2 * handle.index + 1] = clamp(startPin[2 * handle.index + 1] + (p.y - startPoint.y) / overlay.canvasRect.height)
                        overlay.preview({ pin: pin })
                        return
                    }
                    const c = overlay.frame.pivot
                    const scale = startScale * Math.hypot(p.x - c.x, p.y - c.y) / startDistance
                    overlay.preview({ scale: Math.max(0.05, Math.min(4, overlay.snapScale(scale))) })
                }
                onReleased: (mouse) => { follow(mouse); overlay.commit() }
                onCanceled: overlay.draft = null
            }
        }
    }

    // The anchor point: what scaling and turning go about. ⌥-dragging it leaves the picture
    // where it is (the position makes up for it).
    Item {
        objectName: "transformAnchor"
        visible: overlay.frame !== null
        // Drawn once moved from the centre (or while dragged); at the centre it is there for ⌥-drag
        // without marking every picture.
        opacity: overlay.shown.anchorX !== 0 || overlay.shown.anchorY !== 0 || anchorDrag.pressed ? 1 : 0
        readonly property point at: overlay.frame ? overlay.frame.pivot : Qt.point(0, 0)
        x: at.x - width / 2
        y: at.y - height / 2
        width: 18
        height: 18
        Rectangle { anchors.centerIn: parent; width: 12; height: 12; radius: 6; color: "transparent"; border.color: "#80000000"; border.width: 3 }
        Rectangle { anchors.centerIn: parent; width: 12; height: 12; radius: 6; color: "transparent"; border.color: "#FFFFFF"; border.width: 1.5 }
        Rectangle { anchors.centerIn: parent; width: 18; height: 1.5; color: "#FFFFFF" }
        Rectangle { anchors.centerIn: parent; width: 1.5; height: 18; color: "#FFFFFF" }
        MouseArea {
            id: anchorDrag
            anchors.fill: parent
            anchors.margins: -4
            cursorShape: Qt.SizeAllCursor
            property point startPoint
            property var start: null
            // ⌥-drag (a plain drag from the middle moves the picture, as before).
            onPressed: (mouse) => {
                if (!(mouse.modifiers & Qt.AltModifier)) {
                    mouse.accepted = false
                    return
                }
                startPoint = mapToItem(overlay, mouse.x, mouse.y)
                start = overlay.shown
            }
            onPositionChanged: (mouse) => {
                if (!pressed || start === null)
                    return
                const p = mapToItem(overlay, mouse.x, mouse.y)
                const W = overlay.project.canvasWidth, H = overlay.project.canvasHeight
                const toCanvas = W / overlay.canvasRect.width
                const dx = (p.x - startPoint.x) * toCanvas, dy = (p.y - startPoint.y) * toCanvas
                // The anchor moves by the pointer's move undone by the picture's turn and scale
                // (in the fitted picture's pixels); the position keeps the picture still.
                const a = start.rotation * Math.PI / 180, c = Math.cos(a), s = Math.sin(a), k = start.scale
                const fw = overlay.frame.fitW, fh = overlay.frame.fitH
                const ax = start.anchorX * fw + (c * dx + s * dy) / k
                const ay = start.anchorY * fh + (-s * dx + c * dy) / k
                const pivotX = W * (0.5 + start.x) + start.anchorX * fw + dx
                const pivotY = H * (0.5 + start.y) + start.anchorY * fh + dy
                const anchorX = Math.max(-1, Math.min(1, ax / fw)), anchorY = Math.max(-1, Math.min(1, ay / fh))
                overlay.preview({ anchorX: anchorX, anchorY: anchorY,
                                  x: (pivotX - anchorX * fw) / W - 0.5, y: (pivotY - anchorY * fh) / H - 0.5 })
            }
            onReleased: { overlay.commit(); start = null }
            onCanceled: { overlay.draft = null; start = null }
        }
    }

    // The knob: rotate about the anchor point.
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
                const c = overlay.frame.pivot
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
