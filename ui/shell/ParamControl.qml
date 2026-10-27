import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import TechieVideoEditor.Theme

// Editor for one effect parameter, chosen by the manifest type:
// float/int → slider, bool → switch, enum → combo box, color → swatches.
ColumnLayout {
    id: control
    required property var param
    signal valueEdited(var value)    // while dragging (preview)
    signal valueCommitted(var value) // on release / toggle / pick (one undo step)

    spacing: Theme.space.xxs

    RowLayout {
        Layout.fillWidth: true
        visible: control.param.type !== "bool"

        Text {
            text: control.param.title
            color: Theme.color.textSecondary
            font: Theme.type.label
            Layout.fillWidth: true
        }
        Text {
            visible: slider.visible
            text: control.param.type === "int" ? slider.value.toFixed(0) : slider.value.toFixed(2)
            color: Theme.color.textPrimary
            font: Theme.type.mono
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
        onPressedChanged: if (!pressed) control.valueCommitted(value)
    }

    ToggleSwitch {
        visible: control.param.type === "bool"
        text: control.param.title
        checked: control.param.default > 0.5
        onToggled: { control.valueEdited(checked ? 1 : 0); control.valueCommitted(checked ? 1 : 0) }
    }

    ComboBox {
        Layout.fillWidth: true
        visible: control.param.type === "enum"
        model: control.param.values
        currentIndex: control.param.type === "enum" ? control.param.default : 0
        onActivated: (index) => { control.valueEdited(index); control.valueCommitted(index) }
    }

    Row {
        visible: control.param.type === "color"
        spacing: Theme.space.s

        Repeater {
            model: [[0, 0, 0, 1], [1, 1, 1, 1], [0.9, 0.2, 0.2, 1], [0.2, 0.4, 0.95, 1], [1, 0.6, 0.1, 1]]

            delegate: Rectangle {
                required property var modelData
                width: 22
                height: 22
                radius: width / 2
                color: Qt.rgba(modelData[0], modelData[1], modelData[2], 1)
                border.color: Theme.color.borderStrong

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: { control.valueEdited(parent.modelData); control.valueCommitted(parent.modelData) }
                }
            }
        }
    }
}
