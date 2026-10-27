import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Editor for one effect parameter, chosen by the manifest type:
// float/int → slider, bool → switch, enum → combo box, color → swatches.
ColumnLayout {
    id: control
    required property var param
    signal valueEdited(var value)

    readonly property color textColor: "#e6e8eb"
    readonly property color mutedColor: "#8a9099"

    spacing: 2

    RowLayout {
        Layout.fillWidth: true
        visible: control.param.type !== "bool"

        Label {
            text: control.param.title
            color: control.mutedColor
            font.pixelSize: 12
            Layout.fillWidth: true
        }
        Label {
            visible: slider.visible
            text: control.param.type === "int" ? slider.value.toFixed(0) : slider.value.toFixed(2)
            color: control.textColor
            font.family: "Menlo"
            font.pixelSize: 12
        }
    }

    Slider {
        id: slider
        Layout.fillWidth: true
        visible: control.param.type === "float" || control.param.type === "int"
        from: control.param.min
        to: control.param.max
        value: control.param.default
        stepSize: control.param.type === "int" ? 1 : 0
        snapMode: control.param.type === "int" ? Slider.SnapAlways : Slider.NoSnap
        onMoved: control.valueEdited(value)
    }

    Switch {
        visible: control.param.type === "bool"
        text: control.param.title
        checked: control.param.default > 0.5
        palette.windowText: control.textColor
        onToggled: control.valueEdited(checked ? 1 : 0)
    }

    ComboBox {
        Layout.fillWidth: true
        visible: control.param.type === "enum"
        model: control.param.values
        currentIndex: control.param.type === "enum" ? control.param.default : 0
        onActivated: (index) => control.valueEdited(index)
    }

    Row {
        visible: control.param.type === "color"
        spacing: 6

        Repeater {
            model: [[0, 0, 0, 1], [1, 1, 1, 1], [0.9, 0.2, 0.2, 1], [0.2, 0.4, 0.95, 1], [1, 0.6, 0.1, 1]]

            delegate: Rectangle {
                required property var modelData
                width: 22
                height: 22
                radius: 11
                color: Qt.rgba(modelData[0], modelData[1], modelData[2], 1)
                border.color: "#5a606a"

                MouseArea {
                    anchors.fill: parent
                    onClicked: control.valueEdited(parent.modelData)
                }
            }
        }
    }
}
