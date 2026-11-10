import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Labelled button. variant: "primary" (accent fill), "secondary" (outlined), "ghost", or
// "danger" (a red fill, for what can't be undone). Squeezed narrower than its label (a row of
// buttons, a longer language), the label ends in "…" and the tooltip shows all of it.
AbstractButton {
    id: button
    property string variant: "secondary"
    property string iconName
    property string tip

    implicitHeight: Theme.size.control + 4
    implicitWidth: row.implicitWidth + Theme.space.l * 2
    hoverEnabled: true
    focusPolicy: Qt.TabFocus

    // Each button owns its tooltip. (Qt's attached ToolTip is one shared popup: if its
    // button is destroyed while showing it, it stays open and swallows the next click.)
    ToolTip {
        text: button.tip !== "" ? button.tip : button.text
        visible: (button.tip !== "" || label.truncated) && button.hovered
        delay: 600
    }

    readonly property color foreground: !enabled ? Theme.color.textDisabled
                                      : variant === "primary" || variant === "danger" ? Theme.color.textOnAccent
                                      : Theme.color.textPrimary

    background: Rectangle {
        radius: Theme.radius.m
        color: {
            if (button.variant === "danger") {
                if (!button.enabled) return Theme.color.surfaceHover
                return button.pressed || button.hovered ? Qt.darker(Theme.color.danger, 1.15) : Theme.color.danger
            }
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
            width: Math.min(Math.ceil(implicitWidth), parent.width - Theme.space.s * 2)
            spacing: Theme.space.s

            Icon {
                visible: button.iconName !== ""
                name: button.iconName
                size: Theme.size.iconSmall
                color: button.foreground
            }
            Text {
                id: label
                Layout.fillWidth: true
                Layout.maximumWidth: Math.ceil(implicitWidth)
                text: button.text
                font: Theme.type.heading
                color: button.foreground
                elide: Text.ElideRight
            }
        }
    }
}
