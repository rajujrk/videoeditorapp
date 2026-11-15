import QtQuick
import QtQuick.Controls

// A drop-down in the app's look: an input-coloured box with the choice and a chevron, and a
// list that opens like an AppMenu (a raised surface, the current choice ticked, the one under
// the pointer in the accent colour). Everything else is a plain ComboBox: model, textRole,
// valueRole, displayText, currentIndex and activated work as usual.
ComboBox {
    id: control

    implicitHeight: 32
    leftPadding: Theme.space.s + Theme.space.xxs
    rightPadding: Theme.space.s + Theme.size.iconSmall + Theme.space.xs
    font: Theme.type.body

    contentItem: Text {
        text: control.displayText
        font: control.font
        color: control.enabled ? Theme.color.textPrimary : Theme.color.textDisabled
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    indicator: Icon {
        x: control.width - width - Theme.space.s
        y: (control.height - height) / 2
        name: "chevron-down"
        size: Theme.size.iconSmall
        color: control.enabled ? Theme.color.textSecondary : Theme.color.textDisabled
        rotation: control.popup.visible ? 180 : 0
        Behavior on rotation { NumberAnimation { duration: Theme.motion.fast; easing.type: Easing.OutCubic } }
    }

    background: Rectangle {
        implicitWidth: 140
        implicitHeight: 32
        radius: Theme.radius.m
        color: control.down || control.popup.visible ? Theme.color.surfaceHover
             : control.hovered && control.enabled ? Theme.color.surfaceHover : Theme.color.surfaceRaised
        border.color: control.visualFocus || control.popup.visible ? Theme.color.accent : Theme.color.borderSubtle
        border.width: 1
        opacity: control.enabled ? 1 : 0.6
        Behavior on color { ColorAnimation { duration: Theme.motion.fast } }
        Behavior on border.color { ColorAnimation { duration: Theme.motion.fast } }
    }

    delegate: ItemDelegate {
        id: row
        required property int index
        width: ListView.view ? ListView.view.width : implicitWidth
        implicitHeight: 30
        leftPadding: Theme.space.s
        rightPadding: Theme.space.s
        highlighted: control.highlightedIndex === index
        hoverEnabled: control.hoverEnabled
        contentItem: Row {
            spacing: Theme.space.xs
            readonly property color ink: row.highlighted ? Theme.color.textOnAccent : Theme.color.textPrimary
            Text {
                width: 14
                anchors.verticalCenter: parent.verticalCenter
                text: control.currentIndex === row.index ? "✓" : ""
                font: Theme.type.body
                color: parent.ink
            }
            Text {
                width: row.availableWidth - 14 - Theme.space.xs
                anchors.verticalCenter: parent.verticalCenter
                text: control.textAt(row.index)
                font: Theme.type.body
                color: parent.ink
                elide: Text.ElideRight
            }
        }
        background: Rectangle {
            radius: Theme.radius.s
            color: row.highlighted ? Theme.color.accent : "transparent"
            Behavior on color { ColorAnimation { duration: Theme.motion.fast } }
        }
    }

    popup: Popup {
        y: control.height + Theme.space.xxs
        width: Math.max(control.width, 160)
        implicitHeight: Math.min(contentItem.implicitHeight + topPadding + bottomPadding, 360)
        padding: Theme.space.xs
        // Opens with a quick fade and grow, like the menus.
        enter: Transition {
            ParallelAnimation {
                NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.motion.fast; easing.type: Easing.OutCubic }
                NumberAnimation { property: "scale"; from: 0.97; to: 1; duration: Theme.motion.fast; easing.type: Easing.OutCubic }
            }
        }
        exit: Transition {
            NumberAnimation { property: "opacity"; to: 0; duration: Theme.motion.fast }
        }
        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: control.popup.visible ? control.delegateModel : null
            currentIndex: control.highlightedIndex
            boundsBehavior: Flickable.StopAtBounds
            ScrollIndicator.vertical: ScrollIndicator {}
        }
        background: Rectangle {
            radius: Theme.radius.m
            color: Theme.color.surfaceRaised
            border.color: Theme.color.borderStrong
        }
    }
}
