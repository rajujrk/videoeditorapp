import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import TechieVideoEditor.Theme

// One recent project on the Home screen: its cover (a frame a third of the way in), name,
// when it was last opened, canvas and length. A click opens it; a right-click offers Show in
// Finder and Remove from list. A file no longer there is shown as missing (it can only be
// taken off the list).
AbstractButton {
    id: card
    // An entry of ProjectController.recentProjects.
    required property var entry
    signal openRequested(string path)
    signal revealRequested(string path)
    signal removeRequested(string path)

    readonly property bool missing: entry.missing === true
    objectName: "recentProject:" + entry.path
    width: 220
    implicitHeight: cover.height + details.implicitHeight + Theme.space.s * 2
    hoverEnabled: true
    focusPolicy: Qt.TabFocus
    onClicked: if (!missing) card.openRequested(entry.path)

    // Minutes and seconds (hours too when it is that long).
    function length(seconds) {
        const s = Math.max(0, Math.round(seconds || 0))
        const pad = (n) => (n < 10 ? "0" : "") + n
        return s >= 3600 ? Math.floor(s / 3600) + ":" + pad(Math.floor(s / 60) % 60) + ":" + pad(s % 60)
                         : Math.floor(s / 60) + ":" + pad(s % 60)
    }

    ToolTip {
        text: card.missing ? qsTr("%1 is no longer where it was").arg(card.entry.path) : card.entry.path
        visible: card.hovered
        delay: 800
    }

    background: Rectangle {
        radius: Theme.radius.l
        color: card.hovered ? Theme.color.surfaceHover : Theme.color.surface
        border.color: card.visualFocus || (card.hovered && !card.missing) ? Theme.color.accent : Theme.color.borderSubtle
        Behavior on color { ColorAnimation { duration: Theme.motion.fast } }
    }

    contentItem: Item {
        Rectangle {
            id: cover
            objectName: "recentCover"
            x: 1
            y: 1
            width: card.width - 2
            height: Math.round(width * 9 / 16)
            topLeftRadius: Theme.radius.l - 1
            topRightRadius: Theme.radius.l - 1
            color: Theme.color.surfaceRaised
            clip: true

            Image {
                id: frame
                anchors.fill: parent
                source: card.entry.cover || ""
                visible: status === Image.Ready
                fillMode: Image.PreserveAspectFit
                asynchronous: true
                cache: false
                opacity: card.missing ? 0.35 : 1
            }
            Icon {
                anchors.centerIn: parent
                visible: frame.status !== Image.Ready
                name: "timeline"
                size: 28
                color: Theme.color.textDisabled
            }
            // Missing: the file was moved, renamed or deleted since.
            Rectangle {
                visible: card.missing
                objectName: "recentMissing"
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.margins: Theme.space.s
                width: missingText.implicitWidth + Theme.space.s * 2
                height: missingText.implicitHeight + Theme.space.xxs * 2
                radius: Theme.radius.s
                color: Theme.color.warning
                Text {
                    id: missingText
                    anchors.centerIn: parent
                    text: qsTr("Missing")
                    font: Theme.type.caption
                    color: "#1A1A1A"
                }
            }
        }

        ColumnLayout {
            id: details
            anchors.top: cover.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.margins: Theme.space.m
            anchors.topMargin: Theme.space.s
            spacing: Theme.space.xxs
            Text {
                objectName: "recentName"
                Layout.fillWidth: true
                text: card.entry.name
                font: Theme.type.heading
                color: card.missing ? Theme.color.textSecondary : Theme.color.textPrimary
                elide: Text.ElideRight
            }
            Text {
                Layout.fillWidth: true
                text: Fmt.dateTime(card.entry.lastOpened)
                font: Theme.type.caption
                color: Theme.color.textSecondary
                elide: Text.ElideRight
            }
            Text {
                Layout.fillWidth: true
                text: card.entry.duration > 0
                      ? qsTr("%1 × %2 · %3").arg(card.entry.width).arg(card.entry.height).arg(card.length(card.entry.duration))
                      : qsTr("%1 × %2 · empty").arg(card.entry.width).arg(card.entry.height)
                font: Theme.type.caption
                color: Theme.color.textSecondary
                elide: Text.ElideRight
            }
        }
    }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.RightButton
        onClicked: (mouse) => menu.popup(mouse.x, mouse.y)
    }
    AppMenu {
        id: menu
        objectName: "recentMenu"
        AppMenuItem {
            objectName: "recentOpen"
            text: qsTr("Open")
            enabled: !card.missing
            onTriggered: card.openRequested(card.entry.path)
        }
        AppMenuItem {
            objectName: "recentReveal"
            text: qsTr("Show in Finder")
            enabled: !card.missing
            onTriggered: card.revealRequested(card.entry.path)
        }
        AppMenuSeparator {}
        AppMenuItem {
            objectName: "recentRemove"
            text: qsTr("Remove from list")
            onTriggered: card.removeRequested(card.entry.path)
        }
    }
}
