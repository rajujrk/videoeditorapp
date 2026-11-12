import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import TechieVideoEditor.Theme
import TechieVideoEditor.Player

// Keyboard shortcuts: every editor action with its key, by group, or drawn on a keyboard.
// Click a key, then press the new one (Esc cancels); a key another action has asks before it
// moves. A preset (this app's keys, Premiere Pro's or Final Cut Pro's) sets them all; a file
// takes them to another Mac and back. Changes save at once (KeyMap's keymap file).
Popup {
    id: dialog

    property string recording       // the action waiting for a key press
    property string pendingKey      // a key in use elsewhere, waiting for Replace
    property string pendingOwner
    property string filter
    property string view: "list" // or "keyboard"
    property string notice // what an import or export did

    modal: true
    focus: true
    anchors.centerIn: Overlay.overlay
    width: view === "keyboard" ? 900 : 560
    padding: Theme.space.l
    closePolicy: recording !== "" ? Popup.NoAutoClose : Popup.CloseOnEscape | Popup.CloseOnPressOutside
    onAboutToShow: {
        recording = ""
        pendingKey = ""
        filter = ""
        search.text = ""
        notice = ""
        view = "list"
    }

    readonly property var groups: [QT_TR_NOOP("Playback"), QT_TR_NOOP("Editing"), QT_TR_NOOP("Structure"), QT_TR_NOOP("File")]
    function titleOf(id) {
        const found = KeyMap.actions.find((a) => a.id === id)
        return found ? found.title : id
    }
    function startRecording(id) {
        recording = id
        pendingKey = ""
        catcher.forceActiveFocus()
    }
    function pressed(key, modifiers) {
        if (key === Qt.Key_Escape && modifiers === Qt.NoModifier) {
            recording = ""
            return
        }
        const sequence = KeyMap.sequenceFor(key, modifiers)
        if (sequence === "")
            return // a modifier on its own: wait for the rest
        const owner = KeyMap.conflict(sequence, recording)
        if (owner !== "") {
            pendingKey = sequence
            pendingOwner = owner
            return
        }
        KeyMap.assign(recording, sequence)
        recording = ""
    }
    function replace() {
        KeyMap.assign(recording, pendingKey)
        recording = ""
        pendingKey = ""
    }

    Overlay.modal: Rectangle { color: Qt.alpha("#000000", Theme.dark ? 0.55 : 0.35) }
    background: Rectangle {
        radius: Theme.radius.l
        color: Theme.color.surfaceRaised
        border.color: Theme.color.borderStrong
    }

    // Takes the key presses while recording, before the editor's shortcuts see them.
    Item {
        id: catcher
        objectName: "shortcutCatcher"
        Keys.onShortcutOverride: (event) => event.accepted = dialog.recording !== ""
        Keys.onPressed: (event) => {
            if (dialog.recording === "")
                return
            event.accepted = true
            dialog.pressed(event.key, event.modifiers)
        }
    }

    FileDialog {
        id: exportDialog
        title: qsTr("Export the shortcuts")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "json"
        nameFilters: [qsTr("Shortcuts (*.json)")]
        currentFile: "file:///" + qsTr("TechieVideoEditor shortcuts") + ".json"
        onAccepted: if (KeyMap.exportTo(selectedFile)) dialog.notice = qsTr("Saved the shortcuts.")
    }
    FileDialog {
        id: importDialog
        title: qsTr("Import shortcuts")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("Shortcuts (*.json)")]
        onAccepted: dialog.importFile(selectedFile)
    }
    function importFile(url) {
        const named = KeyMap.importFrom(url)
        notice = named < 0 ? "" : named === 1 ? qsTr("Imported the keys of 1 action.")
                                              : qsTr("Imported the keys of %1 actions.").arg(named)
        return named
    }

    contentItem: ColumnLayout {
        spacing: Theme.space.m

        Text {
            text: qsTr("Keyboard shortcuts")
            font: Theme.type.title
            color: Theme.color.textPrimary
        }
        Text {
            Layout.fillWidth: true
            text: qsTr("Click a shortcut, then press the new keys (Esc cancels). Changes are kept for every project.")
            font: Theme.type.body
            color: Theme.color.textSecondary
            wrapMode: Text.WordWrap
        }
        // A ready-made set, and files to carry them.
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.space.s
            Text { text: qsTr("Keys"); font: Theme.type.label; color: Theme.color.textSecondary }
            ComboBox {
                id: presetPicker
                objectName: "shortcutPreset"
                Layout.fillWidth: true
                model: KeyMap.presets
                textRole: "title"
                // Changed from every preset: "Custom".
                currentIndex: KeyMap.presets.findIndex((p) => p.id === KeyMap.preset)
                displayText: currentIndex >= 0 ? currentText : qsTr("Custom")
                onActivated: (index) => {
                    KeyMap.applyPreset(KeyMap.presets[index].id)
                    dialog.notice = ""
                }
                ToolTip.visible: hovered && currentIndex >= 0
                ToolTip.delay: 600
                ToolTip.text: currentIndex >= 0 ? KeyMap.presets[currentIndex].description : ""
            }
            TextButton {
                objectName: "shortcutsImport"
                text: qsTr("Import…")
                variant: "secondary"
                onClicked: importDialog.open()
            }
            TextButton {
                objectName: "shortcutsExport"
                text: qsTr("Export…")
                variant: "secondary"
                onClicked: exportDialog.open()
            }
        }
        Text {
            objectName: "shortcutPresetDescription"
            visible: presetPicker.currentIndex >= 0
            Layout.fillWidth: true
            text: presetPicker.currentIndex >= 0 ? KeyMap.presets[presetPicker.currentIndex].description : ""
            font: Theme.type.caption
            color: Theme.color.textSecondary
            wrapMode: Text.WordWrap
        }
        Text {
            objectName: "shortcutsNotice"
            visible: dialog.notice !== ""
            Layout.fillWidth: true
            text: dialog.notice
            font: Theme.type.caption
            color: Theme.color.success
            wrapMode: Text.WordWrap
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.space.s
            SearchField {
                id: search
                objectName: "shortcutSearch"
                visible: dialog.view === "list"
                Layout.fillWidth: true
                placeholderText: qsTr("Search actions")
                onTextChanged: dialog.filter = text.toLowerCase()
            }
            Item { visible: dialog.view !== "list"; Layout.fillWidth: true }
            TextButton {
                objectName: "shortcutsList"
                text: qsTr("List")
                variant: dialog.view === "list" ? "primary" : "secondary"
                onClicked: dialog.view = "list"
            }
            TextButton {
                objectName: "shortcutsKeyboard"
                text: qsTr("Keyboard")
                iconName: "keyboard"
                variant: dialog.view === "keyboard" ? "primary" : "secondary"
                onClicked: dialog.view = "keyboard"
            }
        }

        // A key in use by another action: move it here, or not.
        Rectangle {
            visible: dialog.pendingKey !== ""
            Layout.fillWidth: true
            implicitHeight: conflictRow.implicitHeight + Theme.space.s * 2
            radius: Theme.radius.m
            color: Qt.alpha(Theme.color.warning, Theme.dark ? 0.18 : 0.14)
            RowLayout {
                id: conflictRow
                anchors.fill: parent
                anchors.margins: Theme.space.s
                Text {
                    objectName: "shortcutConflict"
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1 // wraps in what the buttons leave
                    text: qsTr("%1 is used by “%2”. Move it to “%3”?").arg(KeyMap.keyText(dialog.pendingKey))
                          .arg(dialog.titleOf(dialog.pendingOwner)).arg(dialog.titleOf(dialog.recording))
                    font: Theme.type.body
                    color: Theme.color.textPrimary
                    wrapMode: Text.WordWrap
                }
                TextButton {
                    objectName: "shortcutReplace"
                    text: qsTr("Replace")
                    variant: "primary"
                    onClicked: dialog.replace()
                }
                TextButton {
                    text: qsTr("Cancel")
                    variant: "secondary"
                    onClicked: {
                        dialog.pendingKey = ""
                        dialog.recording = ""
                    }
                }
            }
        }
        Text {
            visible: KeyMap.lastError !== ""
            Layout.fillWidth: true
            text: KeyMap.lastError
            font: Theme.type.body
            color: Theme.color.danger
            wrapMode: Text.WordWrap
        }

        KeyboardMap {
            id: keyboard
            objectName: "keyboardMap"
            visible: dialog.view === "keyboard"
            Layout.fillWidth: true
            onKeyClicked: (id) => dialog.startRecording(id)
        }
        Text {
            visible: dialog.view === "keyboard" && dialog.recording !== ""
            Layout.fillWidth: true
            text: qsTr("Press the new keys for “%1” (Esc cancels)").arg(dialog.titleOf(dialog.recording))
            font: Theme.type.body
            color: Theme.color.accentText
        }

        Flickable {
            visible: dialog.view === "list"
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(contentHeight, 420)
            contentHeight: groupsColumn.implicitHeight
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}

            ColumnLayout {
                id: groupsColumn
                width: parent.width
                spacing: Theme.space.m

                Repeater {
                    model: dialog.groups
                    delegate: ColumnLayout {
                        id: group
                        required property string modelData
                        readonly property var rows: KeyMap.actions.filter((a) => a.group === modelData
                                                    && (dialog.filter === "" || a.title.toLowerCase().indexOf(dialog.filter) >= 0))
                        visible: rows.length > 0
                        Layout.fillWidth: true
                        spacing: Theme.space.xxs

                        Text {
                            text: qsTr(group.modelData)
                            font: Theme.type.label
                            color: Theme.color.textSecondary
                        }
                        Repeater {
                            model: group.rows
                            delegate: RowLayout {
                                id: row
                                required property var modelData
                                readonly property bool listening: dialog.recording === modelData.id
                                Layout.fillWidth: true
                                spacing: Theme.space.xs
                                HoverHandler { id: rowHover }

                                Text {
                                    Layout.fillWidth: true
                                    text: row.modelData.title
                                    font: Theme.type.body
                                    color: Theme.color.textPrimary
                                }
                                IconButton {
                                    objectName: "shortcutReset:" + row.modelData.id
                                    visible: row.modelData.custom
                                    iconName: "undo"
                                    tip: row.modelData.defaults.length ? qsTr("Back to %1").arg(row.modelData.defaults.join(qsTr(" or "))) : qsTr("Back to none")
                                    onClicked: KeyMap.reset(row.modelData.id)
                                }
                                IconButton {
                                    objectName: "shortcutClear:" + row.modelData.id
                                    // Only on the row under the pointer: a column of crosses would crowd the list.
                                    visible: row.modelData.sequences.length > 0 && rowHover.hovered
                                    iconName: "close"
                                    tip: qsTr("No shortcut")
                                    onClicked: KeyMap.clear(row.modelData.id)
                                }
                                AbstractButton {
                                    objectName: "shortcutKey:" + row.modelData.id
                                    implicitWidth: Math.max(96, keyText.implicitWidth + Theme.space.m * 2)
                                    implicitHeight: Theme.size.control
                                    hoverEnabled: true
                                    onClicked: dialog.startRecording(row.modelData.id)
                                    background: Rectangle {
                                        radius: Theme.radius.s
                                        color: row.listening ? Theme.color.accentSubtle
                                             : parent.hovered ? Theme.color.surfaceHover : Theme.color.surface
                                        border.width: row.listening ? 2 : 1
                                        border.color: row.listening ? Theme.color.accentText : Theme.color.borderSubtle
                                    }
                                    contentItem: Text {
                                        id: keyText
                                        text: row.listening ? qsTr("Press keys…")
                                            : row.modelData.sequences.length ? row.modelData.text
                                              + (row.modelData.sequences.length > 1 ? " +" + (row.modelData.sequences.length - 1) : "")
                                            : qsTr("None")
                                        font: Theme.type.body // the key symbols (⌘ ⇧ ⌥) draw small in the mono face
                                        color: row.modelData.sequences.length || row.listening ? Theme.color.textPrimary
                                                                                              : Theme.color.textDisabled
                                        horizontalAlignment: Text.AlignHCenter
                                        verticalAlignment: Text.AlignVCenter
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            TextButton {
                objectName: "shortcutsResetAll"
                text: qsTr("Reset all")
                variant: "ghost"
                enabled: KeyMap.actions.some((a) => a.custom)
                onClicked: KeyMap.resetAll()
            }
            Item { Layout.fillWidth: true }
            TextButton {
                objectName: "shortcutsDone"
                text: qsTr("Done")
                variant: "primary"
                enabled: dialog.recording === ""
                onClicked: dialog.close()
            }
        }
    }
}
