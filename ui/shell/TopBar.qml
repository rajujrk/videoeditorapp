import QtQuick
import QtQuick.Layouts
import TechieVideoEditor.Theme

// Editor title bar: back to Home, project name and canvas, history, save, export.
Rectangle {
    id: bar
    required property var project
    signal homeRequested()
    signal saveRequested()
    signal exportRequested()

    implicitHeight: Theme.size.topBar
    color: Theme.color.surface

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.space.s
        anchors.rightMargin: Theme.space.m
        spacing: Theme.space.s

        IconButton { iconName: "back"; tip: "Home"; onClicked: bar.homeRequested() }
        Divider { implicitWidth: 1; implicitHeight: 20 }

        Text {
            objectName: "projectName"
            text: bar.project.name + (bar.project.modified ? "  •" : "")
            font: Theme.type.heading
            color: Theme.color.textPrimary
            Layout.leftMargin: Theme.space.xs
        }
        Text {
            objectName: "autosaveStatus"
            visible: bar.project.modified && bar.project.lastAutosave > 0
            text: "Autosaved " + new Date(bar.project.lastAutosave).toLocaleTimeString(Qt.locale(), Locale.ShortFormat)
            font: Theme.type.caption
            color: Theme.color.textSecondary
        }
        Rectangle {
            implicitWidth: canvas.implicitWidth + Theme.space.m
            implicitHeight: 20
            radius: Theme.radius.round
            color: Theme.color.surfaceRaised
            border.color: Theme.color.borderSubtle
            Text {
                id: canvas
                anchors.centerIn: parent
                text: bar.project.canvasWidth + " × " + bar.project.canvasHeight + " · "
                      + Math.round(bar.project.frameRate) + " fps"
                font: Theme.type.caption
                color: Theme.color.textSecondary
            }
        }

        Item { Layout.fillWidth: true }

        IconButton {
            objectName: "undoButton"
            iconName: "undo"
            tip: bar.project.canUndo ? "Undo " + bar.project.undoText + " (⌘Z)" : "Nothing to undo"
            enabled: bar.project.canUndo
            onClicked: bar.project.undo()
        }
        IconButton {
            objectName: "redoButton"
            iconName: "redo"
            tip: bar.project.canRedo ? "Redo " + bar.project.redoText + " (⇧⌘Z)" : "Nothing to redo"
            enabled: bar.project.canRedo
            onClicked: bar.project.redo()
        }
        IconButton { iconName: "save"; tip: "Save project (⌘S)"; onClicked: bar.saveRequested() }
        IconButton {
            iconName: Theme.dark ? "sun" : "moon"
            tip: Theme.dark ? "Light theme" : "Dark theme"
            onClicked: Theme.dark = !Theme.dark
        }
        IconButton {
            id: queueButton
            objectName: "renderQueueButton"
            // Exports still to finish: the running one and those waiting.
            readonly property int pending: bar.project.exportJobs.filter((j) => j.state === "running" || j.state === "waiting").length
            visible: bar.project.exportJobs.length > 0
            iconName: "queue"
            tip: "Render queue" + (pending > 0 ? " · " + pending + " to go" : "")
            checkable: true
            checked: queuePanel.visible
            onClicked: queuePanel.visible ? queuePanel.close() : queuePanel.open()

            Rectangle {
                visible: queueButton.pending > 0
                anchors { right: parent.right; top: parent.top; rightMargin: -2; topMargin: -2 }
                width: Math.max(16, badge.implicitWidth + 8)
                height: 16
                radius: 8
                color: Theme.color.accent
                Text {
                    id: badge
                    anchors.centerIn: parent
                    text: queueButton.pending
                    font: Theme.type.caption
                    color: Theme.color.textOnAccent
                }
            }

            RenderQueuePanel {
                id: queuePanel
                objectName: "renderQueuePanel"
                project: bar.project
                x: parent.width - width
                y: parent.height + Theme.space.s
            }
        }
        TextButton {
            objectName: "exportButton"
            text: bar.project.exporting ? (bar.project.exportPaused ? "Paused " : "Exporting ")
                                          + Math.round(bar.project.exportProgress * 100) + " %" : "Export"
            iconName: "export"
            variant: "primary"
            enabled: bar.project.duration > 0
            tip: "Export a video, a GIF or the sound (⌘E); exports queue up and run while you edit"
            Layout.leftMargin: Theme.space.s
            onClicked: bar.exportRequested()
        }
    }

    Divider { anchors.bottom: parent.bottom; width: parent.width }
}
