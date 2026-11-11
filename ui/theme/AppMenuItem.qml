import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// One entry of an AppMenu: its text, a check mark when checkable, its shortcut on the right,
// an arrow when it opens a submenu. Hidden entries take no room.
MenuItem {
    id: entry
    property string shortcut: ""

    implicitHeight: visible ? 30 : 0
    implicitWidth: row.implicitWidth + leftPadding + rightPadding
    leftPadding: Theme.space.s
    rightPadding: Theme.space.s
    indicator: null
    arrow: null

    contentItem: RowLayout {
        id: row
        spacing: Theme.space.s
        readonly property color ink: !entry.enabled ? Theme.color.textDisabled
                                     : entry.highlighted ? "#FFFFFF" : Theme.color.textPrimary
        Text {
            Layout.preferredWidth: 14
            text: entry.checkable && entry.checked ? "✓" : ""
            font: Theme.type.body
            color: row.ink
        }
        Text {
            Layout.fillWidth: true
            text: entry.text
            font: Theme.type.body
            color: row.ink
            elide: Text.ElideRight
        }
        Text {
            visible: entry.shortcut !== ""
            Layout.leftMargin: Theme.space.l
            text: entry.shortcut
            font: Theme.type.caption
            color: entry.highlighted && entry.enabled ? "#FFFFFF" : Theme.color.textSecondary
        }
        Text {
            visible: entry.subMenu !== null
            text: "›"
            font: Theme.type.body
            color: row.ink
        }
    }
    background: Rectangle {
        radius: Theme.radius.s
        color: entry.highlighted && entry.enabled ? Theme.color.accent : "transparent"
    }
}
