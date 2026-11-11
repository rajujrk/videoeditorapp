import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import TechieVideoEditor.Theme

// Asks before something that takes work away (deleting a track with clips on it, media the
// timeline uses): ask(title, message, button, action) runs `action` on the button.
Popup {
    id: dialog
    property string title
    property string message
    property string button
    property var action: null

    function ask(title, message, button, action) {
        dialog.title = title
        dialog.message = message
        dialog.button = button
        dialog.action = action
        open()
    }

    modal: true
    focus: true
    anchors.centerIn: Overlay.overlay
    width: 420
    padding: Theme.space.l
    Overlay.modal: Rectangle { color: Qt.alpha("#000000", Theme.dark ? 0.55 : 0.35) }
    background: Rectangle {
        radius: Theme.radius.l
        color: Theme.color.surfaceRaised
        border.color: Theme.color.borderStrong
    }

    contentItem: ColumnLayout {
        spacing: Theme.space.m
        Text { text: dialog.title; font: Theme.type.title; color: Theme.color.textPrimary }
        Text {
            Layout.fillWidth: true
            text: dialog.message
            font: Theme.type.body
            color: Theme.color.textSecondary
            wrapMode: Text.WordWrap
        }
        RowLayout {
            Layout.alignment: Qt.AlignRight
            spacing: Theme.space.s
            TextButton { objectName: "confirmCancel"; text: qsTr("Cancel"); onClicked: dialog.close() }
            TextButton {
                objectName: "confirmAccept"
                text: dialog.button
                variant: "primary"
                onClicked: {
                    const run = dialog.action
                    dialog.close()
                    if (run)
                        run()
                }
            }
        }
    }
}
