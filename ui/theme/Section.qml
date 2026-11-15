import QtQuick
import QtQuick.Layouts

// Collapsible inspector section: a header row with a chevron, then its content.
ColumnLayout {
    id: section
    property string title
    property bool expanded: true
    default property alias content: body.data

    spacing: 0

    Rectangle {
        Layout.fillWidth: true
        implicitHeight: Theme.size.control + Theme.space.s
        color: mouse.containsMouse ? Theme.color.surfaceHover : "transparent"
        radius: Theme.radius.s
        Behavior on color { ColorAnimation { duration: Theme.motion.fast } }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: Theme.space.xs
            spacing: Theme.space.xs

            // One chevron, turning as the section opens and closes.
            Icon {
                name: "chevron-right"
                size: Theme.size.iconSmall
                rotation: section.expanded ? 90 : 0
                Behavior on rotation { NumberAnimation { duration: Theme.motion.normal; easing.type: Easing.OutCubic } }
            }
            Text {
                text: section.title
                font: Theme.type.heading
                color: Theme.color.textPrimary
                Layout.fillWidth: true
            }
        }

        MouseArea {
            id: mouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: section.expanded = !section.expanded
        }
    }

    ColumnLayout {
        id: body
        visible: section.expanded
        // Its content fades in as it opens.
        opacity: section.expanded ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: Theme.motion.normal; easing.type: Easing.OutCubic } }
        Layout.fillWidth: true
        Layout.leftMargin: Theme.space.l
        Layout.rightMargin: Theme.space.xs
        Layout.topMargin: Theme.space.xs
        Layout.bottomMargin: Theme.space.m
        spacing: Theme.space.s
    }
}
