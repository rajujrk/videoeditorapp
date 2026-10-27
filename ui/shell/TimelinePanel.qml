import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import TechieVideoEditor.Theme
import TechieVideoEditor.Player

// The timeline, bound to the project: tracks and clips from the model, drag to move
// (snapping to edges and the playhead), drag edges to trim, click/drag the ruler to seek,
// drop media from the library onto a track.
Rectangle {
    id: timeline
    required property var project

    property real pixelsPerSecond: 60
    readonly property real snapPixels: 8
    readonly property var tracks: project.tracks // top to bottom

    color: Theme.color.surface

    function trackColor(kind) {
        return kind === "video" ? Theme.color.trackVideo
             : kind === "audio" ? Theme.color.trackAudio
             : kind === "title" ? Theme.color.trackTitle : Theme.color.trackEffect
    }
    function trackHeight(kind) {
        return kind === "video" ? Theme.size.videoTrack : Theme.size.audioTrack
    }
    function rulerLabel(seconds) {
        const s = Math.floor(seconds)
        return String(Math.floor(s / 60)).padStart(2, "0") + ":" + String(s % 60).padStart(2, "0")
    }
    // Track under a y position in the lanes' coordinate space, or null.
    function trackAtY(y) {
        let top = 0
        for (const track of tracks) {
            const h = trackHeight(track.kind)
            if (y >= top && y < top + h)
                return track
            top += h
        }
        return null
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Divider { Layout.fillWidth: true }

        // Toolbar
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.size.toolBar
            Layout.leftMargin: Theme.space.s
            Layout.rightMargin: Theme.space.m
            spacing: Theme.space.xxs

            IconButton { iconName: "select"; tip: "Select (V)"; checkable: true; checked: true }
            IconButton {
                objectName: "splitButton"
                iconName: "split"
                tip: "Split at playhead (S)"
                enabled: timeline.project.duration > 0
                onClicked: timeline.project.splitAtPlayhead()
            }
            IconButton {
                objectName: "deleteButton"
                iconName: "delete"
                tip: timeline.project.selectedTransition > 0 ? "Remove selected transition (⌫)"
                                                             : "Delete selected clip (⌫) — closes the gap on the main track"
                enabled: timeline.project.selectedClip > 0 || timeline.project.selectedTransition > 0
                onClicked: timeline.project.selectedTransition > 0
                           ? timeline.project.removeTransition(timeline.project.selectedTransition)
                           : timeline.project.deleteClip(timeline.project.selectedClip)
            }
            Divider { implicitWidth: 1; implicitHeight: 18; Layout.leftMargin: Theme.space.xs; Layout.rightMargin: Theme.space.xs }
            IconButton { iconName: "magnet"; tip: "Main track closes gaps on delete"; checkable: true; checked: true; enabled: false }
            IconButton { id: snapToggle; iconName: "snap"; tip: "Snapping"; checkable: true; checked: true }
            IconButton {
                objectName: "keyframeToolButton"
                readonly property var info: timeline.project.selectedClipInfo
                readonly property bool keyHere: Object.values(info.keyframeStates || {}).indexOf("key") >= 0
                iconName: "keyframe"
                tip: timeline.project.selectedClip <= 0 ? "Select a clip to animate it"
                   : keyHere ? "Remove the selected clip's keyframe at the playhead"
                             : "Add a keyframe to the selected clip at the playhead"
                enabled: timeline.project.selectedClip > 0
                checked: keyHere
                onClicked: keyHere ? timeline.project.removeKeyframes(info.id)
                                   : timeline.project.addKeyframes(info.id)
            }
            Divider { implicitWidth: 1; implicitHeight: 18; Layout.leftMargin: Theme.space.xs; Layout.rightMargin: Theme.space.xs }
            IconButton {
                id: voiceOverButton
                objectName: "voiceOverButton"
                iconName: "mic"
                tip: "Record voice-over"
                checkable: true
                checked: voiceOver.visible
                onClicked: voiceOver.visible ? voiceOver.close() : voiceOver.open()

                VoiceOverPanel {
                    id: voiceOver
                    objectName: "voiceOverPanel"
                    project: timeline.project
                    y: -height - Theme.space.s
                }
            }

            Item { Layout.fillWidth: true }

            Text {
                text: timeline.project.timecode(timeline.project.duration)
                font: Theme.type.mono
                color: Theme.color.textSecondary
                Layout.rightMargin: Theme.space.m
            }
            IconButton { iconName: "zoom-out"; tip: "Zoom out"; onClicked: zoom.value = Math.max(zoom.from, zoom.value / 1.5) }
            Slider {
                id: zoom
                from: 8
                to: 400
                value: timeline.pixelsPerSecond
                implicitWidth: 120
                onValueChanged: timeline.pixelsPerSecond = value
            }
            IconButton { iconName: "zoom-in"; tip: "Zoom in"; onClicked: zoom.value = Math.min(zoom.to, zoom.value * 1.5) }
        }

        Divider { Layout.fillWidth: true }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // Track headers (fixed width: a layout whose children fill would otherwise
            // claim the spare width itself)
            ColumnLayout {
                Layout.preferredWidth: Theme.size.trackHeader
                Layout.minimumWidth: Theme.size.trackHeader
                Layout.maximumWidth: Theme.size.trackHeader
                Layout.fillWidth: false
                Layout.fillHeight: true
                spacing: 0

                Item { Layout.preferredHeight: Theme.size.ruler; Layout.fillWidth: true }

                Repeater {
                    model: timeline.project.trackModel
                    delegate: Rectangle {
                        required property var track
                        Layout.fillWidth: true
                        Layout.preferredHeight: timeline.trackHeight(track.kind)
                        color: Theme.color.surface

                        Rectangle { width: 3; height: parent.height - 8; anchors.verticalCenter: parent.verticalCenter; radius: 2; color: timeline.trackColor(track.kind) }

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: Theme.space.m
                            anchors.rightMargin: Theme.space.xs
                            spacing: 0

                            Icon {
                                name: track.kind === "audio" ? "audio" : track.kind === "title" ? "titles" : "media"
                                size: Theme.size.iconSmall
                            }
                            Text {
                                Layout.leftMargin: Theme.space.s
                                text: track.name
                                font: Theme.type.label
                                color: Theme.color.textPrimary
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                            }
                            Icon { visible: track.main; name: "magnet"; size: 14; color: Theme.color.accentText; Layout.rightMargin: Theme.space.xs }
                            IconButton {
                                objectName: "lock:" + track.name
                                iconName: "lock"
                                tip: track.locked ? "Unlock track" : "Lock track"
                                buttonSize: 22
                                checkable: true
                                checked: track.locked
                                onClicked: timeline.project.setTrackFlag(track.id, "locked", !track.locked)
                            }
                            IconButton {
                                visible: track.kind === "video"
                                objectName: "hide:" + track.name
                                iconName: "eye"
                                tip: track.hidden ? "Show track" : "Hide track"
                                buttonSize: 22
                                checkable: true
                                checked: track.hidden
                                onClicked: timeline.project.setTrackFlag(track.id, "hidden", !track.hidden)
                            }
                            IconButton {
                                objectName: "mute:" + track.name
                                iconName: "volume"
                                tip: track.muted ? "Unmute track" : "Mute track"
                                buttonSize: 22
                                checkable: true
                                checked: track.muted
                                onClicked: timeline.project.setTrackFlag(track.id, "muted", !track.muted)
                            }
                        }
                        Divider { anchors.bottom: parent.bottom; width: parent.width }
                    }
                }
                Item { Layout.fillHeight: true }
            }

            Divider { Layout.fillHeight: true; implicitWidth: 1 }

            // Ruler + lanes, scrolling horizontally together
            Flickable {
                id: lanes
                Layout.fillWidth: true
                Layout.fillHeight: true
                contentWidth: Math.max(width, (Math.max(timeline.project.duration, timeline.project.playhead) + 20) * timeline.pixelsPerSecond)
                contentHeight: height
                flickableDirection: Flickable.HorizontalFlick
                boundsBehavior: Flickable.StopAtBounds
                interactive: false // drags belong to clips and the ruler; scroll with the bar or trackpad
                clip: true
                ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded }

                WheelHandler {
                    acceptedDevices: PointerDevice.TouchPad | PointerDevice.Mouse
                    onWheel: (event) => {
                        const dx = event.angleDelta.x !== 0 ? event.angleDelta.x : event.angleDelta.y
                        lanes.contentX = Math.max(0, Math.min(lanes.contentWidth - lanes.width, lanes.contentX - dx))
                    }
                }

                // Keep the playhead in view while playing.
                Connections {
                    target: timeline.project
                    function onPlayheadChanged() {
                        if (!timeline.project.playing)
                            return
                        const x = timeline.project.playhead * timeline.pixelsPerSecond
                        if (x > lanes.contentX + lanes.width - 60 || x < lanes.contentX)
                            lanes.contentX = Math.max(0, x - 120)
                    }
                }

                Rectangle {
                    id: content
                    width: lanes.contentWidth
                    height: lanes.height
                    color: Theme.color.surfaceSunken

                    // Ruler: click or drag to seek
                    Rectangle {
                        id: ruler
                        objectName: "ruler"
                        width: parent.width
                        height: Theme.size.ruler
                        color: Theme.color.surface

                        readonly property real step: timeline.pixelsPerSecond >= 40 ? 1 : timeline.pixelsPerSecond >= 15 ? 5 : 15
                        Repeater {
                            model: Math.ceil(content.width / timeline.pixelsPerSecond / ruler.step)
                            delegate: Item {
                                required property int index
                                readonly property real seconds: index * ruler.step
                                readonly property bool major: Math.round(seconds) % (ruler.step * 5) === 0
                                x: seconds * timeline.pixelsPerSecond
                                height: ruler.height
                                Rectangle { width: 1; height: parent.major ? 10 : 5; anchors.bottom: parent.bottom; color: Theme.color.borderStrong }
                                Text {
                                    visible: parent.major
                                    x: 4
                                    y: 4
                                    text: timeline.rulerLabel(parent.seconds)
                                    font: Theme.type.caption
                                    color: Theme.color.textSecondary
                                }
                            }
                        }
                        Divider { anchors.bottom: parent.bottom; width: parent.width }

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.IBeamCursor
                            onPressed: (mouse) => timeline.project.seek(mouse.x / timeline.pixelsPerSecond)
                            onPositionChanged: (mouse) => {
                                if (pressed)
                                    timeline.project.seek(Math.max(0, mouse.x) / timeline.pixelsPerSecond)
                            }
                        }
                    }

                    // Lanes
                    Item {
                        id: laneArea
                        y: Theme.size.ruler
                        width: parent.width
                        height: parent.height - Theme.size.ruler

                        // Clicking empty lane space clears the selection.
                        MouseArea {
                            anchors.fill: parent
                            onPressed: {
                                timeline.project.selectedClip = 0
                                timeline.project.selectedTransition = 0
                            }
                        }

                        Column {
                            id: laneColumn
                            width: parent.width

                            Repeater {
                                model: timeline.project.trackModel
                                delegate: Item {
                                    id: lane
                                    required property var track
                                    width: laneColumn.width
                                    height: timeline.trackHeight(track.kind)

                                    Rectangle {
                                        anchors.fill: parent
                                        color: lane.track.main ? Theme.color.surface : "transparent"
                                        opacity: 0.4
                                    }
                                    // Hidden or muted lanes fade back; locked lanes take no edits.
                                    opacity: lane.track.hidden || lane.track.muted ? 0.45 : 1.0
                                    enabled: !lane.track.locked
                                    Divider { anchors.bottom: parent.bottom; width: parent.width }

                                    Repeater {
                                        model: lane.track.clips
                                        delegate: ClipItem {
                                            required property var modelData
                                            clip: modelData
                                            track: lane.track
                                        }
                                    }
                                    // Transitions sit over the cuts they span.
                                    Repeater {
                                        model: lane.track.clips.filter((c) => c.transition && c.transition.clipId !== undefined)
                                        delegate: TransitionMarker {
                                            required property var modelData
                                            transition: modelData.transition
                                        }
                                    }
                                }
                            }
                        }

                        // Media and transitions dragged from the library
                        DropArea {
                            anchors.fill: parent
                            keys: ["application/x-tve-media", "application/x-tve-transition", "application/x-tve-title"]
                            onDropped: (drop) => {
                                const track = timeline.trackAtY(drop.y)
                                if (!track)
                                    return
                                if (drop.keys.indexOf("application/x-tve-title") >= 0) {
                                    const style = drop.source && drop.source.titleStyle !== undefined
                                                ? drop.source.titleStyle
                                                : JSON.parse(drop.getDataAsString("application/x-tve-title"))
                                    let at = Math.max(0, drop.x / timeline.pixelsPerSecond)
                                    if (snapToggle.checked)
                                        at = timeline.project.snap(at, timeline.snapPixels / timeline.pixelsPerSecond)
                                    // Onto that track if it takes pictures, else wherever there is room.
                                    timeline.project.addTitle(style, track.kind === "audio" ? -1 : track.id, at)
                                    drop.accept()
                                    return
                                }
                                if (drop.keys.indexOf("application/x-tve-transition") >= 0) {
                                    // Onto the nearest cut within a few clip-edge widths.
                                    const id = drop.source && drop.source.transitionId !== undefined
                                             ? drop.source.transitionId
                                             : drop.getDataAsString("application/x-tve-transition")
                                    timeline.project.addTransitionAt(id, track.id, drop.x / timeline.pixelsPerSecond,
                                                                     40 / timeline.pixelsPerSecond)
                                    drop.accept()
                                    return
                                }
                                let start = Math.max(0, drop.x / timeline.pixelsPerSecond)
                                if (snapToggle.checked)
                                    start = timeline.project.snap(start, timeline.snapPixels / timeline.pixelsPerSecond)
                                const mediaId = drop.source && drop.source.mediaId !== undefined
                                              ? drop.source.mediaId
                                              : Number(drop.getDataAsString("application/x-tve-media"))
                                timeline.project.addClip(mediaId, track.id, start)
                                drop.accept()
                            }
                        }
                    }

                    // The take being recorded, from where it started to the playhead
                    Rectangle {
                        objectName: "recordingRange"
                        visible: timeline.project.recording
                        x: timeline.project.recordingStart * timeline.pixelsPerSecond
                        width: Math.max(0, timeline.project.playhead - timeline.project.recordingStart) * timeline.pixelsPerSecond
                        height: parent.height
                        z: 9
                        color: Qt.alpha(Theme.color.danger, 0.12)
                        Rectangle { width: parent.width; height: Theme.size.ruler; color: Qt.alpha(Theme.color.danger, 0.35) }
                        Text {
                            x: Theme.space.xs
                            y: Theme.space.xs
                            visible: parent.width > implicitWidth + Theme.space.s
                            text: "● REC"
                            font: Theme.type.caption
                            color: Theme.color.danger
                        }
                    }

                    // Playhead
                    Item {
                        x: timeline.project.playhead * timeline.pixelsPerSecond
                        height: parent.height
                        z: 10
                        Rectangle { x: -0.5; width: 1.5; height: parent.height; color: Theme.color.playhead }
                        Rectangle { x: -6; y: 0; width: 13; height: 14; radius: 3; color: Theme.color.playhead }
                    }
                }
            }
        }
    }

    // A transition over a cut: spans its window, centred on the lane. Click to select it.
    component TransitionMarker: AbstractButton {
        id: marker
        required property var transition
        readonly property bool selected: timeline.project.selectedTransition === transition.clipId

        objectName: "transition:" + transition.clipId
        x: transition.start * timeline.pixelsPerSecond
        width: Math.max(18, (transition.end - transition.start) * timeline.pixelsPerSecond)
        height: 22
        y: (parent.height - height) / 2
        z: 6
        hoverEnabled: true
        onClicked: timeline.project.selectedTransition = transition.clipId

        ToolTip {
            visible: marker.hovered
            delay: 500
            text: marker.transition.title + " · " + marker.transition.duration.toFixed(1) + " s"
        }
        background: Rectangle {
            radius: Theme.radius.s
            color: marker.selected ? Theme.color.accent : Qt.alpha(Theme.color.surfaceRaised, 0.92)
            border.width: marker.selected ? 2 : 1
            border.color: marker.selected ? Theme.color.selection : Theme.color.borderStrong
        }
        contentItem: Item {
            Icon {
                anchors.centerIn: parent
                name: "transitions"
                size: Theme.size.iconSmall
                color: marker.selected ? Theme.color.textOnAccent : Theme.color.textPrimary
            }
        }
    }

    // One clip. While dragging, it previews its new position locally and commits a single
    // move/trim command on release, so a drag is one undo step.
    component ClipItem: Item {
        id: item
        required property var clip
        required property var track

        property real previewStart: clip.start
        property real previewDuration: clip.duration
        property real previewYOffset: 0
        readonly property bool selected: timeline.project.selectedClip === clip.id
        readonly property real tolerance: timeline.snapPixels / timeline.pixelsPerSecond

        objectName: "clip:" + clip.id
        x: previewStart * timeline.pixelsPerSecond
        y: 3 + previewYOffset
        z: moveArea.pressed ? 5 : 1
        width: Math.max(4, previewDuration * timeline.pixelsPerSecond)
        height: parent.height - 6

        function snapped(value) {
            return snapToggle.checked ? timeline.project.snap(value, tolerance, clip.id) : value
        }

        Rectangle {
            id: body
            anchors.fill: parent
            radius: Theme.radius.s
            readonly property color kindColor: item.clip.isTitle ? Theme.color.trackTitle : timeline.trackColor(item.track.kind)
            color: Qt.alpha(kindColor, Theme.dark ? 0.55 : 0.45)
            border.width: item.selected ? 2 : 1
            border.color: item.selected ? Theme.color.selection : kindColor
            clip: true

            readonly property bool showsSound: item.clip.hasAudio && (item.track.kind === "audio" || item.clip.audioEnabled)

            // Thumbnail strip: one frame per tile, from the source time under the tile's centre.
            Row {
                id: strip
                visible: item.track.kind === "video" && !item.clip.isTitle
                anchors { left: parent.left; right: parent.right; top: parent.top; margins: 3; topMargin: 19 }
                height: parent.height - 22 - (body.showsSound ? 14 : 0)
                spacing: 1
                clip: true
                readonly property real aspect: item.clip.mediaWidth > 0 ? item.clip.mediaWidth / item.clip.mediaHeight : 16 / 9
                readonly property real tileWidth: Math.max(8, height * aspect)

                Repeater {
                    model: strip.visible ? Math.max(1, Math.ceil(strip.width / (strip.tileWidth + 1))) : 0
                    delegate: Rectangle {
                        required property int index
                        width: strip.tileWidth
                        height: strip.height
                        color: Qt.alpha("#000000", 0.25)
                        Image {
                            objectName: "thumb:" + item.clip.id + ":" + parent.index
                            anchors.fill: parent
                            asynchronous: true
                            fillMode: Image.PreserveAspectCrop
                            // Source time at the tile centre, rounded to 0.25 s so zooming reuses cached frames.
                            readonly property real offset: (parent.index + 0.5) * (strip.tileWidth + 1) / timeline.pixelsPerSecond
                            readonly property real sourceTime: {
                                const c = item.clip
                                if (c.freeze) return c.sourceIn
                                const span = c.duration * c.speed
                                const into = Math.min(offset, c.duration) * c.speed
                                return c.reversed ? c.sourceIn + span - into : c.sourceIn + into
                            }
                            source: item.clip.path === "" || strip.height < 4 ? ""
                                    : "image://tvethumb/" + encodeURIComponent(item.clip.path) + "@"
                                      + (Math.round(sourceTime * 4) / 4).toFixed(2) + "@" + Math.round(strip.height * 2)
                        }
                    }
                }
            }

            WaveformItem {
                objectName: "waveform:" + item.clip.id
                visible: body.showsSound
                anchors { left: parent.left; right: parent.right; bottom: parent.bottom; margins: 2 }
                height: item.track.kind === "audio" ? parent.height - 20 : 14
                source: item.clip.path
                sourceIn: item.clip.sourceIn
                duration: item.previewDuration
                speed: item.clip.speed
                reversed: item.clip.reversed
                gainDb: item.clip.gainDb
                fadeIn: item.clip.fadeIn
                fadeOut: item.clip.fadeOut
                color: Theme.dark ? Qt.alpha("#FFFFFF", 0.6) : Qt.alpha("#000000", 0.45)
            }

            // Fade ramps: dark wedges over the faded part of the clip
            Canvas {
                anchors.fill: parent
                visible: item.clip.fadeIn > 0 || item.clip.fadeOut > 0
                property real fadeInPx: item.clip.fadeIn * timeline.pixelsPerSecond
                property real fadeOutPx: item.clip.fadeOut * timeline.pixelsPerSecond
                onFadeInPxChanged: requestPaint()
                onFadeOutPxChanged: requestPaint()
                onWidthChanged: requestPaint()
                onPaint: {
                    const ctx = getContext("2d")
                    ctx.reset()
                    ctx.fillStyle = Qt.rgba(0, 0, 0, 0.3)
                    if (fadeInPx > 0) {
                        ctx.beginPath(); ctx.moveTo(0, 0); ctx.lineTo(fadeInPx, 0); ctx.lineTo(0, height); ctx.closePath(); ctx.fill()
                    }
                    if (fadeOutPx > 0) {
                        ctx.beginPath(); ctx.moveTo(width, 0); ctx.lineTo(width - fadeOutPx, 0); ctx.lineTo(width, height); ctx.closePath(); ctx.fill()
                    }
                }
            }

            Rectangle {
                x: 3
                y: 3
                width: Math.min(clipLabel.implicitWidth + Theme.space.s * 2, parent.width - 6)
                height: clipLabel.implicitHeight + 2
                radius: 3
                color: Qt.alpha("#000000", 0.45)
                Text {
                    id: clipLabel
                    anchors.verticalCenter: parent.verticalCenter
                    x: Theme.space.s - 3
                    width: parent.width - Theme.space.s
                    // Badges first so a short clip still shows them.
                    text: (item.clip.isTitle ? "Title · " : "")
                          + (item.clip.freeze ? "Still · " : "")
                          + (!item.clip.freeze && item.clip.speed !== 1 ? Number(item.clip.speed.toFixed(2)) + "× · " : "")
                          + (item.clip.reversed ? "Reverse · " : "")
                          + item.clip.label
                    font: Theme.type.caption
                    color: "#FFFFFF"
                    elide: Text.ElideRight
                }
            }
        }

        // Body: select and move (horizontally and to another track of the same kind)
        MouseArea {
            id: moveArea
            anchors.fill: parent
            anchors.leftMargin: 6
            anchors.rightMargin: 6
            cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
            property real pressX: 0
            property real pressY: 0
            property bool moved: false

            onPressed: (mouse) => {
                timeline.project.selectedClip = item.clip.id
                const p = mapToItem(content, mouse.x, mouse.y)
                pressX = p.x
                pressY = p.y
                moved = false
            }
            onPositionChanged: (mouse) => {
                const p = mapToItem(content, mouse.x, mouse.y)
                if (!moved && Math.abs(p.x - pressX) < 3 && Math.abs(p.y - pressY) < 3)
                    return
                moved = true
                const proposed = Math.max(0, item.clip.start + (p.x - pressX) / timeline.pixelsPerSecond)
                const byStart = item.snapped(proposed)
                const byEnd = item.snapped(proposed + item.clip.duration) - item.clip.duration
                item.previewStart = byStart !== proposed ? byStart : byEnd
                item.previewYOffset = p.y - pressY
            }
            onReleased: (mouse) => {
                if (moved) {
                    const p = mapToItem(laneArea, mouse.x, mouse.y)
                    const target = timeline.trackAtY(p.y) || item.track
                    const start = item.previewStart
                    item.previewYOffset = 0
                    if (!timeline.project.moveClip(item.clip.id, target.id, start))
                        item.previewStart = item.clip.start
                }
            }
            onCanceled: { item.previewStart = item.clip.start; item.previewYOffset = 0 }
        }

        // Edges: trim
        Repeater {
            model: ["start", "end"]
            delegate: MouseArea {
                required property string modelData
                readonly property bool isStart: modelData === "start"
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                x: isStart ? 0 : item.width - width
                width: 7
                cursorShape: Qt.SizeHorCursor
                hoverEnabled: true

                Rectangle {
                    anchors.fill: parent
                    visible: parent.containsMouse || parent.pressed
                    radius: 2
                    color: Theme.color.selection
                    opacity: 0.6
                }

                onPressed: timeline.project.selectedClip = item.clip.id
                onPositionChanged: (mouse) => {
                    if (!pressed)
                        return
                    const t = item.snapped(mapToItem(content, mouse.x, 0).x / timeline.pixelsPerSecond)
                    const clipEnd = item.clip.start + item.clip.duration
                    // Unused media on each side, in timeline seconds; stills and titles can grow freely.
                    const c = item.clip
                    const before = c.sourceIn / c.speed
                    const after = (c.mediaDuration - c.sourceIn) / c.speed - c.duration
                    const unlimited = c.freeze || c.isTitle
                    const roomBefore = unlimited ? Infinity : c.reversed ? after : before
                    const roomAfter = unlimited ? Infinity : c.reversed ? before : after
                    if (isStart) {
                        // Can't extend past the media's unused frames or past the end edge.
                        const start = Math.min(Math.max(t, c.start - roomBefore, 0), clipEnd - 1 / 30)
                        item.previewStart = start
                        item.previewDuration = clipEnd - start
                    } else {
                        const latest = clipEnd + roomAfter
                        item.previewDuration = Math.min(Math.max(t, c.start + 1 / 30), latest) - c.start
                    }
                }
                onReleased: {
                    const ok = isStart ? timeline.project.trimClip(item.clip.id, true, item.previewStart)
                                       : timeline.project.trimClip(item.clip.id, false, item.previewStart + item.previewDuration)
                    if (!ok) {
                        item.previewStart = item.clip.start
                        item.previewDuration = item.clip.duration
                    }
                }
            }
        }

        // Keyframes along the bottom edge: click to go to one, drag to move it in time.
        Repeater {
            model: item.clip.keyframeTimes || []
            delegate: Item {
                id: diamond
                required property real modelData
                required property int index
                property real previewTime: modelData
                objectName: "keyframeMark:" + item.clip.id + ":" + index
                width: 12
                height: 12
                x: previewTime * timeline.pixelsPerSecond - width / 2
                y: item.height - height - 2
                z: 6
                visible: x > -width && x < item.width

                Rectangle {
                    anchors.centerIn: parent
                    width: 7
                    height: 7
                    rotation: 45
                    color: keyMouse.pressed || keyMouse.containsMouse ? Theme.color.accent : Theme.color.selection
                    border.color: Theme.color.canvas
                }
                MouseArea {
                    id: keyMouse
                    anchors.fill: parent
                    anchors.margins: -3
                    hoverEnabled: true
                    cursorShape: Qt.SizeHorCursor
                    property real pressX: 0
                    onPressed: (mouse) => {
                        pressX = mapToItem(item, mouse.x, 0).x
                        timeline.project.selectedClip = item.clip.id
                    }
                    onPositionChanged: (mouse) => {
                        if (!pressed)
                            return
                        const x = mapToItem(item, mouse.x, 0).x
                        diamond.previewTime = Math.max(0, Math.min(item.clip.duration,
                                                                   diamond.modelData + (x - pressX) / timeline.pixelsPerSecond))
                    }
                    onReleased: {
                        if (Math.abs(diamond.previewTime - diamond.modelData) * timeline.pixelsPerSecond < 2) {
                            diamond.previewTime = diamond.modelData
                            timeline.project.seek(item.clip.start + diamond.modelData)
                        } else if (!timeline.project.moveKeyframes(item.clip.id, diamond.modelData, diamond.previewTime)) {
                            diamond.previewTime = diamond.modelData
                        }
                    }
                }
            }
        }

        // Fade handles: drag the small knobs at the top corners to set fade lengths.
        Repeater {
            model: item.clip.hasAudio && (item.track.kind === "audio" || item.clip.audioEnabled) ? ["in", "out"] : []
            delegate: Rectangle {
                id: knob
                required property string modelData
                readonly property bool isIn: modelData === "in"
                property real previewFade: isIn ? item.clip.fadeIn : item.clip.fadeOut
                objectName: "fade" + modelData + ":" + item.clip.id
                width: 10
                height: 10
                radius: 5
                y: -1
                x: isIn ? Math.min(previewFade * timeline.pixelsPerSecond, item.width - 10) - 1
                        : Math.max(item.width - previewFade * timeline.pixelsPerSecond - 10, 0) + 1
                color: Theme.color.selection
                border.color: Theme.color.canvas
                visible: item.selected || fadeMouse.containsMouse || previewFade > 0
                z: 6

                MouseArea {
                    id: fadeMouse
                    anchors.fill: parent
                    anchors.margins: -4
                    hoverEnabled: true
                    cursorShape: Qt.SizeHorCursor
                    onPositionChanged: (mouse) => {
                        if (!pressed)
                            return
                        const x = mapToItem(item, mouse.x, 0).x
                        const seconds = knob.isIn ? x / timeline.pixelsPerSecond
                                                  : (item.width - x) / timeline.pixelsPerSecond
                        const other = knob.isIn ? item.clip.fadeOut : item.clip.fadeIn
                        knob.previewFade = Math.max(0, Math.min(seconds, item.clip.duration - other))
                    }
                    onReleased: {
                        const c = item.clip
                        timeline.project.setClipAudio(c.id, c.gainDb, knob.isIn ? knob.previewFade : c.fadeIn,
                                                      knob.isIn ? c.fadeOut : knob.previewFade)
                    }
                }
            }
        }
    }
}
