import QtQuick
import QtQuick.Layouts
import TechieVideoEditor.Theme

// Editor title bar: back to Home, project name and canvas, history, save, export.
Rectangle {
    id: bar
    required property var project
    signal homeRequested()
    signal saveRequested()

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
        TextButton {
            text: "Export"
            iconName: "export"
            variant: "primary"
            enabled: false
            tip: "Export arrives later in Phase 1"
            Layout.leftMargin: Theme.space.s
        }
    }

    Divider { anchors.bottom: parent.bottom; width: parent.width }
}
