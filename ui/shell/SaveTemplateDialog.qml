import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import TechieVideoEditor.Theme

// Save this project as a template: a name and category, which clips are replaceable slots
// (pictures and videos), which titles stay editable, and which sound is the music.
Popup {
    id: dialog
    required property var project
    property var candidates: []
    property var slotIds: []
    property var textIds: []
    property double musicId: -1
    property string saved: ""

    function toggle(list, id) {
        const next = list.slice()
        const at = next.indexOf(id)
        if (at >= 0)
            next.splice(at, 1)
        else
            next.push(id)
        return next
    }
    onAboutToShow: {
        candidates = project.templateCandidates()
        // Suggested: every picture, every title, the longest sound.
        slotIds = candidates.filter((c) => c.kind === "picture").map((c) => c.clipId)
        textIds = candidates.filter((c) => c.kind === "title").map((c) => c.clipId)
        const sounds = candidates.filter((c) => c.kind === "sound").sort((a, b) => b.duration - a.duration)
        musicId = sounds.length > 0 ? sounds[0].clipId : -1
        name.text = ""
        saved = ""
    }

    parent: Overlay.overlay
    anchors.centerIn: parent
    modal: true
    width: 520
    height: Math.min(implicitHeight, (parent ? parent.height : 800) - Theme.space.xl * 2)
    padding: Theme.space.l
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    background: Rectangle {
        radius: Theme.radius.l
        color: Theme.color.surfaceRaised
        border.color: Theme.color.borderStrong
    }

    contentItem: ColumnLayout {
        spacing: Theme.space.s
        RowLayout {
            Layout.fillWidth: true
            Text { Layout.fillWidth: true; text: qsTr("Save as template"); font: Theme.type.title; color: Theme.color.textPrimary }
            IconButton { iconName: "close"; tip: qsTr("Close"); onClicked: dialog.close() }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.space.s
            TextField {
                id: name
                objectName: "templateName"
                Layout.fillWidth: true
                placeholderText: qsTr("Template name")
                color: Theme.color.textPrimary
                placeholderTextColor: Theme.color.textDisabled
                selectByMouse: true
                background: Rectangle {
                    implicitHeight: 32
                    radius: Theme.radius.s
                    color: Theme.color.surfaceSunken
                    border.color: parent.activeFocus ? Theme.color.accent : Theme.color.borderSubtle
                }
            }
            ComboBox {
                id: category
                objectName: "templateCategory"
                model: dialog.project.templates.categories.filter((c) => c.id !== "" && c.id !== "favorites")
                textRole: "label"
                valueRole: "id"
                Component.onCompleted: currentIndex = model.length - 1 // Mine
            }
        }
        Text {
            Layout.fillWidth: true
            Layout.preferredWidth: 1
            text: qsTr("Replaceable clips become numbered slots; their media is left out. Everything else the project uses is copied into the template.")
            font: Theme.type.caption
            color: Theme.color.textSecondary
            wrapMode: Text.WordWrap
        }
        ScrollView {
            id: scroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredHeight: Math.min(rows.implicitHeight, 360)
            contentWidth: availableWidth
            clip: true
            ColumnLayout {
                id: rows
                width: scroll.availableWidth
                spacing: 2
                Repeater {
                    model: dialog.candidates
                    delegate: RowLayout {
                        id: row
                        required property var modelData
                        Layout.fillWidth: true
                        spacing: Theme.space.s
                        CheckBox {
                            objectName: "mark:" + row.modelData.clipId
                            checked: row.modelData.kind === "picture" ? dialog.slotIds.indexOf(row.modelData.clipId) >= 0
                                   : row.modelData.kind === "title" ? dialog.textIds.indexOf(row.modelData.clipId) >= 0
                                   : dialog.musicId === row.modelData.clipId
                            onClicked: {
                                if (row.modelData.kind === "picture")
                                    dialog.slotIds = dialog.toggle(dialog.slotIds, row.modelData.clipId)
                                else if (row.modelData.kind === "title")
                                    dialog.textIds = dialog.toggle(dialog.textIds, row.modelData.clipId)
                                else
                                    dialog.musicId = dialog.musicId === row.modelData.clipId ? -1 : row.modelData.clipId
                            }
                        }
                        Text {
                            Layout.preferredWidth: 90
                            text: row.modelData.kind === "picture" ? qsTr("Replaceable") : row.modelData.kind === "title" ? qsTr("Editable") : qsTr("Music")
                            font: Theme.type.caption
                            color: Theme.color.textSecondary
                        }
                        Text {
                            Layout.fillWidth: true
                            text: row.modelData.label + "  ·  " + row.modelData.track + " · " + Fmt.seconds(row.modelData.start)
                            font: Theme.type.label
                            color: Theme.color.textPrimary
                            elide: Text.ElideRight
                        }
                    }
                }
            }
        }
        Text {
            objectName: "saveTemplateMessage"
            visible: dialog.saved !== ""
            text: qsTr("Saved: it is under Mine in Templates.")
            font: Theme.type.label
            color: Theme.color.textSecondary
        }
        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            TextButton {
                objectName: "saveTemplate"
                text: qsTr("Save template")
                variant: "primary"
                enabled: dialog.slotIds.length > 0
                onClicked: dialog.saved = dialog.project.saveAsTemplate(name.text, category.currentValue, dialog.slotIds,
                                                                        dialog.textIds, dialog.musicId)
            }
        }
    }
}
