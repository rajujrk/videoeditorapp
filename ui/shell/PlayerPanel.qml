import QtQuick
import QtQuick.Layouts
import TechieVideoEditor.Theme
import TechieVideoEditor.Player

// Center panel: the live preview and its transport bar.
Rectangle {
    id: panel
    property alias player: video
    property bool showStats: true

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
                onDropped: (drop) => {
                    if (drop.hasUrls)
                        video.source = drop.urls[0]
                }
            }

            Column {
                anchors.centerIn: parent
                visible: video.source === ""
                spacing: Theme.space.s
                Icon { anchors.horizontalCenter: parent.horizontalCenter; name: "media"; size: 32; color: Theme.color.textDisabled }
                Text {
                    text: "Drop a video file here to preview it"
                    font: Theme.type.body
                    color: Theme.color.textSecondary
                }
            }

            Rectangle {
                anchors { left: parent.left; top: parent.top; margins: Theme.space.m + Theme.space.s }
                visible: panel.showStats && video.source !== ""
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
                    text: "00:00:00:00"
                    font: Theme.type.mono
                    color: Theme.color.textPrimary
                }
                Text {
                    text: "/ 00:00:20:00"
                    font: Theme.type.mono
                    color: Theme.color.textSecondary
                }

                Item { Layout.fillWidth: true }

                IconButton { iconName: "skip-back"; tip: "Previous frame (Phase 1)"; enabled: false }
                IconButton { iconName: "pause"; tip: "Play / pause (Phase 1)"; enabled: false; buttonSize: Theme.size.control + 4 }
                IconButton { iconName: "skip-forward"; tip: "Next frame (Phase 1)"; enabled: false }

                Item { Layout.fillWidth: true }

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
