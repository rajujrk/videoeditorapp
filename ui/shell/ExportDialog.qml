import QtCore
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import TechieVideoEditor.Theme

// Export: a preset or the settings one by one (where, format, size, frame rate, quality,
// sound, loudness, which part of the timeline), then Export (and watch it) or Add to queue
// (and keep editing). Exports run one after another; the render queue in the top bar
// lists them.
Popup {
    id: dialog
    required property var project

    // "settings" → "exporting" → "done" or "failed"
    property string stage: "settings"
    property string message
    property url file
    property real jobId: 0 // the export this dialog follows

    modal: true
    focus: true
    anchors.centerIn: Overlay.overlay
    width: 540
    padding: Theme.space.l
    closePolicy: Popup.CloseOnEscape | (stage === "exporting" ? 0 : Popup.CloseOnPressOutside)

    function baseName() { return (project.name || "Untitled").replace(/[\\/:*?"<>|]/g, "-") }
    function prepare() {
        stage = "settings"
        message = ""
        jobId = 0
        presetPicker.currentIndex = 0
        formatPicker.currentIndex = 0
        sizePicker.currentIndex = 0
        ratePicker.currentIndex = 0
        rangePicker.currentIndex = 0
        soundSwitch.checked = true
        quality = "standard"
        fromField.text = "0"
        toField.text = project.duration.toFixed(2)
        file = StandardPaths.writableLocation(StandardPaths.MoviesLocation) + "/" + baseName() + "." + format.extension
    }
    onAboutToShow: prepare()

    property string quality: "standard"
    readonly property var format: project.exportFormats[Math.max(0, formatPicker.currentIndex)]

    // The canvas, standard heights below it, two fixed shapes, then upscaled heights.
    readonly property var sizes: {
        const w = project.canvasWidth
        const h = project.canvasHeight
        const short = Math.min(w, h)
        const even = (v) => Math.max(2, Math.round(v / 2) * 2)
        const at = (side, label) => ({ label: label + " · " + even(w * side / short) + " × " + even(h * side / short),
                                        width: even(w * side / short), height: even(h * side / short), fill: false })
        const list = [{ label: "Project · " + w + " × " + h, width: w, height: h, fill: false }]
        const heights = [[2160, "4K"], [1440, "1440p"], [1080, "1080p"], [720, "720p"], [540, "540p"], [480, "480p"], [360, "360p"]]
        for (const target of heights)
            if (target[0] < short)
                list.push(at(target[0], target[1]))
        list.push({ label: "Vertical 9:16 · 1080 × 1920", width: 1080, height: 1920, fill: true })
        list.push({ label: "Square 1:1 · 1080 × 1080", width: 1080, height: 1080, fill: true })
        for (const target of heights)
            if (target[0] > short)
                list.push(at(target[0], target[1] + " (upscaled)"))
        return list
    }
    readonly property var size: sizes[Math.max(0, sizePicker.currentIndex)]
    readonly property var rates: [{ label: "Project · " + Math.round(project.frameRate) + " fps", fps: 0 },
                                  { label: "24 fps", fps: 24 }, { label: "25 fps", fps: 25 }, { label: "30 fps", fps: 30 },
                                  { label: "50 fps", fps: 50 }, { label: "60 fps", fps: 60 }, { label: "15 fps (GIF)", fps: 15 }]
    // Whole timeline, the selection, each marker with a length, all of them as files, custom.
    readonly property var ranges: {
        const list = [{ label: "Whole timeline", range: "all" }]
        if (project.selectedClips.length > 0)
            list.push({ label: "Selected clips", range: "selection" })
        const marked = project.markers.filter((m) => m.duration > 0)
        for (const m of marked)
            list.push({ label: "Marker · " + (m.note || "range") + " (" + project.timecode(m.time).substring(3, 8) + "–"
                               + project.timecode(m.time + m.duration).substring(3, 8) + ")",
                        range: "marker", markerId: m.id })
        if (marked.length > 1)
            list.push({ label: "Each marker range · " + marked.length + " files", range: "each", count: marked.length })
        list.push({ label: "Custom…", range: "custom" })
        return list
    }
    readonly property var range: ranges[Math.max(0, Math.min(rangePicker.currentIndex, ranges.length - 1))]

    readonly property var options: ({
        format: format.id,
        width: size.width,
        height: size.height,
        fill: size.fill,
        frameRate: rates[Math.max(0, ratePicker.currentIndex)].fps,
        quality: quality,
        audio: soundSwitch.checked,
        normalizeLoudness: loudnessSwitch.checked,
        loudnessTarget: loudnessPicker.currentValue,
        range: range.range === "each" ? "all" : range.range,
        markerId: range.markerId || 0,
        from: Number(fromField.text),
        to: Number(toField.text)
    })
    readonly property real rangeSeconds: {
        if (range.range === "each")
            return project.markers.filter((m) => m.duration > 0).reduce((sum, m) => sum + m.duration, 0)
        return project.exportRange(options).duration || 0
    }
    readonly property real estimatedMegabytes: project.exportBitrate(options) * rangeSeconds / 8 / 1e6
    readonly property var job: project.exportJobs.find((j) => j.id === jobId)

    function fileName(url) {
        const s = String(url)
        return decodeURIComponent(s.substring(s.lastIndexOf("/") + 1))
    }
    function withExtension(url, extension) {
        const s = String(url)
        const slash = s.lastIndexOf("/")
        const dot = s.lastIndexOf(".")
        return (dot > slash ? s.substring(0, dot) : s) + "." + extension
    }
    function indexOfFormat(id) { return project.exportFormats.findIndex((f) => f.id === id) }
    // A new format: the file's extension follows; a GIF starts small and at 15 fps.
    function formatChosen() {
        file = withExtension(file, format.extension)
        if (format.id === "gif") {
            const small = sizes.findIndex((s) => Math.min(s.width, s.height) === 360)
            if (small >= 0)
                sizePicker.currentIndex = small
            ratePicker.currentIndex = rates.length - 1
        }
    }
    function applyPreset(preset) {
        if (!preset.format)
            return
        formatPicker.currentIndex = Math.max(0, indexOfFormat(preset.format))
        file = withExtension(file, format.extension)
        let sizeIndex = 0
        if (preset.width && preset.height)
            sizeIndex = sizes.findIndex((s) => s.width === preset.width && s.height === preset.height)
        else if (preset.shortSide)
            sizeIndex = sizes.findIndex((s) => Math.min(s.width, s.height) === preset.shortSide)
        sizePicker.currentIndex = Math.max(0, sizeIndex) // the canvas when it is already that size
        ratePicker.currentIndex = Math.max(0, rates.findIndex((r) => r.fps === (preset.frameRate || 0)))
        quality = preset.quality || "standard"
    }
    function start(watch) {
        let ok
        if (range.range === "each") {
            const s = String(file)
            ok = project.exportMarkerRanges(s.substring(0, s.lastIndexOf("/")), options) > 0
            watch = false
        } else {
            ok = project.exportVideo(file, options)
        }
        if (!ok) {
            stage = "failed"
            message = project.lastError
            return
        }
        if (!watch) {
            close()
            return
        }
        jobId = project.exportJobs[project.exportJobs.length - 1].id
        stage = "exporting"
    }

    Connections {
        target: dialog.project
        function onExportFinished(ok, message, jobId) {
            if (dialog.stage !== "exporting" || jobId !== dialog.jobId)
                return
            dialog.stage = ok ? "done" : message === "" ? "settings" : "failed"
            dialog.message = message
        }
    }

    FileDialog {
        id: fileDialog
        title: "Export"
        fileMode: FileDialog.SaveFile
        defaultSuffix: dialog.format.extension
        nameFilters: [dialog.format.label + " (*." + dialog.format.extension + ")"]
        currentFile: dialog.file
        onAccepted: dialog.file = selectedFile
    }

    Overlay.modal: Rectangle { color: Qt.alpha("#000000", Theme.dark ? 0.55 : 0.35) }

    background: Rectangle {
        radius: Theme.radius.l
        color: Theme.color.surfaceRaised
        border.color: Theme.color.borderStrong
    }

    contentItem: ColumnLayout {
        spacing: Theme.space.m

        Text {
            text: dialog.stage === "done" ? "Export finished"
                : dialog.stage === "failed" ? "Export failed"
                : dialog.stage === "exporting" ? (dialog.job && dialog.job.state === "waiting" ? "Waiting in the queue…" : "Exporting…")
                : "Export"
            font: Theme.type.title
            color: Theme.color.textPrimary
        }

        // Settings
        GridLayout {
            visible: dialog.stage === "settings"
            Layout.fillWidth: true
            columns: 2
            columnSpacing: Theme.space.m
            rowSpacing: Theme.space.s

            Text { text: "Preset"; font: Theme.type.label; color: Theme.color.textSecondary }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                ComboBox {
                    id: presetPicker
                    objectName: "exportPreset"
                    Layout.fillWidth: true
                    model: [{ id: "", title: "Custom", description: "Choose each setting below" }].concat(dialog.project.exportPresets)
                    textRole: "title"
                    onActivated: dialog.applyPreset(model[currentIndex])
                }
                Text {
                    objectName: "exportPresetDescription"
                    Layout.fillWidth: true
                    text: presetPicker.model[Math.max(0, presetPicker.currentIndex)].description
                    font: Theme.type.caption
                    color: Theme.color.textSecondary
                    wrapMode: Text.WordWrap
                }
            }

            Text { text: "Save as"; font: Theme.type.label; color: Theme.color.textSecondary }
            RowLayout {
                Layout.fillWidth: true
                Text {
                    objectName: "exportFileName"
                    Layout.fillWidth: true
                    text: dialog.range.range === "each" ? "One file per marker, in that folder" : dialog.fileName(dialog.file)
                    font: Theme.type.body
                    color: Theme.color.textPrimary
                    elide: Text.ElideMiddle
                }
                TextButton { text: "Choose…"; variant: "secondary"; onClicked: fileDialog.open() }
            }

            Text { text: "Format"; font: Theme.type.label; color: Theme.color.textSecondary }
            ComboBox {
                id: formatPicker
                objectName: "exportFormat"
                Layout.fillWidth: true
                model: dialog.project.exportFormats
                textRole: "label"
                onActivated: {
                    presetPicker.currentIndex = 0
                    dialog.formatChosen()
                }
            }

            Text { visible: dialog.format.video; text: "Size"; font: Theme.type.label; color: Theme.color.textSecondary }
            ComboBox {
                id: sizePicker
                objectName: "exportSize"
                visible: dialog.format.video
                Layout.fillWidth: true
                model: dialog.sizes
                textRole: "label"
                onActivated: presetPicker.currentIndex = 0
            }

            Text { visible: dialog.format.video; text: "Frame rate"; font: Theme.type.label; color: Theme.color.textSecondary }
            ComboBox {
                id: ratePicker
                objectName: "exportFrameRate"
                visible: dialog.format.video
                Layout.fillWidth: true
                model: dialog.rates
                textRole: "label"
                onActivated: presetPicker.currentIndex = 0
            }

            Text { visible: dialog.format.quality; text: "Quality"; font: Theme.type.label; color: Theme.color.textSecondary }
            RowLayout {
                visible: dialog.format.quality
                spacing: Theme.space.xs
                Repeater {
                    model: [{ id: "high", label: "High" }, { id: "standard", label: "Standard" }, { id: "small", label: "Small" }]
                    delegate: TextButton {
                        required property var modelData
                        objectName: "exportQuality:" + modelData.id
                        text: modelData.label
                        variant: dialog.quality === modelData.id ? "primary" : "secondary"
                        onClicked: {
                            dialog.quality = modelData.id
                            presetPicker.currentIndex = 0
                        }
                    }
                }
            }

            Text { visible: dialog.format.sound && dialog.format.video; text: "Sound"; font: Theme.type.label; color: Theme.color.textSecondary }
            RowLayout {
                visible: dialog.format.sound && dialog.format.video
                Layout.fillWidth: true
                ToggleSwitch {
                    id: soundSwitch
                    objectName: "exportSound"
                    checked: true
                }
                Text {
                    Layout.fillWidth: true
                    text: soundSwitch.checked ? "Include the mix as the sound track" : "No sound track"
                    font: Theme.type.body
                    color: Theme.color.textPrimary
                }
            }

            Text { visible: dialog.format.sound; text: "Loudness"; font: Theme.type.label; color: Theme.color.textSecondary }
            RowLayout {
                visible: dialog.format.sound
                Layout.fillWidth: true
                ToggleSwitch {
                    id: loudnessSwitch
                    objectName: "exportNormalize"
                    enabled: soundSwitch.checked || !dialog.format.video
                    ToolTip.visible: hovered
                    ToolTip.text: "Measure the mix and set its volume to the target (peaks limited to −1 dBFS)"
                }
                ComboBox {
                    id: loudnessPicker
                    objectName: "exportLoudnessTarget"
                    Layout.fillWidth: true
                    enabled: loudnessSwitch.checked && loudnessSwitch.enabled
                    textRole: "label"
                    valueRole: "lufs"
                    model: [{ label: "−14 LUFS (online)", lufs: -14 }, { label: "−16 LUFS (podcast)", lufs: -16 },
                            { label: "−23 LUFS (EBU R128)", lufs: -23 }]
                }
            }

            Text { text: "Range"; font: Theme.type.label; color: Theme.color.textSecondary }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: Theme.space.xs
                ComboBox {
                    id: rangePicker
                    objectName: "exportRange"
                    Layout.fillWidth: true
                    model: dialog.ranges
                    textRole: "label"
                }
                RowLayout {
                    visible: dialog.range.range === "custom"
                    spacing: Theme.space.s
                    Text { text: "From"; font: Theme.type.body; color: Theme.color.textPrimary }
                    TextField {
                        id: fromField
                        objectName: "exportFrom"
                        implicitWidth: 80
                        validator: DoubleValidator { bottom: 0 }
                    }
                    Text { text: "to"; font: Theme.type.body; color: Theme.color.textPrimary }
                    TextField {
                        id: toField
                        objectName: "exportTo"
                        implicitWidth: 80
                        validator: DoubleValidator { bottom: 0 }
                    }
                    Text { text: "seconds"; font: Theme.type.body; color: Theme.color.textSecondary }
                }
            }

            Text { text: "Estimate"; font: Theme.type.label; color: Theme.color.textSecondary }
            Text {
                objectName: "exportEstimate"
                text: dialog.rangeSeconds <= 0 ? "Nothing to export in that range"
                    : dialog.project.timecode(dialog.rangeSeconds).substring(0, 8) + " · about "
                      + (dialog.estimatedMegabytes < 10 ? dialog.estimatedMegabytes.toFixed(1)
                                                        : Math.round(dialog.estimatedMegabytes)) + " MB"
                font: Theme.type.body
                color: Theme.color.textPrimary
            }
        }

        // Progress of this dialog's export
        ColumnLayout {
            visible: dialog.stage === "exporting"
            Layout.fillWidth: true
            spacing: Theme.space.s
            Text {
                text: dialog.fileName(dialog.file)
                font: Theme.type.body
                color: Theme.color.textSecondary
                elide: Text.ElideMiddle
                Layout.fillWidth: true
            }
            ProgressBar {
                id: progressBar
                objectName: "exportProgressBar"
                Layout.fillWidth: true
                value: dialog.job ? dialog.job.progress : 0
                background: Rectangle {
                    implicitHeight: 6
                    radius: 3
                    color: Theme.color.borderSubtle
                }
                contentItem: Item {
                    implicitHeight: 6
                    Rectangle {
                        width: parent.width * progressBar.visualPosition
                        height: parent.height
                        radius: 3
                        color: Theme.color.accent
                    }
                }
            }
            Text {
                text: !dialog.job ? ""
                    : dialog.job.state === "waiting" ? "Starts when the exports before it are done"
                    : (dialog.project.exportPaused ? "Paused · " : "") + Math.round(dialog.job.progress * 100) + " %"
                font: Theme.type.mono
                color: Theme.color.textPrimary
            }
        }

        // Result
        Text {
            visible: dialog.stage === "done" || dialog.stage === "failed"
            objectName: "exportResult"
            Layout.fillWidth: true
            text: dialog.stage === "done" ? "Saved " + dialog.fileName(dialog.file) + "." : dialog.message
            font: Theme.type.body
            color: dialog.stage === "failed" ? Theme.color.danger : Theme.color.textPrimary
            wrapMode: Text.WordWrap
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: Theme.space.s
            spacing: Theme.space.s
            TextButton {
                objectName: "exportKeepEditing"
                visible: dialog.stage === "exporting"
                text: "Keep editing"
                variant: "ghost"
                tip: "Close this; the export goes on (the render queue in the top bar shows it)"
                onClicked: dialog.close()
            }
            Item { Layout.fillWidth: true }
            TextButton {
                visible: dialog.stage === "done"
                text: "Show in Finder"
                variant: "secondary"
                onClicked: {
                    const s = String(dialog.file)
                    Qt.openUrlExternally(s.substring(0, s.lastIndexOf("/")))
                }
            }
            TextButton {
                objectName: "exportCancelButton"
                text: dialog.stage === "exporting" ? "Cancel export" : dialog.stage === "settings" ? "Cancel" : "Done"
                variant: "secondary"
                onClicked: dialog.stage === "exporting" ? dialog.project.cancelExport(dialog.jobId) : dialog.close()
            }
            TextButton {
                objectName: "exportQueueButton"
                visible: dialog.stage === "settings"
                enabled: dialog.rangeSeconds > 0
                text: dialog.range.range === "each" ? "Queue " + dialog.range.count + " files" : "Add to queue"
                iconName: "queue"
                variant: "secondary"
                tip: "Export after the exports already queued, while you keep editing"
                onClicked: dialog.start(false)
            }
            TextButton {
                objectName: "exportStartButton"
                visible: dialog.stage === "settings" && dialog.range.range !== "each"
                enabled: dialog.rangeSeconds > 0
                text: "Export"
                iconName: "export"
                variant: "primary"
                onClicked: dialog.start(true)
            }
        }
    }
}
