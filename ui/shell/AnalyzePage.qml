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
            text: !page.hasClip ? "Select a clip to find its pauses, scene changes and beat."
                : !page.plainSpeed ? "Analysis works on clips playing at normal speed."
                : "This clip has nothing to analyse (no sound and no moving pictures)."
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
                    text: "Cancel"
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
            title: "Cursor"
            function change(values) { page.project.setCursorEffects(page.clipInfo.id, values) }

            Finding {
                text: (cursorSection.cursor ? cursorSection.cursor.clicks : 0) + " clicks recorded with the screen."
                color: Theme.color.textSecondary
            }
            ToggleSwitch {
                objectName: "cursorRings"
                text: "Rings where it clicks"
                checked: cursorSection.cursor !== undefined && cursorSection.cursor.rings
                onToggled: cursorSection.change({ rings: checked })
            }
            ToggleSwitch {
                objectName: "cursorHighlight"
                text: "Highlight around it"
                checked: cursorSection.cursor !== undefined && cursorSection.cursor.highlight
                onToggled: cursorSection.change({ highlight: checked })
            }
            ToggleSwitch {
                objectName: "cursorSpotlight"
                text: "Spotlight (dim the rest)"
                checked: cursorSection.cursor !== undefined && cursorSection.cursor.spotlight
                onToggled: cursorSection.change({ spotlight: checked })
            }
            Setting {
                objectName: "cursorSize"
                label: "Size"
                from: 0.02; to: 0.2; stepSize: 0.01
                value: cursorSection.cursor ? cursorSection.cursor.size : 0.05
                format: (v) => Math.round(v * 100) + " %"
                onCommitted: cursorSection.change({ size: value })
            }
            RowLayout {
                TextButton {
                    objectName: "zoomOnClicks"
                    text: "Zoom on clicks"
                    variant: "secondary"
                    enabled: cursorSection.cursor !== undefined && cursorSection.cursor.clicks > 0
                    onClicked: page.project.zoomOnClicks(page.clipInfo.id)
                }
                TextButton {
                    objectName: "addClickSounds"
                    text: "Click sounds"
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
            title: "Sync by sound"

            Finding {
                text: "Lines the " + page.project.selectedClips.length
                      + " selected clips up by what they heard (cameras and a recorder at the same event). The first one selected stays; the others move on their tracks."
                color: Theme.color.textSecondary
            }
            TextButton {
                objectName: "syncBySound"
                text: "Line up by sound"
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
            title: "Cutout"
            readonly property var current: page.clipInfo.cutout || ({})
            readonly property bool hasArea: page.project.trackArea.clipId === page.clipInfo.id
            function change(values) { page.project.setPortraitCutout(page.clipInfo.id, values) }

            Finding {
                text: "Keeps the person — or any subject you draw a box around — and makes the background transparent (Vision, on this Mac), so the tracks below show through."
                color: Theme.color.textSecondary
            }
            ToggleSwitch {
                objectName: "cutoutSwitch"
                text: cutout.current.subject ? "Cut out the subject" : "Cut out the person"
                checked: cutout.current.enabled === true
                onToggled: cutout.change({ enabled: checked })
            }
            Finding {
                objectName: "subjectState"
                visible: cutout.current.subject === true
                text: "Cutting out the subject in the box, followed through the clip."
            }
            RowLayout {
                TextButton {
                    objectName: "drawSubject"
                    text: page.project.pickingTrackArea ? "Drag a box on the player…" : "Draw around a subject"
                    variant: "secondary"
                    tip: "For something other than people: an object, an animal, a product"
                    onClicked: page.project.pickingTrackArea = !page.project.pickingTrackArea
                }
                TextButton {
                    objectName: "cutOutSubject"
                    visible: cutout.hasArea
                    text: "Cut it out"
                    variant: "primary"
                    enabled: !page.project.analyzing && page.steady
                    onClicked: page.project.cutOutSubject(page.clipInfo.id)
                }
                TextButton {
                    objectName: "backToPeople"
                    visible: cutout.current.subject === true
                    text: "People instead"
                    variant: "ghost"
                    onClicked: cutout.change({ subject: false })
                }
            }
            ToggleSwitch {
                objectName: "cutoutInvert"
                visible: cutout.current.enabled === true
                text: "Remove the person instead"
                checked: cutout.current.invert === true
                onToggled: cutout.change({ invert: checked })
            }
            Setting {
                id: softness
                objectName: "cutoutSoftness"
                visible: cutout.current.enabled === true
                label: "Edge softness"
                from: 0; to: 1; stepSize: 0.05
                value: cutout.current.softness !== undefined ? cutout.current.softness : 0.3
                format: (v) => Math.round(v * 100) + " %"
                onCommitted: cutout.change({ softness: value })
            }
            Setting {
                objectName: "cutoutShift"
                visible: cutout.current.enabled === true
                label: "Edge (shrink … grow)"
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
            title: "Faces"
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
                text: "Find faces"
                enabled: !page.project.analyzing
                onClicked: page.project.findFaces(page.clipInfo.id)
            }
            Finding {
                objectName: "facesFound"
                visible: faces.found !== null
                text: {
                    const n = faces.found ? faces.found.tracks.length : 0
                    return n === 0 ? "No faces in this clip." : n + (n === 1 ? " face" : " faces") + ": choose the ones to cover."
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
                        text: "Face " + modelData.id + " · " + faces.timeText(Math.floor(modelData.start))
                              + "–" + faces.timeText(Math.ceil(modelData.end))
                        variant: on ? "primary" : "secondary"
                        tip: "Click to " + (on ? "leave it out" : "cover it") + "; double-click to go there"
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
                    model: [{ id: "mosaic", label: "Mosaic" }, { id: "blur", label: "Blur" }, { id: "fill", label: "Fill" }]
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
                label: "Strength"
                from: 0; to: 1; stepSize: 0.05
                value: faces.cover.amount !== undefined ? faces.cover.amount : 0.5
                format: (v) => Math.round(v * 100) + " %"
                onCommitted: if (faces.covering) page.project.setFaceCover(page.clipInfo.id, { amount: value })
            }
            Finding {
                objectName: "facesCovered"
                visible: faces.covering
                text: "Covering " + (faces.cover.faceIds.length === 1 ? "face " : "faces ") + faces.cover.faceIds.join(", ") + "."
            }
            RowLayout {
                TextButton {
                    objectName: "coverFaces"
                    visible: faces.found !== null && faces.found.tracks.length > 0
                    text: faces.covering ? "Cover these instead" : "Cover faces"
                    variant: "primary"
                    enabled: faces.chosenIds().length > 0
                    onClicked: page.project.coverFaces(page.clipInfo.id, faces.chosenIds(),
                                                       { style: faces.style, amount: faceAmount.value })
                }
                TextButton {
                    objectName: "uncoverFaces"
                    visible: faces.covering
                    text: "Uncover"
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
            title: "Auto reframe"
            property string aspect: "9:16"

            Finding {
                text: "Changes the canvas shape and keeps the subject (faces, or what draws the eye) in the frame, with position keyframes."
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
                label: "Smoothness"
                from: 0; to: 1; stepSize: 0.05; value: 0.6
                format: (v) => Math.round(v * 100) + " %"
            }
            TextButton {
                objectName: "autoReframe"
                text: "Reframe"
                variant: "primary"
                enabled: !page.project.analyzing && page.steady
                tip: page.steady ? "" : "Works on clips playing forwards at a steady speed"
                onClicked: page.project.autoReframe(page.clipInfo.id, { aspect: reframe.aspect, smoothing: reframeSmoothing.value })
            }
        }

        // Processed pictures: upscale, smooth slow motion, object removal
        Section {
            id: picture
            objectName: "pictureSection"
            visible: page.canFindPeople
            Layout.fillWidth: true
            title: "Picture"
            readonly property var processed: page.clipInfo.processed
            readonly property var model: page.project.upscaleModel
            readonly property bool hasArea: page.project.trackArea.clipId === page.clipInfo.id
            property int scale: 2
            property int factor: 4
            function stepText(s) {
                return s.startsWith("upscale") ? "upscaled " + s.substring(7) + "×"
                     : s.startsWith("smooth") ? "smooth slow motion " + s.substring(6) + "×"
                     : s === "remove" ? "an object removed" : s
            }

            Finding {
                text: "Makes a processed copy of the clip's part of its file, which the clip then plays (kept beside the app's data)."
                color: Theme.color.textSecondary
            }
            Finding {
                objectName: "processedState"
                visible: picture.processed !== undefined
                text: !picture.processed ? ""
                    : picture.processed.missing ? "The processed copy is missing; the clip plays its own pictures."
                    : "Processed: " + picture.processed.steps.map(picture.stepText).join(", ") + "."
            }
            TextButton {
                objectName: "restoreVideo"
                visible: picture.processed !== undefined
                text: "Original picture"
                variant: "ghost"
                onClicked: page.project.restoreVideo(page.clipInfo.id)
            }

            Text { text: "Upscale (Apple's model, pictures up to 1080p)"; font: Theme.type.label; color: Theme.color.textSecondary }
            RowLayout {
                visible: picture.model.status === "download" || picture.model.status === "downloading"
                TextButton {
                    objectName: "downloadUpscaleModel"
                    visible: picture.model.status === "download"
                    text: "Download the model"
                    variant: "secondary"
                    tip: "macOS downloads Apple's upscaling model once"
                    onClicked: page.project.downloadUpscaleModel()
                }
                Text {
                    visible: picture.model.status === "downloading"
                    text: "Downloading… " + Math.round((picture.model.progress || 0) * 100) + " %"
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
                    text: "Upscale"
                    variant: "primary"
                    enabled: !page.project.analyzing
                    onClicked: page.project.processVideo(page.clipInfo.id, { kind: "upscale", scale: picture.scale })
                }
            }
            Text {
                visible: picture.model.status === "unsupported"
                text: "Upscaling is not available on this Mac."
                font: Theme.type.body
                color: Theme.color.textSecondary
            }

            Text { text: "Smooth slow motion (frames made between)"; font: Theme.type.label; color: Theme.color.textSecondary }
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
                    text: "Make frames"
                    variant: "primary"
                    enabled: !page.project.analyzing
                    tip: "For slowing the clip down: " + picture.factor + " frames for every one"
                    onClicked: page.project.processVideo(page.clipInfo.id, { kind: "smooth", factor: picture.factor })
                }
            }

            Text { text: "Remove an object"; font: Theme.type.label; color: Theme.color.textSecondary }
            RowLayout {
                TextButton {
                    objectName: "drawObject"
                    text: page.project.pickingTrackArea ? "Drag a box on the player…" : "Draw around it"
                    variant: "secondary"
                    tip: "At the playhead; it is followed to the clip's end"
                    onClicked: page.project.pickingTrackArea = !page.project.pickingTrackArea
                }
                TextButton {
                    objectName: "removeObject"
                    visible: picture.hasArea
                    text: "Remove it"
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
            title: "Steady and clean"
            readonly property var stab: page.clipInfo.stabilization || ({})
            readonly property var flicker: page.clipInfo.deflicker || ({})

            Finding {
                text: "Smooths a shaky camera (zooming in just enough to hide the edges), evens out flicker and takes grain out over time."
                color: Theme.color.textSecondary
            }
            Setting {
                id: smoothness
                objectName: "stabilizeSmoothness"
                label: "Smoothness"
                from: 0; to: 1; stepSize: 0.05
                value: steady.stab.smoothness !== undefined ? steady.stab.smoothness : 0.5
                format: (v) => Math.round(v * 100) + " %"
                enabled: !lockSwitch.checked
                onCommitted: if (steady.stab.enabled) page.project.stabilize(page.clipInfo.id, { smoothness: value })
            }
            ToggleSwitch {
                id: lockSwitch
                objectName: "stabilizeLock"
                text: "Lock the camera (tripod)"
                checked: steady.stab.lock === true
                onToggled: if (steady.stab.enabled) page.project.stabilize(page.clipInfo.id, { lock: checked })
            }
            Finding {
                objectName: "stabilizeState"
                visible: steady.stab.enabled === true
                text: "Stabilized, zoomed in " + Math.round(((steady.stab.zoom || 1) - 1) * 100) + " %."
            }
            RowLayout {
                TextButton {
                    objectName: "stabilize"
                    text: steady.stab.enabled ? "Stabilize again" : "Stabilize"
                    variant: "primary"
                    enabled: !page.project.analyzing
                    onClicked: page.project.stabilize(page.clipInfo.id, { smoothness: smoothness.value, lock: lockSwitch.checked })
                }
                TextButton {
                    objectName: "removeStabilization"
                    visible: steady.stab.enabled === true
                    text: "Remove"
                    variant: "ghost"
                    onClicked: page.project.removeStabilization(page.clipInfo.id)
                }
            }
            Setting {
                id: flickerStrength
                objectName: "deflickerStrength"
                label: "Flicker: even out changes up to"
                from: 0; to: 1; stepSize: 0.05
                value: steady.flicker.strength !== undefined ? steady.flicker.strength : 0.5
                format: (v) => (0.1 + 1.9 * v).toFixed(1) + " s"
                onCommitted: if (steady.flicker.enabled) page.project.removeFlicker(page.clipInfo.id, { strength: value })
            }
            RowLayout {
                TextButton {
                    objectName: "removeFlicker"
                    text: steady.flicker.enabled ? "Flicker removed" : "Remove flicker"
                    variant: steady.flicker.enabled ? "secondary" : "primary"
                    enabled: !page.project.analyzing && steady.flicker.enabled !== true
                    onClicked: page.project.removeFlicker(page.clipInfo.id, { strength: flickerStrength.value })
                }
                TextButton {
                    objectName: "keepFlicker"
                    visible: steady.flicker.enabled === true
                    text: "Undo it"
                    variant: "ghost"
                    onClicked: page.project.keepFlicker(page.clipInfo.id)
                }
            }
            Setting {
                objectName: "denoiseStrength"
                label: "Denoise over time"
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
            title: "Track motion"
            readonly property bool hasArea: page.project.trackArea.clipId === page.clipInfo.id

            Finding {
                text: "Follows something through the clip from the playhead, so titles and stickers can move with it."
                color: Theme.color.textSecondary
            }
            TextButton {
                objectName: "chooseTrackArea"
                text: page.project.pickingTrackArea ? "Drag a box on the player…" : tracking.hasArea ? "Draw again" : "Choose what to follow"
                variant: "secondary"
                onClicked: page.project.pickingTrackArea = !page.project.pickingTrackArea
            }
            RowLayout {
                visible: tracking.hasArea
                TextButton {
                    objectName: "trackObject"
                    text: "Track object"
                    variant: "primary"
                    enabled: !page.project.analyzing && page.steady
                    tip: "Something that moves and may change shape: a person, a car, a ball"
                    onClicked: page.project.trackMotion(page.clipInfo.id, { surface: false })
                }
                TextButton {
                    objectName: "trackSurface"
                    text: "Track surface"
                    variant: "secondary"
                    enabled: !page.project.analyzing && page.steady
                    tip: "A flat rectangle that also turns: a screen, a sign, a page"
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
                        text: "Track " + modelData.id + " · " + (modelData.surface ? "surface" : "object") + " · "
                              + modelData.start.toFixed(1) + "–" + modelData.end.toFixed(1) + " s"
                        font: Theme.type.body
                        color: Theme.color.textPrimary
                    }
                    TextButton {
                        objectName: "deleteTrack:" + modelData.id
                        text: "Delete"
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
            title: "Follow a track"

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
                text: "Grow and shrink with it"
            }
            ToggleSwitch {
                id: followTurn
                objectName: "followTurn"
                text: "Turn with it (surfaces)"
            }
            TextButton {
                objectName: "followTrack"
                text: "Follow"
                variant: "primary"
                tip: "Moves this clip with the track from where it is at the playhead, as keyframes"
                onClicked: {
                    const c = follow.choices[followChoice.currentIndex]
                    page.project.followTrack(page.clipInfo.id, c.clipId, c.trackId,
                                             { scale: followScale.checked, rotation: followTurn.checked })
                }
            }
        }

        // Pauses
        Section {
            id: pauses
            objectName: "pausesSection"
            visible: page.hasClip && page.plainSpeed && page.hasSound
            Layout.fillWidth: true
            title: "Pauses"
            readonly property var found: page.hasClip && page.project.silences.clipId === page.clipInfo.id ? page.project.silences : null

            Setting { id: quieter; objectName: "pauseThreshold"; label: "Quieter than (dB)"; from: -60; to: -20; stepSize: 1; value: -40; format: (v) => v.toFixed(0) }
            Setting { id: longer; objectName: "pauseLength"; label: "Longer than (s)"; from: 0.2; to: 3; stepSize: 0.1; value: 0.5 }
            Setting { id: keep; objectName: "pausePadding"; label: "Keep at each side (s)"; from: 0; to: 0.5; stepSize: 0.05; value: 0.1; format: (v) => v.toFixed(2) }
            TextButton {
                objectName: "findPauses"
                text: "Find pauses"
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
                    return n === 0 ? "No pauses that long." : n + (n === 1 ? " pause, " : " pauses, ") + f.total.toFixed(1) + " s in all (red on the timeline)."
                }
            }
            RowLayout {
                visible: pauses.found !== null && pauses.found.ranges.length > 0
                TextButton {
                    objectName: "removePauses"
                    text: "Remove them"
                    variant: "primary"
                    tip: "Cuts the pauses out and closes the gaps on this clip's track"
                    onClicked: page.project.removeSilences()
                }
                TextButton { text: "Clear"; variant: "ghost"; onClicked: page.project.clearAnalysis() }
            }
        }

        // Scene changes
        Section {
            id: scenes
            objectName: "scenesSection"
            visible: page.hasClip && page.plainSpeed && page.hasPictures
            Layout.fillWidth: true
            title: "Scene changes"
            readonly property var found: page.hasClip && page.project.scenes.clipId === page.clipInfo.id ? page.project.scenes : null

            Setting { id: sensitivity; objectName: "sceneSensitivity"; label: "Sensitivity"; from: 0; to: 1; stepSize: 0.05; value: 0.55; format: (v) => Math.round(v * 100) + " %" }
            TextButton {
                objectName: "findScenes"
                text: "Find scene changes"
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
                    return n === 0 ? "One continuous shot." : n + (n === 1 ? " scene change" : " scene changes") + " (green on the timeline)."
                }
            }
            RowLayout {
                visible: scenes.found !== null && scenes.found.cuts.length > 0
                TextButton { objectName: "splitAtScenes"; text: "Split there"; variant: "primary"; onClicked: page.project.splitAtScenes() }
                TextButton { objectName: "markScenes"; text: "Add markers"; variant: "secondary"; onClicked: page.project.markScenes() }
            }
        }

        // The beat
        Section {
            id: beat
            objectName: "beatSection"
            visible: page.hasClip && page.plainSpeed && page.hasSound
            Layout.fillWidth: true
            title: "Beat"
            readonly property var found: page.hasClip && page.project.beats.clipId === page.clipInfo.id ? page.project.beats : null

            TextButton {
                objectName: "findBeats"
                text: "Find the beat"
                enabled: !page.project.analyzing
                onClicked: page.project.findBeats(page.clipInfo.id)
            }
            Finding {
                objectName: "beatsFound"
                visible: beat.found !== null
                text: {
                    const f = beat.found
                    if (!f) return ""
                    return f.times.length === 0 ? "No steady beat." : Math.round(f.bpm) + " BPM · " + f.times.length + " beats (yellow ticks on the ruler)."
                }
            }
            RowLayout {
                visible: beat.found !== null && beat.found.times.length > 0
                TextButton { objectName: "markEveryBeat"; text: "Mark every beat"; variant: "secondary"; onClicked: page.project.addBeatMarkers(1) }
                TextButton { objectName: "markEveryBar"; text: "Every 4 beats"; variant: "secondary"; onClicked: page.project.addBeatMarkers(4) }
            }
        }

        // Speech enhancement
        Section {
            id: enhance
            objectName: "enhanceSection"
            visible: page.hasClip && page.hasSound && page.clipInfo.trackKind !== undefined
            Layout.fillWidth: true
            title: "Enhance speech"
            readonly property var model: page.project.enhanceModel
            readonly property var download: page.project.modelDownload
            readonly property bool downloading: download.id !== undefined && download.id === model.id
            readonly property var current: page.clipInfo.enhanced

            Finding {
                text: "Takes noise, hum and room echo out of voices with DeepFilterNet3, on this Mac. The clip then plays the cleaned sound."
                color: Theme.color.textSecondary
            }
            // The model, once
            RowLayout {
                visible: enhance.model.downloaded !== true
                Layout.fillWidth: true
                TextButton {
                    objectName: "downloadEnhanceModel"
                    visible: !enhance.downloading
                    text: "Download model (" + Math.round((enhance.model.size || 0) / 1e6) + " MB)"
                    variant: "secondary"
                    onClicked: page.project.downloadSpeechModel(enhance.model.id)
                }
                Text {
                    objectName: "enhanceModelProgress"
                    visible: enhance.downloading
                    Layout.fillWidth: true
                    text: "Downloading… " + Math.round((enhance.download.progress || 0) * 100) + " %"
                    font: Theme.type.body
                    color: Theme.color.textPrimary
                }
                TextButton {
                    visible: enhance.downloading
                    text: "Cancel"
                    variant: "ghost"
                    onClicked: page.project.cancelModelDownload()
                }
            }
            Setting {
                id: strength
                objectName: "enhanceStrength"
                label: "Lower noise by up to"
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
                    : enhance.current.missing ? "The cleaned sound is missing; enhance again to make it."
                    : enhance.current.limitDb >= 100 ? "Enhanced: the clip plays its cleaned sound."
                    : "Enhanced, noise lowered by up to " + enhance.current.limitDb.toFixed(0) + " dB."
            }
            RowLayout {
                TextButton {
                    objectName: "enhanceSpeech"
                    text: enhance.current && !enhance.current.missing ? "Apply strength" : "Enhance speech"
                    variant: "primary"
                    enabled: enhance.model.downloaded === true && !page.project.analyzing
                    onClicked: page.project.enhanceSpeech(page.clipInfo.id, { limitDb: strength.value >= 42 ? 100 : strength.value })
                }
                TextButton {
                    objectName: "removeEnhancement"
                    visible: enhance.current !== undefined
                    text: "Original sound"
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
            title: "Lower under speech"

            Finding {
                text: "Turns this clip (music) down while the other clips playing with it have someone talking, as volume keyframes."
                color: Theme.color.textSecondary
            }
            Setting { id: lower; objectName: "duckAmount"; label: "Lower by (dB)"; from: -24; to: -3; stepSize: 1; value: -12; format: (v) => v.toFixed(0) }
            Setting { id: duckFade; objectName: "duckFade"; label: "Fade (s)"; from: 0.1; to: 1; stepSize: 0.05; value: 0.25; format: (v) => v.toFixed(2) }
            TextButton {
                objectName: "duckUnderSpeech"
                text: "Lower under speech"
                enabled: !page.project.analyzing
                onClicked: page.project.duckUnderSpeech(page.clipInfo.id, { amountDb: lower.value, fade: duckFade.value })
            }
        }
    }
}
