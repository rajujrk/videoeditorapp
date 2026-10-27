import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import TechieVideoEditor.Theme

// One effect in the inspector's stack: on/off switch, then its manifest-driven controls.
Rectangle {
    id: card
    required property var player
    required property var effect

    readonly property bool active: player.enabledEffectCount >= 0 && player.isEffectEnabled(effect.id)

    implicitHeight: column.implicitHeight + Theme.space.m
    radius: Theme.radius.m
    color: active ? Theme.color.surfaceRaised : "transparent"
    border.color: active ? Theme.color.accent : Theme.color.borderSubtle

    ColumnLayout {
        id: column
        anchors { left: parent.left; right: parent.right; top: parent.top; margins: Theme.space.s }
        spacing: Theme.space.xs

        RowLayout {
            Layout.fillWidth: true
            Icon { name: "effects"; size: Theme.size.iconSmall; color: card.active ? Theme.color.accentText : Theme.color.textSecondary }
            Text {
                text: card.effect.title
                font: Theme.type.heading
                color: Theme.color.textPrimary
                Layout.fillWidth: true
            }
            Switch {
                objectName: "effect:" + card.effect.id
                checked: card.active
                onToggled: card.player.setEffectEnabled(card.effect.id, checked)
            }
        }

        Repeater {
            model: card.active ? card.effect.params : []
            delegate: ParamControl {
                required property var modelData
                Layout.fillWidth: true
                param: modelData
                onValueEdited: (value) => card.player.setEffectParam(card.effect.id, modelData.id, value)
            }
        }
    }
}
