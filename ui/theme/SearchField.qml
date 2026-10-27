import QtQuick
import QtQuick.Controls

TextField {
    id: field
    implicitHeight: Theme.size.control + 4
    leftPadding: Theme.space.s + Theme.size.iconSmall + Theme.space.s
    font: Theme.type.body
    color: Theme.color.textPrimary
    placeholderTextColor: Theme.color.textDisabled
    selectionColor: Theme.color.accent
    selectedTextColor: Theme.color.textOnAccent

    background: Rectangle {
        radius: Theme.radius.m
        color: Theme.color.surfaceRaised
        border.width: field.activeFocus ? 2 : 1
        border.color: field.activeFocus ? Theme.color.accentText : Theme.color.borderSubtle
    }

    Icon {
        anchors.verticalCenter: parent.verticalCenter
        x: Theme.space.s
        name: "search"
        size: Theme.size.iconSmall
        color: Theme.color.textDisabled
    }
}
