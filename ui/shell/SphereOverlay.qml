import QtQuick
import TechieVideoEditor.Theme

// Looking around 360° video in the player by dragging (scroll to widen or narrow the view,
// double-click to face the front again). In a 360° project looking around in the player
// (`player.lookAround`), it moves the preview's own view; in a flat project, with a 360°
// clip selected, it sets where that clip looks — live while dragging, one undo step when let
// go (a keyframe at the playhead if the direction is animated).
Item {
    id: overlay
    required property var project
    required property var player // VideoView
    readonly property var clip: project.selectedClipInfo
    readonly property bool looking: project.spherical && player.lookAround
    readonly property bool reframing: !project.spherical && clip.spherical === true && project.activeMask < 0
    visible: looking || reframing

    // Where the clip looks while it is dragged (committed when let go).
    property var pending: null
    function view() {
        if (looking)
            return { yaw: player.lookYaw, pitch: player.lookPitch, fov: player.lookFov }
        const v = clip.visual || {}
        return { yaw: v.yaw || 0, pitch: v.pitch || 0, fov: v.fov === undefined ? 90 : v.fov }
    }
    function show(changes) {
        if (looking) {
            if (changes.yaw !== undefined) player.lookYaw = changes.yaw
            if (changes.pitch !== undefined) player.lookPitch = changes.pitch
            if (changes.fov !== undefined) player.lookFov = changes.fov
            return
        }
        pending = Object.assign(pending || {}, changes)
        project.previewClipVisual(clip.id, pending)
    }
    function commit() {
        if (looking || pending === null)
            return
        const changes = pending
        pending = null
        project.setClipVisual(clip.id, changes)
    }

    Timer { id: settle; interval: 400; onTriggered: overlay.commit() } // after the wheel stops

    MouseArea {
        objectName: "sphereDrag"
        anchors.fill: parent
        cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
        property point last
        property var start: null
        onPressed: (mouse) => {
            last = Qt.point(mouse.x, mouse.y)
            start = overlay.view()
        }
        onPositionChanged: (mouse) => {
            if (!pressed || start === null)
                return
            // The picture follows the pointer: dragging right turns the view left.
            const fov = start.fov
            const tall = fov * height / Math.max(1, width)
            start = { yaw: start.yaw - (mouse.x - last.x) / Math.max(1, width) * fov,
                      pitch: Math.max(-90, Math.min(90, start.pitch + (mouse.y - last.y) / Math.max(1, height) * tall)),
                      fov: fov }
            last = Qt.point(mouse.x, mouse.y)
            const yaw = ((start.yaw + 540) % 360 + 360) % 360 - 180
            overlay.show({ yaw: Math.round(yaw * 10) / 10, pitch: Math.round(start.pitch * 10) / 10 })
        }
        onReleased: {
            start = null
            overlay.commit()
        }
        onDoubleClicked: {
            overlay.show({ yaw: 0, pitch: 0 })
            overlay.commit()
        }
        onWheel: (wheel) => {
            const fov = Math.max(20, Math.min(150, overlay.view().fov - wheel.angleDelta.y / 24))
            overlay.show({ fov: Math.round(fov) })
            settle.restart()
        }
    }

    Rectangle {
        anchors { left: parent.left; bottom: parent.bottom; margins: Theme.space.m }
        width: hint.implicitWidth + Theme.space.m
        height: hint.implicitHeight + Theme.space.xs
        radius: Theme.radius.m
        color: Theme.color.overlay
        Text {
            id: hint
            objectName: "sphereHint"
            anchors.centerIn: parent
            font: Theme.type.caption
            color: "white" // always on the dark scrim
            text: (overlay.looking ? "360° view" : "Reframing") + " · drag to look around, scroll to zoom · "
                  + Math.round(overlay.view().yaw) + "°, " + Math.round(overlay.view().pitch) + "° · "
                  + Math.round(overlay.view().fov) + "° wide"
        }
    }
}
