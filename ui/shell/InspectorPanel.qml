import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
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
    readonly property var keyStates: clipInfo.keyframeStates || ({})
    readonly property bool keyHere: Object.values(keyStates).indexOf("key") >= 0
    // Distinct keyframe times of the selected clip (any property).
    readonly property int keyframeCount: {
        const times = new Set()
        const all = clipInfo.keyframes || {}
        for (const id in all)
            for (const k of all[id])
                times.add(Math.round(k.time * 1000))
        return times.size
    }
    readonly property var transitionInfo: project.selectedTransitionInfo
    readonly property bool showsTransition: transitionInfo.clipId !== undefined

    color: Theme.color.surface

    readonly property var tabs: ["Video", "Speed", "Audio", "Color", "Effects"]

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Segmented tab bar
        Rectangle {
            visible: !inspector.showsTransition
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

        // The selected transition replaces the clip tabs.
        InspectorPage {
            objectName: "transitionPage"
            visible: inspector.showsTransition
            Layout.fillWidth: true
            Layout.fillHeight: true

            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: Theme.space.s
                Icon { name: "transitions"; color: Theme.color.accentText }
                Text {
                    text: "Transition"
                    font: Theme.type.heading
                    color: Theme.color.textPrimary
                    Layout.fillWidth: true
                }
                TextButton {
                    objectName: "removeTransitionButton"
                    text: "Remove"
                    variant: "ghost"
                    iconName: "delete"
                    onClicked: inspector.project.removeTransition(inspector.transitionInfo.clipId)
                }
            }

            ComboBox {
                id: transitionPicker
                objectName: "transitionPicker"
                Layout.fillWidth: true
                model: inspector.project.availableTransitions
                textRole: "title"
                valueRole: "id"
                currentIndex: Math.max(0, indexOfValue(inspector.transitionInfo.transitionId))
                onActivated: inspector.project.addTransition(currentValue, inspector.transitionInfo.clipId)
            }

            Section {
                title: "Timing"
                AudioSlider {
                    objectName: "transitionDuration"
                    label: "Duration"
                    from: 0.1
                    to: 5
                    value: inspector.transitionInfo.requestedDuration || 1
                    format: (v) => v.toFixed(1) + " s"
                    onCommitted: (v) => inspector.project.setTransition(inspector.transitionInfo.clipId, { duration: v })
                }
                Text {
                    visible: inspector.showsTransition
                             && inspector.transitionInfo.duration < inspector.transitionInfo.requestedDuration - 0.01
                    Layout.fillWidth: true
                    text: "Plays for " + (inspector.transitionInfo.duration || 0).toFixed(1)
                          + " s: each side is limited to half its clip."
                    font: Theme.type.label
                    color: Theme.color.textSecondary
                    wrapMode: Text.WordWrap
                }
                Text { text: "Position"; font: Theme.type.label; color: Theme.color.textSecondary }
                RowLayout {
                    spacing: Theme.space.xs
                    Repeater {
                        model: [{ id: "endsAtCut", label: "Before cut" }, { id: "center", label: "Centered" },
                                { id: "startsAtCut", label: "After cut" }]
                        delegate: TextButton {
                            required property var modelData
                            objectName: "alignment:" + modelData.id
                            text: modelData.label
                            variant: inspector.transitionInfo.alignment === modelData.id ? "primary" : "secondary"
                            onClicked: inspector.project.setTransition(inspector.transitionInfo.clipId, { alignment: modelData.id })
                        }
                    }
                }
            }

            Section {
                title: "Settings"
                visible: (inspector.transitionInfo.params || []).length > 0
                Repeater {
                    model: inspector.transitionInfo.params || []
                    delegate: ParamControl {
                        required property var modelData
                        objectName: "transitionParam:" + modelData.id
                        Layout.fillWidth: true
                        param: Object.assign({}, modelData, (inspector.transitionInfo.values || {})[modelData.id] !== undefined
                                                            ? { default: inspector.transitionInfo.values[modelData.id] } : {})
                        onValueCommitted: (value) => {
                            const values = {}
                            values[modelData.id] = value
                            inspector.project.setTransition(inspector.transitionInfo.clipId, { values: values })
                        }
                    }
                }
            }
        }

        StackLayout {
            visible: !inspector.showsTransition
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

                    // Keyframes: add/remove at the playhead, step between them, easing.
                    Section {
                        objectName: "keyframesSection"
                        title: "Keyframes"
                        Layout.fillWidth: true
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.space.xs
                            IconButton {
                                objectName: "previousKeyframe"
                                iconName: "skip-back"
                                tip: "Previous keyframe"
                                enabled: inspector.keyframeCount > 0
                                onClicked: inspector.project.seekKeyframe(inspector.clipInfo.id, -1)
                            }
                            TextButton {
                                objectName: "keyframeButton"
                                Layout.fillWidth: true
                                text: inspector.keyHere ? "Remove" : "Add keyframe"
                                iconName: "keyframe"
                                tip: inspector.keyHere ? "Remove every keyframe at the playhead"
                                                       : "Keep position, scale, rotation and opacity as they are at the playhead"
                                onClicked: inspector.keyHere ? inspector.project.removeKeyframes(inspector.clipInfo.id)
                                                             : inspector.project.addKeyframes(inspector.clipInfo.id)
                            }
                            IconButton {
                                objectName: "nextKeyframe"
                                iconName: "skip-forward"
                                tip: "Next keyframe"
                                enabled: inspector.keyframeCount > 0
                                onClicked: inspector.project.seekKeyframe(inspector.clipInfo.id, 1)
                            }
                        }
                        RowLayout {
                            visible: inspector.keyHere
                            Layout.fillWidth: true
                            Text { text: "Easing"; font: Theme.type.label; color: Theme.color.textSecondary }
                            ComboBox {
                                objectName: "keyframeEasing"
                                Layout.fillWidth: true
                                Layout.preferredWidth: 1 // shares the row instead of asking for its longest item
                                readonly property var names: ["linear", "easeIn", "easeOut", "easeInOut", "hold", "curve"]
                                model: ["Linear", "Ease in", "Ease out", "Ease in and out", "Hold", "Custom curve"]
                                currentIndex: Math.max(0, names.indexOf(inspector.clipInfo.keyframeEasing || "linear"))
                                onActivated: (index) => inspector.project.setKeyframeEasing(inspector.clipInfo.id, names[index])
                            }
                        }
                        // The timing curve to the next keyframe: drag its handles for a custom one.
                        CurveEditor {
                            objectName: "curveEditor"
                            visible: inspector.keyHere
                            Layout.fillWidth: true
                            curve: inspector.clipInfo.keyframeCurve || [0, 0, 1, 1]
                            hold: inspector.clipInfo.keyframeEasing === "hold"
                            onCommitted: (c) => inspector.project.setKeyframeCurve(inspector.clipInfo.id, c[0], c[1], c[2], c[3])
                        }
                        RowLayout {
                            visible: inspector.keyHere
                            Layout.fillWidth: true
                            spacing: Theme.space.xs
                            Repeater {
                                // Curves the named easings cannot make: past the target and back, a
                                // wind-up before moving, and a quick snap.
                                model: [{ id: "overshoot", label: "Overshoot", curve: [0.34, 1.56, 0.64, 1] },
                                        { id: "anticipate", label: "Anticipate", curve: [0.36, 0, 0.66, -0.56] },
                                        { id: "snap", label: "Snap", curve: [0.9, 0, 0.1, 1] }]
                                delegate: TextButton {
                                    required property var modelData
                                    objectName: "curvePreset:" + modelData.id
                                    Layout.fillWidth: true
                                    Layout.preferredWidth: 1
                                    text: modelData.label
                                    variant: "secondary"
                                    onClicked: inspector.project.setKeyframeCurve(inspector.clipInfo.id, modelData.curve[0],
                                                                                  modelData.curve[1], modelData.curve[2],
                                                                                  modelData.curve[3])
                                }
                            }
                        }
                        RowLayout {
                        Layout.fillWidth: true
                        Text {
                            Layout.fillWidth: true
                            Layout.preferredWidth: 1 // wraps to the panel instead of widening it
                            wrapMode: Text.WordWrap
                            text: inspector.keyframeCount === 0
                                  ? "Add a keyframe, move the playhead and change a value: the clip animates between them. ◆ beside a value animates just that one."
                                  : inspector.keyframeCount + (inspector.keyframeCount === 1 ? " keyframe" : " keyframes")
                                    + ". Changing an animated value sets its keyframe at the playhead."
                            font: Theme.type.caption
                            color: Theme.color.textSecondary
                        }
                        TextButton {
                            objectName: "clearKeyframesButton"
                            Layout.alignment: Qt.AlignTop
                            text: "Clear"
                            variant: "ghost"
                            visible: inspector.keyframeCount > 0
                            tip: "Stop animating: every property keeps its value at the playhead"
                            onClicked: inspector.project.clearKeyframes(inspector.clipInfo.id)
                        }
                        }
                    }

                    // A title's text and style. Text commits when typing pauses; the rest on each change.
                    Section {
                        id: textSection
                        objectName: "titleTextSection"
                        visible: inspector.clipInfo.isTitle === true
                        title: "Text"
                        readonly property var text: inspector.clipInfo.text || ({})
                        function change(fields) { inspector.project.setClipText(inspector.clipInfo.id, fields) }

                        TextArea {
                            id: titleText
                            objectName: "titleText"
                            Layout.fillWidth: true
                            Layout.preferredHeight: Math.max(64, implicitHeight)
                            wrapMode: TextEdit.Wrap
                            font: Theme.type.body
                            color: Theme.color.textPrimary
                            placeholderText: "Type your title"
                            background: Rectangle {
                                radius: Theme.radius.s
                                color: Theme.color.surfaceSunken
                                border.color: titleText.activeFocus ? Theme.color.accent : Theme.color.borderSubtle
                            }
                            // Follows the clip unless being typed in.
                            Binding on text { value: textSection.text.text || ""; when: !titleText.activeFocus }
                            onTextChanged: if (activeFocus) commitTimer.restart()
                            onActiveFocusChanged: if (!activeFocus && commitTimer.running) { commitTimer.stop(); textSection.change({ text: text }) }
                            Timer {
                                id: commitTimer
                                interval: 600
                                onTriggered: textSection.change({ text: titleText.text })
                            }
                        }

                        ComboBox {
                            objectName: "titleFont"
                            Layout.fillWidth: true
                            model: inspector.project.titleFonts
                            currentIndex: Math.max(0, model.indexOf(textSection.text.fontFamily || ""))
                            onActivated: (index) => textSection.change({ fontFamily: model[index] })
                        }

                        RowLayout {
                            spacing: Theme.space.xs
                            Repeater {
                                model: [{ key: "bold", label: "Bold" }, { key: "italic", label: "Italic" }]
                                delegate: TextButton {
                                    required property var modelData
                                    objectName: "titleStyle:" + modelData.key
                                    text: modelData.label
                                    variant: textSection.text[modelData.key] ? "primary" : "secondary"
                                    onClicked: {
                                        const fields = {}
                                        fields[modelData.key] = !textSection.text[modelData.key]
                                        textSection.change(fields)
                                    }
                                }
                            }
                        }
                        RowLayout {
                            spacing: Theme.space.xs
                            Repeater {
                                model: ["left", "center", "right"]
                                delegate: TextButton {
                                    required property string modelData
                                    objectName: "titleAlign:" + modelData
                                    text: modelData === "left" ? "Left" : modelData === "center" ? "Center" : "Right"
                                    variant: (textSection.text.alignment || "center") === modelData ? "primary" : "secondary"
                                    onClicked: textSection.change({ alignment: modelData })
                                }
                            }
                        }

                        AudioSlider {
                            objectName: "titleSize"
                            label: "Size"
                            from: 0.01
                            to: 0.5
                            value: textSection.text.size || 0.08
                            format: (v) => (v * 100).toFixed(1) + "%"
                            onCommitted: (v) => textSection.change({ size: v })
                        }

                        Text { text: "Color"; font: Theme.type.label; color: Theme.color.textSecondary }
                        Swatches {
                            objectName: "titleColor"
                            current: textSection.text.color || [1, 1, 1, 1]
                            onPicked: (c) => textSection.change({ color: c })
                        }

                        AudioSlider {
                            objectName: "titleLetterSpacing"
                            label: "Letter spacing"
                            from: -0.2
                            to: 1
                            value: textSection.text.letterSpacing || 0
                            onCommitted: (v) => textSection.change({ letterSpacing: v })
                        }
                        AudioSlider {
                            objectName: "titleLineSpacing"
                            label: "Line spacing"
                            from: 0.5
                            to: 3
                            value: textSection.text.lineSpacing || 1
                            format: (v) => v.toFixed(2) + "×"
                            onCommitted: (v) => textSection.change({ lineSpacing: v })
                        }
                        AudioSlider {
                            objectName: "titleOutline"
                            label: "Outline"
                            from: 0
                            to: 0.3
                            value: textSection.text.outlineWidth || 0
                            onCommitted: (v) => textSection.change({ outlineWidth: v })
                        }
                        Swatches {
                            objectName: "titleOutlineColor"
                            visible: (textSection.text.outlineWidth || 0) > 0
                            current: textSection.text.outlineColor || [0, 0, 0, 1]
                            onPicked: (c) => textSection.change({ outlineColor: c })
                        }
                        ToggleSwitch {
                            objectName: "titleShadow"
                            text: "Shadow"
                            checked: textSection.text.shadow === true
                            onToggled: textSection.change({ shadow: checked })
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

                Text {
                    visible: inspector.clipInfo.isTitle === true
                    Layout.fillWidth: true
                    Layout.topMargin: Theme.space.l
                    text: "Titles have no speed; trim them to change how long they show."
                    font: Theme.type.body
                    color: Theme.color.textSecondary
                    wrapMode: Text.WordWrap
                }

                Section {
                    id: speedSection
                    visible: inspector.clipInfo.id !== undefined && inspector.clipInfo.isTitle !== true
                    Layout.topMargin: Theme.space.s
                    title: "Speed"
                    readonly property bool still: inspector.clipInfo.freeze === true
                    readonly property bool ramped: (inspector.clipInfo.speedRamp || []).length > 0

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
                        label: speedSection.ramped ? "Average speed" : "Speed"
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
                                variant: !speedSection.ramped && Math.abs((inspector.clipInfo.speed || 1) - modelData) < 0.001
                                         ? "primary" : "secondary"
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

                // Speed ramp: the speed changes through the clip; the footage stays the same.
                Section {
                    id: rampSection
                    objectName: "speedRampSection"
                    visible: speedSection.visible && !speedSection.still
                    Layout.fillWidth: true
                    title: "Speed ramp"
                    readonly property var points: inspector.clipInfo.speedRamp || []
                    readonly property bool ramped: points.length > 0

                    Flow {
                        Layout.fillWidth: true
                        spacing: Theme.space.xs
                        Repeater {
                            model: inspector.project.speedRampPresets
                            delegate: TextButton {
                                required property var modelData
                                objectName: "rampPreset:" + modelData.id
                                text: modelData.title
                                variant: JSON.stringify(rampSection.points) === JSON.stringify(modelData.points) ? "primary" : "secondary"
                                onClicked: inspector.project.applySpeedRampPreset(inspector.clipInfo.id, modelData.id)
                            }
                        }
                    }
                    SpeedRampEditor {
                        id: rampEditor
                        objectName: "speedRampEditor"
                        visible: rampSection.ramped
                        Layout.fillWidth: true
                        points: rampSection.points
                        playheadAt: inspector.clipInfo.duration > 0
                                    ? (inspector.project.playhead - (inspector.clipInfo.start || 0)) / inspector.clipInfo.duration : -1
                        onCommitted: (points) => inspector.project.setClipSpeedRamp(inspector.clipInfo.id, points)
                    }
                    RowLayout {
                        visible: rampSection.ramped
                        Layout.fillWidth: true
                        spacing: Theme.space.xs
                        TextButton {
                            objectName: "removeRampPoint"
                            Layout.fillWidth: true
                            Layout.preferredWidth: 1
                            text: "Remove point"
                            variant: "secondary"
                            enabled: rampEditor.selected > 0 && rampEditor.selected < rampSection.points.length - 1
                            onClicked: {
                                const next = rampSection.points.filter((p, i) => i !== rampEditor.selected)
                                rampEditor.selected = -1
                                inspector.project.setClipSpeedRamp(inspector.clipInfo.id, next)
                            }
                        }
                        TextButton {
                            objectName: "removeRampButton"
                            Layout.fillWidth: true
                            Layout.preferredWidth: 1
                            text: "Remove ramp"
                            variant: "secondary"
                            tip: "Back to a constant speed (the ramp's average), keeping the clip's length"
                            onClicked: inspector.project.removeSpeedRamp(inspector.clipInfo.id)
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        wrapMode: Text.WordWrap
                        text: rampSection.ramped
                              ? "Plays the same footage in " + (inspector.clipInfo.duration || 0).toFixed(1) + " s (average "
                                + (inspector.clipInfo.speed || 1).toFixed(2) + "×). Drag points; double-click the graph to add one. A constant speed above replaces the ramp."
                              : "Pick a ramp to change the speed through the clip. It plays the same footage, so its length follows."
                        font: Theme.type.caption
                        color: Theme.color.textSecondary
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
                            animKey: "volume"
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

            // Color: the selected clip's grade
            InspectorPage {
                id: colorPage
                readonly property var grade: inspector.clipInfo.color || ({})
                readonly property bool gradable: inspector.clipIsVideo && inspector.clipInfo.isTitle !== true
                property string channel: "master"

                function preview(changes) { inspector.project.previewClipColor(inspector.clipInfo.id, changes) }
                function commit(changes) { inspector.project.setClipColor(inspector.clipInfo.id, changes) }

                Text {
                    visible: !colorPage.gradable
                    Layout.fillWidth: true
                    Layout.topMargin: Theme.space.l
                    text: inspector.clipInfo.isTitle === true ? "Titles take their colour from their text style."
                                                             : "Select a video clip to grade it."
                    font: Theme.type.body
                    color: Theme.color.textSecondary
                    wrapMode: Text.WordWrap
                }

                RowLayout {
                    visible: colorPage.gradable
                    Layout.fillWidth: true
                    Layout.topMargin: Theme.space.s
                    spacing: Theme.space.xs
                    TextButton {
                        objectName: "copyGradeButton"
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        text: "Copy"
                        variant: "secondary"
                        tip: "Copy this clip's grade"
                        onClicked: inspector.project.copyGrade(inspector.clipInfo.id)
                    }
                    TextButton {
                        objectName: "pasteGradeButton"
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        text: "Paste"
                        variant: "secondary"
                        enabled: inspector.project.hasCopiedGrade
                        tip: "Give this clip the copied grade"
                        onClicked: inspector.project.pasteGrade(inspector.clipInfo.id)
                    }
                    TextButton {
                        objectName: "resetColorButton"
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        text: "Reset"
                        variant: "ghost"
                        enabled: colorPage.grade.neutral === false
                        onClicked: inspector.project.resetClipColor(inspector.clipInfo.id)
                    }
                }

                Section {
                    visible: colorPage.gradable
                    Layout.fillWidth: true
                    title: "Basic"
                    GradeSlider { key: "exposure"; label: "Exposure"; from: -4; to: 4
                                  format: (v) => (v > 0 ? "+" : "") + v.toFixed(2) + " EV" }
                    GradeSlider { key: "contrast"; label: "Contrast" }
                    GradeSlider { key: "saturation"; label: "Saturation" }
                    GradeSlider { key: "temperature"; label: "Temperature" }
                    GradeSlider { key: "tint"; label: "Tint" }
                }

                Section {
                    visible: colorPage.gradable
                    Layout.fillWidth: true
                    title: "Wheels"
                    GridLayout {
                        Layout.fillWidth: true
                        columns: 2
                        columnSpacing: Theme.space.m
                        rowSpacing: Theme.space.s
                        Repeater {
                            model: [{ key: "lift", title: "Lift (shadows)" }, { key: "gamma", title: "Gamma (midtones)" },
                                    { key: "gain", title: "Gain (highlights)" }, { key: "offset", title: "Offset" }]
                            delegate: ColorWheelControl {
                                required property var modelData
                                objectName: "wheel:" + modelData.key
                                Layout.fillWidth: true
                                Layout.preferredWidth: 1
                                title: modelData.title
                                wheel: colorPage.grade[modelData.key] || ({ x: 0, y: 0, master: 0 })
                                onMoved: (w) => { const m = {}; m[modelData.key] = w; colorPage.preview(m) }
                                onCommitted: (w) => { const m = {}; m[modelData.key] = w; colorPage.commit(m) }
                            }
                        }
                    }
                }

                Section {
                    visible: colorPage.gradable
                    Layout.fillWidth: true
                    title: "Curves"
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.space.xs
                        Repeater {
                            model: [{ key: "master", label: "Master" }, { key: "red", label: "Red" },
                                    { key: "green", label: "Green" }, { key: "blue", label: "Blue" }]
                            delegate: TextButton {
                                required property var modelData
                                objectName: "curveChannel:" + modelData.key
                                Layout.fillWidth: true
                                Layout.preferredWidth: 1
                                text: modelData.label
                                variant: colorPage.channel === modelData.key ? "primary" : "secondary"
                                onClicked: colorPage.channel = modelData.key
                            }
                        }
                    }
                    ToneCurveEditor {
                        objectName: "toneCurve"
                        Layout.fillWidth: true
                        project: inspector.project
                        points: colorPage.grade[colorPage.channel] || []
                        lineColor: colorPage.channel === "red" ? "#E5484D" : colorPage.channel === "green" ? "#30A46C"
                                 : colorPage.channel === "blue" ? "#3E7BFA" : Theme.color.accentText
                        onCommitted: (points) => { const m = {}; m[colorPage.channel] = points; colorPage.commit(m) }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            Layout.fillWidth: true
                            Layout.preferredWidth: 1
                            wrapMode: Text.WordWrap
                            text: "Drag points; double-click to add one, or an inner point to remove it."
                            font: Theme.type.caption
                            color: Theme.color.textSecondary
                        }
                        TextButton {
                            objectName: "resetCurveButton"
                            text: "Reset curve"
                            variant: "ghost"
                            enabled: (colorPage.grade[colorPage.channel] || []).length > 0
                            onClicked: { const m = {}; m[colorPage.channel] = []; colorPage.commit(m) }
                        }
                    }
                }

                Section {
                    visible: colorPage.gradable
                    Layout.fillWidth: true
                    title: "LUT"
                    Text {
                        objectName: "lutName"
                        Layout.fillWidth: true
                        elide: Text.ElideMiddle
                        text: colorPage.grade.lutName ? colorPage.grade.lutName + (colorPage.grade.lutMissing ? " (missing)" : "")
                                                      : "No LUT. Choose a .cube or .3dl file."
                        font: Theme.type.body
                        color: colorPage.grade.lutMissing ? Theme.color.danger
                             : colorPage.grade.lutName ? Theme.color.textPrimary : Theme.color.textSecondary
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.space.xs
                        TextButton {
                            objectName: "chooseLutButton"
                            Layout.fillWidth: true
                            Layout.preferredWidth: 1
                            text: colorPage.grade.lutName ? "Change LUT…" : "Choose LUT…"
                            variant: "secondary"
                            onClicked: lutDialog.open()
                        }
                        TextButton {
                            objectName: "removeLutButton"
                            Layout.fillWidth: true
                            Layout.preferredWidth: 1
                            text: "Remove"
                            variant: "secondary"
                            enabled: !!colorPage.grade.lutName
                            onClicked: inspector.project.removeLut(inspector.clipInfo.id)
                        }
                    }
                    GradeSlider { key: "lutIntensity"; label: "Intensity"; from: 0; to: 1
                                  visible: !!colorPage.grade.lutName; format: (v) => Math.round(v * 100) + "%" }
                    FileDialog {
                        id: lutDialog
                        title: "Choose a LUT"
                        nameFilters: ["LUTs (*.cube *.3dl)", "All files (*)"]
                        onAccepted: inspector.project.chooseLut(inspector.clipInfo.id, selectedFile)
                    }
                }

                Section {
                    visible: colorPage.gradable
                    Layout.fillWidth: true
                    title: "Presets"
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.space.xs
                        ComboBox {
                            id: presetPicker
                            objectName: "colorPresetPicker"
                            Layout.fillWidth: true
                            Layout.preferredWidth: 1
                            model: inspector.project.colorPresets
                            enabled: count > 0
                            displayText: count > 0 ? currentText : "No presets yet"
                        }
                        TextButton {
                            objectName: "applyColorPreset"
                            text: "Apply"
                            variant: "secondary"
                            enabled: presetPicker.count > 0
                            onClicked: inspector.project.applyColorPreset(inspector.clipInfo.id, presetPicker.currentText)
                        }
                        IconButton {
                            objectName: "deleteColorPreset"
                            iconName: "delete"
                            tip: "Delete this preset"
                            enabled: presetPicker.count > 0
                            onClicked: inspector.project.deleteColorPreset(presetPicker.currentText)
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.space.xs
                        TextField {
                            id: presetName
                            objectName: "colorPresetName"
                            Layout.fillWidth: true
                            Layout.preferredWidth: 1
                            placeholderText: "Preset name"
                        }
                        TextButton {
                            objectName: "saveColorPreset"
                            text: "Save"
                            variant: "secondary"
                            enabled: presetName.text.trim() !== ""
                            onClicked: {
                                if (inspector.project.saveColorPreset(inspector.clipInfo.id, presetName.text)) {
                                    presetPicker.currentIndex = inspector.project.colorPresets.indexOf(presetName.text.trim())
                                    presetName.text = ""
                                }
                            }
                        }
                    }
                }
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
                    // By count, so cards stay put while values change (animated values change
                    // with every frame) and are only rebuilt when effects come or go.
                    model: inspector.clipIsVideo ? (inspector.clipInfo.effects || []).length : 0
                    delegate: ClipEffectCard {
                        required property int index
                        readonly property var current: (inspector.clipInfo.effects || [])[index] || ({ effectId: "", enabled: false, values: {} })
                        Layout.fillWidth: true
                        project: inspector.project
                        clipId: inspector.clipInfo.id
                        stackIndex: index
                        entry: current
                        keyStates: inspector.keyStates
                        spec: inspector.player.availableEffects.find((e) => e.id === current.effectId) || ({ title: current.effectId, params: [] })
                    }
                }
            }
        }
    }

    function commitAudio(gainDb, fadeIn, fadeOut) {
        project.setClipAudio(clipInfo.id, gainDb, fadeIn, fadeOut)
    }

    // A colour grade number: previews live while dragging, commits one undo step on release.
    component GradeSlider: AudioSlider {
        required property string key
        objectName: "grade:" + key
        from: -1
        to: 1
        value: inspector.clipInfo.color ? inspector.clipInfo.color[key] : 0
        format: (v) => (v > 0 ? "+" : "") + Math.round(v * 100) + "%"
        onLiveValue: (v) => { const m = {}; m[key] = v; inspector.project.previewClipColor(inspector.clipInfo.id, m) }
        onCommitted: (v) => { const m = {}; m[key] = v; inspector.project.setClipColor(inspector.clipInfo.id, m) }
    }

    // A picture property: previews live while dragging, commits one undo step on release.
    component VisualSlider: AudioSlider {
        required property string key
        animKey: key
        onLiveValue: (v) => { const m = {}; m[key] = v; inspector.project.previewClipVisual(inspector.clipInfo.id, m) }
        onCommitted: (v) => { const m = {}; m[key] = v; inspector.project.setClipVisual(inspector.clipInfo.id, m) }
    }

    // A row of colour swatches; the one matching `current` is ringed.
    component Swatches: Flow {
        id: swatches
        property var current: [1, 1, 1, 1]
        signal picked(var color)
        Layout.fillWidth: true
        spacing: Theme.space.s
        readonly property var colors: [[1, 1, 1, 1], [0, 0, 0, 1], [1, 0.84, 0.2, 1], [1, 0.55, 0.1, 1],
                                       [0.92, 0.25, 0.3, 1], [0.95, 0.45, 0.75, 1], [0.35, 0.55, 1, 1],
                                       [0.3, 0.8, 0.55, 1], [1, 0.95, 0.85, 1]]
        Repeater {
            model: swatches.colors
            delegate: Rectangle {
                required property var modelData
                required property int index
                objectName: swatches.objectName + ":" + index
                readonly property bool chosen: swatches.current.length >= 3
                                               && Math.abs(swatches.current[0] - modelData[0]) < 0.01
                                               && Math.abs(swatches.current[1] - modelData[1]) < 0.01
                                               && Math.abs(swatches.current[2] - modelData[2]) < 0.01
                width: 22
                height: 22
                radius: width / 2
                color: Qt.rgba(modelData[0], modelData[1], modelData[2], 1)
                border.width: chosen ? 2 : 1
                border.color: chosen ? Theme.color.accent : Theme.color.borderStrong
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: swatches.picked(parent.modelData)
                }
            }
        }
    }

    // A slider that previews while dragging and commits one undo step on release.
    component AudioSlider: ColumnLayout {
        id: audioSlider
        property string label
        property alias from: slider.from
        property alias to: slider.to
        property real value: 0
        property var format: (v) => v.toFixed(2)
        property string animKey // an animatable property: shows its keyframe toggle
        signal committed(real value)
        signal liveValue(real value)
        Layout.fillWidth: true
        spacing: 0
        onValueChanged: slider.value = value

        RowLayout {
            Layout.fillWidth: true
            KeyframeToggle {
                objectName: "keyframe:" + audioSlider.animKey
                visible: audioSlider.animKey !== ""
                keyState: inspector.keyStates[audioSlider.animKey] || "none"
                onClicked: inspector.toggleKeyframe(audioSlider.animKey)
            }
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

    // Adds or removes channel `key`'s keyframe at the playhead (see KeyframeToggle).
    function toggleKeyframe(key) {
        if ((keyStates[key] || "none") === "key")
            project.removeKeyframes(clipInfo.id, [key])
        else
            project.addKeyframes(clipInfo.id, [key])
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
