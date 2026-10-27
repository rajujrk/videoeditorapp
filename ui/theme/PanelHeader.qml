import QtQuick
import QtQuick.Layouts

// Title bar at the top of a panel. Children go to the right-hand side.
Rectangle {
    id: header
    property string title
    default property alias trailing: trailingRow.data

    implicitHeight: Theme.size.toolBar
    color: Theme.color.surface

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.space.m
        anchors.rightMargin: Theme.space.s
        spacing: Theme.space.xs

        Text {
            text: header.title
            font: Theme.type.heading
            color: Theme.color.textPrimary
            elide: Text.ElideRight
            Layout.fillWidth: true
        }
        RowLayout {
            id: trailingRow
            spacing: Theme.space.xs
        }
    }

    Divider {
        anchors.bottom: parent.bottom
        width: parent.width
    }
}
