import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import TechieVideoEditor.Theme

// Using a template: which shape to make it in (six, drawn to scale), then a new project.
Popup {
    id: picker
    required property var project
    property var template: ({})
    property string shape: "16:9"

    function show(entry) {
        template = entry
        shape = entry.shape || "16:9"
        open()
    }

    parent: Overlay.overlay
    anchors.centerIn: parent
    modal: true
    width: 460
    padding: Theme.space.l
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    background: Rectangle {
        radius: Theme.radius.l
        color: Theme.color.surfaceRaised
        border.color: Theme.color.borderStrong
    }

    contentItem: ColumnLayout {
        spacing: Theme.space.m
        RowLayout {
            Layout.fillWidth: true
            Text { Layout.fillWidth: true; text: picker.template.name || ""; font: Theme.type.title; color: Theme.color.textPrimary }
            IconButton { iconName: "close"; tip: qsTr("Close"); onClicked: picker.close() }
        }
        Text {
            Layout.fillWidth: true
            Layout.preferredWidth: 1
            text: (picker.template.description || "") + " " + qsTr("%1 slots to fill.").arg(Fmt.num(picker.template.slots || 0))
            font: Theme.type.body
            color: Theme.color.textSecondary
            wrapMode: Text.WordWrap
        }
        Text { text: qsTr("Shape"); font: Theme.type.label; color: Theme.color.textSecondary }
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.space.s
            Repeater {
                model: picker.project.templates.shapes
                delegate: AbstractButton {
                    id: shapeButton
                    required property var modelData
                    objectName: "shape:" + modelData.id
                    Layout.fillWidth: true
                    implicitHeight: 74
                    checked: picker.shape === modelData.id
                    hoverEnabled: true
                    onClicked: picker.shape = modelData.id
                    background: Rectangle {
                        radius: Theme.radius.m
                        color: shapeButton.checked ? Theme.color.accentSubtle : shapeButton.hovered ? Theme.color.surfaceHover : "transparent"
                        border.color: shapeButton.checked ? Theme.color.accent : Theme.color.borderSubtle
                    }
                    contentItem: Item {
                        Rectangle {
                            // The shape, to scale, at most 40 × 40.
                            readonly property real ratio: shapeButton.modelData.width / shapeButton.modelData.height
                            anchors.horizontalCenter: parent.horizontalCenter
                            y: 8 + (40 - height) / 2
                            width: ratio >= 1 ? 40 : 40 * ratio
                            height: ratio >= 1 ? 40 / ratio : 40
                            radius: 3
                            color: "transparent"
                            border.width: 2
                            border.color: shapeButton.checked ? Theme.color.accent : Theme.color.textSecondary
                        }
                        Text {
                            anchors { horizontalCenter: parent.horizontalCenter; bottom: parent.bottom; bottomMargin: 6 }
                            text: shapeButton.modelData.id
                            font: Theme.type.caption
                            color: shapeButton.checked ? Theme.color.accentText : Theme.color.textSecondary
                        }
                    }
                }
            }
        }
        Text {
            Layout.fillWidth: true
            Layout.preferredWidth: 1
            text: qsTr("This starts a new project in place of the current one; save that first if you want to keep it.")
            font: Theme.type.caption
            color: Theme.color.textSecondary
            wrapMode: Text.WordWrap
        }
        RowLayout {
            Layout.fillWidth: true
            TextButton {
                objectName: "deleteTemplate"
                visible: picker.template.builtIn === false
                text: qsTr("Delete template")
                variant: "ghost"
                onClicked: if (picker.project.templates.remove(picker.template.id)) picker.close()
            }
            Item { Layout.fillWidth: true }
            TextButton {
                objectName: "useTemplate"
                text: qsTr("Use template")
                variant: "primary"
                onClicked: if (picker.project.newFromTemplate(picker.template.id, picker.shape)) picker.close()
            }
        }
    }
}
