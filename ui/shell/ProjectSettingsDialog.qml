import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import TechieVideoEditor.Theme
import TechieVideoEditor.Player

// The project's canvas and frame rate, changed after it has begun (clips keep their times and
// fit the new shape), and the whole project archived with its media into one folder.
Popup {
    id: dialog
    objectName: "projectSettingsDialog"
    required property var project

    // Shapes and sizes to pick from; Custom takes any width and height.
    readonly property var sizes: [
        { id: "720p", label: qsTr("Landscape 16:9 · 1280 × 720"), width: 1280, height: 720 },
        { id: "1080p", label: qsTr("Landscape 16:9 · 1920 × 1080"), width: 1920, height: 1080 },
        { id: "1440p", label: qsTr("Landscape 16:9 · 2560 × 1440"), width: 2560, height: 1440 },
        { id: "4k", label: qsTr("Landscape 16:9 · 3840 × 2160"), width: 3840, height: 2160 },
        { id: "vertical", label: qsTr("Vertical 9:16 · 1080 × 1920"), width: 1080, height: 1920 },
        { id: "portrait", label: qsTr("Portrait 4:5 · 1080 × 1350"), width: 1080, height: 1350 },
        { id: "square", label: qsTr("Square 1:1 · 1080 × 1080"), width: 1080, height: 1080 },
        { id: "cinema", label: qsTr("Cinema 2.39:1 · 2048 × 858"), width: 2048, height: 858 },
        { id: "custom", label: qsTr("Custom"), width: 0, height: 0 }
    ]
    readonly property var rates: [23.976, 24, 25, 29.97, 30, 50, 59.94, 60]
    property string result: "" // what the archive did

    modal: true
    focus: true
    anchors.centerIn: Overlay.overlay
    width: 480
    padding: Theme.space.l
    closePolicy: project.archiving ? Popup.NoAutoClose : Popup.CloseOnEscape | Popup.CloseOnPressOutside

    function prepare() {
        const w = project.canvasWidth, h = project.canvasHeight
        const found = sizes.findIndex((s) => s.width === w && s.height === h)
        sizePicker.currentIndex = found >= 0 ? found : sizes.length - 1
        widthField.text = String(w)
        heightField.text = String(h)
        const rate = rates.findIndex((r) => Math.abs(r - project.frameRate) < 0.01)
        ratePicker.currentIndex = rate >= 0 ? rate : 4
        result = ""
    }
    onAboutToShow: prepare()
    readonly property int chosenWidth: sizes[sizePicker.currentIndex].id === "custom" ? Number(widthField.text) : sizes[sizePicker.currentIndex].width
    readonly property int chosenHeight: sizes[sizePicker.currentIndex].id === "custom" ? Number(heightField.text) : sizes[sizePicker.currentIndex].height
    readonly property real chosenRate: rates[Math.max(0, ratePicker.currentIndex)]
    readonly property bool changed: chosenWidth !== project.canvasWidth || chosenHeight !== project.canvasHeight
                                    || Math.abs(chosenRate - project.frameRate) > 0.001
    function apply() {
        if (project.setCanvas(chosenWidth, chosenHeight, chosenRate))
            close()
    }

    Connections {
        target: dialog.project
        function onArchiveFinished(ok, path, message) {
            dialog.result = ok ? (message ? qsTr("Archived to %1. %2").arg(path).arg(message) : qsTr("Archived to %1").arg(path))
                               : message
        }
    }

    background: Rectangle {
        radius: Theme.radius.l
        color: Theme.color.surfaceRaised
        border.color: Theme.color.borderStrong
    }

    contentItem: ColumnLayout {
        spacing: Theme.space.m

        Text { text: qsTr("Project settings"); font: Theme.type.title; color: Theme.color.textPrimary }

        // Canvas
        GridLayout {
            Layout.fillWidth: true
            columns: 2
            columnSpacing: Theme.space.m
            rowSpacing: Theme.space.s
            Text { text: qsTr("Size"); font: Theme.type.label; color: Theme.color.textSecondary }
            ComboBox {
                id: sizePicker
                objectName: "canvasSize"
                Layout.fillWidth: true
                model: dialog.sizes
                textRole: "label"
            }
            Item { visible: customRow.visible; implicitWidth: 1 }
            RowLayout {
                id: customRow
                visible: dialog.sizes[sizePicker.currentIndex].id === "custom"
                Layout.fillWidth: true
                spacing: Theme.space.xs
                component SizeField: TextField {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignRight
                    font: Theme.type.body
                    color: Theme.color.textPrimary
                    selectByMouse: true
                    validator: IntValidator { bottom: 16; top: 16384 }
                    background: Rectangle {
                        radius: Theme.radius.s
                        color: Theme.color.surfaceSunken
                        border.color: parent.activeFocus ? Theme.color.accent : Theme.color.borderSubtle
                    }
                }
                SizeField { id: widthField; objectName: "canvasWidth" }
                Text { text: "×"; font: Theme.type.body; color: Theme.color.textSecondary }
                SizeField { id: heightField; objectName: "canvasHeight" }
                Text { text: qsTr("pixels"); font: Theme.type.caption; color: Theme.color.textSecondary }
            }
            Text { text: qsTr("Frame rate"); font: Theme.type.label; color: Theme.color.textSecondary }
            ComboBox {
                id: ratePicker
                objectName: "canvasRate"
                Layout.fillWidth: true
                model: dialog.rates.map((r) => qsTr("%1 fps").arg(Fmt.brief(r, 3)))
            }
        }
        Text {
            Layout.fillWidth: true
            Layout.preferredWidth: 1
            wrapMode: Text.WordWrap
            text: qsTr("Clips keep their times. Pictures fit the new shape as they do any canvas; move or scale them afterwards if they should fill it.")
            font: Theme.type.caption
            color: Theme.color.textSecondary
        }
        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            TextButton {
                objectName: "applyCanvas"
                text: qsTr("Change the canvas")
                variant: "primary"
                enabled: dialog.changed && dialog.chosenWidth >= 16 && dialog.chosenHeight >= 16
                onClicked: dialog.apply()
            }
        }

        Divider { Layout.fillWidth: true; implicitHeight: 1 }

        // Archive
        Text { text: qsTr("Archive"); font: Theme.type.heading; color: Theme.color.textPrimary }
        Text {
            Layout.fillWidth: true
            Layout.preferredWidth: 1
            wrapMode: Text.WordWrap
            text: qsTr("Copies the project and every file it uses (media, cleaned sound, LUTs) into one new folder, to move or keep. The project open here stays as it is.")
            font: Theme.type.body
            color: Theme.color.textSecondary
        }
        ToggleSwitch {
            id: usedOnlySwitch
            objectName: "archiveUsedOnly"
            text: qsTr("Only media the timeline uses")
            enabled: !dialog.project.archiving
        }
        RowLayout {
            Layout.fillWidth: true
            visible: dialog.project.archiving
            ProgressBar {
                objectName: "archiveProgress"
                Layout.fillWidth: true
                value: dialog.project.archiveProgress
            }
            TextButton { objectName: "cancelArchive"; text: qsTr("Cancel"); variant: "secondary"; onClicked: dialog.project.cancelArchive() }
        }
        Text {
            objectName: "archiveResult"
            visible: dialog.result !== ""
            Layout.fillWidth: true
            Layout.preferredWidth: 1
            wrapMode: Text.WrapAnywhere
            text: dialog.result
            font: Theme.type.caption
            color: Theme.color.textSecondary
        }
        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            TextButton {
                objectName: "archiveProject"
                text: qsTr("Archive…")
                iconName: "folder"
                variant: "secondary"
                enabled: !dialog.project.archiving
                onClicked: archiveFolder.open()
            }
            TextButton { objectName: "closeProjectSettings"; text: qsTr("Done"); variant: "secondary"; enabled: !dialog.project.archiving; onClicked: dialog.close() }
        }
    }

    FolderDialog {
        id: archiveFolder
        title: qsTr("Where to put the archive")
        onAccepted: {
            dialog.result = ""
            dialog.project.archiveProject(selectedFolder, { usedOnly: usedOnlySwitch.checked })
        }
    }
    // (Tests choose the folder directly.)
    function archiveInto(folder) {
        result = ""
        return project.archiveProject(folder, { usedOnly: usedOnlySwitch.checked })
    }
}
