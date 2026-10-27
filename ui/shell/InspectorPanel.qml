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
    property int currentTab: 4
    readonly property var clipInfo: project.selectedClipInfo
    readonly property bool clipIsVideo: clipInfo.trackKind === "video"
    readonly property var visual: clipInfo.visual || ({})

    color: Theme.color.surface

    readonly property var tabs: ["Video", "Speed", "Audio", "Color", "Effects"]

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

            // Video: placement, crop and compositing of the selected clip
            InspectorPage {

                Text {
                    visible: !inspector.clipIsVideo
                    Layout.fillWidth: true
                    Layout.topMargin: Theme.space.l
                    text: inspector.clipInfo.id === undefined ? "Select a video clip to adjust its picture."
                                                             : "Audio clips have no picture."
                    font: Theme.type.body
                    color: Theme.color.textSecondary
                    wrapMode: Text.WordWrap
                }

                ColumnLayout {
                    visible: inspector.clipIsVideo
                    Layout.fillWidth: true
                    spacing: Theme.space.s

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.topMargin: Theme.space.s
                        Text {
                            text: inspector.clipInfo.label || ""
                            font: Theme.type.heading
                            color: Theme.color.textPrimary
                            elide: Text.ElideMiddle
                            Layout.fillWidth: true
                        }
                        TextButton {
                            objectName: "resetVisualButton"
                            text: "Reset"
                            variant: "ghost"
                            onClicked: inspector.project.resetClipVisual(inspector.clipInfo.id)
                        }
                    }

                    Section {
                        title: "Transform"
                        VisualSlider { key: "x"; objectName: "visual:x"; label: "Position X"; from: -1; to: 1; value: inspector.visual.x || 0
                                       format: (v) => Math.round(v * inspector.project.canvasWidth) + " px" }
                        VisualSlider { key: "y"; objectName: "visual:y"; label: "Position Y"; from: -1; to: 1; value: inspector.visual.y || 0
                                       format: (v) => Math.round(v * inspector.project.canvasHeight) + " px" }
                        VisualSlider { key: "scale"; objectName: "visual:scale"; label: "Scale"; from: 0.05; to: 4
                                       value: inspector.visual.scale === undefined ? 1 : inspector.visual.scale
                                       format: (v) => Math.round(v * 100) + "%" }
                        VisualSlider { key: "rotation"; objectName: "visual:rotation"; label: "Rotation"; from: -180; to: 180
                                       value: inspector.visual.rotation || 0; format: (v) => v.toFixed(1) + "°" }
                    }
                    Section {
                        title: "Crop"
                        VisualSlider { key: "cropLeft"; label: "Left"; from: 0; to: 0.45; value: inspector.visual.cropLeft || 0; format: (v) => Math.round(v * 100) + "%" }
                        VisualSlider { key: "cropRight"; label: "Right"; from: 0; to: 0.45; value: inspector.visual.cropRight || 0; format: (v) => Math.round(v * 100) + "%" }
                        VisualSlider { key: "cropTop"; label: "Top"; from: 0; to: 0.45; value: inspector.visual.cropTop || 0; format: (v) => Math.round(v * 100) + "%" }
                        VisualSlider { key: "cropBottom"; label: "Bottom"; from: 0; to: 0.45; value: inspector.visual.cropBottom || 0; format: (v) => Math.round(v * 100) + "%" }
                    }
                    Section {
                        title: "Compositing"
                        VisualSlider { key: "opacity"; objectName: "visual:opacity"; label: "Opacity"; from: 0; to: 1
                                       value: inspector.visual.opacity === undefined ? 1 : inspector.visual.opacity
                                       format: (v) => Math.round(v * 100) + "%" }
                        RowLayout {
                            Layout.fillWidth: true
                            Text { text: "Blend mode"; font: Theme.type.label; color: Theme.color.textSecondary; Layout.fillWidth: true }
                            ComboBox {
                                objectName: "blendMode"
                                readonly property var modes: ["normal", "multiply", "screen", "overlay", "add", "darken", "lighten"]
                                model: ["Normal", "Multiply", "Screen", "Overlay", "Add", "Darken", "Lighten"]
                                currentIndex: Math.max(0, modes.indexOf(inspector.visual.blend || "normal"))
                                onActivated: (index) => inspector.project.setClipVisual(inspector.clipInfo.id, { blend: modes[index] })
                            }
                        }
                    }
                }
            }

            // Speed: constant speed, direction and freeze frames of the selected clip
            InspectorPage {
                Text {
                    visible: inspector.clipInfo.id === undefined
                    Layout.fillWidth: true
                    Layout.topMargin: Theme.space.l
                    text: "Select a clip to change its speed."
                    font: Theme.type.body
                    color: Theme.color.textSecondary
                    wrapMode: Text.WordWrap
                }

                Section {
                    id: speedSection
                    visible: inspector.clipInfo.id !== undefined
                    Layout.topMargin: Theme.space.s
                    title: "Speed"
                    readonly property bool still: inspector.clipInfo.freeze === true

                    Text {
                        visible: speedSection.still
                        Layout.fillWidth: true
                        text: "A freeze frame holds one frame; trim it to change how long."
                        font: Theme.type.label
                        color: Theme.color.textSecondary
                        wrapMode: Text.WordWrap
                    }
                    // Logarithmic: the slider runs from log10(0.1) to log10(10).
                    AudioSlider {
                        objectName: "speedSlider"
                        visible: !speedSection.still
                        label: "Speed"
                        from: -1
                        to: 1
                        value: Math.log10(inspector.clipInfo.speed || 1)
                        format: (v) => Math.pow(10, v).toFixed(2) + "×"
                        onCommitted: (v) => inspector.project.setClipSpeed(inspector.clipInfo.id, Math.pow(10, v),
                                                                           inspector.clipInfo.reversed === true)
                    }
                    RowLayout {
                        visible: !speedSection.still
                        spacing: Theme.space.xs
                        Repeater {
                            model: [0.5, 1, 2, 4]
                            delegate: TextButton {
                                required property real modelData
                                objectName: "speedPreset:" + modelData
                                text: modelData + "×"
                                variant: Math.abs((inspector.clipInfo.speed || 1) - modelData) < 0.001 ? "primary" : "secondary"
                                onClicked: inspector.project.setClipSpeed(inspector.clipInfo.id, modelData,
                                                                          inspector.clipInfo.reversed === true)
                            }
                        }
                    }
                    ToggleSwitch {
                        objectName: "reverseSwitch"
                        visible: !speedSection.still
                        text: "Reverse"
                        checked: inspector.clipInfo.reversed === true
                        onToggled: inspector.project.setClipSpeed(inspector.clipInfo.id, inspector.clipInfo.speed || 1, checked)
                    }
                    TextButton {
                        objectName: "freezeFrameButton"
                        visible: inspector.clipIsVideo
                        text: "Freeze frame at playhead"
                        iconName: "pause"
                        tip: "Insert a 2-second still of the frame under the playhead"
                        onClicked: inspector.project.freezeFrame(2)
                    }
                }
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

            // Effects: the selected clip's effect stack
            InspectorPage {

                Text {
                    visible: !inspector.clipIsVideo
                    Layout.fillWidth: true
                    Layout.topMargin: Theme.space.l
                    text: "Select a video clip to add effects to it."
                    font: Theme.type.body
                    color: Theme.color.textSecondary
                    wrapMode: Text.WordWrap
                }

                RowLayout {
                    visible: inspector.clipIsVideo
                    Layout.fillWidth: true
                    Layout.topMargin: Theme.space.s
                    ComboBox {
                        id: effectPicker
                        objectName: "effectPicker"
                        Layout.fillWidth: true
                        model: inspector.player.availableEffects.map((e) => e.title)
                    }
                    TextButton {
                        objectName: "addEffectButton"
                        text: "Add"
                        iconName: "plus"
                        onClicked: inspector.project.addClipEffect(inspector.clipInfo.id,
                                                                   inspector.player.availableEffects[effectPicker.currentIndex].id)
                    }
                }

                Text {
                    visible: inspector.clipIsVideo && (inspector.clipInfo.effects || []).length === 0
                    Layout.fillWidth: true
                    text: "No effects on this clip yet. Pick one above, or click an effect in the library."
                    font: Theme.type.label
                    color: Theme.color.textSecondary
                    wrapMode: Text.WordWrap
                }

                Repeater {
                    model: inspector.clipIsVideo ? (inspector.clipInfo.effects || []) : []
                    delegate: ClipEffectCard {
                        required property var modelData
                        required property int index
                        Layout.fillWidth: true
                        project: inspector.project
                        clipId: inspector.clipInfo.id
                        stackIndex: index
                        entry: modelData
                        spec: inspector.player.availableEffects.find((e) => e.id === modelData.effectId) || ({ title: modelData.effectId, params: [] })
                    }
                }
            }
        }
    }

    function commitAudio(gainDb, fadeIn, fadeOut) {
        project.setClipAudio(clipInfo.id, gainDb, fadeIn, fadeOut)
    }

    // A picture property: previews live while dragging, commits one undo step on release.
    component VisualSlider: AudioSlider {
        required property string key
        onLiveValue: (v) => { const m = {}; m[key] = v; inspector.project.previewClipVisual(inspector.clipInfo.id, m) }
        onCommitted: (v) => { const m = {}; m[key] = v; inspector.project.setClipVisual(inspector.clipInfo.id, m) }
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
        signal liveValue(real value)
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
            onMoved: audioSlider.liveValue(value)
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
