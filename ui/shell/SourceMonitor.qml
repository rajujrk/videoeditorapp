import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import TechieVideoEditor.Theme
import TechieVideoEditor.Player

// The source monitor: one media item played on its own (a player of its own), to choose a
// part with In and Out before putting it on the timeline — inserted at the playhead (later
// clips move), over what is there, or after the last clip.
Rectangle {
    id: monitor
    required property var project // the editor's ProjectController, where the clips go
    property var media: null      // the library's item: {id, name, path, duration, hasVideo…}
    property real topInset: 0     // room left at the top (for the player's tabs)
    // Its own controller (a project of its own holding just the media; never autosaved).
    readonly property var controller: holder.item
    readonly property bool ready: media !== null && controller !== null && controller.duration > 0

    // Opens a media item (a new player each time: its canvas and frame rate are the media's).
    function load(item) {
        if (controller)
            controller.pause()
        media = item
        holder.active = false
        holder.active = true
        controller.importAndAppend("file://" + item.path)
        controller.seek(0)
    }
    // The part from In to Out (all of it without marks) onto the timeline.
    function place(mode) {
        if (!ready)
            return 0
        const c = controller
        const from = c.markIn >= 0 ? c.markIn : 0
        const to = c.markOut >= 0 ? c.markOut : 0
        return monitor.project.placeMediaRange(monitor.media.id, from, to, mode)
    }

    color: Theme.color.surfaceSunken

    Loader {
        id: holder
        active: false
        sourceComponent: ProjectController {
            autosaveEnabled: false
            view: sourceView
        }
    }

    // (Clicks stay here: the editing overlays under it are for the timeline.)
    MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons; onWheel: (wheel) => wheel.accepted = true }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        VideoView {
            id: sourceView
            objectName: "sourcePlayer"
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: Theme.space.s
            Layout.topMargin: Theme.space.s + monitor.topInset
        }

        // Scrubber: the media's whole length, the playhead, In to Out shaded.
        Item {
            objectName: "sourceScrubber"
            Layout.fillWidth: true
            Layout.leftMargin: Theme.space.m
            Layout.rightMargin: Theme.space.m
            implicitHeight: 22
            readonly property real length: monitor.ready ? monitor.controller.duration : 1
            function at(x) { return Math.max(0, Math.min(length, x / width * length)) }
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width
                height: 6
                radius: 3
                color: Theme.color.surfaceRaised
                border.color: Theme.color.borderSubtle
            }
            Rectangle {
                objectName: "sourceRange"
                readonly property real from: monitor.ready && monitor.controller.markIn >= 0 ? monitor.controller.markIn : 0
                readonly property real to: monitor.ready && monitor.controller.markOut >= 0 ? monitor.controller.markOut : parent.length
                visible: monitor.ready && (monitor.controller.markIn >= 0 || monitor.controller.markOut >= 0)
                anchors.verticalCenter: parent.verticalCenter
                x: from / parent.length * parent.width
                width: Math.max(2, (to - from) / parent.length * parent.width)
                height: 10
                radius: 3
                color: Qt.alpha(Theme.color.accent, 0.35)
                border.color: Theme.color.accent
            }
            Rectangle {
                objectName: "sourcePlayhead"
                x: (monitor.ready ? monitor.controller.playhead : 0) / parent.length * parent.width - width / 2
                width: 2
                height: parent.height
                color: Theme.color.playhead
            }
            MouseArea {
                anchors.fill: parent
                enabled: monitor.ready
                cursorShape: Qt.PointingHandCursor
                onPressed: (mouse) => monitor.controller.seek(parent.at(mouse.x))
                onPositionChanged: (mouse) => { if (pressed) monitor.controller.seek(parent.at(mouse.x)) }
            }
        }

        // The media's name, its marked part, and where it goes.
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: Theme.space.s
            Layout.leftMargin: Theme.space.m
            spacing: Theme.space.s
            Text {
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                readonly property var c: monitor.controller
                text: !monitor.media ? ""
                      : monitor.ready && (c.markIn >= 0 || c.markOut >= 0)
                        ? qsTr("%1 · %2 marked").arg(monitor.media.name)
                              .arg(c.timecode((c.markOut >= 0 ? c.markOut : c.duration) - (c.markIn >= 0 ? c.markIn : 0)))
                        : qsTr("%1 · mark In and Out (I, O) to take a part").arg(monitor.media.name)
                font: Theme.type.caption
                color: Theme.color.textSecondary
                elide: Text.ElideMiddle
            }
            TextButton {
                objectName: "sourceInsert"
                text: qsTr("Insert")
                variant: "primary"
                enabled: monitor.ready
                tip: qsTr("At the playhead on the main track; later clips move to make room")
                onClicked: monitor.place("insert")
            }
            TextButton {
                objectName: "sourceOverwrite"
                text: qsTr("Overwrite")
                variant: "secondary"
                enabled: monitor.ready
                tip: qsTr("At the playhead on the main track, over what is there")
                onClicked: monitor.place("overwrite")
            }
            TextButton {
                objectName: "sourceAppend"
                text: qsTr("Append")
                variant: "secondary"
                enabled: monitor.ready
                tip: qsTr("After the last clip on the main track")
                onClicked: monitor.place("append")
            }
        }
    }
}
