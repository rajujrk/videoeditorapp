import QtQuick
import QtQuick.Controls.Basic

// On/off switch with an optional label, in theme colours (the Basic style's are fixed).
Switch {
    id: control

    spacing: Theme.space.s
    padding: 0
    // The label's left padding already makes room for the indicator; without a label the
    // indicator alone sets the size.
    implicitWidth: Math.max(implicitIndicatorWidth, implicitContentWidth) + leftPadding + rightPadding
    implicitHeight: Math.max(implicitIndicatorHeight, implicitContentHeight) + topPadding + bottomPadding

    indicator: Rectangle {
        implicitWidth: 32
        implicitHeight: 18
        x: control.leftPadding
        y: (control.height - height) / 2
        radius: height / 2
        color: control.checked ? (control.enabled ? Theme.color.accent : Theme.color.borderStrong)
                               : Theme.color.surfaceSunken
        border.color: control.checked ? "transparent" : Theme.color.borderStrong
        Behavior on color { ColorAnimation { duration: Theme.motion.fast } }

        Rectangle {
            width: 14
            height: 14
            radius: 7
            y: 2
            x: control.checked ? parent.width - width - 2 : 2
            color: control.checked ? Theme.color.textOnAccent : Theme.color.textSecondary
            Behavior on x { NumberAnimation { duration: Theme.motion.fast } }
        }
    }

    contentItem: Text {
        leftPadding: control.text === "" ? 0 : control.indicator.width + control.spacing
        text: control.text
        font: Theme.type.label
        color: control.enabled ? Theme.color.textPrimary : Theme.color.textDisabled
        verticalAlignment: Text.AlignVCenter
    }
}
