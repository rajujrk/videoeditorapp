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

    function baseName() { return (project.name || qsTr("Untitled")).replace(/[\\/:*?"<>|]/g, "-") }
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
    readonly property bool hdrProject: project.projectColour !== "sdr" && format.video
    readonly property bool hevc: format.id.indexOf("hevc") >= 0
    readonly property bool sphereProject: project.spherical && format.video

    // The canvas, standard heights below it, two fixed shapes, then upscaled heights.
    readonly property var sizes: {
        const w = project.canvasWidth
        const h = project.canvasHeight
        const short = Math.min(w, h)
        const even = (v) => Math.max(2, Math.round(v / 2) * 2)
        const at = (side, label) => ({ label: qsTr("%1 · %2 × %3").arg(label).arg(even(w * side / short)).arg(even(h * side / short)),
                                        width: even(w * side / short), height: even(h * side / short), fill: false })
        const list = [{ label: qsTr("Project · %1 × %2").arg(w).arg(h), width: w, height: h, fill: false }]
        const heights = [[2160, "4K"], [1440, "1440p"], [1080, "1080p"], [720, "720p"], [540, "540p"], [480, "480p"], [360, "360p"]]
        for (const target of heights)
            if (target[0] < short)
                list.push(at(target[0], target[1]))
        list.push({ label: qsTr("Vertical 9:16 · 1080 × 1920"), width: 1080, height: 1920, fill: true })
        list.push({ label: qsTr("Square 1:1 · 1080 × 1080"), width: 1080, height: 1080, fill: true })
        for (const target of heights)
            if (target[0] > short)
                list.push(at(target[0], qsTr("%1 (upscaled)").arg(target[1])))
        return list
    }
    readonly property var size: sizes[Math.max(0, sizePicker.currentIndex)]
    readonly property var rates: [{ label: qsTr("Project · %1 fps").arg(Fmt.num(project.frameRate)), fps: 0 },
                                  { label: qsTr("24 fps"), fps: 24 }, { label: qsTr("25 fps"), fps: 25 }, { label: qsTr("30 fps"), fps: 30 },
                                  { label: qsTr("50 fps"), fps: 50 }, { label: qsTr("60 fps"), fps: 60 }, { label: qsTr("15 fps (GIF)"), fps: 15 }]
    // Whole timeline, the selection, each marker with a length, all of them as files, custom.
    readonly property var ranges: {
        const list = [{ label: qsTr("Whole timeline"), range: "all" }]
        if (project.selectedClips.length > 0)
            list.push({ label: qsTr("Selected clips"), range: "selection" })
        const marked = project.markers.filter((m) => m.duration > 0)
        for (const m of marked)
            list.push({ label: qsTr("Marker · %1 (%2–%3)").arg(m.note || qsTr("range")).arg(project.timecode(m.time).substring(3, 8))
                                .arg(project.timecode(m.time + m.duration).substring(3, 8)),
                        range: "marker", markerId: m.id })
        if (marked.length > 1)
            list.push({ label: qsTr("Each marker range · %1 files").arg(Fmt.num(marked.length)), range: "each", count: marked.length })
        list.push({ label: qsTr("Custom…"), range: "custom" })
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
        hdr: hdrSwitch.checked,
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
        title: qsTr("Export")
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
            text: dialog.stage === "done" ? qsTr("Export finished")
                : dialog.stage === "failed" ? qsTr("Export failed")
                : dialog.stage === "exporting" ? (dialog.job && dialog.job.state === "waiting" ? qsTr("Waiting in the queue…") : qsTr("Exporting…"))
                : qsTr("Export")
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

            Text { text: qsTr("Preset"); font: Theme.type.label; color: Theme.color.textSecondary }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                ComboBox {
                    id: presetPicker
                    objectName: "exportPreset"
                    Layout.fillWidth: true
                    model: [{ id: "", title: qsTr("Custom"), description: qsTr("Choose each setting below") }].concat(dialog.project.exportPresets)
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

            Text { text: qsTr("Save as"); font: Theme.type.label; color: Theme.color.textSecondary }
            RowLayout {
                Layout.fillWidth: true
                Text {
                    objectName: "exportFileName"
                    Layout.fillWidth: true
                    text: dialog.range.range === "each" ? qsTr("One file per marker, in that folder") : dialog.fileName(dialog.file)
                    font: Theme.type.body
                    color: Theme.color.textPrimary
                    elide: Text.ElideMiddle
                }
                TextButton { text: qsTr("Choose…"); variant: "secondary"; onClicked: fileDialog.open() }
            }

            Text { text: qsTr("Format"); font: Theme.type.label; color: Theme.color.textSecondary }
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

            Text { visible: dialog.format.video; text: qsTr("Size"); font: Theme.type.label; color: Theme.color.textSecondary }
            ComboBox {
                id: sizePicker
                objectName: "exportSize"
                visible: dialog.format.video
                Layout.fillWidth: true
                model: dialog.sizes
                textRole: "label"
                onActivated: presetPicker.currentIndex = 0
            }

            Text { visible: dialog.format.video; text: qsTr("Frame rate"); font: Theme.type.label; color: Theme.color.textSecondary }
            ComboBox {
                id: ratePicker
                objectName: "exportFrameRate"
                visible: dialog.format.video
                Layout.fillWidth: true
                model: dialog.rates
                textRole: "label"
                onActivated: presetPicker.currentIndex = 0
            }

            Text { visible: dialog.format.quality; text: qsTr("Quality"); font: Theme.type.label; color: Theme.color.textSecondary }
            RowLayout {
                visible: dialog.format.quality
                spacing: Theme.space.xs
                Repeater {
                    model: [{ id: "high", label: qsTr("High") }, { id: "standard", label: qsTr("Standard") }, { id: "small", label: qsTr("Small") }]
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

            // An HDR project: HEVC keeps it HDR; everything else is tone-mapped to SDR.
            Text { visible: dialog.hdrProject; text: qsTr("Colour"); font: Theme.type.label; color: Theme.color.textSecondary }
            RowLayout {
                visible: dialog.hdrProject
                Layout.fillWidth: true
                ToggleSwitch {
                    id: hdrSwitch
                    objectName: "exportHdr"
                    visible: dialog.hevc
                    checked: true
                }
                Text {
                    objectName: "exportColourNote"
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: !dialog.hevc
                          ? qsTr("SDR Rec.709: the HDR picture is tone-mapped (HDR goes out as HEVC)")
                          : hdrSwitch.checked
                          ? (dialog.project.projectColour === "hdr-pq" ? qsTr("HDR: 10-bit HEVC, Rec.2020 PQ, with its mastering display")
                                                                         : qsTr("HDR: 10-bit HEVC, Rec.2020 HLG, with its mastering display"))
                          : qsTr("SDR Rec.709: the HDR picture is tone-mapped")
                    font: Theme.type.body
                    color: Theme.color.textPrimary
                }
            }

            Text { visible: dialog.sphereProject; text: "360°"; font: Theme.type.label; color: Theme.color.textSecondary }
            Text {
                objectName: "exportSphereNote"
                visible: dialog.sphereProject
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: dialog.format.extension === "mp4"
                      ? qsTr("360° video: equirectangular, marked so (spherical metadata) for 360° players and sites")
                      : qsTr("The equirectangular picture, not marked as 360° (only MP4 is)")
                font: Theme.type.body
                color: Theme.color.textPrimary
            }

            Text { visible: dialog.format.sound && dialog.format.video; text: qsTr("Sound"); font: Theme.type.label; color: Theme.color.textSecondary }
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
                    text: soundSwitch.checked ? qsTr("Include the mix as the sound track") : qsTr("No sound track")
                    font: Theme.type.body
                    color: Theme.color.textPrimary
                }
            }

            Text { visible: dialog.format.sound; text: qsTr("Loudness"); font: Theme.type.label; color: Theme.color.textSecondary }
            RowLayout {
                visible: dialog.format.sound
                Layout.fillWidth: true
                ToggleSwitch {
                    id: loudnessSwitch
                    objectName: "exportNormalize"
                    enabled: soundSwitch.checked || !dialog.format.video
                    ToolTip.visible: hovered
                    ToolTip.text: qsTr("Measure the mix and set its volume to the target (peaks limited to −1 dBFS)")
                }
                ComboBox {
                    id: loudnessPicker
                    objectName: "exportLoudnessTarget"
                    Layout.fillWidth: true
                    enabled: loudnessSwitch.checked && loudnessSwitch.enabled
                    textRole: "label"
                    valueRole: "lufs"
                    model: [{ label: qsTr("−14 LUFS (online)"), lufs: -14 }, { label: qsTr("−16 LUFS (podcast)"), lufs: -16 },
                            { label: qsTr("−23 LUFS (EBU R128)"), lufs: -23 }]
                }
            }

            Text { text: qsTr("Range"); font: Theme.type.label; color: Theme.color.textSecondary }
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
                    Text { text: qsTr("From"); font: Theme.type.body; color: Theme.color.textPrimary }
                    TextField {
                        id: fromField
                        objectName: "exportFrom"
                        implicitWidth: 80
                        validator: DoubleValidator { bottom: 0 }
                    }
                    Text { text: qsTr("to"); font: Theme.type.body; color: Theme.color.textPrimary }
                    TextField {
                        id: toField
                        objectName: "exportTo"
                        implicitWidth: 80
                        validator: DoubleValidator { bottom: 0 }
                    }
                    Text { text: qsTr("seconds"); font: Theme.type.body; color: Theme.color.textSecondary }
                }
            }

            Text { text: qsTr("Estimate"); font: Theme.type.label; color: Theme.color.textSecondary }
            Text {
                objectName: "exportEstimate"
                text: dialog.rangeSeconds <= 0 ? qsTr("Nothing to export in that range")
                    : qsTr("%1 · about %2 MB").arg(dialog.project.timecode(dialog.rangeSeconds).substring(0, 8))
                      .arg(Fmt.num(dialog.estimatedMegabytes, dialog.estimatedMegabytes < 10 ? 1 : 0))
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
                    : dialog.job.state === "waiting" ? qsTr("Starts when the exports before it are done")
                    : (dialog.project.exportPaused ? qsTr("Paused · ") : "") + Math.round(dialog.job.progress * 100) + " %"
                font: Theme.type.mono
                color: Theme.color.textPrimary
            }
        }

        // Result
        Text {
            visible: dialog.stage === "done" || dialog.stage === "failed"
            objectName: "exportResult"
            Layout.fillWidth: true
            text: dialog.stage === "done" ? qsTr("Saved %1.").arg(dialog.fileName(dialog.file)) : dialog.message
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
                text: qsTr("Keep editing")
                variant: "ghost"
                tip: qsTr("Close this; the export goes on (the render queue in the top bar shows it)")
                onClicked: dialog.close()
            }
            Item { Layout.fillWidth: true }
            TextButton {
                visible: dialog.stage === "done"
                text: qsTr("Show in Finder")
                variant: "secondary"
                onClicked: {
                    const s = String(dialog.file)
                    Qt.openUrlExternally(s.substring(0, s.lastIndexOf("/")))
                }
            }
            TextButton {
                objectName: "exportCancelButton"
                text: dialog.stage === "exporting" ? qsTr("Cancel export") : dialog.stage === "settings" ? qsTr("Cancel") : qsTr("Done")
                variant: "secondary"
                onClicked: dialog.stage === "exporting" ? dialog.project.cancelExport(dialog.jobId) : dialog.close()
            }
            TextButton {
                objectName: "exportQueueButton"
                visible: dialog.stage === "settings"
                enabled: dialog.rangeSeconds > 0
                text: dialog.range.range === "each" ? qsTr("Queue %1 files").arg(Fmt.num(dialog.range.count)) : qsTr("Add to queue")
                iconName: "queue"
                variant: "secondary"
                tip: qsTr("Export after the exports already queued, while you keep editing")
                onClicked: dialog.start(false)
            }
            TextButton {
                objectName: "exportStartButton"
                visible: dialog.stage === "settings" && dialog.range.range !== "each"
                enabled: dialog.rangeSeconds > 0
                text: qsTr("Export")
                iconName: "export"
                variant: "primary"
                onClicked: dialog.start(true)
            }
        }
    }
}
