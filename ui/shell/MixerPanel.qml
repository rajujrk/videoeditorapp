import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import TechieVideoEditor.Theme

// The mixer: a strip per track that carries sound (volume, balance, mute, solo, its own
// effects — built-in or plug-ins — and a stereo meter of peak over RMS), and the master with its loudness (EBU R128 momentary,
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
            Text { text: qsTr("Mixer"); font: Theme.type.heading; color: Theme.color.textPrimary; Layout.fillWidth: true }
            Text {
                text: mixer.project.playing ? "" : qsTr("Play to see the levels")
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
                            height: 326
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
                                    text: fader.value <= -60 ? qsTr("−∞ dB") : qsTr("%1 dB").arg((fader.value > 0 ? "+" : "") + Fmt.num(fader.value, 1))
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
                                    ToolTip.text: Math.abs(value) < 0.01 ? qsTr("Centre")
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
                                        text: qsTr("M")
                                        tip: qsTr("Mute")
                                        on: stripItem.modelData.muted
                                        onColour: Theme.color.warning
                                        onClicked: mixer.project.setTrackFlag(stripItem.modelData.id, "muted", !on)
                                    }
                                    StripButton {
                                        objectName: "trackSolo:" + stripItem.modelData.id
                                        text: qsTr("S")
                                        tip: qsTr("Solo: hear only soloed tracks")
                                        on: stripItem.modelData.solo
                                        onColour: Theme.color.success
                                        onClicked: mixer.project.setTrackMix(stripItem.modelData.id, { solo: !on })
                                    }
                                }
                                StripButton {
                                    objectName: "trackEffects:" + stripItem.modelData.id
                                    Layout.fillWidth: true
                                    text: (stripItem.modelData.audioEffects || []).length > 0
                                          ? qsTr("FX %1").arg(stripItem.modelData.audioEffects.length) : qsTr("FX")
                                    tip: qsTr("The track's own effects and plug-ins, on all of its sound")
                                    on: (stripItem.modelData.audioEffects || []).length > 0
                                    onClicked: { trackEffects.trackId = stripItem.modelData.id; trackEffects.open() }
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
                        text: qsTr("Master")
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
                                model: [{ key: "momentary", label: qsTr("Momentary") }, { key: "shortTerm", label: qsTr("Short-term") },
                                        { key: "integrated", label: qsTr("Integrated") }]
                                delegate: ColumnLayout {
                                    required property var modelData
                                    spacing: 0
                                    Text { text: modelData.label; font: Theme.type.caption; color: Theme.color.textSecondary }
                                    Text {
                                        objectName: "masterLufs:" + modelData.key
                                        text: qsTr("%1 LUFS").arg(mixer.lufsText(mixer.levels[modelData.key]))
                                        font: Theme.type.mono
                                        color: Theme.color.textPrimary
                                    }
                                }
                            }
                            Text { text: qsTr("Peak"); font: Theme.type.caption; color: Theme.color.textSecondary; Layout.topMargin: 4 }
                            Text {
                                objectName: "masterPeak"
                                readonly property var m: mixer.levels.master
                                text: m ? qsTr("%1 dBFS").arg(Math.max(m[0], m[1]) > 0 ? Fmt.num(20 * Math.log10(Math.max(m[0], m[1])), 1) : "−∞")
                                        : qsTr("— dBFS")
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

    // A track's own effects: add, switch off, change, remove, open a plug-in's window.
    Popup {
        id: trackEffects
        objectName: "trackEffectsPanel"
        property double trackId: -1
        readonly property var track: mixer.project.tracks.find((t) => t.id === trackId) || ({ name: "", audioEffects: [] })
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: 380
        height: Math.min(implicitHeight, (parent ? parent.height : 700) - Theme.space.xl * 2)
        padding: Theme.space.m
        background: Rectangle {
            radius: Theme.radius.l
            color: Theme.color.surfaceRaised
            border.color: Theme.color.borderStrong
        }
        contentItem: ColumnLayout {
            spacing: Theme.space.s
            Text {
                text: qsTr("Effects on %1").arg(trackEffects.track.name)
                font: Theme.type.heading
                color: Theme.color.textPrimary
            }
            Text {
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                text: qsTr("On the track's whole sound, before its volume and balance.")
                      + (mixer.project.scanningPlugins ? " " + qsTr("Looking for plug-ins…") : "")
                font: Theme.type.caption
                color: Theme.color.textSecondary
                wrapMode: Text.WordWrap
            }
            RowLayout {
                Layout.fillWidth: true
                ComboBox {
                    id: trackPicker
                    objectName: "trackEffectPicker"
                    Layout.fillWidth: true
                    textRole: "label"
                    valueRole: "id"
                    model: mixer.project.audioEffects.map((e) => ({ id: e.id, label: e.title }))
                        .concat(mixer.project.audioPlugins.filter((p) => p.problem === "").map((p) => ({
                            id: p.id, label: qsTr("Plug-in · %1 (%2)").arg(p.name).arg([p.vendor, p.format].filter((x) => x).join(", "))
                        })))
                }
                TextButton {
                    objectName: "addTrackEffect"
                    text: qsTr("Add")
                    iconName: "plus"
                    onClicked: mixer.project.addTrackAudioEffect(trackEffects.trackId, trackPicker.currentValue)
                }
            }
            ScrollView {
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(trackCards.implicitHeight, 420)
                contentWidth: availableWidth
                ColumnLayout {
                    id: trackCards
                    width: parent.width
                    spacing: Theme.space.s
                    Repeater {
                        model: (trackEffects.track.audioEffects || []).length
                        delegate: ClipEffectCard {
                            required property int index
                            readonly property var current: (trackEffects.track.audioEffects || [])[index]
                                                           || ({ effectId: "", enabled: false, values: {} })
                            Layout.fillWidth: true
                            audio: true
                            project: mixer.project
                            clipId: -1
                            trackId: trackEffects.trackId
                            stackIndex: index
                            entry: current
                            spec: mixer.project.audioEffects.find((e) => e.id === current.effectId)
                                  || ({ title: (mixer.project.audioPlugins.find((p) => p.id === current.effectId) || { name: "Plug-in" }).name,
                                        params: [] })
                        }
                    }
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
