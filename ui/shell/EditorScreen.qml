import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import TechieVideoEditor.Theme
import TechieVideoEditor.Player

// The editor workspace:
//   ┌──────────────────────── top bar ────────────────────────┐
//   │ library │          player          │      inspector      │
//   ├─────────┴──────────────────────────┴─────────────────────┤
//   │                        timeline                          │
//   └──────────────────────────────────────────────────────────┘
// All dividers are draggable; sizes come from Theme.size.
Rectangle {
    id: editor
    required property var project // ProjectController
    property alias player: playerPanel.player
    signal homeRequested()

    color: Theme.color.canvas

    function importMedia() { importDialog.open() }
    function saveProject() {
        if (project.filePath !== "")
            project.save("")
        else
            saveDialog.open()
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        TopBar {
            Layout.fillWidth: true
            project: editor.project
            onHomeRequested: editor.homeRequested()
            onSaveRequested: editor.saveProject()
            onExportRequested: exportDialog.open()
            onShortcutsRequested: shortcutsDialog.open()
        }

        // Media files that moved: say so until they are found.
        Rectangle {
            objectName: "missingMediaBar"
            visible: editor.project.missingMedia.length > 0
            Layout.fillWidth: true
            implicitHeight: missingRow.implicitHeight + Theme.space.s * 2
            color: Qt.alpha(Theme.color.warning, Theme.dark ? 0.18 : 0.14)
            RowLayout {
                id: missingRow
                anchors.fill: parent
                anchors.leftMargin: Theme.space.l
                anchors.rightMargin: Theme.space.m
                Text {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1 // elides in what the button leaves
                    readonly property var names: editor.project.missingMedia.map((m) => m.name)
                    text: (names.length === 1 ? "1 media file is missing: " : names.length + " media files are missing: ")
                          + names.slice(0, 3).join(", ") + (names.length > 3 ? "…" : "")
                    font: Theme.type.body
                    color: Theme.color.textPrimary
                    elide: Text.ElideRight
                }
                TextButton {
                    objectName: "relinkButton"
                    text: "Relink…"
                    variant: "secondary"
                    onClicked: relinkDialog.open()
                }
            }
        }

        SplitView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            orientation: Qt.Vertical
            handle: Grip { vertical: false }

            SplitView {
                SplitView.fillHeight: true
                orientation: Qt.Horizontal
                handle: Grip { vertical: true }

                LibraryPanel {
                    SplitView.preferredWidth: Theme.size.libraryWidth
                    SplitView.minimumWidth: Theme.size.libraryMinWidth
                    player: playerPanel.player
                    project: editor.project
                    onImportRequested: editor.importMedia()
                }
                PlayerPanel {
                    id: playerPanel
                    SplitView.fillWidth: true
                    SplitView.minimumWidth: Theme.size.playerMinWidth
                    project: editor.project
                }
                InspectorPanel {
                    SplitView.preferredWidth: Theme.size.inspectorWidth
                    SplitView.minimumWidth: Theme.size.inspectorMinWidth
                    player: playerPanel.player
                    project: editor.project
                }
            }

            TimelinePanel {
                SplitView.preferredHeight: Theme.size.timelineHeight
                SplitView.minimumHeight: Theme.size.timelineMinHeight
                project: editor.project
            }
        }
    }

    FileDialog {
        id: importDialog
        title: "Import media"
        fileMode: FileDialog.OpenFiles
        nameFilters: ["Media files (*.mp4 *.mov *.m4v *.mkv *.avi *.webm *.mxf *.mts *.m2ts *.gif *.wav *.mp3 *.m4a *.aac *.flac *.aif *.aiff *.ogg *.png *.jpg *.jpeg *.heic *.heif *.webp *.tif *.tiff *.bmp)",
                      "Videos (*.mp4 *.mov *.m4v *.mkv *.avi *.webm *.mxf *.mts *.m2ts *.gif)",
                      "Images (*.png *.jpg *.jpeg *.heic *.heif *.webp *.tif *.tiff *.bmp *.gif)",
                      "Audio (*.wav *.mp3 *.m4a *.aac *.flac *.aif *.aiff *.ogg)", "All files (*)"]
        onAccepted: editor.project.importMedia(selectedFiles)
    }

    ShortcutsDialog {
        id: shortcutsDialog
        objectName: "shortcutsDialog"
    }

    RelinkDialog {
        id: relinkDialog
        objectName: "relinkDialog"
        project: editor.project
    }

    ExportDialog {
        id: exportDialog
        objectName: "exportDialog"
        project: editor.project
    }

    FileDialog {
        id: saveDialog
        title: "Save project"
        fileMode: FileDialog.SaveFile
        defaultSuffix: "tve"
        nameFilters: ["TechieVideoEditor projects (*.tve)"]
        onAccepted: editor.project.save(selectedFile)
    }

    // Keyboard shortcuts. Single-key ones are off while a text field has focus.
    readonly property bool typing: Window.activeFocusItem !== null
                                   && Window.activeFocusItem.hasOwnProperty("cursorPosition")
    // Keys come from KeyMap (changed in the Keyboard shortcuts dialog).
    readonly property var keys: KeyMap.sequences
    Shortcut { sequences: editor.keys.play; enabled: editor.visible && !editor.typing; onActivated: editor.project.togglePlay() }
    Shortcut { objectName: "shortcut:split"; sequences: editor.keys.split; enabled: editor.visible && !editor.typing; onActivated: editor.project.splitAtPlayhead() }
    Shortcut {
        sequences: editor.keys["delete"]
        enabled: editor.visible && !editor.typing
                 && (editor.project.selectedClip > 0 || editor.project.selectedTransition > 0)
        onActivated: editor.project.selectedTransition > 0
                     ? editor.project.removeTransition(editor.project.selectedTransition)
                     : editor.project.deleteClip(editor.project.selectedClip)
    }
    Shortcut { sequences: editor.keys.frameBack; enabled: editor.visible && !editor.typing; onActivated: editor.project.stepFrames(-1) }
    Shortcut { sequences: editor.keys.frameForward; enabled: editor.visible && !editor.typing; onActivated: editor.project.stepFrames(1) }
    Shortcut { sequences: editor.keys.goToStart; enabled: editor.visible && !editor.typing; onActivated: editor.project.seek(0) }
    Shortcut { sequences: editor.keys.goToEnd; enabled: editor.visible && !editor.typing; onActivated: editor.project.seek(editor.project.duration) }
    Shortcut { sequences: editor.keys.addMarker; enabled: editor.visible && !editor.typing; onActivated: editor.project.addMarker() }
    Shortcut { sequences: editor.keys.nextMarker; enabled: editor.visible && !editor.typing; onActivated: editor.project.seekMarker(1) }
    Shortcut { sequences: editor.keys.previousMarker; enabled: editor.visible && !editor.typing; onActivated: editor.project.seekMarker(-1) }
    Shortcut { sequences: editor.keys.group; enabled: editor.visible && !editor.typing; onActivated: editor.project.groupSelected() }
    Shortcut { sequences: editor.keys.ungroup; enabled: editor.visible && !editor.typing; onActivated: editor.project.ungroupSelected() }
    Shortcut {
        sequences: editor.keys.compound
        enabled: editor.visible && !editor.typing
        onActivated: editor.project.selectedClipInfo.isCompound === true
                     ? editor.project.breakCompound(editor.project.selectedClip)
                     : editor.project.createCompound()
    }
    Shortcut { sequences: editor.keys.undo; enabled: editor.visible; onActivated: editor.project.undo() }
    Shortcut { sequences: editor.keys.redo; enabled: editor.visible; onActivated: editor.project.redo() }
    Shortcut {
        sequences: editor.keys["export"]
        enabled: editor.visible && editor.project.duration > 0
        onActivated: exportDialog.open()
    }
    Shortcut { sequences: editor.keys.save; enabled: editor.visible; onActivated: editor.saveProject() }
    Shortcut { sequences: editor.keys["import"]; enabled: editor.visible; onActivated: editor.importMedia() }
    Shortcut { sequences: editor.keys.shortcuts; enabled: editor.visible; onActivated: shortcutsDialog.open() }

    // Error toast: explains why an edit was refused.
    Rectangle {
        id: toast
        objectName: "errorToast"
        property string message
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Theme.space.xl
        width: toastText.implicitWidth + Theme.space.xl
        height: toastText.implicitHeight + Theme.space.m
        radius: Theme.radius.m
        color: Theme.color.surfaceRaised
        border.color: Theme.color.danger
        opacity: message !== "" ? 1 : 0
        visible: opacity > 0
        Behavior on opacity { NumberAnimation { duration: Theme.motion.normal } }

        Text {
            id: toastText
            anchors.centerIn: parent
            text: toast.message
            font: Theme.type.body
            color: Theme.color.textPrimary
        }
        Timer { id: toastTimer; interval: 3500; onTriggered: toast.message = "" }
        Connections {
            target: editor.project
            function onErrorOccurred(message) {
                toast.message = message
                toastTimer.restart()
            }
        }
    }

    // 1px divider with a wider invisible grab area.
    component Grip: Rectangle {
        id: grip
        required property bool vertical
        implicitWidth: vertical ? 1 : 0
        implicitHeight: vertical ? 0 : 1
        color: SplitHandle.pressed ? Theme.color.accent
             : SplitHandle.hovered ? Theme.color.borderStrong : Theme.color.borderSubtle

        containmentMask: Item {
            x: grip.vertical ? -3 : 0
            y: grip.vertical ? 0 : -3
            width: grip.vertical ? 7 : grip.width
            height: grip.vertical ? grip.height : 7
        }
    }
}
