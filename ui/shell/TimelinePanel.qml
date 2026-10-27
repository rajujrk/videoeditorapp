import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import TechieVideoEditor.Theme

// Bottom panel layout: toolbar, ruler, track headers and lanes. The clips are fixed
// sample data to show the visual language; Phase 1 binds this to the project model.
Rectangle {
    id: timeline
    color: Theme.color.surface

    property real pixelsPerSecond: 60
    property real playheadSeconds: 3.2

    readonly property var tracks: [
        { name: "Title 1", kind: "title", height: Theme.size.audioTrack },
        { name: "Video 2", kind: "video", height: Theme.size.videoTrack },
        { name: "Video 1", kind: "video", height: Theme.size.videoTrack, main: true },
        { name: "Audio 1", kind: "audio", height: Theme.size.audioTrack },
        { name: "Music", kind: "audio", height: Theme.size.audioTrack }
    ]
    readonly property var clips: [
        { track: 0, start: 1.0, length: 4.0, label: "Opening title" },
        { track: 1, start: 6.5, length: 5.0, label: "B-roll.mov" },
        { track: 2, start: 0.0, length: 6.5, label: "4k30_h264.mp4", effects: 2 },
        { track: 2, start: 6.5, length: 9.0, label: "4k60_h264.mp4" },
        { track: 2, start: 15.5, length: 5.5, label: "4k60_hevc_10bit.mp4" },
        { track: 3, start: 0.0, length: 6.5, label: "4k30_h264.mp4" },
        { track: 4, start: 0.0, length: 21.0, label: "Soundtrack.m4a" }
    ]

    function trackColor(kind) {
        return kind === "video" ? Theme.color.trackVideo
             : kind === "audio" ? Theme.color.trackAudio
             : kind === "title" ? Theme.color.trackTitle : Theme.color.trackEffect
    }
    function timecode(seconds) {
        const s = Math.floor(seconds)
        return String(Math.floor(s / 60)).padStart(2, "0") + ":" + String(s % 60).padStart(2, "0")
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
            IconButton { iconName: "split"; tip: "Split at playhead (S) — Phase 1"; enabled: false }
            IconButton { iconName: "delete"; tip: "Delete — Phase 1"; enabled: false }
            Divider { implicitWidth: 1; implicitHeight: 18; Layout.leftMargin: Theme.space.xs; Layout.rightMargin: Theme.space.xs }
            IconButton { iconName: "magnet"; tip: "Magnetic main track"; checkable: true; checked: true }
            IconButton { iconName: "snap"; tip: "Snapping"; checkable: true; checked: true }
            IconButton { iconName: "keyframe"; tip: "Keyframes — Phase 2"; enabled: false }

            Item { Layout.fillWidth: true }

            IconButton { iconName: "zoom-out"; tip: "Zoom out"; onClicked: zoom.value = Math.max(zoom.from, zoom.value / 1.5) }
            Slider {
                id: zoom
                from: 15
                to: 240
                value: timeline.pixelsPerSecond
                implicitWidth: 120
                onMoved: timeline.pixelsPerSecond = value
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
                    model: timeline.tracks
                    delegate: Rectangle {
                        required property var modelData
                        Layout.fillWidth: true
                        Layout.preferredHeight: modelData.height
                        color: Theme.color.surface

                        Rectangle { width: 3; height: parent.height - 8; anchors.verticalCenter: parent.verticalCenter; radius: 2; color: timeline.trackColor(modelData.kind) }

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: Theme.space.m
                            anchors.rightMargin: Theme.space.xs
                            spacing: 0

                            Icon {
                                name: modelData.kind === "audio" ? "audio" : modelData.kind === "title" ? "titles" : "media"
                                size: Theme.size.iconSmall
                            }
                            Text {
                                Layout.leftMargin: Theme.space.s
                                text: modelData.name
                                font: Theme.type.label
                                color: Theme.color.textPrimary
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                            }
                            Icon { visible: !!modelData.main; name: "magnet"; size: 14; color: Theme.color.accentText; Layout.rightMargin: Theme.space.xs }
                            IconButton { iconName: "lock"; tip: "Lock track"; buttonSize: 22; checkable: true }
                            IconButton { iconName: modelData.kind === "audio" ? "volume" : "eye"; tip: modelData.kind === "audio" ? "Mute" : "Hide"; buttonSize: 22; checkable: true }
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
                contentWidth: Math.max(width, 30 * timeline.pixelsPerSecond)
                contentHeight: height
                flickableDirection: Flickable.HorizontalFlick
                boundsBehavior: Flickable.StopAtBounds
                clip: true
                ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded }

                Rectangle {
                    width: lanes.contentWidth
                    height: lanes.height
                    color: Theme.color.surfaceSunken

                    // Ruler
                    Rectangle {
                        id: ruler
                        width: parent.width
                        height: Theme.size.ruler
                        color: Theme.color.surface

                        Repeater {
                            model: Math.ceil(lanes.contentWidth / timeline.pixelsPerSecond)
                            delegate: Item {
                                required property int index
                                x: index * timeline.pixelsPerSecond
                                height: ruler.height
                                Rectangle { width: 1; height: index % 5 === 0 ? 10 : 5; anchors.bottom: parent.bottom; color: Theme.color.borderStrong }
                                Text {
                                    visible: index % 5 === 0
                                    x: 4
                                    y: 4
                                    text: timeline.timecode(index)
                                    font: Theme.type.caption
                                    color: Theme.color.textSecondary
                                }
                            }
                        }
                        Divider { anchors.bottom: parent.bottom; width: parent.width }
                    }

                    // Lanes with clips
                    Column {
                        y: Theme.size.ruler
                        width: parent.width

                        Repeater {
                            model: timeline.tracks
                            delegate: Item {
                                id: lane
                                required property var modelData
                                required property int index
                                width: parent.width
                                height: modelData.height

                                Rectangle {
                                    anchors.fill: parent
                                    color: lane.modelData.main ? Theme.color.surface : "transparent"
                                    opacity: 0.4
                                }
                                Divider { anchors.bottom: parent.bottom; width: parent.width }

                                Repeater {
                                    model: timeline.clips.filter((c) => c.track === lane.index)
                                    delegate: Rectangle {
                                        required property var modelData
                                        x: modelData.start * timeline.pixelsPerSecond + 1
                                        y: 3
                                        width: modelData.length * timeline.pixelsPerSecond - 2
                                        height: lane.height - 6
                                        radius: Theme.radius.s
                                        color: Qt.alpha(timeline.trackColor(lane.modelData.kind), Theme.dark ? 0.55 : 0.45)
                                        border.color: timeline.trackColor(lane.modelData.kind)
                                        clip: true

                                        // Thumbnail strip (video) or waveform hint (audio)
                                        Row {
                                            visible: lane.modelData.kind === "video"
                                            anchors { left: parent.left; right: parent.right; bottom: parent.bottom; margins: 3 }
                                            height: parent.height - 22
                                            spacing: 2
                                            Repeater {
                                                model: Math.max(1, Math.floor((parent.width) / 44))
                                                Rectangle { width: 42; height: parent.height; radius: 2; color: Qt.alpha("#000000", 0.25) }
                                            }
                                        }
                                        Row {
                                            visible: lane.modelData.kind === "audio"
                                            anchors { left: parent.left; right: parent.right; bottom: parent.bottom; margins: 4 }
                                            height: parent.height - 18
                                            spacing: 2
                                            Repeater {
                                                model: Math.max(1, Math.floor(parent.width / 4))
                                                Rectangle {
                                                    required property int index
                                                    width: 2
                                                    height: 2 + Math.abs(Math.sin(index * 0.7) * Math.cos(index * 0.23)) * (parent.height - 2)
                                                    anchors.verticalCenter: parent.verticalCenter
                                                    color: Theme.dark ? Qt.alpha("#FFFFFF", 0.55) : Qt.alpha("#000000", 0.35)
                                                }
                                            }
                                        }

                                        // Label on a small scrim so it stays readable over thumbnails and waveforms
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
                                                text: parent.parent.modelData.label
                                                      + (parent.parent.modelData.effects ? "  · fx " + parent.parent.modelData.effects : "")
                                                font: Theme.type.caption
                                                color: "#FFFFFF"
                                                elide: Text.ElideRight
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }

                    // Playhead
                    Item {
                        x: timeline.playheadSeconds * timeline.pixelsPerSecond
                        height: parent.height
                        Rectangle { x: -0.5; width: 1.5; height: parent.height; color: Theme.color.playhead }
                        Rectangle { x: -6; y: 0; width: 13; height: 14; radius: 3; color: Theme.color.playhead }
                    }
                }
            }
        }
    }
}
