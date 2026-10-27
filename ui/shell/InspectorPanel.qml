import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import TechieVideoEditor.Theme

// Right panel: properties of the selected clip. Effects are live today; the other tabs
// show the planned layout with disabled controls until Phase 1–2 wires them.
Rectangle {
    id: inspector
    required property var player
    required property var project
    property int currentTab: 3
    readonly property var clipInfo: project.selectedClipInfo

    color: Theme.color.surface

    readonly property var tabs: ["Video", "Audio", "Color", "Effects"]

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Segmented tab bar
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: Theme.size.toolBar
            color: Theme.color.surface

            RowLayout {
                anchors.fill: parent
                anchors.margins: Theme.space.xs
                spacing: Theme.space.xxs

                Repeater {
                    model: inspector.tabs
                    delegate: AbstractButton {
                        id: tab
                        required property string modelData
                        required property int index
                        objectName: "inspectorTab:" + modelData
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        hoverEnabled: true
                        checked: inspector.currentTab === index
                        onClicked: inspector.currentTab = index

                        background: Rectangle {
                            radius: Theme.radius.s
                            color: tab.checked ? Theme.color.surfaceRaised
                                 : tab.hovered ? Theme.color.surfaceHover : "transparent"
                            border.color: tab.checked ? Theme.color.borderStrong : "transparent"
                        }
                        contentItem: Text {
                            text: tab.modelData === "Effects" && inspector.player.enabledEffectCount > 0
                                  ? "Effects · " + inspector.player.enabledEffectCount : tab.modelData
                            font: Theme.type.label
                            color: tab.checked ? Theme.color.textPrimary : Theme.color.textSecondary
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                }
            }

            Divider { anchors.bottom: parent.bottom; width: parent.width }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: inspector.currentTab

            // Video
            InspectorPage {
                Section {
                    title: "Transform"
                    PlannedSlider { label: "Position X" }
                    PlannedSlider { label: "Position Y" }
                    PlannedSlider { label: "Scale"; value: 0.5 }
                    PlannedSlider { label: "Rotation" }
                }
                Section {
                    title: "Compositing"
                    PlannedSlider { label: "Opacity"; value: 1 }
                    PlannedRow { label: "Blend mode"; detail: "Normal" }
                }
                Section { title: "Crop"; expanded: false; PlannedRow { label: "Crop & zoom"; detail: "Phase 1" } }
                Section { title: "Speed"; expanded: false; PlannedRow { label: "Constant / ramp"; detail: "Phase 1–2" } }
            }

            // Audio: live for the selected clip
            InspectorPage {
                Text {
                    visible: inspector.clipInfo.id === undefined || !inspector.clipInfo.hasAudio
                    Layout.fillWidth: true
                    Layout.topMargin: Theme.space.l
                    text: inspector.clipInfo.id === undefined ? "Select a clip to adjust its sound."
                                                             : "This clip has no sound."
                    font: Theme.type.body
                    color: Theme.color.textSecondary
                    wrapMode: Text.WordWrap
                }
                Text {
                    visible: inspector.clipInfo.hasAudio === true && inspector.clipInfo.audioEnabled === false
                    Layout.fillWidth: true
                    Layout.topMargin: Theme.space.l
                    text: "This clip's sound was detached to an audio track. Select the audio clip to adjust it."
                    font: Theme.type.body
                    color: Theme.color.textSecondary
                    wrapMode: Text.WordWrap
                }

                ColumnLayout {
                    visible: inspector.clipInfo.hasAudio === true && inspector.clipInfo.audioEnabled === true
                    Layout.fillWidth: true
                    spacing: Theme.space.s

                    Text {
                        text: inspector.clipInfo.label || ""
                        font: Theme.type.heading
                        color: Theme.color.textPrimary
                        elide: Text.ElideMiddle
                        Layout.fillWidth: true
                        Layout.topMargin: Theme.space.s
                    }

                    Section {
                        title: "Volume"
                        AudioSlider {
                            objectName: "volumeSlider"
                            label: "Gain"
                            from: -60
                            to: 12
                            value: inspector.clipInfo.gainDb || 0
                            format: (v) => v <= -60 ? "−∞ dB" : (v > 0 ? "+" : "") + v.toFixed(1) + " dB"
                            onCommitted: (v) => inspector.commitAudio(v, inspector.clipInfo.fadeIn, inspector.clipInfo.fadeOut)
                        }
                    }
                    Section {
                        title: "Fades"
                        AudioSlider {
                            objectName: "fadeInSlider"
                            label: "Fade in"
                            from: 0
                            to: Math.max(0.01, Math.min(10, (inspector.clipInfo.duration || 0) - (inspector.clipInfo.fadeOut || 0)))
                            value: inspector.clipInfo.fadeIn || 0
                            format: (v) => v.toFixed(2) + " s"
                            onCommitted: (v) => inspector.commitAudio(inspector.clipInfo.gainDb, v, inspector.clipInfo.fadeOut)
                        }
                        AudioSlider {
                            objectName: "fadeOutSlider"
                            label: "Fade out"
                            from: 0
                            to: Math.max(0.01, Math.min(10, (inspector.clipInfo.duration || 0) - (inspector.clipInfo.fadeIn || 0)))
                            value: inspector.clipInfo.fadeOut || 0
                            format: (v) => v.toFixed(2) + " s"
                            onCommitted: (v) => inspector.commitAudio(inspector.clipInfo.gainDb, inspector.clipInfo.fadeIn, v)
                        }
                    }
                    TextButton {
                        objectName: "detachAudioButton"
                        visible: inspector.clipInfo.trackKind === "video"
                        text: "Detach audio"
                        iconName: "audio"
                        tip: "Move this clip's sound to an audio track"
                        onClicked: inspector.project.detachAudio(inspector.clipInfo.id)
                    }
                }
                Section { title: "Audio effects"; expanded: false; PlannedRow { label: "EQ, dynamics, voice"; detail: "Phase 2" } }
            }

            // Color
            InspectorPage {
                Section {
                    title: "Basic"
                    PlannedSlider { label: "Exposure"; value: 0.5 }
                    PlannedSlider { label: "Contrast"; value: 0.5 }
                    PlannedSlider { label: "Saturation"; value: 0.5 }
                    PlannedSlider { label: "Temperature"; value: 0.5 }
                }
                Section { title: "Curves"; expanded: false; PlannedRow { label: "RGB / Hue curves"; detail: "Phase 2" } }
                Section { title: "LUT"; expanded: false; PlannedRow { label: "3D LUT"; detail: "Phase 2" } }
            }

            // Effects: the live effect stack
            InspectorPage {
                Repeater {
                    model: inspector.player.availableEffects
                    delegate: EffectCard {
                        required property var modelData
                        Layout.fillWidth: true
                        player: inspector.player
                        effect: modelData
                    }
                }
            }
        }
    }

    function commitAudio(gainDb, fadeIn, fadeOut) {
        project.setClipAudio(clipInfo.id, gainDb, fadeIn, fadeOut)
    }

    // A slider that previews while dragging and commits one undo step on release.
    component AudioSlider: ColumnLayout {
        id: audioSlider
        property string label
        property alias from: slider.from
        property alias to: slider.to
        property real value: 0
        property var format: (v) => v.toFixed(2)
        signal committed(real value)
        Layout.fillWidth: true
        spacing: 0
        onValueChanged: slider.value = value

        RowLayout {
            Layout.fillWidth: true
            Text { text: audioSlider.label; font: Theme.type.label; color: Theme.color.textSecondary; Layout.fillWidth: true }
            Text { text: audioSlider.format(slider.value); font: Theme.type.mono; color: Theme.color.textPrimary }
        }
        Slider {
            id: slider
            Layout.fillWidth: true
            value: audioSlider.value
            onPressedChanged: {
                if (!pressed && value !== audioSlider.value)
                    audioSlider.committed(value)
            }
        }
    }

    component InspectorPage: ScrollView {
        default property alias content: pageColumn.data
        contentWidth: availableWidth
        clip: true

        ColumnLayout {
            id: pageColumn
            width: parent.width - Theme.space.m * 2
            x: Theme.space.m
            y: Theme.space.s
            spacing: Theme.space.s
        }
    }

    // Disabled control showing where a Phase 1–2 property will live.
    component PlannedSlider: ColumnLayout {
        property string label
        property real value: 0
        Layout.fillWidth: true
        spacing: 0
        Text { text: parent.label; font: Theme.type.label; color: Theme.color.textSecondary }
        Slider { Layout.fillWidth: true; enabled: false; value: parent.value }
    }

    component PlannedRow: RowLayout {
        property string label
        property string detail
        Layout.fillWidth: true
        Text { text: parent.label; font: Theme.type.label; color: Theme.color.textSecondary; Layout.fillWidth: true }
        Text { text: parent.detail; font: Theme.type.caption; color: Theme.color.textDisabled }
    }
}
