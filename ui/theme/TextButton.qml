import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Labelled button. variant: "primary" (accent fill), "secondary" (outlined) or "ghost".
AbstractButton {
    id: button
    property string variant: "secondary"
    property string iconName
    property string tip

    implicitHeight: Theme.size.control + 4
    implicitWidth: row.implicitWidth + Theme.space.l * 2
    hoverEnabled: true
    focusPolicy: Qt.TabFocus

    ToolTip.visible: tip !== "" && hovered
    ToolTip.text: tip
    ToolTip.delay: 600

    readonly property color foreground: !enabled ? Theme.color.textDisabled
                                      : variant === "primary" ? Theme.color.textOnAccent
                                      : Theme.color.textPrimary

    background: Rectangle {
        radius: Theme.radius.m
        color: {
            if (button.variant === "primary") {
                if (!button.enabled) return Theme.color.surfaceHover
                return button.pressed ? Theme.color.accentPressed
                     : button.hovered ? Theme.color.accentHover : Theme.color.accent
            }
            return button.pressed ? Theme.color.borderStrong
                 : button.hovered && button.enabled ? Theme.color.surfaceHover
                 : button.variant === "secondary" ? Theme.color.surfaceRaised : "transparent"
        }
        border.width: button.visualFocus ? 2 : (button.variant === "secondary" ? 1 : 0)
        border.color: button.visualFocus ? Theme.color.accentText : Theme.color.borderStrong
        Behavior on color { ColorAnimation { duration: Theme.motion.fast } }
    }

    contentItem: Item {
        RowLayout {
            id: row
            anchors.centerIn: parent
            spacing: Theme.space.s

            Icon {
                visible: button.iconName !== ""
                name: button.iconName
                size: Theme.size.iconSmall
                color: button.foreground
            }
            Text {
                text: button.text
                font: Theme.type.heading
                color: button.foreground
            }
        }
    }
}
