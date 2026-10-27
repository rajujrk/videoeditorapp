import QtQuick
import QtQuick.Controls

// Square toolbar button with an icon, hover/pressed/checked states and a tooltip.
AbstractButton {
    id: button
    property string iconName
    property string tip
    property int buttonSize: Theme.size.control

    implicitWidth: buttonSize
    implicitHeight: buttonSize
    hoverEnabled: true
    focusPolicy: Qt.TabFocus

    ToolTip.visible: tip !== "" && hovered
    ToolTip.text: tip
    ToolTip.delay: 600

    background: Rectangle {
        radius: Theme.radius.s
        color: button.checked ? Theme.color.accentSubtle
             : button.pressed ? Theme.color.borderStrong
             : button.hovered && button.enabled ? Theme.color.surfaceHover
             : "transparent"
        border.width: button.visualFocus ? 2 : 0
        border.color: Theme.color.accent
        Behavior on color { ColorAnimation { duration: Theme.motion.fast } }
    }

    contentItem: Item {
        Icon {
            anchors.centerIn: parent
            name: button.iconName
            size: Theme.size.iconSmall + 2
            color: !button.enabled ? Theme.color.textDisabled
                 : button.checked ? Theme.color.accentText
                 : Theme.color.textPrimary
        }
    }
}
