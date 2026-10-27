import QtQuick
import QtQuick.Layouts
import TechieVideoEditor.Theme
import TechieVideoEditor.Player

// Center panel: the live preview and its transport bar.
Rectangle {
    id: panel
    required property var project // ProjectController
    property alias player: video
    property bool showStats: false

    color: Theme.color.surfaceSunken

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            VideoView {
                id: video
                objectName: "player"
                anchors.fill: parent
                anchors.margins: Theme.space.s
            }

            DropArea {
                anchors.fill: parent
                keys: ["text/uri-list"]
                onDropped: (drop) => {
                    for (const url of drop.urls)
                        panel.project.importAndAppend(url)
                }
            }

            Column {
                anchors.centerIn: parent
                visible: panel.project.duration === 0
                spacing: Theme.space.s
                Icon { anchors.horizontalCenter: parent.horizontalCenter; name: "media"; size: 32; color: Theme.color.textDisabled }
                Text {
                    text: "Drop video files here, or import them in the media library"
                    font: Theme.type.body
                    color: Theme.color.textSecondary
                }
            }

            Rectangle {
                anchors { left: parent.left; top: parent.top; margins: Theme.space.m + Theme.space.s }
                visible: panel.showStats
                color: Theme.color.overlay
                radius: Theme.radius.m
                width: stats.implicitWidth + Theme.space.l
                height: stats.implicitHeight + Theme.space.m

                Text {
                    id: stats
                    anchors.centerIn: parent
                    color: "white" // always on the dark scrim, in both themes
                    font: Theme.type.mono
                    text: video.errorString !== ""
                          ? video.errorString
                          : video.streamDescription + "\n"
                            + (video.hardwareAccelerated ? "VideoToolbox" : "software decode") + "\n"
                            + "decode " + video.decodeFps.toFixed(1) + " fps · present " + video.presentFps.toFixed(1) + " fps\n"
                            + "skipped " + video.skippedFrames + " · effects " + video.enabledEffectCount
                }
            }
        }

        // Transport bar
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: Theme.size.toolBar + Theme.space.xs
            color: Theme.color.surface

            Divider { anchors.top: parent.top; width: parent.width }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.space.m
                anchors.rightMargin: Theme.space.s
                spacing: Theme.space.xs

                Text {
                    objectName: "playheadTimecode"
                    text: panel.project.timecode(panel.project.playhead)
                    font: Theme.type.mono
                    color: Theme.color.textPrimary
                }
                Text {
                    text: "/ " + panel.project.timecode(panel.project.duration)
                    font: Theme.type.mono
                    color: Theme.color.textSecondary
                }

                Item { Layout.fillWidth: true }

                IconButton { iconName: "skip-back"; tip: "Previous frame (←)"; enabled: panel.project.duration > 0; onClicked: panel.project.stepFrames(-1) }
                IconButton {
                    objectName: "playButton"
                    iconName: panel.project.playing ? "pause" : "play"
                    tip: panel.project.playing ? "Pause (Space)" : "Play (Space)"
                    enabled: panel.project.duration > 0
                    buttonSize: Theme.size.control + 4
                    onClicked: panel.project.togglePlay()
                }
                IconButton { iconName: "skip-forward"; tip: "Next frame (→)"; enabled: panel.project.duration > 0; onClicked: panel.project.stepFrames(1) }

                Item { Layout.fillWidth: true }

                // Output level meter (peak, per channel)
                Column {
                    objectName: "levelMeter"
                    spacing: 2
                    Layout.rightMargin: Theme.space.s
                    Repeater {
                        model: [panel.project.levelLeft, panel.project.levelRight]
                        delegate: Rectangle {
                            required property real modelData
                            width: 64
                            height: 4
                            radius: 2
                            color: Theme.color.surfaceSunken
                            Rectangle {
                                width: parent.width * Math.min(1, parent.modelData)
                                height: parent.height
                                radius: 2
                                color: parent.modelData > 0.98 ? Theme.color.danger
                                     : parent.modelData > 0.7 ? Theme.color.warning : Theme.color.success
                            }
                        }
                    }
                }
                IconButton {
                    iconName: "stats"
                    tip: "Playback statistics"
                    checkable: true
                    checked: panel.showStats
                    onToggled: panel.showStats = checked
                }
                IconButton { iconName: "fit"; tip: "Fit to window"; checkable: true; checked: true; enabled: false }
            }
        }
    }
}
