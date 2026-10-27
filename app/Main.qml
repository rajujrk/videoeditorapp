import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import TechieVideoEditor.Player

ApplicationWindow {
    id: root
    width: 1440
    height: 820
    visible: true
    title: "TechieVideoEditor — Playback Spike"
    color: "#111316"

    readonly property color panelColor: "#1b1e23"
    readonly property color textColor: "#e6e8eb"
    readonly property color mutedColor: "#8a9099"
    readonly property color accentColor: "#4c8dff"

    RowLayout {
        anchors.fill: parent
        spacing: 0

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            VideoView {
                id: player
                objectName: "player"
                anchors.fill: parent
            }

            DropArea {
                anchors.fill: parent
                onDropped: (drop) => {
                    if (drop.hasUrls)
                        player.source = drop.urls[0]
                }
            }

            Text {
                anchors.centerIn: parent
                visible: player.source === ""
                text: "Drop a video file here"
                color: root.mutedColor
                font.pixelSize: 20
            }

            Rectangle {
                anchors { left: parent.left; top: parent.top; margins: 12 }
                visible: player.source !== ""
                color: "#b3000000"
                radius: 6
                width: stats.implicitWidth + 20
                height: stats.implicitHeight + 14

                Text {
                    id: stats
                    anchors.centerIn: parent
                    color: "white"
                    font.family: "Menlo"
                    font.pixelSize: 13
                    text: player.errorString !== ""
                          ? player.errorString
                          : player.streamDescription + "\n"
                            + (player.hardwareAccelerated ? "VideoToolbox" : "software decode") + "\n"
                            + "decode " + player.decodeFps.toFixed(1) + " fps · present " + player.presentFps.toFixed(1) + " fps\n"
                            + "skipped " + player.skippedFrames + " · effects " + player.enabledEffectCount
                }
            }
        }

        // Effect stack: one card per built-in effect, controls generated from its manifest.
        Rectangle {
            Layout.preferredWidth: 320
            Layout.fillHeight: true
            color: root.panelColor

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 14
                spacing: 10

                Label {
                    text: "Effects"
                    color: root.textColor
                    font.pixelSize: 17
                    font.bold: true
                }

                ScrollView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    contentWidth: availableWidth
                    clip: true

                    ColumnLayout {
                        width: parent.width
                        spacing: 8

                        Repeater {
                            model: player.availableEffects

                            delegate: Rectangle {
                                id: card
                                required property var modelData
                                // Re-evaluated whenever the stack changes (also from --effects).
                                readonly property bool enabled_: player.enabledEffectCount >= 0
                                                                 && player.isEffectEnabled(modelData.id)

                                Layout.fillWidth: true
                                implicitHeight: cardColumn.implicitHeight + 16
                                radius: 6
                                color: enabled_ ? "#232832" : "#1f2227"
                                border.color: enabled_ ? root.accentColor : "#2c3038"

                                ColumnLayout {
                                    id: cardColumn
                                    anchors { left: parent.left; right: parent.right; top: parent.top; margins: 8 }
                                    spacing: 4

                                    CheckBox {
                                        objectName: "effect:" + card.modelData.id
                                        text: card.modelData.title
                                        checked: card.enabled_
                                        palette.windowText: root.textColor
                                        onToggled: player.setEffectEnabled(card.modelData.id, checked)
                                    }

                                    Repeater {
                                        model: card.enabled_ ? card.modelData.params : []

                                        delegate: ParamControl {
                                            required property var modelData
                                            Layout.fillWidth: true
                                            param: modelData
                                            onValueEdited: (value) => player.setEffectParam(card.modelData.id, modelData.id, value)
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
