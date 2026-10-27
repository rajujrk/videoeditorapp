import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import TechieVideoEditor.Theme

// Right panel: properties of the selected clip. Effects are live today; the other tabs
// show the planned layout with disabled controls until Phase 1–2 wires them.
Rectangle {
    id: inspector
    required property var player
    property int currentTab: 3

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

            // Audio
            InspectorPage {
                Section {
                    title: "Volume"
                    PlannedSlider { label: "Gain"; value: 0.7 }
                    PlannedSlider { label: "Pan"; value: 0.5 }
                }
                Section {
                    title: "Fades"
                    PlannedSlider { label: "Fade in" }
                    PlannedSlider { label: "Fade out" }
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
