import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import TechieVideoEditor.Theme

// Finds media whose files moved: search a folder for all of them (by name, and by content
// for renamed copies), or locate each by hand. A file whose content differs from the one
// imported needs a second yes ("Use anyway").
Popup {
    id: dialog
    required property var project
    readonly property var missing: project.missingMedia
    property string message
    property real pendingId: 0   // the media a "Use anyway" answer is for
    property url pendingFile

    modal: true
    focus: true
    anchors.centerIn: Overlay.overlay
    width: 520
    padding: Theme.space.l
    onAboutToShow: {
        message = ""
        pendingId = 0
    }

    function relink(id, file, force) {
        if (dialog.project.relinkMedia(id, file, force)) {
            message = ""
            pendingId = 0
            return
        }
        message = dialog.project.lastError
        if (message.indexOf("not the same file") >= 0) {
            pendingId = id
            pendingFile = file
        }
    }

    FolderDialog {
        id: folderDialog
        title: "Search a folder for the missing files"
        onAccepted: {
            const before = dialog.missing.length
            const found = dialog.project.relinkFromFolder(selectedFolder)
            dialog.message = found > 0 ? "Found " + found + " of " + before + "." : dialog.project.lastError
            dialog.pendingId = 0
        }
    }
    FileDialog {
        id: fileDialog
        property real mediaId: 0
        title: "Locate the file"
        fileMode: FileDialog.OpenFile
        onAccepted: dialog.relink(mediaId, selectedFile, false)
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
            text: dialog.missing.length > 0 ? "Missing media" : "All media found"
            font: Theme.type.title
            color: Theme.color.textPrimary
        }
        Text {
            Layout.fillWidth: true
            text: dialog.missing.length > 0
                  ? "These files are not where the project expects them. Their clips stay on the timeline, "
                    + "but show and play nothing until you find the files."
                  : "Every clip has its file again."
            font: Theme.type.body
            color: Theme.color.textSecondary
            wrapMode: Text.WordWrap
        }
        TextButton {
            objectName: "relinkSearchFolder"
            visible: dialog.missing.length > 0
            text: "Search a folder…"
            iconName: "search"
            variant: "primary"
            tip: "Looks in the folder and the folders in it, by file name and by content (renamed copies)"
            onClicked: folderDialog.open()
        }

        ListView {
            id: list
            objectName: "missingMediaList"
            visible: dialog.missing.length > 0
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(contentHeight, 300)
            clip: true
            spacing: Theme.space.xs
            model: dialog.missing
            delegate: Rectangle {
                id: row
                required property var modelData
                objectName: "missing:" + modelData.name
                width: list.width
                implicitHeight: rowLayout.implicitHeight + Theme.space.s * 2
                radius: Theme.radius.m
                color: Theme.color.surface
                border.color: Theme.color.borderSubtle
                RowLayout {
                    id: rowLayout
                    anchors.fill: parent
                    anchors.margins: Theme.space.s
                    anchors.leftMargin: Theme.space.m
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        Text {
                            Layout.fillWidth: true
                            text: row.modelData.name
                            font: Theme.type.label
                            color: Theme.color.textPrimary
                            elide: Text.ElideMiddle
                        }
                        Text {
                            Layout.fillWidth: true
                            text: "Was in " + row.modelData.folder
                            font: Theme.type.caption
                            color: Theme.color.textSecondary
                            elide: Text.ElideMiddle
                        }
                    }
                    TextButton {
                        objectName: "locate:" + row.modelData.name
                        text: "Locate…"
                        variant: "secondary"
                        onClicked: {
                            fileDialog.mediaId = row.modelData.id
                            fileDialog.currentFolder = "file://" + row.modelData.folder
                            fileDialog.open()
                        }
                    }
                }
            }
        }

        // The outcome of the last search or locate.
        RowLayout {
            visible: dialog.message !== ""
            Layout.fillWidth: true
            Text {
                objectName: "relinkMessage"
                Layout.fillWidth: true
                Layout.preferredWidth: 1 // wraps in what the button leaves
                text: dialog.message
                font: Theme.type.body
                color: dialog.pendingId > 0 || dialog.message.indexOf("Found") !== 0 ? Theme.color.warning
                                                                                      : Theme.color.textPrimary
                wrapMode: Text.WordWrap
            }
            TextButton {
                objectName: "relinkUseAnyway"
                visible: dialog.pendingId > 0
                text: "Use anyway"
                variant: "secondary"
                onClicked: dialog.relink(dialog.pendingId, dialog.pendingFile, true)
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            TextButton {
                objectName: "relinkDone"
                text: dialog.missing.length > 0 ? "Later" : "Done"
                variant: dialog.missing.length > 0 ? "secondary" : "primary"
                onClicked: dialog.close()
            }
        }
    }
}
