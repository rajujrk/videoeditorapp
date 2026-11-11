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
    property bool hdrViewerOpen: false
    readonly property var hdrViewer: hdrViewerLoader.item

    // The HDR viewer, a window of its own, while open.
    Loader {
        id: hdrViewerLoader
        active: panel.hdrViewerOpen
        sourceComponent: HdrViewer {
            leader: video
            project: panel.project
            onClosing: panel.hdrViewerOpen = false
        }
    }

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

            // Move, scale and rotate the selected clip by hand (beneath the editing overlays).
            TransformOverlay {
                objectName: "transformOverlay"
                anchors.fill: video
                project: panel.project
                blocked: maskOverlay.visible || trackOverlay.visible || drawingOverlay.visible || sphereOverlay.visible
            }

            MaskOverlay {
                id: maskOverlay
                objectName: "maskOverlay"
                anchors.fill: video
                project: panel.project
            }

            TrackOverlay {
                id: trackOverlay
                objectName: "trackOverlay"
                anchors.fill: video
                project: panel.project
            }

            DrawingOverlay {
                id: drawingOverlay
                objectName: "drawingOverlay"
                anchors.fill: video
                project: panel.project
            }

            SphereOverlay {
                id: sphereOverlay
                objectName: "sphereOverlay"
                anchors.fill: video
                project: panel.project
                player: video
            }

            // Files from Finder: imported and added to the end; media from the library: placed
            // at the playhead (on a track with room there).
            DropArea {
                id: playerDrop
                objectName: "playerDrop"
                anchors.fill: parent
                keys: ["text/uri-list", "application/x-tve-media"]
                onDropped: (drop) => {
                    if (drop.keys.indexOf("application/x-tve-media") >= 0) {
                        const mediaId = drop.source && drop.source.mediaId !== undefined
                                      ? drop.source.mediaId : Number(drop.getDataAsString("application/x-tve-media"))
                        panel.project.addMediaAt(mediaId)
                        drop.accept()
                        return
                    }
                    for (const url of drop.urls)
                        panel.project.importAndAppend(url)
                    drop.accept()
                }
            }
            Rectangle {
                anchors.fill: video
                visible: playerDrop.containsDrag
                color: Qt.alpha(Theme.color.accent, 0.12)
                border.color: Theme.color.accent
                border.width: 2
                radius: Theme.radius.m
            }

            Column {
                anchors.centerIn: parent
                visible: panel.project.duration === 0
                spacing: Theme.space.s
                Icon { anchors.horizontalCenter: parent.horizontalCenter; name: "media"; size: 32; color: Theme.color.textDisabled }
                Text {
                    text: qsTr("Drop video files here, or import them in the media library")
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
                            + (video.hardwareAccelerated ? qsTr("VideoToolbox") : qsTr("software decode")) + "\n"
                            + qsTr("decode %1 fps · present %2 fps").arg(Fmt.num(video.decodeFps, 1)).arg(Fmt.num(video.presentFps, 1)) + "\n"
                            + qsTr("skipped %1 · effects %2").arg(video.skippedFrames).arg(video.enabledEffectCount)
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

                IconButton { iconName: "skip-back"; tip: qsTr("Previous frame") + KeyMap.hints["frameBack"]; enabled: panel.project.duration > 0; onClicked: panel.project.stepFrames(-1) }
                IconButton {
                    objectName: "playButton"
                    iconName: panel.project.playing ? "pause" : "play"
                    tip: (panel.project.playing ? qsTr("Pause") : qsTr("Play")) + KeyMap.hints["play"]
                    enabled: panel.project.duration > 0
                    buttonSize: Theme.size.control + 4
                    onClicked: panel.project.togglePlay()
                }
                IconButton { iconName: "skip-forward"; tip: qsTr("Next frame") + KeyMap.hints["frameForward"]; enabled: panel.project.duration > 0; onClicked: panel.project.stepFrames(1) }

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
                    objectName: "proxyToggle"
                    iconName: "proxy"
                    tip: panel.project.useProxies
                         ? qsTr("Playing proxies of large media where ready (export uses the originals)")
                         : qsTr("Playing the original media")
                    checkable: true
                    checked: panel.project.useProxies
                    onToggled: panel.project.useProxies = checked
                }
                IconButton {
                    objectName: "halfResolutionToggle"
                    iconName: "half-size"
                    tip: video.halfResolution
                         ? qsTr("Previewing at half resolution: heavy timelines play smoothly (export is full size)")
                         : qsTr("Previewing at full resolution; half resolution plays heavy timelines more smoothly")
                    checkable: true
                    checked: video.halfResolution
                    onToggled: video.halfResolution = checked
                }
                IconButton {
                    objectName: "lookAroundButton"
                    visible: panel.project.spherical
                    iconName: "sphere"
                    tip: video.lookAround ? qsTr("Showing a view to look around in (drag it); off: the whole 360° picture")
                                          : qsTr("Look around the 360° picture in the player (export is unchanged)")
                    checkable: true
                    checked: video.lookAround
                    onToggled: video.lookAround = checked
                }
                IconButton {
                    objectName: "hdrViewerButton"
                    iconName: "hdr"
                    tip: panel.project.projectColour === "sdr"
                         ? qsTr("HDR viewer: the picture in a window of its own, HDR on an HDR display (this project is SDR)")
                         : qsTr("HDR viewer: the picture in a window of its own, HDR on an HDR display")
                    checkable: true
                    checked: panel.hdrViewerOpen
                    onToggled: panel.hdrViewerOpen = checked
                }
                IconButton {
                    iconName: "stats"
                    tip: qsTr("Playback statistics")
                    checkable: true
                    checked: panel.showStats
                    onToggled: panel.showStats = checked
                }
                IconButton { iconName: "fit"; tip: qsTr("Fit to window"); checkable: true; checked: true; enabled: false }
            }
        }
    }
}
