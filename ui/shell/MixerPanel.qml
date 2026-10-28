import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import TechieVideoEditor.Theme

// The mixer: a strip per track that carries sound (volume, balance, mute, solo and a
// stereo meter of peak over RMS), and the master with its loudness (EBU R128 momentary,
// short-term and integrated since play started) and peak. Meters move while playing.
Popup {
    id: mixer
    required property var project

    readonly property var tracks: project.tracks.filter((t) => t.kind !== "title")
    readonly property var levels: project.mixLevels

    padding: Theme.space.l
    width: Math.min(tracks.length * (strip + Theme.space.s) + masterWidth + Theme.space.l * 3, 900)
    readonly property int strip: 76
    readonly property int masterWidth: 150

    background: Rectangle {
        radius: Theme.radius.l
        color: Theme.color.surfaceRaised
        border.color: Theme.color.borderStrong
    }

    // A level (0..1) on the meters' scale: -60 dBFS at the bottom, 0 at the top.
    function meterValue(x) { return x > 0 ? Math.max(0, Math.min(1, (20 * Math.log10(x) + 60) / 60)) : 0 }
    function lufsText(v) { return v === undefined || v === null ? "—" : v.toFixed(1) }

    contentItem: ColumnLayout {
        spacing: Theme.space.m

        RowLayout {
            Layout.fillWidth: true
            Text { text: "Mixer"; font: Theme.type.heading; color: Theme.color.textPrimary; Layout.fillWidth: true }
            Text {
                text: mixer.project.playing ? "" : "Play to see the levels"
                font: Theme.type.caption
                color: Theme.color.textSecondary
            }
        }

        RowLayout {
            spacing: Theme.space.s

            Flickable {
                Layout.preferredWidth: Math.min(stripsRow.implicitWidth, 900 - mixer.masterWidth - Theme.space.l * 3)
                Layout.preferredHeight: stripsRow.implicitHeight
                contentWidth: stripsRow.implicitWidth
                clip: true
                boundsBehavior: Flickable.StopAtBounds

                Row {
                    id: stripsRow
                    spacing: Theme.space.s
                    Repeater {
                        model: mixer.tracks
                        delegate: Rectangle {
                            id: stripItem
                            required property var modelData
                            readonly property var level: (mixer.levels.tracks || {})[String(modelData.id)] || [0, 0, 0, 0]
                            objectName: "mixerStrip:" + modelData.id
                            width: mixer.strip
                            height: 300
                            radius: Theme.radius.m
                            color: Theme.color.surface
                            border.color: Theme.color.borderSubtle

                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: Theme.space.s
                                spacing: Theme.space.xs

                                Text {
                                    Layout.fillWidth: true
                                    text: stripItem.modelData.name
                                    font: Theme.type.label
                                    color: Theme.color.textPrimary
                                    elide: Text.ElideRight
                                    horizontalAlignment: Text.AlignHCenter
                                }
                                RowLayout {
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    spacing: Theme.space.xs
                                    Meter {
                                        objectName: "trackMeter:" + stripItem.modelData.id
                                        Layout.fillHeight: true
                                        level: stripItem.level
                                    }
                                    Slider {
                                        id: fader
                                        objectName: "trackGain:" + stripItem.modelData.id
                                        Layout.fillHeight: true
                                        Layout.fillWidth: true
                                        orientation: Qt.Vertical
                                        from: -60
                                        to: 12
                                        value: stripItem.modelData.gainDb
                                        onPressedChanged: {
                                            if (!pressed && Math.abs(value - stripItem.modelData.gainDb) > 0.01)
                                                mixer.project.setTrackMix(stripItem.modelData.id, { gainDb: value })
                                        }
                                    }
                                }
                                Text {
                                    Layout.fillWidth: true
                                    horizontalAlignment: Text.AlignHCenter
                                    text: fader.value <= -60 ? "−∞ dB" : (fader.value > 0 ? "+" : "") + fader.value.toFixed(1) + " dB"
                                    font: Theme.type.mono
                                    color: Theme.color.textSecondary
                                }
                                Slider {
                                    id: balance
                                    objectName: "trackPan:" + stripItem.modelData.id
                                    Layout.fillWidth: true
                                    from: -1
                                    to: 1
                                    value: stripItem.modelData.pan
                                    ToolTip.visible: pressed || hovered
                                    ToolTip.text: Math.abs(value) < 0.01 ? "Centre"
                                                  : Math.round(Math.abs(value) * 100) + "% " + (value < 0 ? "left" : "right")
                                    onPressedChanged: {
                                        if (!pressed && Math.abs(value - stripItem.modelData.pan) > 0.001)
                                            mixer.project.setTrackMix(stripItem.modelData.id, { pan: value })
                                    }
                                }
                                RowLayout {
                                    Layout.alignment: Qt.AlignHCenter
                                    spacing: Theme.space.xs
                                    StripButton {
                                        objectName: "trackMute:" + stripItem.modelData.id
                                        text: "M"
                                        tip: "Mute"
                                        on: stripItem.modelData.muted
                                        onColour: Theme.color.warning
                                        onClicked: mixer.project.setTrackFlag(stripItem.modelData.id, "muted", !on)
                                    }
                                    StripButton {
                                        objectName: "trackSolo:" + stripItem.modelData.id
                                        text: "S"
                                        tip: "Solo: hear only soloed tracks"
                                        on: stripItem.modelData.solo
                                        onColour: Theme.color.success
                                        onClicked: mixer.project.setTrackMix(stripItem.modelData.id, { solo: !on })
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // Master
            Rectangle {
                objectName: "mixerMaster"
                Layout.preferredWidth: mixer.masterWidth
                Layout.preferredHeight: 300
                radius: Theme.radius.m
                color: Theme.color.surface
                border.color: Theme.color.accent

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: Theme.space.s
                    spacing: Theme.space.xs
                    Text {
                        Layout.fillWidth: true
                        text: "Master"
                        font: Theme.type.label
                        color: Theme.color.textPrimary
                        horizontalAlignment: Text.AlignHCenter
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        spacing: Theme.space.s
                        Meter {
                            objectName: "masterMeter"
                            Layout.fillHeight: true
                            level: mixer.levels.master || [0, 0, 0, 0]
                        }
                        GridLayout {
                            Layout.fillWidth: true
                            Layout.alignment: Qt.AlignTop
                            columns: 1
                            rowSpacing: 2
                            Repeater {
                                model: [{ key: "momentary", label: "Momentary" }, { key: "shortTerm", label: "Short-term" },
                                        { key: "integrated", label: "Integrated" }]
                                delegate: ColumnLayout {
                                    required property var modelData
                                    spacing: 0
                                    Text { text: modelData.label; font: Theme.type.caption; color: Theme.color.textSecondary }
                                    Text {
                                        objectName: "masterLufs:" + modelData.key
                                        text: mixer.lufsText(mixer.levels[modelData.key]) + " LUFS"
                                        font: Theme.type.mono
                                        color: Theme.color.textPrimary
                                    }
                                }
                            }
                            Text { text: "Peak"; font: Theme.type.caption; color: Theme.color.textSecondary; Layout.topMargin: 4 }
                            Text {
                                objectName: "masterPeak"
                                readonly property var m: mixer.levels.master
                                text: m ? (Math.max(m[0], m[1]) > 0 ? (20 * Math.log10(Math.max(m[0], m[1]))).toFixed(1) : "−∞") + " dBFS"
                                        : "— dBFS"
                                font: Theme.type.mono
                                color: m && Math.max(m[0], m[1]) >= 0.999 ? Theme.color.danger : Theme.color.textPrimary
                            }
                        }
                    }
                }
            }
        }
    }

    // Two bars (left, right): RMS solid, peak as a thin line above it.
    component Meter: Row {
        id: meter
        property var level: [0, 0, 0, 0] // peakL, peakR, rmsL, rmsR
        readonly property real peakLeft: level[0]
        spacing: 2
        width: 14
        Repeater {
            model: 2
            delegate: Rectangle {
                required property int index
                width: 6
                height: meter.height
                radius: 2
                color: Theme.color.surfaceSunken
                readonly property real rms: mixer.meterValue(meter.level[index + 2])
                readonly property real peak: mixer.meterValue(meter.level[index])
                Rectangle {
                    anchors.bottom: parent.bottom
                    width: parent.width
                    height: parent.height * parent.rms
                    radius: 2
                    color: parent.peak > 0.985 ? Theme.color.danger : parent.peak > 0.9 ? Theme.color.warning : Theme.color.success
                }
                Rectangle {
                    visible: parent.peak > 0
                    width: parent.width
                    height: 2
                    y: parent.height * (1 - parent.peak)
                    color: Theme.color.textPrimary
                }
            }
        }
    }

    component StripButton: AbstractButton {
        id: button
        property bool on: false
        property color onColour: Theme.color.accent
        property string tip
        implicitWidth: 26
        implicitHeight: 22
        hoverEnabled: true
        ToolTip.visible: hovered && tip !== ""
        ToolTip.text: tip
        background: Rectangle {
            radius: Theme.radius.s
            color: button.on ? button.onColour : button.hovered ? Theme.color.surfaceHover : "transparent"
            border.color: button.on ? button.onColour : Theme.color.borderStrong
        }
        contentItem: Text {
            text: button.text
            font: Theme.type.label
            color: button.on ? "white" : Theme.color.textSecondary
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
    }
}
