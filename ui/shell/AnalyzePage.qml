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
        Layout.fillWidth: true
        spacing: 0
        RowLayout {
            Layout.fillWidth: true
            Text { text: setting.label; font: Theme.type.label; color: Theme.color.textSecondary; Layout.fillWidth: true }
            Text { text: setting.format(slider.value); font: Theme.type.mono; color: Theme.color.textPrimary }
        }
        Slider { id: slider; Layout.fillWidth: true }
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
            visible: !page.hasClip || !page.plainSpeed || (!page.hasSound && !page.hasPictures)
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
