import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import TechieVideoEditor.Theme

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
    Shortcut { sequence: "Space"; enabled: editor.visible && !editor.typing; onActivated: editor.project.togglePlay() }
    Shortcut { sequence: "S"; enabled: editor.visible && !editor.typing; onActivated: editor.project.splitAtPlayhead() }
    Shortcut {
        sequences: ["Delete", "Backspace"]
        enabled: editor.visible && !editor.typing
                 && (editor.project.selectedClip > 0 || editor.project.selectedTransition > 0)
        onActivated: editor.project.selectedTransition > 0
                     ? editor.project.removeTransition(editor.project.selectedTransition)
                     : editor.project.deleteClip(editor.project.selectedClip)
    }
    Shortcut { sequence: "Left"; enabled: editor.visible && !editor.typing; onActivated: editor.project.stepFrames(-1) }
    Shortcut { sequence: "Right"; enabled: editor.visible && !editor.typing; onActivated: editor.project.stepFrames(1) }
    Shortcut { sequence: "Home"; enabled: editor.visible && !editor.typing; onActivated: editor.project.seek(0) }
    Shortcut { sequence: "End"; enabled: editor.visible && !editor.typing; onActivated: editor.project.seek(editor.project.duration) }
    Shortcut { sequences: [StandardKey.Undo]; enabled: editor.visible; onActivated: editor.project.undo() }
    Shortcut {
        sequence: "Ctrl+E"
        enabled: editor.visible && editor.project.duration > 0
        onActivated: exportDialog.open()
    }
    Shortcut { sequences: [StandardKey.Redo]; enabled: editor.visible; onActivated: editor.project.redo() }
    Shortcut { sequences: [StandardKey.Save]; enabled: editor.visible; onActivated: editor.saveProject() }
    Shortcut { sequence: "Ctrl+I"; enabled: editor.visible; onActivated: editor.importMedia() }

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
