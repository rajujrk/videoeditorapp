import QtQuick
import QtQuick.Layouts
import TechieVideoEditor.Theme

// Editor title bar: back to Home, project name and canvas, history, export.
Rectangle {
    id: bar
    property string projectName
    property string canvasDescription
    signal homeRequested()

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
            text: bar.projectName
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
                text: bar.canvasDescription
                font: Theme.type.caption
                color: Theme.color.textSecondary
            }
        }

        Item { Layout.fillWidth: true }

        IconButton { iconName: "undo"; tip: "Undo (Phase 1)"; enabled: false }
        IconButton { iconName: "redo"; tip: "Redo (Phase 1)"; enabled: false }
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
            tip: "Export arrives in Phase 1"
            Layout.leftMargin: Theme.space.s
        }
    }

    Divider { anchors.bottom: parent.bottom; width: parent.width }
}
