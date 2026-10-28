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
    readonly property var masks: clipInfo.masks || []
    readonly property var activeMaskData: project.activeMask >= 0 && project.activeMask < masks.length
                                          ? masks[project.activeMask] : null
    readonly property var chromaKey: clipInfo.chromaKey || ({})
    function commitMask(changes) { project.setMask(clipInfo.id, project.activeMask, changes) }
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

            TextButton {
                objectName: "applyTransitionToAll"
                Layout.fillWidth: true
                text: "Apply to all cuts"
                variant: "secondary"
                tip: "Put this transition, this long, on every cut between clips"
                onClicked: inspector.project.applyTransitionToAll(inspector.transitionInfo.transitionId,
                                                                  inspector.transitionInfo.requestedDuration
                                                                  || inspector.transitionInfo.duration)
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

                    // A shape behind the text: boxes, a pill, bubbles, a banner and more.
                    Section {
                        id: shapeSection
                        objectName: "titleShapeSection"
                        visible: inspector.clipInfo.isTitle === true
                        Layout.fillWidth: true
                        expanded: (textSection.text.shape || {}).kind !== undefined && textSection.text.shape.kind !== "none"
                        title: "Shape"
                        readonly property var shape: textSection.text.shape || ({ kind: "none", color: [0, 0, 0, 0.6], padding: 0.4, tail: 0.25 })
                        function change(fields) { textSection.change({ shape: Object.assign({}, shape, fields) }) }
                        RowLayout {
                            Layout.fillWidth: true
                            Text { text: "Behind the text"; font: Theme.type.label; color: Theme.color.textSecondary; Layout.fillWidth: true }
                            ComboBox {
                                objectName: "titleShapeKind"
                                readonly property var kinds: ["none", "box", "rounded", "pill", "speech", "thought", "banner",
                                                              "underline", "circle", "arrow", "tag"]
                                model: ["None", "Box", "Rounded box", "Pill", "Speech bubble", "Thought bubble", "Banner",
                                        "Underline", "Circle", "Arrow", "Tag"]
                                currentIndex: Math.max(0, kinds.indexOf(shapeSection.shape.kind))
                                onActivated: (index) => shapeSection.change({ kind: kinds[index] })
                            }
                        }
                        Swatches {
                            objectName: "titleShapeColor"
                            visible: shapeSection.shape.kind !== "none"
                            current: shapeSection.shape.color
                            // The swatch's colour at the shape's own opacity.
                            onPicked: (c) => shapeSection.change({ color: [c[0], c[1], c[2], shapeSection.shape.color[3]] })
                        }
                        AudioSlider {
                            objectName: "titleShapeOpacity"
                            visible: shapeSection.shape.kind !== "none"
                            label: "Opacity"
                            from: 0
                            to: 1
                            value: shapeSection.shape.color[3]
                            format: (v) => Math.round(v * 100) + "%"
                            onCommitted: (v) => shapeSection.change({ color: [shapeSection.shape.color[0], shapeSection.shape.color[1],
                                                                              shapeSection.shape.color[2], v] })
                        }
                        AudioSlider {
                            objectName: "titleShapePadding"
                            visible: shapeSection.shape.kind !== "none"
                            label: "Padding"
                            from: 0
                            to: 2
                            value: shapeSection.shape.padding
                            onCommitted: (v) => shapeSection.change({ padding: v })
                        }
                        AudioSlider {
                            objectName: "titleShapeTail"
                            visible: shapeSection.shape.kind === "speech" || shapeSection.shape.kind === "thought"
                            label: "Tail"
                            from: 0
                            to: 1
                            value: shapeSection.shape.tail
                            format: (v) => v < 0.5 ? "Left" : "Right"
                            onCommitted: (v) => shapeSection.change({ tail: v })
                        }
                    }

                    // How the text arrives, leaves and keeps moving, and its scrolling.
                    Section {
                        id: motionSection
                        objectName: "titleAnimationSection"
                        visible: inspector.clipInfo.isTitle === true
                        Layout.fillWidth: true
                        title: "Animation"
                        readonly property var units: ["whole", "line", "word", "character"]
                        readonly property var unitNames: ["Whole", "Lines", "Words", "Letters"]
                        Repeater {
                            model: [{ key: "animationIn", label: "In", loop: false }, { key: "animationOut", label: "Out", loop: false },
                                    { key: "animationLoop", label: "Loop", loop: true }]
                            delegate: ColumnLayout {
                                id: motionRow
                                required property var modelData
                                readonly property var animation: textSection.text[modelData.key] || ({ preset: "" })
                                readonly property var presets: [{ id: "", title: "None" }].concat(
                                    inspector.project.textAnimations.filter((p) => p.loop === modelData.loop))
                                function change(fields) {
                                    const m = {}
                                    m[modelData.key] = Object.assign({}, animation, fields)
                                    textSection.change(m)
                                }
                                Layout.fillWidth: true
                                spacing: Theme.space.xxs
                                RowLayout {
                                    Layout.fillWidth: true
                                    Text { text: motionRow.modelData.label; font: Theme.type.label; color: Theme.color.textSecondary; Layout.preferredWidth: 36 }
                                    ComboBox {
                                        objectName: "titleAnimation:" + motionRow.modelData.key
                                        Layout.fillWidth: true
                                        model: motionRow.presets
                                        textRole: "title"
                                        valueRole: "id"
                                        currentIndex: Math.max(0, motionRow.presets.findIndex((p) => p.id === motionRow.animation.preset))
                                        onActivated: (index) => motionRow.change({ preset: motionRow.presets[index].id })
                                    }
                                    ComboBox {
                                        objectName: "titleAnimationUnit:" + motionRow.modelData.key
                                        visible: motionRow.animation.preset !== ""
                                        Layout.preferredWidth: 100
                                        model: motionSection.unitNames
                                        currentIndex: Math.max(0, motionSection.units.indexOf(motionRow.animation.unit))
                                        onActivated: (index) => motionRow.change({ unit: motionSection.units[index] })
                                    }
                                }
                                AudioSlider {
                                    objectName: "titleAnimationDuration:" + motionRow.modelData.key
                                    visible: motionRow.animation.preset !== ""
                                    label: motionRow.modelData.loop ? "Cycle" : "Duration"
                                    from: 0.1
                                    to: motionRow.modelData.loop ? 5 : 3
                                    value: motionRow.animation.duration || 0.8
                                    format: (v) => v.toFixed(1) + " s"
                                    onCommitted: (v) => motionRow.change({ duration: v })
                                }
                                AudioSlider {
                                    objectName: "titleAnimationStagger:" + motionRow.modelData.key
                                    visible: motionRow.animation.preset !== "" && !motionRow.modelData.loop
                                             && motionRow.animation.unit !== "whole" && motionRow.animation.preset !== "typewriter"
                                    label: "Stagger"
                                    from: 0
                                    to: 1
                                    value: motionRow.animation.stagger === undefined ? 0.6 : motionRow.animation.stagger
                                    format: (v) => Math.round(v * 100) + "%"
                                    onCommitted: (v) => motionRow.change({ stagger: v })
                                }
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            Text { text: "Scroll"; font: Theme.type.label; color: Theme.color.textSecondary; Layout.fillWidth: true }
                            ComboBox {
                                objectName: "titleScroll"
                                readonly property var modes: ["none", "rollUp", "crawlLeft"]
                                model: ["None", "Roll up (credits)", "Crawl left (ticker)"]
                                currentIndex: Math.max(0, modes.indexOf(textSection.text.scroll || "none"))
                                onActivated: (index) => textSection.change({ scroll: modes[index] })
                            }
                        }
                    }

                    Text {
                        objectName: "adjustmentNote"
                        visible: inspector.clipInfo.isAdjustment === true
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: "An adjustment layer changes everything below it while it lasts. Give it a look "
                              + "(Filters), a grade (Color) or effects (Effects); its opacity sets how strongly."
                        font: Theme.type.body
                        color: Theme.color.textSecondary
                    }
                    Section {
                        visible: inspector.clipInfo.isAdjustment !== true
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
                        visible: inspector.clipInfo.isAdjustment !== true
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

                    // Masks: shapes that keep (or, inverted, cut out) part of the picture. The
                    // selected one is also drawn on the player, where it can be dragged.
                    Section {
                        id: maskSection
                        objectName: "masksSection"
                        visible: inspector.clipInfo.isAdjustment !== true
                        Layout.fillWidth: true
                        title: "Masks"
                        readonly property var masks: inspector.masks
                        readonly property int active: inspector.project.activeMask
                        readonly property var mask: inspector.activeMaskData
                        function commit(changes) { inspector.commitMask(changes) }

                        Flow {
                            Layout.fillWidth: true
                            spacing: Theme.space.xs
                            Repeater {
                                model: [{ id: "rectangle", label: "Rectangle" }, { id: "ellipse", label: "Ellipse" },
                                        { id: "line", label: "Line" }, { id: "star", label: "Star" },
                                        { id: "heart", label: "Heart" }, { id: "pen", label: "Pen" }]
                                delegate: TextButton {
                                    required property var modelData
                                    objectName: "addMask:" + modelData.id
                                    text: "+ " + modelData.label
                                    variant: "ghost"
                                    enabled: maskSection.masks.length < 8
                                    onClicked: inspector.project.addMask(inspector.clipInfo.id, modelData.id)
                                }
                            }
                        }
                        Text {
                            visible: maskSection.masks.length === 0
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            text: "Add a shape to show only part of the picture. Pen masks take any outline: "
                                  + "click on the player to add points."
                            font: Theme.type.caption
                            color: Theme.color.textSecondary
                        }
                        Flow {
                            visible: maskSection.masks.length > 0
                            Layout.fillWidth: true
                            spacing: Theme.space.xs
                            Repeater {
                                model: maskSection.masks.length
                                delegate: AbstractButton {
                                    id: maskChip
                                    required property int index
                                    readonly property var mask: maskSection.masks[index]
                                    objectName: "maskChip:" + index
                                    checked: maskSection.active === index
                                    hoverEnabled: true
                                    implicitWidth: chipText.implicitWidth + Theme.space.m * 2
                                    implicitHeight: 26
                                    onClicked: inspector.project.activeMask = checked ? -1 : index
                                    background: Rectangle {
                                        radius: Theme.radius.s
                                        color: maskChip.checked ? Theme.color.accentSubtle
                                             : maskChip.hovered ? Theme.color.surfaceHover : Theme.color.surfaceRaised
                                        border.color: maskChip.checked ? Theme.color.accent : Theme.color.borderSubtle
                                    }
                                    contentItem: Text {
                                        id: chipText
                                        text: (maskChip.index + 1) + " · " + maskChip.mask.shape.charAt(0).toUpperCase()
                                              + maskChip.mask.shape.slice(1) + (maskChip.mask.invert ? " (inverted)" : "")
                                        font: Theme.type.label
                                        color: Theme.color.textPrimary
                                        horizontalAlignment: Text.AlignHCenter
                                        verticalAlignment: Text.AlignVCenter
                                    }
                                }
                            }
                        }
                        ColumnLayout {
                            objectName: "maskEditor"
                            visible: maskSection.mask !== null
                            Layout.fillWidth: true
                            spacing: Theme.space.xs
                            readonly property bool shaped: maskSection.mask !== null && maskSection.mask.shape !== "pen"

                            MaskSlider { key: "x"; label: "Centre X"; from: -0.5; to: 1.5; visible: parent.shaped
                                         format: (v) => Math.round(v * 100) + "%" }
                            MaskSlider { key: "y"; label: "Centre Y"; from: -0.5; to: 1.5; visible: parent.shaped
                                         format: (v) => Math.round(v * 100) + "%" }
                            MaskSlider { key: "width"; label: "Width"; from: 0.01; to: 2
                                         visible: parent.shaped && maskSection.mask.shape !== "line"
                                         format: (v) => Math.round(v * 100) + "%" }
                            MaskSlider { key: "height"; label: "Height"; from: 0.01; to: 2
                                         visible: parent.shaped && maskSection.mask.shape !== "line"
                                         format: (v) => Math.round(v * 100) + "%" }
                            MaskSlider { key: "rotation"; label: "Rotation"; from: -180; to: 180; visible: parent.shaped
                                         format: (v) => v.toFixed(1) + "°" }
                            MaskSlider { key: "roundness"; label: "Roundness"; from: 0; to: 1
                                         visible: maskSection.mask !== null && maskSection.mask.shape === "rectangle"
                                         format: (v) => Math.round(v * 100) + "%" }
                            MaskSlider { key: "feather"; label: "Feather"; from: 0; to: 0.5
                                         format: (v) => Math.round(v * 200) + "%" }
                            Text {
                                visible: maskSection.mask !== null && maskSection.mask.shape === "pen"
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                text: "Drag the points on the player; click between them to add one, double-click one to remove it."
                                font: Theme.type.caption
                                color: Theme.color.textSecondary
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                Text { text: "Invert"; font: Theme.type.label; color: Theme.color.textSecondary; Layout.fillWidth: true }
                                ToggleSwitch {
                                    objectName: "maskInvert"
                                    checked: maskSection.mask !== null && maskSection.mask.invert
                                    onToggled: maskSection.commit({ invert: checked })
                                }
                            }
                            TextButton {
                                objectName: "removeMaskButton"
                                text: "Remove mask"
                                variant: "ghost"
                                onClicked: inspector.project.removeMask(inspector.clipInfo.id, maskSection.active)
                            }
                        }
                    }

                    // Chroma key: removes a colour (a green or blue screen) from the picture.
                    Section {
                        id: keySection
                        objectName: "chromaKeySection"
                        visible: inspector.clipInfo.isAdjustment !== true && inspector.clipInfo.isTitle !== true
                        Layout.fillWidth: true
                        expanded: key.enabled === true
                        title: "Chroma key"
                        readonly property var key: inspector.chromaKey
                        readonly property var keyColor: key.color || [0, 0.69, 0.25]
                        function commit(changes) { inspector.project.setChromaKey(inspector.clipInfo.id, changes) }

                        RowLayout {
                            Layout.fillWidth: true
                            Text { text: "Remove a colour"; font: Theme.type.label; color: Theme.color.textSecondary; Layout.fillWidth: true }
                            ToggleSwitch {
                                objectName: "chromaKeyEnabled"
                                checked: keySection.key.enabled === true
                                onToggled: keySection.commit({ enabled: checked })
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.space.s
                            Rectangle {
                                objectName: "keyColorSwatch"
                                width: 26
                                height: 26
                                radius: Theme.radius.s
                                color: Qt.rgba(keySection.keyColor[0], keySection.keyColor[1], keySection.keyColor[2], 1)
                                border.color: Theme.color.borderStrong
                            }
                            TextButton {
                                objectName: "keyGreen"
                                text: "Green"
                                variant: "ghost"
                                onClicked: keySection.commit({ enabled: true, color: [0, 0.69, 0.25] })
                            }
                            TextButton {
                                objectName: "keyBlue"
                                text: "Blue"
                                variant: "ghost"
                                onClicked: keySection.commit({ enabled: true, color: [0.05, 0.28, 0.85] })
                            }
                            Item { Layout.fillWidth: true }
                            TextButton {
                                objectName: "pickKeyColor"
                                text: inspector.project.pickingKeyColor ? "Click the player…" : "Pick"
                                variant: inspector.project.pickingKeyColor ? "primary" : "secondary"
                                tip: "Click a colour on the player to remove it"
                                onClicked: inspector.project.pickingKeyColor = !inspector.project.pickingKeyColor
                            }
                        }
                        KeySlider { key: "tolerance"; objectName: "keyTolerance"; label: "Tolerance"; from: 0; to: 0.6
                                    format: (v) => Math.round(v * 100 / 0.6) + "%" }
                        KeySlider { key: "softness"; objectName: "keySoftness"; label: "Edge softness"; from: 0; to: 0.4
                                    format: (v) => Math.round(v * 100 / 0.4) + "%" }
                        KeySlider { key: "spill"; objectName: "keySpill"; label: "Spill removal"; from: 0; to: 1
                                    format: (v) => Math.round(v * 100) + "%" }
                    }

                    // Track matte: this clip shows only where the clip on the track above it does.
                    Section {
                        objectName: "matteSection"
                        visible: inspector.clipInfo.isAdjustment !== true
                        Layout.fillWidth: true
                        expanded: (inspector.clipInfo.matte || "none") !== "none"
                        title: "Track matte"
                        RowLayout {
                            Layout.fillWidth: true
                            Text { text: "Shape from above"; font: Theme.type.label; color: Theme.color.textSecondary; Layout.fillWidth: true }
                            ComboBox {
                                objectName: "matteMode"
                                readonly property var modes: ["none", "alpha", "invertedAlpha", "luma", "invertedLuma"]
                                model: ["None", "Alpha", "Alpha inverted", "Luma", "Luma inverted"]
                                currentIndex: Math.max(0, modes.indexOf(inspector.clipInfo.matte || "none"))
                                onActivated: (index) => inspector.project.setMatte(inspector.clipInfo.id, modes[index])
                            }
                        }
                        Text {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            text: "The clip on the track just above becomes the shape (by its transparency or brightness) "
                                  + "and is not shown itself."
                            font: Theme.type.caption
                            color: Theme.color.textSecondary
                        }
                    }

                    // Layouts: arranges the clips at the playhead into a split screen or an inset.
                    Section {
                        objectName: "layoutSection"
                        visible: inspector.clipInfo.isAdjustment !== true
                        Layout.fillWidth: true
                        expanded: false
                        title: "Split screen"
                        Flow {
                            Layout.fillWidth: true
                            spacing: Theme.space.s
                            Repeater {
                                model: inspector.project.layouts
                                delegate: AbstractButton {
                                    id: layoutButton
                                    required property var modelData
                                    objectName: "layout:" + modelData.id
                                    hoverEnabled: true
                                    implicitWidth: 64
                                    implicitHeight: 52
                                    ToolTip.visible: hovered
                                    ToolTip.text: modelData.title
                                    onClicked: inspector.project.applyLayout(modelData.id)
                                    background: Rectangle {
                                        radius: Theme.radius.s
                                        color: layoutButton.hovered ? Theme.color.surfaceHover : Theme.color.surfaceRaised
                                        border.color: layoutButton.hovered ? Theme.color.borderStrong : Theme.color.borderSubtle
                                    }
                                    // A miniature canvas with the layout's regions.
                                    contentItem: Item {
                                        Rectangle {
                                            id: miniCanvas
                                            anchors.centerIn: parent
                                            width: 48
                                            height: 27
                                            color: Theme.color.surfaceSunken
                                            border.color: Theme.color.borderStrong
                                            Repeater {
                                                model: layoutButton.modelData.regions
                                                delegate: Rectangle {
                                                    required property var modelData
                                                    x: modelData[0] * miniCanvas.width + 1
                                                    y: modelData[1] * miniCanvas.height + 1
                                                    width: (modelData[2] - modelData[0]) * miniCanvas.width - 2
                                                    height: (modelData[3] - modelData[1]) * miniCanvas.height - 2
                                                    radius: 1
                                                    color: Theme.color.accentSubtle
                                                    border.color: Theme.color.accent
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                        Text {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            text: "Uses the clips at the playhead on separate tracks, top track first. "
                                  + "Picture in picture shrinks the top one into the corner."
                            font: Theme.type.caption
                            color: Theme.color.textSecondary
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
                    visible: inspector.clipInfo.isTitle === true || inspector.clipInfo.isAdjustment === true
                             || inspector.clipInfo.isStill === true || inspector.clipInfo.isCompound === true
                    Layout.fillWidth: true
                    Layout.topMargin: Theme.space.l
                    text: (inspector.clipInfo.isTitle === true ? "Titles"
                           : inspector.clipInfo.isSticker === true ? "Stickers"
                           : inspector.clipInfo.isCompound === true ? "Compound clips"
                           : inspector.clipInfo.isStill === true ? "Images" : "Adjustment layers")
                          + " have no speed; trim them to change how long they last."
                    font: Theme.type.body
                    color: Theme.color.textSecondary
                    wrapMode: Text.WordWrap
                }

                Section {
                    id: speedSection
                    visible: inspector.clipInfo.id !== undefined && inspector.clipInfo.isTitle !== true
                             && inspector.clipInfo.isAdjustment !== true && inspector.clipInfo.isStill !== true
                             && inspector.clipInfo.isCompound !== true
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
                    ToggleSwitch {
                        objectName: "keepPitchSwitch"
                        visible: !speedSection.still && inspector.clipInfo.hasSound === true
                        enabled: inspector.clipInfo.reversed !== true
                        text: "Keep pitch"
                        checked: inspector.clipInfo.keepPitch === true
                        ToolTip.visible: hovered
                        ToolTip.text: enabled ? "Faster or slower, the sound keeps its pitch (off: it rises and falls like tape)"
                                              : "Reversed sound always follows the speed"
                        onToggled: inspector.project.setKeepPitch(inspector.clipInfo.id, checked)
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
                // Audio effects: EQ, dynamics, rooms, echoes, pitch and voices, in a stack.
                Section {
                    id: audioEffectsSection
                    objectName: "audioEffectsSection"
                    visible: inspector.clipInfo.hasSound === true
                    Layout.fillWidth: true
                    title: "Audio effects"
                    readonly property var categoryNames: ({ eq: "EQ", dynamics: "Dynamics", space: "Room", echo: "Echo",
                                                            modulation: "Modulation", pitch: "Pitch", voice: "Voice" })
                    RowLayout {
                        Layout.fillWidth: true
                        ComboBox {
                            id: audioEffectPicker
                            objectName: "audioEffectPicker"
                            Layout.fillWidth: true
                            textRole: "label"
                            valueRole: "id"
                            model: inspector.project.audioEffects.map((e) => Object.assign(
                                { label: (audioEffectsSection.categoryNames[e.categories[0]] || e.categories[0]) + " · " + e.title }, e))
                        }
                        TextButton {
                            objectName: "addAudioEffectButton"
                            text: "Add"
                            iconName: "plus"
                            onClicked: inspector.project.addAudioEffect(inspector.clipInfo.id, audioEffectPicker.currentValue)
                        }
                    }
                    Text {
                        visible: (inspector.clipInfo.audioEffects || []).length === 0
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: "Effects run in order, after the volume and fades. Echoes and reverb ring on past the clip's end."
                        font: Theme.type.caption
                        color: Theme.color.textSecondary
                    }
                    Repeater {
                        model: (inspector.clipInfo.audioEffects || []).length
                        delegate: ClipEffectCard {
                            required property int index
                            readonly property var current: (inspector.clipInfo.audioEffects || [])[index]
                                                           || ({ effectId: "", enabled: false, values: {} })
                            Layout.fillWidth: true
                            audio: true
                            project: inspector.project
                            clipId: inspector.clipInfo.id
                            stackIndex: index
                            entry: current
                            spec: inspector.project.audioEffects.find((e) => e.id === current.effectId)
                                  || ({ title: current.effectId, params: [] })
                        }
                    }
                }

                // Loudness: measures the clip and sets its volume to a target.
                Section {
                    id: loudnessSection
                    objectName: "loudnessSection"
                    visible: inspector.clipInfo.hasSound === true
                    Layout.fillWidth: true
                    title: "Loudness"
                    readonly property var loudness: inspector.clipInfo.loudness || null
                    RowLayout {
                        Layout.fillWidth: true
                        ComboBox {
                            id: loudnessTarget
                            objectName: "loudnessTarget"
                            Layout.fillWidth: true
                            textRole: "label"
                            valueRole: "lufs"
                            model: [{ label: "−14 LUFS · online", lufs: -14 }, { label: "−16 LUFS · podcast", lufs: -16 },
                                    { label: "−23 LUFS · broadcast", lufs: -23 }]
                        }
                        TextButton {
                            objectName: "normalizeLoudnessButton"
                            text: inspector.project.measuringLoudness ? "Measuring…" : "Normalize"
                            enabled: !inspector.project.measuringLoudness
                            tip: "Measure this clip and set its volume to reach the target"
                            onClicked: inspector.project.normalizeLoudness(inspector.clipInfo.id, loudnessTarget.currentValue)
                        }
                    }
                    Text {
                        objectName: "loudnessResult"
                        visible: loudnessSection.loudness !== null
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: loudnessSection.loudness === null ? ""
                              : "Measured " + loudnessSection.loudness.measured.toFixed(1) + " LUFS; the volume now brings it to "
                                + loudnessSection.loudness.target.toFixed(0) + " LUFS."
                        font: Theme.type.caption
                        color: Theme.color.textSecondary
                    }
                }
            }

            // Color: the selected clip's grade
            InspectorPage {
                id: colorPage
                readonly property var grade: inspector.clipInfo.color || ({})
                readonly property bool gradable: inspector.clipIsVideo && inspector.clipInfo.isTitle !== true
                property string channel: "master"
                property string scopeMode: "waveform"
                property int hslBand: 0
                property string hueCurve: "hueVsSat"
                readonly property var bandColors: ["#E5484D", "#F2994A", "#F5D90A", "#46A758", "#12A594", "#3E63DD", "#8E4EC6", "#D6409F"]
                readonly property var bandNames: ["Red", "Orange", "Yellow", "Green", "Aqua", "Blue", "Purple", "Magenta"]
                readonly property var band: (grade.hsl || [])[hslBand] || ({ hue: 0, saturation: 0, luminance: 0 })

                function preview(changes) { inspector.project.previewClipColor(inspector.clipInfo.id, changes) }
                function commit(changes) { inspector.project.setClipColor(inspector.clipInfo.id, changes) }

                // Scopes of the preview (whatever is on screen, graded or not).
                Section {
                    objectName: "scopesSection"
                    Layout.fillWidth: true
                    Layout.topMargin: Theme.space.s
                    title: "Scopes"
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.space.xxs
                        Repeater {
                            model: [{ id: "waveform", label: "Waveform" }, { id: "parade", label: "Parade" },
                                    { id: "vectorscope", label: "Vector" }, { id: "histogram", label: "Histogram" }]
                            delegate: TextButton {
                                required property var modelData
                                objectName: "scopeMode:" + modelData.id
                                Layout.fillWidth: true
                                Layout.preferredWidth: 1
                                implicitWidth: 40
                                text: modelData.label
                                variant: colorPage.scopeMode === modelData.id ? "primary" : "secondary"
                                onClicked: colorPage.scopeMode = modelData.id
                            }
                        }
                    }
                    ScopeView {
                        id: scope
                        objectName: "scopeView"
                        Layout.fillWidth: true
                        Layout.preferredHeight: colorPage.scopeMode === "vectorscope" ? 220 : colorPage.scopeMode === "histogram" ? 120 : 170
                        view: inspector.player
                        mode: colorPage.scopeMode
                    }
                }

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

                // The clip's look (filter), before the grade below.
                Section {
                    objectName: "lookSection"
                    visible: colorPage.gradable
                    Layout.fillWidth: true
                    title: "Look"
                    Text {
                        objectName: "lookName"
                        Layout.fillWidth: true
                        text: inspector.clipInfo.look ? inspector.clipInfo.look.title : "No look. Pick one in the Filters library."
                        font: Theme.type.body
                        color: inspector.clipInfo.look ? Theme.color.textPrimary : Theme.color.textSecondary
                        wrapMode: Text.WordWrap
                    }
                    AudioSlider {
                        objectName: "lookIntensity"
                        visible: !!inspector.clipInfo.look
                        label: "Intensity"
                        from: 0
                        to: 1
                        value: inspector.clipInfo.look ? inspector.clipInfo.look.intensity : 1
                        format: (v) => Math.round(v * 100) + "%"
                        onLiveValue: (v) => inspector.project.previewLookIntensity(inspector.clipInfo.id, v)
                        onCommitted: (v) => inspector.project.setLookIntensity(inspector.clipInfo.id, v)
                    }
                    TextButton {
                        objectName: "removeLookButton"
                        visible: !!inspector.clipInfo.look
                        text: "Remove look"
                        variant: "secondary"
                        onClicked: inspector.project.removeLook(inspector.clipInfo.id)
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

                // HSL: change one range of hues at a time.
                Section {
                    objectName: "hslSection"
                    visible: colorPage.gradable
                    Layout.fillWidth: true
                    title: "HSL"
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.space.xs
                        Repeater {
                            model: 8
                            delegate: Rectangle {
                                id: chip
                                required property int index
                                readonly property var settings: (colorPage.grade.hsl || [])[index] || ({})
                                readonly property bool changed: settings.hue !== 0 || settings.saturation !== 0 || settings.luminance !== 0
                                objectName: "hslBand:" + index
                                Layout.fillWidth: true
                                implicitHeight: 22
                                radius: 11
                                color: colorPage.bandColors[index]
                                border.width: colorPage.hslBand === index ? 2 : 0
                                border.color: Theme.color.textPrimary
                                Rectangle { // a dot marks bands that change something
                                    visible: chip.changed
                                    anchors.centerIn: parent
                                    width: 6; height: 6; radius: 3
                                    color: "white"
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: colorPage.hslBand = chip.index
                                }
                            }
                        }
                    }
                    Text {
                        objectName: "hslBandName"
                        text: colorPage.bandNames[colorPage.hslBand]
                        font: Theme.type.heading
                        color: Theme.color.textPrimary
                    }
                    Repeater {
                        model: [{ key: "hue", label: "Hue" }, { key: "saturation", label: "Saturation" },
                                { key: "luminance", label: "Luminance" }]
                        delegate: AudioSlider {
                            required property var modelData
                            objectName: "hsl:" + modelData.key
                            label: modelData.label
                            from: -1
                            to: 1
                            value: colorPage.band[modelData.key] || 0
                            format: modelData.key === "hue" ? ((v) => (v > 0 ? "+" : "") + Math.round(v * 30) + "°")
                                                            : ((v) => (v > 0 ? "+" : "") + Math.round(v * 100) + "%")
                            onLiveValue: (v) => {
                                const change = { band: colorPage.hslBand }
                                change[modelData.key] = v
                                colorPage.preview({ hsl: [change] })
                            }
                            onCommitted: (v) => {
                                const change = { band: colorPage.hslBand }
                                change[modelData.key] = v
                                colorPage.commit({ hsl: [change] })
                            }
                        }
                    }
                }

                // Hue curves: saturation or hue by hue, saturation by brightness.
                Section {
                    objectName: "hueCurvesSection"
                    visible: colorPage.gradable
                    Layout.fillWidth: true
                    title: "Hue curves"
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.space.xxs
                        Repeater {
                            model: [{ key: "hueVsSat", label: "Hue · Sat" }, { key: "hueVsHue", label: "Hue · Hue" },
                                    { key: "lumaVsSat", label: "Luma · Sat" }]
                            delegate: TextButton {
                                required property var modelData
                                objectName: "hueCurve:" + modelData.key
                                Layout.fillWidth: true
                                Layout.preferredWidth: 1
                                implicitWidth: 40
                                text: modelData.label
                                variant: colorPage.hueCurve === modelData.key ? "primary" : "secondary"
                                onClicked: colorPage.hueCurve = modelData.key
                            }
                        }
                    }
                    ToneCurveEditor {
                        objectName: "hueCurveEditor"
                        Layout.fillWidth: true
                        project: inspector.project
                        kind: colorPage.hueCurve === "lumaVsSat" ? "luma" : "hue"
                        points: colorPage.grade[colorPage.hueCurve] || []
                        onCommitted: (points) => { const m = {}; m[colorPage.hueCurve] = points; colorPage.commit(m) }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            Layout.fillWidth: true
                            Layout.preferredWidth: 1
                            wrapMode: Text.WordWrap
                            text: colorPage.hueCurve === "hueVsHue" ? "Above the line turns those hues forward, below turns them back (up to 60°)."
                                : "Above the line adds saturation there, below takes it away."
                            font: Theme.type.caption
                            color: Theme.color.textSecondary
                        }
                        TextButton {
                            objectName: "resetHueCurveButton"
                            text: "Reset"
                            variant: "ghost"
                            enabled: (colorPage.grade[colorPage.hueCurve] || []).length > 0
                            onClicked: { const m = {}; m[colorPage.hueCurve] = []; colorPage.commit(m) }
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
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.space.xs
                        Text { text: "Built-in"; font: Theme.type.label; color: Theme.color.textSecondary }
                        ComboBox {
                            objectName: "lutLibraryPicker"
                            Layout.fillWidth: true
                            Layout.preferredWidth: 1
                            model: inspector.project.lutLibrary
                            textRole: "name"
                            displayText: "Choose…"
                            onActivated: (index) => inspector.project.chooseLut(inspector.clipInfo.id,
                                                                                "qrc" + model[index].path)
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

    // A slider for the active mask's `key` field (see the Masks section).
    component MaskSlider: AudioSlider {
        required property string key
        objectName: "mask:" + key
        value: inspector.activeMaskData !== null && inspector.activeMaskData[key] !== undefined ? inspector.activeMaskData[key] : 0
        onLiveValue: (v) => { const m = {}; m[key] = v; inspector.project.previewMask(inspector.clipInfo.id, inspector.project.activeMask, m) }
        onCommitted: (v) => { const m = {}; m[key] = v; inspector.commitMask(m) }
    }

    // A slider for the chroma key's `key` field.
    component KeySlider: AudioSlider {
        required property string key
        value: inspector.chromaKey[key] !== undefined ? inspector.chromaKey[key] : 0
        onLiveValue: (v) => { const m = {}; m[key] = v; inspector.project.previewChromaKey(inspector.clipInfo.id, m) }
        onCommitted: (v) => { const m = {}; m[key] = v; inspector.project.setChromaKey(inspector.clipInfo.id, m) }
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
