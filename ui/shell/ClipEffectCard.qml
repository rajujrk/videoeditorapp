import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import TechieVideoEditor.Theme

// One effect in the selected clip's stack: on/off, remove, and its manifest-driven
// controls. Dragging previews live; releasing commits one undo step.
Rectangle {
    id: card
    required property var project
    required property double clipId
    required property int stackIndex
    required property var entry // {effectId, enabled, values}
    required property var spec  // the effect's manifest summary from availableEffects

    implicitHeight: column.implicitHeight + Theme.space.m
    radius: Theme.radius.m
    color: entry.enabled ? Theme.color.surfaceRaised : "transparent"
    border.color: entry.enabled ? Theme.color.accent : Theme.color.borderSubtle

    ColumnLayout {
        id: column
        anchors { left: parent.left; right: parent.right; top: parent.top; margins: Theme.space.s }
        spacing: Theme.space.xs

        RowLayout {
            Layout.fillWidth: true
            Icon { name: "effects"; size: Theme.size.iconSmall; color: card.entry.enabled ? Theme.color.accentText : Theme.color.textSecondary }
            Text {
                text: card.spec.title
                font: Theme.type.heading
                color: Theme.color.textPrimary
                Layout.fillWidth: true
                elide: Text.ElideRight
            }
            ToggleSwitch {
                objectName: "clipEffect:" + card.stackIndex
                checked: card.entry.enabled
                onToggled: card.project.setClipEffectEnabled(card.clipId, card.stackIndex, checked)
            }
            IconButton {
                objectName: "removeEffect:" + card.stackIndex
                iconName: "delete"
                tip: "Remove effect"
                buttonSize: 24
                onClicked: card.project.removeClipEffect(card.clipId, card.stackIndex)
            }
        }

        Repeater {
            model: card.entry.enabled ? card.spec.params : []
            delegate: ParamControl {
                required property var modelData
                Layout.fillWidth: true
                // Show the clip's value when set, else the manifest default.
                param: Object.assign({}, modelData, card.entry.values[modelData.id] !== undefined
                                                    ? { default: card.entry.values[modelData.id] } : {})
                onValueEdited: (value) => card.project.previewClipEffectParam(card.clipId, card.stackIndex, modelData.id, value)
                onValueCommitted: (value) => card.project.setClipEffectParam(card.clipId, card.stackIndex, modelData.id, value)
            }
        }
    }
}
