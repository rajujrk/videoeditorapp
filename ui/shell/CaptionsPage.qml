import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import TechieVideoEditor.Theme

// The library's Captions tab: speech to text on this Mac (Whisper models, downloaded once),
// captions on the caption track, and subtitle files in and out.
ScrollView {
    id: page
    required property var project

    property string model: {
        const downloaded = project.speechModels.find((m) => m.downloaded)
        return downloaded ? downloaded.id : "base.en-q5_1"
    }
    readonly property var modelInfo: project.speechModels.find((m) => m.id === model) || ({})
    readonly property var download: project.modelDownload
    readonly property bool fromSelection: project.selectedClips.length > 0

    contentWidth: availableWidth
    clip: true

    function megabytes(bytes) { return Math.round(bytes / 1e6) + " MB" }

    FileDialog {
        id: importDialog
        title: "Import subtitles"
        fileMode: FileDialog.OpenFile
        nameFilters: ["Subtitles (*.srt *.vtt)"]
        onAccepted: page.project.importSubtitles(selectedFile)
    }
    FileDialog {
        id: exportDialog
        property string extension: "srt"
        title: "Export subtitles"
        fileMode: FileDialog.SaveFile
        defaultSuffix: extension
        nameFilters: [extension === "srt" ? "SubRip (*.srt)" : "WebVTT (*.vtt)"]
        onAccepted: page.project.exportSubtitles(selectedFile)
    }

    ColumnLayout {
        width: parent.width - Theme.space.m * 2
        x: Theme.space.m
        spacing: Theme.space.m

        Text {
            Layout.fillWidth: true
            text: "Captions from the speech in your clips, made on this Mac: nothing is uploaded."
            font: Theme.type.body
            color: Theme.color.textSecondary
            wrapMode: Text.WordWrap
        }

        Text { text: "Speech model"; font: Theme.type.label; color: Theme.color.textSecondary }
        Repeater {
            model: page.project.speechModels
            delegate: AbstractButton {
                id: row
                required property var modelData
                readonly property bool chosen: page.model === modelData.id
                readonly property bool downloading: page.download.id === modelData.id
                objectName: "speechModel:" + modelData.id
                Layout.fillWidth: true
                implicitHeight: rowLayout.implicitHeight + Theme.space.s * 2
                hoverEnabled: true
                onClicked: page.model = modelData.id
                background: Rectangle {
                    radius: Theme.radius.m
                    color: row.chosen ? Theme.color.accentSubtle : row.hovered ? Theme.color.surfaceHover : Theme.color.surfaceRaised
                    border.color: row.chosen ? Theme.color.accentText : Theme.color.borderSubtle
                }
                contentItem: ColumnLayout {
                    id: rowLayout
                    spacing: 2
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.leftMargin: Theme.space.s
                        Layout.rightMargin: Theme.space.s
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 0
                            Text { text: row.modelData.title; font: Theme.type.label; color: Theme.color.textPrimary; Layout.fillWidth: true; elide: Text.ElideRight }
                            Text {
                                text: page.megabytes(row.modelData.size) + " · " + row.modelData.note
                                      + (row.modelData.downloaded ? " · on this Mac" : row.downloading ? " · downloading " + Math.round(page.download.progress * 100) + " %" : "")
                                font: Theme.type.caption
                                color: Theme.color.textSecondary
                            }
                        }
                        TextButton {
                            objectName: "downloadModel:" + row.modelData.id
                            visible: !row.modelData.downloaded && !row.downloading
                            enabled: page.download.id === undefined
                            text: "Download"
                            variant: "secondary"
                            tip: "From the whisper.cpp model repository (Hugging Face); checked before use"
                            onClicked: {
                                page.model = row.modelData.id
                                page.project.downloadSpeechModel(row.modelData.id)
                            }
                        }
                        TextButton {
                            visible: row.downloading
                            text: "Cancel"
                            variant: "ghost"
                            onClicked: page.project.cancelModelDownload()
                        }
                    }
                    ProgressBar {
                        id: bar
                        visible: row.downloading
                        Layout.fillWidth: true
                        Layout.leftMargin: Theme.space.s
                        Layout.rightMargin: Theme.space.s
                        value: page.download.progress || 0
                        background: Rectangle { implicitHeight: 4; radius: 2; color: Theme.color.borderSubtle }
                        contentItem: Item {
                            implicitHeight: 4
                            Rectangle { width: parent.width * bar.visualPosition; height: parent.height; radius: 2; color: Theme.color.accent }
                        }
                    }
                }
            }
        }

        Text {
            visible: !page.modelInfo.englishOnly
            text: "Language"
            font: Theme.type.label
            color: Theme.color.textSecondary
        }
        ComboBox {
            id: language
            objectName: "captionLanguage"
            visible: !page.modelInfo.englishOnly
            Layout.fillWidth: true
            model: page.project.captionLanguages
            textRole: "name"
            valueRole: "code"
        }

        TextButton {
            objectName: "generateCaptions"
            Layout.fillWidth: true
            text: "Make captions"
            variant: "primary"
            enabled: page.modelInfo.downloaded === true && !page.project.analyzing
            tip: page.modelInfo.downloaded ? "Replaces the captions on the Captions track (one undo step)" : "Download the speech model first"
            onClicked: page.project.generateCaptions({ model: page.model, language: language.currentValue || "auto" })
        }
        Text {
            objectName: "captionScope"
            Layout.fillWidth: true
            Layout.topMargin: -Theme.space.s
            text: page.fromSelection ? "From the selected clips" : "From every clip with sound"
            font: Theme.type.caption
            color: Theme.color.textSecondary
            horizontalAlignment: Text.AlignHCenter
        }
        ColumnLayout {
            visible: page.project.analyzing
            Layout.fillWidth: true
            RowLayout {
                Layout.fillWidth: true
                Text {
                    objectName: "captionProgress"
                    Layout.fillWidth: true
                    text: page.project.analysisTask + "… " + Math.round(page.project.analysisProgress * 100) + " %"
                    font: Theme.type.body
                    color: Theme.color.textPrimary
                }
                TextButton { text: "Cancel"; variant: "ghost"; onClicked: page.project.cancelAnalysis() }
            }
        }
        Text {
            objectName: "captionSummary"
            Layout.fillWidth: true
            text: page.project.captionCount === 0 ? "No captions yet."
                : page.project.captionCount + (page.project.captionCount === 1 ? " caption" : " captions")
                  + " on the Captions track. Select one to change its words or look."
            font: Theme.type.body
            color: Theme.color.textPrimary
            wrapMode: Text.WordWrap
        }

        Text { visible: page.project.captionCount > 0; text: "Style"; font: Theme.type.label; color: Theme.color.textSecondary }
        Flow {
            visible: page.project.captionCount > 0
            Layout.fillWidth: true
            spacing: Theme.space.xs
            Repeater {
                model: [{ id: "none", label: "Plain" }, { id: "highlight", label: "Highlight" }, { id: "pop", label: "Pop" },
                        { id: "karaoke", label: "Karaoke" }, { id: "reveal", label: "Word by word" }]
                delegate: TextButton {
                    required property var modelData
                    objectName: "captionStyle:" + modelData.id
                    text: modelData.label
                    variant: page.project.captionMotion === modelData.id ? "primary" : "secondary"
                    onClicked: page.project.setCaptionStyle(modelData.id)
                }
            }
        }

        Text { text: "Subtitle files"; font: Theme.type.label; color: Theme.color.textSecondary }
        Flow {
            Layout.fillWidth: true
            spacing: Theme.space.xs
            TextButton { objectName: "importSubtitles"; text: "Import SRT or VTT…"; variant: "secondary"; onClicked: importDialog.open() }
            TextButton {
                objectName: "exportSrt"
                text: "Export SRT…"
                variant: "secondary"
                enabled: page.project.captionCount > 0
                onClicked: { exportDialog.extension = "srt"; exportDialog.open() }
            }
            TextButton {
                objectName: "exportVtt"
                text: "Export VTT…"
                variant: "secondary"
                enabled: page.project.captionCount > 0
                onClicked: { exportDialog.extension = "vtt"; exportDialog.open() }
            }
        }
        Item { implicitHeight: Theme.space.m }
    }
}
