import QtCore
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import TechieVideoEditor.Theme

// Timelines with other editors. Exporting: OpenTimelineIO (DaVinci Resolve, Premiere Pro,
// Avid and others), Final Cut Pro XML, or an EDL; then what didn't carry over. After a
// timeline from another editor is opened (Open project…), the same report for it.
Popup {
    id: dialog
    required property var project

    // "export" → "done" (or "failed"); "opened" after a timeline was opened
    property string stage: "export"
    property string format: "otio"
    property url file
    property string openedName
    property var notes: []
    property string message

    readonly property var formats: [
        { id: "otio", label: qsTr("OpenTimelineIO"), extension: "otio",
          note: qsTr("For DaVinci Resolve, Premiere Pro, Avid and most other editors") },
        { id: "fcpxml", label: qsTr("Final Cut Pro XML"), extension: "fcpxml", note: qsTr("For Final Cut Pro (and DaVinci Resolve)") },
        { id: "edl", label: qsTr("EDL"), extension: "edl", note: qsTr("The main video track as a CMX 3600 edit list, for any editor") }
    ]
    readonly property var chosen: formats.find((f) => f.id === format)

    function baseName() { return (project.name || qsTr("Untitled")).replace(/[\\/:*?"<>|]/g, "-") }
    function withExtension(url, extension) { return String(url).replace(/\.[^./]*$/, "") + "." + extension }
    function prepare() {
        stage = "export"
        notes = []
        message = ""
        file = StandardPaths.writableLocation(StandardPaths.DocumentsLocation) + "/" + baseName() + "." + chosen.extension
    }
    function showOpened(list, name) {
        stage = "opened"
        openedName = name
        notes = list
        open()
    }
    function start() {
        const result = project.exportTimeline(file, format)
        if (result.ok) {
            notes = result.notes
            stage = "done"
        } else {
            message = project.lastError
            stage = "failed"
        }
    }

    modal: true
    focus: true
    anchors.centerIn: Overlay.overlay
    width: 540
    padding: Theme.space.l
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    FileDialog {
        id: fileDialog
        title: qsTr("Export timeline")
        fileMode: FileDialog.SaveFile
        defaultSuffix: dialog.chosen.extension
        nameFilters: [dialog.chosen.label + " (*." + dialog.chosen.extension + ")"]
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
            objectName: "interchangeTitle"
            text: dialog.stage === "opened" ? qsTr("Opened %1").arg(dialog.openedName)
                : dialog.stage === "done" ? qsTr("Timeline exported")
                : dialog.stage === "failed" ? qsTr("Export failed")
                : qsTr("Export timeline")
            font: Theme.type.title
            color: Theme.color.textPrimary
        }

        // Choosing the format and where.
        ColumnLayout {
            visible: dialog.stage === "export"
            Layout.fillWidth: true
            spacing: Theme.space.s
            Text {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: qsTr("The edit for another editor: the clips and their timing, speed, cross dissolves, gaps, markers, and titles as placeholders. Colour, effects and keyframes stay here.")
                font: Theme.type.body
                color: Theme.color.textSecondary
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.space.xs
                Repeater {
                    model: dialog.formats
                    delegate: TextButton {
                        required property var modelData
                        objectName: "interchangeFormat:" + modelData.id
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        text: modelData.label
                        variant: dialog.format === modelData.id ? "primary" : "secondary"
                        onClicked: {
                            dialog.format = modelData.id
                            dialog.file = dialog.withExtension(dialog.file, modelData.extension)
                        }
                    }
                }
            }
            Text {
                objectName: "interchangeFormatNote"
                Layout.fillWidth: true
                text: dialog.chosen.note
                font: Theme.type.caption
                color: Theme.color.textSecondary
            }
            RowLayout {
                Layout.fillWidth: true
                Text {
                    Layout.fillWidth: true
                    text: decodeURIComponent(String(dialog.file).replace(/^file:\/\//, ""))
                    font: Theme.type.body
                    color: Theme.color.textPrimary
                    elide: Text.ElideMiddle
                }
                TextButton { text: qsTr("Choose…"); variant: "secondary"; onClicked: fileDialog.open() }
            }
        }

        Text {
            visible: dialog.stage === "failed"
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: dialog.message
            font: Theme.type.body
            color: Theme.color.danger
        }

        // The report: what didn't carry over.
        ColumnLayout {
            visible: dialog.stage === "done" || dialog.stage === "opened"
            Layout.fillWidth: true
            spacing: Theme.space.xs
            Text {
                objectName: "interchangeSummary"
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: dialog.notes.length === 0
                      ? (dialog.stage === "opened" ? qsTr("Everything in it came over.") : qsTr("Everything carried over."))
                      : (dialog.stage === "opened" ? qsTr("These didn't come over as they were:") : qsTr("These didn't carry over as they are here:"))
                font: Theme.type.body
                color: Theme.color.textPrimary
            }
            ScrollView {
                visible: dialog.notes.length > 0
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(240, notesColumn.implicitHeight)
                clip: true
                ColumnLayout {
                    id: notesColumn
                    width: dialog.availableWidth
                    spacing: Theme.space.xxs
                    Repeater {
                        model: dialog.notes
                        delegate: Text {
                            required property string modelData
                            required property int index
                            objectName: "interchangeNote:" + index
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            text: "• " + modelData
                            font: Theme.type.body
                            color: Theme.color.textSecondary
                        }
                    }
                }
            }
            Text {
                visible: dialog.stage === "opened"
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: qsTr("It is a new project: save it to keep it. Media that isn't on this Mac can be found again from the library.")
                font: Theme.type.caption
                color: Theme.color.textSecondary
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: Theme.space.s
            Item { Layout.fillWidth: true }
            TextButton {
                objectName: "interchangeClose"
                text: dialog.stage === "export" ? qsTr("Cancel") : qsTr("Done")
                variant: dialog.stage === "export" ? "ghost" : "primary"
                onClicked: dialog.close()
            }
            TextButton {
                objectName: "interchangeExportButton"
                visible: dialog.stage === "export"
                text: qsTr("Export")
                iconName: "export"
                variant: "primary"
                onClicked: dialog.start()
            }
        }
    }
}
