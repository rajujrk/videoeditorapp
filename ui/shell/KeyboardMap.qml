import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import TechieVideoEditor.Theme
import TechieVideoEditor.Player

// The keyboard, drawn: with the modifiers chosen above it (none, ⌘, ⇧, ⌥, ⌃ or any mix),
// each key an action uses is filled and names it. Hover for the whole name; click a key in
// use to give its action another key (`keyClicked`).
ColumnLayout {
    id: map
    spacing: Theme.space.s

    // Chosen modifiers, as KeyMap writes them ("Ctrl" is ⌘ and "Meta" is ⌃ on a Mac).
    property var modifiers: []
    signal keyClicked(string actionId)

    // Each action's keys taken apart: {mods: "Ctrl+Shift", key: "G", id, title}.
    readonly property var bindings: {
        const list = []
        for (const action of KeyMap.actions)
            for (const sequence of action.sequences) {
                const parts = sequence.split("+")
                const key = parts.pop() || "+"
                list.push({ mods: parts.sort().join("+"), key: key, id: action.id, title: action.title })
            }
        return list
    }
    readonly property string chosen: modifiers.slice().sort().join("+")
    function actionsOn(key) { return bindings.filter((b) => b.key === key && b.mods === chosen) }
    readonly property int used: bindings.filter((b) => b.mods === chosen).length

    function toggle(mod) {
        const next = modifiers.slice()
        const at = next.indexOf(mod)
        if (at >= 0)
            next.splice(at, 1)
        else
            next.push(mod)
        modifiers = next
    }

    // A Mac keyboard: [label, key name, width in key units]; then the navigation keys.
    readonly property var rows: [
        [["esc", "Esc", 1.5], ["F1", "F1", 1], ["F2", "F2", 1], ["F3", "F3", 1], ["F4", "F4", 1], ["F5", "F5", 1],
         ["F6", "F6", 1], ["F7", "F7", 1], ["F8", "F8", 1], ["F9", "F9", 1], ["F10", "F10", 1], ["F11", "F11", 1], ["F12", "F12", 1]],
        [["`", "`", 1], ["1", "1", 1], ["2", "2", 1], ["3", "3", 1], ["4", "4", 1], ["5", "5", 1], ["6", "6", 1], ["7", "7", 1],
         ["8", "8", 1], ["9", "9", 1], ["0", "0", 1], ["-", "-", 1], ["=", "=", 1], ["⌫", "Backspace", 1.5]],
        [["⇥", "Tab", 1.5], ["Q", "Q", 1], ["W", "W", 1], ["E", "E", 1], ["R", "R", 1], ["T", "T", 1], ["Y", "Y", 1], ["U", "U", 1],
         ["I", "I", 1], ["O", "O", 1], ["P", "P", 1], ["[", "[", 1], ["]", "]", 1], ["\\", "\\", 1]],
        [["⇪", "", 1.75], ["A", "A", 1], ["S", "S", 1], ["D", "D", 1], ["F", "F", 1], ["G", "G", 1], ["H", "H", 1], ["J", "J", 1],
         ["K", "K", 1], ["L", "L", 1], [";", ";", 1], ["'", "'", 1], ["↩", "Return", 1.75]],
        [["⇧", "", 2.25], ["Z", "Z", 1], ["X", "X", 1], ["C", "C", 1], ["V", "V", 1], ["B", "B", 1], ["N", "N", 1], ["M", "M", 1],
         [",", ",", 1], [".", ".", 1], ["/", "/", 1], ["⇧", "", 2.25]],
        [["fn", "", 1], ["⌃", "", 1], ["⌥", "", 1], ["⌘", "", 1.25], [qsTr("space"), "Space", 5.75], ["⌘", "", 1.25], ["⌥", "", 1.5]]
    ]
    readonly property var navigation: [
        [["⌦", "Del", 1], ["home", "Home", 1], ["pg up", "PgUp", 1]],
        [["", "", 1], ["end", "End", 1], ["pg dn", "PgDown", 1]],
        [["", "", 1], ["", "", 1], ["", "", 1]],
        [["", "", 1], ["↑", "Up", 1], ["", "", 1]],
        [["←", "Left", 1], ["↓", "Down", 1], ["→", "Right", 1]]
    ]
    readonly property real unit: 44

    // Modifier chips, and how many keys are in use with them.
    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.space.xs
        Text { text: qsTr("With"); font: Theme.type.label; color: Theme.color.textSecondary }
        Repeater {
            model: [{ mod: "Ctrl", label: "⌘" }, { mod: "Shift", label: "⇧" }, { mod: "Alt", label: "⌥" }, { mod: "Meta", label: "⌃" }]
            delegate: TextButton {
                required property var modelData
                objectName: "keyboardModifier:" + modelData.mod
                text: modelData.label
                variant: map.modifiers.indexOf(modelData.mod) >= 0 ? "primary" : "secondary"
                onClicked: map.toggle(modelData.mod)
            }
        }
        Text {
            objectName: "keyboardUsed"
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignRight
            text: map.used === 1 ? qsTr("1 shortcut on these keys") : qsTr("%1 shortcuts on these keys").arg(map.used)
            font: Theme.type.caption
            color: Theme.color.textSecondary
        }
    }

    component Key: AbstractButton {
        id: cell
        required property var spec // [label, key name, width]
        readonly property var actions: spec[1] !== "" ? map.actionsOn(spec[1]) : []
        readonly property bool inUse: actions.length > 0
        objectName: spec[1] !== "" ? "keyboardKey:" + spec[1] : ""
        width: map.unit * spec[2] - 4
        height: map.unit - 4
        hoverEnabled: true
        enabled: spec[0] !== "" || spec[1] !== ""
        onClicked: if (inUse) map.keyClicked(actions[0].id)
        ToolTip.visible: hovered && inUse
        ToolTip.delay: 300
        ToolTip.text: actions.map((a) => a.title).join("\n")
        background: Rectangle {
            visible: cell.enabled
            radius: Theme.radius.s
            color: cell.inUse ? (cell.hovered ? Theme.color.accent : Theme.color.accentSubtle) : Theme.color.surface
            border.color: cell.inUse ? Theme.color.accentText : Theme.color.borderSubtle
        }
        contentItem: Column {
            spacing: 0
            Text {
                width: parent.width
                text: cell.spec[0]
                horizontalAlignment: Text.AlignHCenter
                font: Theme.type.caption
                color: cell.inUse && cell.hovered ? Theme.color.textOnAccent
                     : cell.spec[1] === "" ? Theme.color.textDisabled : Theme.color.textPrimary
            }
            Text {
                width: parent.width
                visible: cell.inUse
                text: cell.inUse ? cell.actions[0].title : ""
                elide: Text.ElideRight
                horizontalAlignment: Text.AlignHCenter
                font.pixelSize: 8
                font.family: Theme.type.caption.family
                color: cell.hovered ? Theme.color.textOnAccent : Theme.color.accentText
            }
        }
    }

    Row {
        spacing: map.unit * 0.5
        Column {
            spacing: 0
            Repeater {
                model: map.rows
                delegate: Row {
                    required property var modelData
                    spacing: 0
                    Repeater {
                        model: parent.modelData
                        delegate: Item {
                            required property var modelData
                            width: map.unit * modelData[2]
                            height: map.unit
                            Key { anchors.centerIn: parent; spec: parent.modelData }
                        }
                    }
                }
            }
        }
        Column {
            y: map.unit // beside the number row down
            spacing: 0
            Repeater {
                model: map.navigation
                delegate: Row {
                    required property var modelData
                    spacing: 0
                    Repeater {
                        model: parent.modelData
                        delegate: Item {
                            required property var modelData
                            width: map.unit * modelData[2]
                            height: map.unit
                            Key { anchors.centerIn: parent; spec: parent.modelData }
                        }
                    }
                }
            }
        }
    }
}
