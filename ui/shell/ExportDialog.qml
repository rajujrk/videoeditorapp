import QtCore
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import TechieVideoEditor.Theme

// Export to MP4: where, size, format and quality; then progress, and the result.
Popup {
    id: dialog
    required property var project

    // "settings" → "exporting" → "done" or "failed"
    property string stage: "settings"
    property string message
    property url file

    modal: true
    focus: true
    anchors.centerIn: Overlay.overlay
    width: 460
    padding: Theme.space.l
    closePolicy: stage === "exporting" ? Popup.NoAutoClose : Popup.CloseOnEscape | Popup.CloseOnPressOutside

    function prepare() {
        stage = "settings"
        message = ""
        const name = (project.name || "Untitled").replace(/[\\/:*?"<>|]/g, "-")
        file = StandardPaths.writableLocation(StandardPaths.MoviesLocation) + "/" + name + ".mp4"
        sizePicker.currentIndex = 0
        codecPicker.currentIndex = 0
        quality = "standard"
    }
    onAboutToShow: prepare()

    property string quality: "standard"
    // Project size, then standard heights smaller than it, keeping the canvas's shape.
    readonly property var sizes: {
        const w = project.canvasWidth
        const h = project.canvasHeight
        const even = (v) => Math.max(2, Math.round(v / 2) * 2)
        const list = [{ label: "Project · " + w + " × " + h, width: w, height: h }]
        for (const target of [[2160, "4K"], [1440, "1440p"], [1080, "1080p"], [720, "720p"], [480, "480p"]]) {
            const short = Math.min(w, h)
            if (target[0] >= short)
                continue
            const scale = target[0] / short
            list.push({ label: target[1] + " · " + even(w * scale) + " × " + even(h * scale),
                        width: even(w * scale), height: even(h * scale) })
        }
        return list
    }
    readonly property var options: ({
        width: sizes[Math.max(0, sizePicker.currentIndex)].width,
        height: sizes[Math.max(0, sizePicker.currentIndex)].height,
        codec: codecPicker.currentIndex === 1 ? "hevc" : "h264",
        quality: quality,
        normalizeLoudness: loudnessSwitch.checked,
        loudnessTarget: loudnessPicker.currentValue
    })
    readonly property real estimatedMegabytes: project.exportBitrate(options) * project.duration / 8 / 1e6

    function fileName(url) {
        const s = String(url)
        return decodeURIComponent(s.substring(s.lastIndexOf("/") + 1))
    }

    Connections {
        target: dialog.project
        function onExportFinished(ok, message) {
            if (dialog.stage !== "exporting")
                return
            dialog.stage = ok ? "done" : message === "" ? "settings" : "failed"
            dialog.message = message
        }
    }

    FileDialog {
        id: fileDialog
        title: "Export video"
        fileMode: FileDialog.SaveFile
        defaultSuffix: "mp4"
        nameFilters: ["MP4 video (*.mp4)"]
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
                : dialog.stage === "exporting" ? "Exporting…" : "Export video"
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

            Text { text: "Save as"; font: Theme.type.label; color: Theme.color.textSecondary }
            RowLayout {
                Layout.fillWidth: true
                Text {
                    objectName: "exportFileName"
                    Layout.fillWidth: true
                    text: dialog.fileName(dialog.file)
                    font: Theme.type.body
                    color: Theme.color.textPrimary
                    elide: Text.ElideMiddle
                }
                TextButton { text: "Choose…"; variant: "secondary"; onClicked: fileDialog.open() }
            }

            Text { text: "Size"; font: Theme.type.label; color: Theme.color.textSecondary }
            ComboBox {
                id: sizePicker
                objectName: "exportSize"
                Layout.fillWidth: true
                model: dialog.sizes
                textRole: "label"
            }

            Text { text: "Format"; font: Theme.type.label; color: Theme.color.textSecondary }
            ComboBox {
                id: codecPicker
                objectName: "exportCodec"
                Layout.fillWidth: true
                model: ["MP4 · H.264 (plays everywhere)", "MP4 · HEVC (smaller files)"]
            }

            Text { text: "Quality"; font: Theme.type.label; color: Theme.color.textSecondary }
            RowLayout {
                spacing: Theme.space.xs
                Repeater {
                    model: [{ id: "high", label: "High" }, { id: "standard", label: "Standard" }, { id: "small", label: "Small" }]
                    delegate: TextButton {
                        required property var modelData
                        objectName: "exportQuality:" + modelData.id
                        text: modelData.label
                        variant: dialog.quality === modelData.id ? "primary" : "secondary"
                        onClicked: dialog.quality = modelData.id
                    }
                }
            }

            Text { text: "Loudness"; font: Theme.type.label; color: Theme.color.textSecondary }
            RowLayout {
                Layout.fillWidth: true
                ToggleSwitch {
                    id: loudnessSwitch
                    objectName: "exportNormalize"
                    ToolTip.visible: hovered
                    ToolTip.text: "Measure the whole mix and set its volume to the target (peaks limited to −1 dBFS)"
                }
                ComboBox {
                    id: loudnessPicker
                    objectName: "exportLoudnessTarget"
                    Layout.fillWidth: true
                    enabled: loudnessSwitch.checked
                    textRole: "label"
                    valueRole: "lufs"
                    model: [{ label: "−14 LUFS (online)", lufs: -14 }, { label: "−16 LUFS (podcast)", lufs: -16 },
                            { label: "−23 LUFS (EBU R128)", lufs: -23 }]
                }
            }

            Text { text: "Estimate"; font: Theme.type.label; color: Theme.color.textSecondary }
            Text {
                objectName: "exportEstimate"
                text: dialog.project.timecode(dialog.project.duration).substring(0, 8) + " · about "
                      + (dialog.estimatedMegabytes < 10 ? dialog.estimatedMegabytes.toFixed(1)
                                                        : Math.round(dialog.estimatedMegabytes)) + " MB"
                font: Theme.type.body
                color: Theme.color.textPrimary
            }
        }

        // Progress
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
                objectName: "exportProgressBar"
                Layout.fillWidth: true
                value: dialog.project.exportProgress
            }
            Text {
                text: Math.round(dialog.project.exportProgress * 100) + " %"
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
                onClicked: dialog.stage === "exporting" ? dialog.project.cancelExport() : dialog.close()
            }
            TextButton {
                objectName: "exportStartButton"
                visible: dialog.stage === "settings"
                text: "Export"
                iconName: "export"
                variant: "primary"
                onClicked: {
                    if (dialog.project.exportVideo(dialog.file, dialog.options))
                        dialog.stage = "exporting"
                    else {
                        dialog.stage = "failed"
                        dialog.message = dialog.project.lastError
                    }
                }
            }
        }
    }
}
