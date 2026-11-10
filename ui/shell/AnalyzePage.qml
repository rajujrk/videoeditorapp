import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import TechieVideoEditor.Theme

// The inspector's Analyze tab: tools that listen to and look at the selected clip's media —
// pauses to cut out, scene changes to split at, the beat to mark, and lowering music under
// speech. Each finds first (shown on the timeline), then applies on request.
ScrollView {
    id: page
    required property var project
    required property var clipInfo

    readonly property bool hasClip: clipInfo.id !== undefined
    readonly property bool plainSpeed: (clipInfo.speed || 1) === 1 && !clipInfo.reversed && !clipInfo.freeze
                                       && (clipInfo.speedRamp || []).length === 0
    readonly property bool hasSound: clipInfo.hasAudio === true && !clipInfo.isCompound
    readonly property bool hasPictures: clipInfo.trackKind === "video" && clipInfo.isTitle !== true
                                        && clipInfo.isAdjustment !== true && clipInfo.isStill !== true
                                        && clipInfo.isCompound !== true && clipInfo.isSticker !== true
    // People (Vision): clips of video files, at any speed; reframing needs a steady one.
    readonly property bool canFindPeople: hasClip && clipInfo.canFindPeople === true
    readonly property bool steady: !clipInfo.reversed && !clipInfo.freeze && (clipInfo.speedRamp || []).length === 0

    contentWidth: availableWidth
    clip: true

    component Setting: ColumnLayout {
        id: setting
        property string label
        property alias from: slider.from
        property alias to: slider.to
        property alias value: slider.value
        property alias stepSize: slider.stepSize
        property var format: (v) => v.toFixed(1)
        // The value chosen: when the slider is let go (or stepped with the keys).
        signal committed()
        Layout.fillWidth: true
        spacing: 0
        RowLayout {
            Layout.fillWidth: true
            Text { text: setting.label; font: Theme.type.label; color: Theme.color.textSecondary; Layout.fillWidth: true }
            Text { text: setting.format(slider.value); font: Theme.type.mono; color: Theme.color.textPrimary }
        }
        Slider {
            id: slider
            Layout.fillWidth: true
            onPressedChanged: if (!pressed) setting.committed()
            Keys.onReleased: (event) => { if (event.key === Qt.Key_Left || event.key === Qt.Key_Right) setting.committed() }
        }
    }
    component Finding: Text {
        Layout.fillWidth: true
        font: Theme.type.body
        color: Theme.color.textPrimary
        wrapMode: Text.WordWrap
    }

    ColumnLayout {
        width: parent.width - Theme.space.m * 2
        x: Theme.space.m
        y: Theme.space.s
        spacing: Theme.space.s

        Text {
            visible: !page.hasClip || (!page.plainSpeed && !page.canFindPeople) || (!page.hasSound && !page.hasPictures)
            Layout.fillWidth: true
            Layout.topMargin: Theme.space.l
            text: !page.hasClip ? qsTr("Select a clip to find its pauses, scene changes and beat.")
                : !page.plainSpeed ? qsTr("Analysis works on clips playing at normal speed.")
                : qsTr("This clip has nothing to analyse (no sound and no moving pictures).")
            font: Theme.type.body
            color: Theme.color.textSecondary
            wrapMode: Text.WordWrap
        }

        // A running analysis
        ColumnLayout {
            visible: page.project.analyzing
            Layout.fillWidth: true
            spacing: Theme.space.xs
            RowLayout {
                Layout.fillWidth: true
                Text {
                    objectName: "analysisTask"
                    Layout.fillWidth: true
                    text: page.project.analysisTask + "… " + Math.round(page.project.analysisProgress * 100) + " %"
                    font: Theme.type.body
                    color: Theme.color.textPrimary
                }
                TextButton {
                    objectName: "cancelAnalysis"
                    text: qsTr("Cancel")
                    variant: "ghost"
                    onClicked: page.project.cancelAnalysis()
                }
            }
            ProgressBar {
                id: progress
                Layout.fillWidth: true
                value: page.project.analysisProgress
                background: Rectangle { implicitHeight: 4; radius: 2; color: Theme.color.borderSubtle }
                contentItem: Item {
                    implicitHeight: 4
                    Rectangle { width: parent.width * progress.visualPosition; height: parent.height; radius: 2; color: Theme.color.accent }
                }
            }
        }

        // A screen recording's cursor
        Section {
            id: cursorSection
            objectName: "cursorSection"
            readonly property var cursor: page.clipInfo.cursor
            visible: page.hasClip && cursor !== undefined
            Layout.fillWidth: true
            title: qsTr("Cursor")
            function change(values) { page.project.setCursorEffects(page.clipInfo.id, values) }

            Finding {
                text: qsTr("%1 clicks recorded with the screen.").arg(Fmt.num(cursorSection.cursor ? cursorSection.cursor.clicks : 0))
                color: Theme.color.textSecondary
            }
            ToggleSwitch {
                objectName: "cursorRings"
                text: qsTr("Rings where it clicks")
                checked: cursorSection.cursor !== undefined && cursorSection.cursor.rings
                onToggled: cursorSection.change({ rings: checked })
            }
            ToggleSwitch {
                objectName: "cursorHighlight"
                text: qsTr("Highlight around it")
                checked: cursorSection.cursor !== undefined && cursorSection.cursor.highlight
                onToggled: cursorSection.change({ highlight: checked })
            }
            ToggleSwitch {
                objectName: "cursorSpotlight"
                text: qsTr("Spotlight (dim the rest)")
                checked: cursorSection.cursor !== undefined && cursorSection.cursor.spotlight
                onToggled: cursorSection.change({ spotlight: checked })
            }
            Setting {
                objectName: "cursorSize"
                label: qsTr("Size")
                from: 0.02; to: 0.2; stepSize: 0.01
                value: cursorSection.cursor ? cursorSection.cursor.size : 0.05
                format: (v) => Math.round(v * 100) + " %"
                onCommitted: cursorSection.change({ size: value })
            }
            RowLayout {
                TextButton {
                    objectName: "zoomOnClicks"
                    text: qsTr("Zoom on clicks")
                    variant: "secondary"
                    enabled: cursorSection.cursor !== undefined && cursorSection.cursor.clicks > 0
                    onClicked: page.project.zoomOnClicks(page.clipInfo.id)
                }
                TextButton {
                    objectName: "addClickSounds"
                    text: qsTr("Click sounds")
                    variant: "secondary"
                    enabled: cursorSection.cursor !== undefined && cursorSection.cursor.clicks > 0
                    onClicked: page.project.addClickSounds(page.clipInfo.id)
                }
            }
        }

        // Multicam
        Section {
            objectName: "syncSection"
            visible: page.project.selectedClips.length >= 2
            Layout.fillWidth: true
            title: qsTr("Sync by sound")

            Finding {
                text: qsTr("Lines the %1 selected clips up by what they heard (cameras and a recorder at the same event). The first one selected stays; the others move on their tracks.")
                      .arg(Fmt.num(page.project.selectedClips.length))
                color: Theme.color.textSecondary
            }
            TextButton {
                objectName: "syncBySound"
                text: qsTr("Line up by sound")
                variant: "primary"
                enabled: !page.project.analyzing
                onClicked: page.project.syncBySound(page.project.selectedClips)
            }
        }

        // Portrait cutout
        Section {
            id: cutout
            objectName: "cutoutSection"
            visible: page.canFindPeople
            Layout.fillWidth: true
            title: qsTr("Cutout")
            readonly property var current: page.clipInfo.cutout || ({})
            readonly property bool hasArea: page.project.trackArea.clipId === page.clipInfo.id
            function change(values) { page.project.setPortraitCutout(page.clipInfo.id, values) }

            Finding {
                text: qsTr("Keeps the person — or any subject you draw a box around — and makes the background transparent (Vision, on this Mac), so the tracks below show through.")
                color: Theme.color.textSecondary
            }
            ToggleSwitch {
                objectName: "cutoutSwitch"
                text: cutout.current.subject ? qsTr("Cut out the subject") : qsTr("Cut out the person")
                checked: cutout.current.enabled === true
                onToggled: cutout.change({ enabled: checked })
            }
            Finding {
                objectName: "subjectState"
                visible: cutout.current.subject === true
                text: qsTr("Cutting out the subject in the box, followed through the clip.")
            }
            RowLayout {
                TextButton {
                    objectName: "drawSubject"
                    text: page.project.pickingTrackArea ? qsTr("Drag a box on the player…") : qsTr("Draw around a subject")
                    variant: "secondary"
                    tip: qsTr("For something other than people: an object, an animal, a product")
                    onClicked: page.project.pickingTrackArea = !page.project.pickingTrackArea
                }
                TextButton {
                    objectName: "cutOutSubject"
                    visible: cutout.hasArea
                    text: qsTr("Cut it out")
                    variant: "primary"
                    enabled: !page.project.analyzing && page.steady
                    onClicked: page.project.cutOutSubject(page.clipInfo.id)
                }
                TextButton {
                    objectName: "backToPeople"
                    visible: cutout.current.subject === true
                    text: qsTr("People instead")
                    variant: "ghost"
                    onClicked: cutout.change({ subject: false })
                }
            }
            ToggleSwitch {
                objectName: "cutoutInvert"
                visible: cutout.current.enabled === true
                text: qsTr("Remove the person instead")
                checked: cutout.current.invert === true
                onToggled: cutout.change({ invert: checked })
            }
            Setting {
                id: softness
                objectName: "cutoutSoftness"
                visible: cutout.current.enabled === true
                label: qsTr("Edge softness")
                from: 0; to: 1; stepSize: 0.05
                value: cutout.current.softness !== undefined ? cutout.current.softness : 0.3
                format: (v) => Math.round(v * 100) + " %"
                onCommitted: cutout.change({ softness: value })
            }
            Setting {
                objectName: "cutoutShift"
                visible: cutout.current.enabled === true
                label: qsTr("Edge (shrink … grow)")
                from: -1; to: 1; stepSize: 0.05
                value: cutout.current.shift || 0
                format: (v) => (v > 0 ? "+" : "") + v.toFixed(2)
                onCommitted: cutout.change({ shift: value })
            }
        }

        // Faces
        Section {
            id: faces
            objectName: "facesSection"
            visible: page.canFindPeople
            Layout.fillWidth: true
            title: qsTr("Faces")
            readonly property var found: page.hasClip && page.project.faces.clipId === page.clipInfo.id ? page.project.faces : null
            readonly property var cover: page.clipInfo.faceCover || ({ style: "mosaic", amount: 0.5, faceIds: [] })
            readonly property bool covering: (cover.faceIds || []).length > 0
            property var chosen: ({}) // face id → false when left out
            property string style: cover.style || "mosaic"
            onFoundChanged: chosen = ({})

            function timeText(s) { // whole seconds, m:ss
                const m = Math.floor(s / 60)
                return m + ":" + String(Math.floor(s % 60)).padStart(2, "0")
            }
            function chosenIds() {
                return (found ? found.tracks : []).filter((t) => chosen[t.id] !== false).map((t) => t.id)
            }

            TextButton {
                objectName: "findFaces"
                text: qsTr("Find faces")
                enabled: !page.project.analyzing
                onClicked: page.project.findFaces(page.clipInfo.id)
            }
            Finding {
                objectName: "facesFound"
                visible: faces.found !== null
                text: {
                    const n = faces.found ? faces.found.tracks.length : 0
                    return n === 0 ? qsTr("No faces in this clip.")
                         : n === 1 ? qsTr("1 face: choose whether to cover it.")
                         : qsTr("%1 faces: choose the ones to cover.").arg(Fmt.num(n))
                }
            }
            Flow {
                visible: faces.found !== null
                Layout.fillWidth: true
                spacing: Theme.space.xs
                Repeater {
                    model: faces.found ? faces.found.tracks : []
                    delegate: TextButton {
                        required property var modelData
                        objectName: "faceChip:" + modelData.id
                        readonly property bool on: faces.chosen[modelData.id] !== false
                        // Its span rounded outwards, so a face seen until 2.97 s reads "–0:03".
                        text: qsTr("Face %1 · %2–%3").arg(modelData.id).arg(faces.timeText(Math.floor(modelData.start)))
                              .arg(faces.timeText(Math.ceil(modelData.end)))
                        variant: on ? "primary" : "secondary"
                        tip: on ? qsTr("Click to leave it out; double-click to go there")
                                : qsTr("Click to cover it; double-click to go there")
                        onClicked: {
                            const next = Object.assign({}, faces.chosen)
                            next[modelData.id] = !on
                            faces.chosen = next
                        }
                        onDoubleClicked: page.project.seek(modelData.start)
                    }
                }
            }
            RowLayout {
                visible: faces.found !== null || faces.covering
                spacing: Theme.space.xs
                Repeater {
                    model: [{ id: "mosaic", label: qsTr("Mosaic") }, { id: "blur", label: qsTr("Blur") }, { id: "fill", label: qsTr("Fill") }]
                    delegate: TextButton {
                        required property var modelData
                        objectName: "faceStyle:" + modelData.id
                        text: modelData.label
                        variant: faces.style === modelData.id ? "primary" : "secondary"
                        onClicked: {
                            faces.style = modelData.id
                            if (faces.covering)
                                page.project.setFaceCover(page.clipInfo.id, { style: modelData.id })
                        }
                    }
                }
            }
            Setting {
                id: faceAmount
                objectName: "faceAmount"
                visible: faces.found !== null || faces.covering
                label: qsTr("Strength")
                from: 0; to: 1; stepSize: 0.05
                value: faces.cover.amount !== undefined ? faces.cover.amount : 0.5
                format: (v) => Math.round(v * 100) + " %"
                onCommitted: if (faces.covering) page.project.setFaceCover(page.clipInfo.id, { amount: value })
            }
            Finding {
                objectName: "facesCovered"
                visible: faces.covering
                text: faces.cover.faceIds.length === 1 ? qsTr("Covering face %1.").arg(faces.cover.faceIds[0])
                    : qsTr("Covering faces %1.").arg(faces.cover.faceIds.join(", "))
            }
            RowLayout {
                TextButton {
                    objectName: "coverFaces"
                    visible: faces.found !== null && faces.found.tracks.length > 0
                    text: faces.covering ? qsTr("Cover these instead") : qsTr("Cover faces")
                    variant: "primary"
                    enabled: faces.chosenIds().length > 0
                    onClicked: page.project.coverFaces(page.clipInfo.id, faces.chosenIds(),
                                                       { style: faces.style, amount: faceAmount.value })
                }
                TextButton {
                    objectName: "uncoverFaces"
                    visible: faces.covering
                    text: qsTr("Uncover")
                    variant: "ghost"
                    onClicked: page.project.uncoverFaces(page.clipInfo.id)
                }
            }
        }

        // Auto reframe
        Section {
            id: reframe
            objectName: "reframeSection"
            visible: page.canFindPeople
            Layout.fillWidth: true
            title: qsTr("Auto reframe")
            property string aspect: "9:16"

            Finding {
                text: qsTr("Changes the canvas shape and keeps the subject (faces, or what draws the eye) in the frame, with position keyframes.")
                color: Theme.color.textSecondary
            }
            RowLayout {
                spacing: Theme.space.xs
                Repeater {
                    model: ["9:16", "1:1", "4:5", "16:9"]
                    delegate: TextButton {
                        required property string modelData
                        objectName: "reframeAspect:" + modelData
                        text: modelData
                        variant: reframe.aspect === modelData ? "primary" : "secondary"
                        onClicked: reframe.aspect = modelData
                    }
                }
            }
            Setting {
                id: reframeSmoothing
                objectName: "reframeSmoothing"
                label: qsTr("Smoothness")
                from: 0; to: 1; stepSize: 0.05; value: 0.6
                format: (v) => Math.round(v * 100) + " %"
            }
            TextButton {
                objectName: "autoReframe"
                text: qsTr("Reframe")
                variant: "primary"
                enabled: !page.project.analyzing && page.steady
                tip: page.steady ? "" : qsTr("Works on clips playing forwards at a steady speed")
                onClicked: page.project.autoReframe(page.clipInfo.id, { aspect: reframe.aspect, smoothing: reframeSmoothing.value })
            }
        }

        // Processed pictures: upscale, smooth slow motion, object removal
        Section {
            id: picture
            objectName: "pictureSection"
            visible: page.canFindPeople
            Layout.fillWidth: true
            title: qsTr("Picture")
            readonly property var processed: page.clipInfo.processed
            readonly property var model: page.project.upscaleModel
            readonly property bool hasArea: page.project.trackArea.clipId === page.clipInfo.id
            property int scale: 2
            property int factor: 4
            function stepText(s) {
                return s.startsWith("upscale") ? qsTr("upscaled %1×").arg(s.substring(7))
                     : s.startsWith("smooth") ? qsTr("smooth slow motion %1×").arg(s.substring(6))
                     : s === "remove" ? qsTr("an object removed") : s
            }

            Finding {
                text: qsTr("Makes a processed copy of the clip's part of its file, which the clip then plays (kept beside the app's data).")
                color: Theme.color.textSecondary
            }
            Finding {
                objectName: "processedState"
                visible: picture.processed !== undefined
                text: !picture.processed ? ""
                    : picture.processed.missing ? qsTr("The processed copy is missing; the clip plays its own pictures.")
                    : qsTr("Processed: %1.").arg(picture.processed.steps.map(picture.stepText).join(", "))
            }
            TextButton {
                objectName: "restoreVideo"
                visible: picture.processed !== undefined
                text: qsTr("Original picture")
                variant: "ghost"
                onClicked: page.project.restoreVideo(page.clipInfo.id)
            }

            Text { text: qsTr("Upscale (Apple's model, pictures up to 1080p)"); font: Theme.type.label; color: Theme.color.textSecondary }
            RowLayout {
                visible: picture.model.status === "download" || picture.model.status === "downloading"
                TextButton {
                    objectName: "downloadUpscaleModel"
                    visible: picture.model.status === "download"
                    text: qsTr("Download the model")
                    variant: "secondary"
                    tip: qsTr("macOS downloads Apple's upscaling model once")
                    onClicked: page.project.downloadUpscaleModel()
                }
                Text {
                    visible: picture.model.status === "downloading"
                    text: qsTr("Downloading… %1").arg(Fmt.percent(picture.model.progress))
                    font: Theme.type.body
                    color: Theme.color.textPrimary
                }
            }
            RowLayout {
                visible: picture.model.status === "ready"
                spacing: Theme.space.xs
                Repeater {
                    model: [2, 4]
                    delegate: TextButton {
                        required property int modelData
                        objectName: "upscaleScale:" + modelData
                        text: modelData + "×"
                        variant: picture.scale === modelData ? "primary" : "secondary"
                        onClicked: picture.scale = modelData
                    }
                }
                TextButton {
                    objectName: "upscale"
                    text: qsTr("Upscale")
                    variant: "primary"
                    enabled: !page.project.analyzing
                    onClicked: page.project.processVideo(page.clipInfo.id, { kind: "upscale", scale: picture.scale })
                }
            }
            Text {
                visible: picture.model.status === "unsupported"
                text: qsTr("Upscaling is not available on this Mac.")
                font: Theme.type.body
                color: Theme.color.textSecondary
            }

            Text { text: qsTr("Smooth slow motion (frames made between)"); font: Theme.type.label; color: Theme.color.textSecondary }
            RowLayout {
                spacing: Theme.space.xs
                Repeater {
                    model: [2, 4, 8]
                    delegate: TextButton {
                        required property int modelData
                        objectName: "smoothFactor:" + modelData
                        text: modelData + "×"
                        variant: picture.factor === modelData ? "primary" : "secondary"
                        onClicked: picture.factor = modelData
                    }
                }
                TextButton {
                    objectName: "smooth"
                    text: qsTr("Make frames")
                    variant: "primary"
                    enabled: !page.project.analyzing
                    tip: qsTr("For slowing the clip down: %1 frames for every one").arg(Fmt.num(picture.factor))
                    onClicked: page.project.processVideo(page.clipInfo.id, { kind: "smooth", factor: picture.factor })
                }
            }

            Text { text: qsTr("Remove an object"); font: Theme.type.label; color: Theme.color.textSecondary }
            RowLayout {
                TextButton {
                    objectName: "drawObject"
                    text: page.project.pickingTrackArea ? qsTr("Drag a box on the player…") : qsTr("Draw around it")
                    variant: "secondary"
                    tip: qsTr("At the playhead; it is followed to the clip's end")
                    onClicked: page.project.pickingTrackArea = !page.project.pickingTrackArea
                }
                TextButton {
                    objectName: "removeObject"
                    visible: picture.hasArea
                    text: qsTr("Remove it")
                    variant: "primary"
                    enabled: !page.project.analyzing && page.steady
                    onClicked: page.project.processVideo(page.clipInfo.id, { kind: "remove" })
                }
            }
        }

        // Steadying: stabilization, flicker, temporal denoise
        Section {
            id: steady
            objectName: "steadySection"
            visible: page.canFindPeople
            Layout.fillWidth: true
            title: qsTr("Steady and clean")
            readonly property var stab: page.clipInfo.stabilization || ({})
            readonly property var flicker: page.clipInfo.deflicker || ({})

            Finding {
                text: qsTr("Smooths a shaky camera (zooming in just enough to hide the edges), evens out flicker and takes grain out over time.")
                color: Theme.color.textSecondary
            }
            Setting {
                id: smoothness
                objectName: "stabilizeSmoothness"
                label: qsTr("Smoothness")
                from: 0; to: 1; stepSize: 0.05
                value: steady.stab.smoothness !== undefined ? steady.stab.smoothness : 0.5
                format: (v) => Math.round(v * 100) + " %"
                enabled: !lockSwitch.checked
                onCommitted: if (steady.stab.enabled) page.project.stabilize(page.clipInfo.id, { smoothness: value })
            }
            ToggleSwitch {
                id: lockSwitch
                objectName: "stabilizeLock"
                text: qsTr("Lock the camera (tripod)")
                checked: steady.stab.lock === true
                onToggled: if (steady.stab.enabled) page.project.stabilize(page.clipInfo.id, { lock: checked })
            }
            Finding {
                objectName: "stabilizeState"
                visible: steady.stab.enabled === true
                text: qsTr("Stabilized, zoomed in %1.").arg(Fmt.percent((steady.stab.zoom || 1) - 1))
            }
            RowLayout {
                TextButton {
                    objectName: "stabilize"
                    text: steady.stab.enabled ? qsTr("Stabilize again") : qsTr("Stabilize")
                    variant: "primary"
                    enabled: !page.project.analyzing
                    onClicked: page.project.stabilize(page.clipInfo.id, { smoothness: smoothness.value, lock: lockSwitch.checked })
                }
                TextButton {
                    objectName: "removeStabilization"
                    visible: steady.stab.enabled === true
                    text: qsTr("Remove")
                    variant: "ghost"
                    onClicked: page.project.removeStabilization(page.clipInfo.id)
                }
            }
            Setting {
                id: flickerStrength
                objectName: "deflickerStrength"
                label: qsTr("Flicker: even out changes up to")
                from: 0; to: 1; stepSize: 0.05
                value: steady.flicker.strength !== undefined ? steady.flicker.strength : 0.5
                format: (v) => (0.1 + 1.9 * v).toFixed(1) + " s"
                onCommitted: if (steady.flicker.enabled) page.project.removeFlicker(page.clipInfo.id, { strength: value })
            }
            RowLayout {
                TextButton {
                    objectName: "removeFlicker"
                    text: steady.flicker.enabled ? qsTr("Flicker removed") : qsTr("Remove flicker")
                    variant: steady.flicker.enabled ? "secondary" : "primary"
                    enabled: !page.project.analyzing && steady.flicker.enabled !== true
                    onClicked: page.project.removeFlicker(page.clipInfo.id, { strength: flickerStrength.value })
                }
                TextButton {
                    objectName: "keepFlicker"
                    visible: steady.flicker.enabled === true
                    text: qsTr("Undo it")
                    variant: "ghost"
                    onClicked: page.project.keepFlicker(page.clipInfo.id)
                }
            }
            Setting {
                objectName: "denoiseStrength"
                label: qsTr("Denoise over time")
                from: 0; to: 1; stepSize: 0.05
                value: page.clipInfo.denoise || 0
                format: (v) => v === 0 ? "off" : Math.round(v * 100) + " %"
                onCommitted: page.project.setDenoise(page.clipInfo.id, value)
            }
        }

        // Tracking
        Section {
            id: tracking
            objectName: "trackSection"
            visible: page.canFindPeople
            Layout.fillWidth: true
            title: qsTr("Track motion")
            readonly property bool hasArea: page.project.trackArea.clipId === page.clipInfo.id

            Finding {
                text: qsTr("Follows something through the clip from the playhead, so titles and stickers can move with it.")
                color: Theme.color.textSecondary
            }
            TextButton {
                objectName: "chooseTrackArea"
                text: page.project.pickingTrackArea ? qsTr("Drag a box on the player…") : tracking.hasArea ? qsTr("Draw again") : qsTr("Choose what to follow")
                variant: "secondary"
                onClicked: page.project.pickingTrackArea = !page.project.pickingTrackArea
            }
            RowLayout {
                visible: tracking.hasArea
                TextButton {
                    objectName: "trackObject"
                    text: qsTr("Track object")
                    variant: "primary"
                    enabled: !page.project.analyzing && page.steady
                    tip: qsTr("Something that moves and may change shape: a person, a car, a ball")
                    onClicked: page.project.trackMotion(page.clipInfo.id, { surface: false })
                }
                TextButton {
                    objectName: "trackSurface"
                    text: qsTr("Track surface")
                    variant: "secondary"
                    enabled: !page.project.analyzing && page.steady
                    tip: qsTr("A flat rectangle that also turns: a screen, a sign, a page")
                    onClicked: page.project.trackMotion(page.clipInfo.id, { surface: true })
                }
            }
            Repeater {
                model: page.clipInfo.motionTracks || []
                delegate: RowLayout {
                    required property var modelData
                    Layout.fillWidth: true
                    Text {
                        objectName: "motionTrack:" + modelData.id
                        Layout.fillWidth: true
                        text: (modelData.surface ? qsTr("Track %1 · surface · %2–%3 s") : qsTr("Track %1 · object · %2–%3 s"))
                              .arg(modelData.id).arg(Fmt.num(modelData.start, 1)).arg(Fmt.num(modelData.end, 1))
                        font: Theme.type.body
                        color: Theme.color.textPrimary
                    }
                    TextButton {
                        objectName: "deleteTrack:" + modelData.id
                        text: qsTr("Delete")
                        variant: "ghost"
                        onClicked: page.project.deleteTrack(page.clipInfo.id, modelData.id)
                    }
                }
            }
        }

        // Following a track (any clip with a picture)
        Section {
            id: follow
            objectName: "followSection"
            readonly property var choices: {
                page.project.undoText // new tracks
                return page.hasClip && page.clipInfo.isAdjustment !== true && page.clipInfo.trackKind === "video"
                        ? page.project.tracksToFollow(page.clipInfo.id) : []
            }
            visible: choices.length > 0
            Layout.fillWidth: true
            title: qsTr("Follow a track")

            ComboBox {
                id: followChoice
                objectName: "followChoice"
                Layout.fillWidth: true
                model: follow.choices
                textRole: "label"
            }
            ToggleSwitch {
                id: followScale
                objectName: "followScale"
                text: qsTr("Grow and shrink with it")
            }
            ToggleSwitch {
                id: followTurn
                objectName: "followTurn"
                text: qsTr("Turn with it (surfaces)")
            }
            TextButton {
                objectName: "followTrack"
                text: qsTr("Follow")
                variant: "primary"
                tip: qsTr("Moves this clip with the track from where it is at the playhead, as keyframes")
                onClicked: {
                    const c = follow.choices[followChoice.currentIndex]
                    page.project.followTrack(page.clipInfo.id, c.clipId, c.trackId,
                                             { scale: followScale.checked, rotation: followTurn.checked })
                }
            }
        }

        // Following a drawn path (any clip with a picture, while a drawing is on screen)
        Section {
            id: followPath
            objectName: "followPathSection"
            readonly property var choices: {
                page.project.undoText // new paths
                return page.hasClip && page.clipInfo.isAdjustment !== true && page.clipInfo.trackKind === "video"
                        && page.clipInfo.isDrawing !== true ? page.project.pathsToFollow(page.clipInfo.id) : []
            }
            visible: choices.length > 0
            Layout.fillWidth: true
            title: qsTr("Follow a drawn path")

            ComboBox {
                id: pathChoice
                objectName: "pathChoice"
                Layout.fillWidth: true
                model: followPath.choices
                textRole: "label"
            }
            ToggleSwitch {
                id: pathTurn
                objectName: "pathTurn"
                text: qsTr("Turn along it")
            }
            TextButton {
                objectName: "followPath"
                text: qsTr("Follow")
                variant: "primary"
                tip: qsTr("Moves this clip along the path from its start to its end over the clip's length, as keyframes")
                onClicked: {
                    const c = followPath.choices[pathChoice.currentIndex]
                    page.project.followPath(page.clipInfo.id, c.clipId, c.index, { turn: pathTurn.checked })
                }
            }
        }

        // Pauses
        Section {
            id: pauses
            objectName: "pausesSection"
            visible: page.hasClip && page.plainSpeed && page.hasSound
            Layout.fillWidth: true
            title: qsTr("Pauses")
            readonly property var found: page.hasClip && page.project.silences.clipId === page.clipInfo.id ? page.project.silences : null

            Setting { id: quieter; objectName: "pauseThreshold"; label: qsTr("Quieter than (dB)"); from: -60; to: -20; stepSize: 1; value: -40; format: (v) => v.toFixed(0) }
            Setting { id: longer; objectName: "pauseLength"; label: qsTr("Longer than (s)"); from: 0.2; to: 3; stepSize: 0.1; value: 0.5 }
            Setting { id: keep; objectName: "pausePadding"; label: qsTr("Keep at each side (s)"); from: 0; to: 0.5; stepSize: 0.05; value: 0.1; format: (v) => v.toFixed(2) }
            TextButton {
                objectName: "findPauses"
                text: qsTr("Find pauses")
                enabled: !page.project.analyzing
                onClicked: page.project.findSilences(page.clipInfo.id, { thresholdDb: quieter.value, minDuration: longer.value, padding: keep.value })
            }
            Finding {
                objectName: "pausesFound"
                visible: pauses.found !== null
                text: {
                    const f = pauses.found
                    if (!f) return ""
                    const n = f.ranges.length
                    return n === 0 ? qsTr("No pauses that long.")
                         : n === 1 ? qsTr("1 pause, %1 s in all (red on the timeline).").arg(Fmt.num(f.total, 1))
                         : qsTr("%1 pauses, %2 s in all (red on the timeline).").arg(Fmt.num(n)).arg(Fmt.num(f.total, 1))
                }
            }
            RowLayout {
                visible: pauses.found !== null && pauses.found.ranges.length > 0
                TextButton {
                    objectName: "removePauses"
                    text: qsTr("Remove them")
                    variant: "primary"
                    tip: qsTr("Cuts the pauses out and closes the gaps on this clip's track")
                    onClicked: page.project.removeSilences()
                }
                TextButton { text: qsTr("Clear"); variant: "ghost"; onClicked: page.project.clearAnalysis() }
            }
        }

        // Scene changes
        Section {
            id: scenes
            objectName: "scenesSection"
            visible: page.hasClip && page.plainSpeed && page.hasPictures
            Layout.fillWidth: true
            title: qsTr("Scene changes")
            readonly property var found: page.hasClip && page.project.scenes.clipId === page.clipInfo.id ? page.project.scenes : null

            Setting { id: sensitivity; objectName: "sceneSensitivity"; label: qsTr("Sensitivity"); from: 0; to: 1; stepSize: 0.05; value: 0.55; format: (v) => Math.round(v * 100) + " %" }
            TextButton {
                objectName: "findScenes"
                text: qsTr("Find scene changes")
                enabled: !page.project.analyzing
                onClicked: page.project.findScenes(page.clipInfo.id, { threshold: 0.6 - 0.45 * sensitivity.value })
            }
            Finding {
                objectName: "scenesFound"
                visible: scenes.found !== null
                text: {
                    const f = scenes.found
                    if (!f) return ""
                    const n = f.cuts.length
                    return n === 0 ? qsTr("One continuous shot.")
                         : n === 1 ? qsTr("1 scene change (green on the timeline).")
                         : qsTr("%1 scene changes (green on the timeline).").arg(Fmt.num(n))
                }
            }
            RowLayout {
                visible: scenes.found !== null && scenes.found.cuts.length > 0
                TextButton { objectName: "splitAtScenes"; text: qsTr("Split there"); variant: "primary"; onClicked: page.project.splitAtScenes() }
                TextButton { objectName: "markScenes"; text: qsTr("Add markers"); variant: "secondary"; onClicked: page.project.markScenes() }
            }
        }

        // The beat
        Section {
            id: beat
            objectName: "beatSection"
            visible: page.hasClip && page.plainSpeed && page.hasSound
            Layout.fillWidth: true
            title: qsTr("Beat")
            readonly property var found: page.hasClip && page.project.beats.clipId === page.clipInfo.id ? page.project.beats : null

            TextButton {
                objectName: "findBeats"
                text: qsTr("Find the beat")
                enabled: !page.project.analyzing
                onClicked: page.project.findBeats(page.clipInfo.id)
            }
            Finding {
                objectName: "beatsFound"
                visible: beat.found !== null
                text: {
                    const f = beat.found
                    if (!f) return ""
                    return f.times.length === 0 ? qsTr("No steady beat.")
                         : qsTr("%1 BPM · %2 beats (yellow ticks on the ruler).").arg(Fmt.num(f.bpm)).arg(Fmt.num(f.times.length))
                }
            }
            RowLayout {
                visible: beat.found !== null && beat.found.times.length > 0
                TextButton { objectName: "markEveryBeat"; text: qsTr("Mark every beat"); variant: "secondary"; onClicked: page.project.addBeatMarkers(1) }
                TextButton { objectName: "markEveryBar"; text: qsTr("Every 4 beats"); variant: "secondary"; onClicked: page.project.addBeatMarkers(4) }
            }
        }

        // Speech enhancement
        Section {
            id: enhance
            objectName: "enhanceSection"
            visible: page.hasClip && page.hasSound && page.clipInfo.trackKind !== undefined
            Layout.fillWidth: true
            title: qsTr("Enhance speech")
            readonly property var model: page.project.enhanceModel
            readonly property var download: page.project.modelDownload
            readonly property bool downloading: download.id !== undefined && download.id === model.id
            readonly property var current: page.clipInfo.enhanced

            Finding {
                text: qsTr("Takes noise, hum and room echo out of voices with DeepFilterNet3, on this Mac. The clip then plays the cleaned sound.")
                color: Theme.color.textSecondary
            }
            // The model, once
            RowLayout {
                visible: enhance.model.downloaded !== true
                Layout.fillWidth: true
                TextButton {
                    objectName: "downloadEnhanceModel"
                    visible: !enhance.downloading
                    text: qsTr("Download model (%1)").arg(Fmt.mb(enhance.model.size))
                    variant: "secondary"
                    onClicked: page.project.downloadSpeechModel(enhance.model.id)
                }
                Text {
                    objectName: "enhanceModelProgress"
                    visible: enhance.downloading
                    Layout.fillWidth: true
                    text: qsTr("Downloading… %1").arg(Fmt.percent(enhance.download.progress))
                    font: Theme.type.body
                    color: Theme.color.textPrimary
                }
                TextButton {
                    visible: enhance.downloading
                    text: qsTr("Cancel")
                    variant: "ghost"
                    onClicked: page.project.cancelModelDownload()
                }
            }
            Setting {
                id: strength
                objectName: "enhanceStrength"
                label: qsTr("Lower noise by up to")
                from: 6
                to: 42
                stepSize: 6
                value: enhance.current ? Math.min(42, enhance.current.limitDb) : 42
                format: (v) => v >= 42 ? "all of it" : v.toFixed(0) + " dB"
            }
            Finding {
                objectName: "enhanceState"
                visible: enhance.current !== undefined
                text: !enhance.current ? ""
                    : enhance.current.missing ? qsTr("The cleaned sound is missing; enhance again to make it.")
                    : enhance.current.limitDb >= 100 ? qsTr("Enhanced: the clip plays its cleaned sound.")
                    : qsTr("Enhanced, noise lowered by up to %1 dB.").arg(Fmt.num(enhance.current.limitDb))
            }
            RowLayout {
                TextButton {
                    objectName: "enhanceSpeech"
                    text: enhance.current && !enhance.current.missing ? qsTr("Apply strength") : qsTr("Enhance speech")
                    variant: "primary"
                    enabled: enhance.model.downloaded === true && !page.project.analyzing
                    onClicked: page.project.enhanceSpeech(page.clipInfo.id, { limitDb: strength.value >= 42 ? 100 : strength.value })
                }
                TextButton {
                    objectName: "removeEnhancement"
                    visible: enhance.current !== undefined
                    text: qsTr("Original sound")
                    variant: "ghost"
                    onClicked: page.project.removeEnhancement(page.clipInfo.id)
                }
            }
        }

        // Ducking
        Section {
            objectName: "duckingSection"
            visible: page.hasClip && page.hasSound
            Layout.fillWidth: true
            title: qsTr("Lower under speech")

            Finding {
                text: qsTr("Turns this clip (music) down while the other clips playing with it have someone talking, as volume keyframes.")
                color: Theme.color.textSecondary
            }
            Setting { id: lower; objectName: "duckAmount"; label: qsTr("Lower by (dB)"); from: -24; to: -3; stepSize: 1; value: -12; format: (v) => v.toFixed(0) }
            Setting { id: duckFade; objectName: "duckFade"; label: qsTr("Fade (s)"); from: 0.1; to: 1; stepSize: 0.05; value: 0.25; format: (v) => v.toFixed(2) }
            TextButton {
                objectName: "duckUnderSpeech"
                text: qsTr("Lower under speech")
                enabled: !page.project.analyzing
                onClicked: page.project.duckUnderSpeech(page.clipInfo.id, { amountDb: lower.value, fade: duckFade.value })
            }
        }
    }
}
