import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import TechieVideoEditor.Theme

// One effect in a stack: on/off, remove, and its controls. Dragging previews live; releasing
// commits one undo step. With `audio`, an effect in the clip's audio stack — or, with
// `trackId`, a track's — heard on the next play. An audio plug-in shows its own parameters
// (0..1, as the plug-in words them) and a button for its own window; a clip's can be keyframed.
Rectangle {
    id: card
    required property var project
    required property double clipId
    required property int stackIndex
    required property var entry // {effectId, enabled, values, plugin}
    required property var spec  // the effect's manifest summary from availableEffects
    property var keyStates: ({}) // channel → "key" / "animated" (selectedClipInfo.keyframeStates)
    property bool audio: false
    property double trackId: -1 // a track's audio stack
    readonly property bool onTrack: trackId > 0
    readonly property bool plugin: entry.plugin === true
    // An OpenFX plug-in: runs on the clip's own picture, before its colour and our effects.
    readonly property bool videoPlugin: (entry.effectId || "").startsWith("ofx:")
    readonly property string prefix: onTrack ? "trackEffect" : audio ? "audioEffect" : "clipEffect"
    // A plug-in's parameters (read again when the stack changes).
    readonly property var pluginParams: {
        card.entry // (dependency)
        return plugin ? project.pluginParameters(onTrack ? -1 : clipId, onTrack ? trackId : -1, stackIndex) : []
    }
    readonly property var params: plugin ? pluginParams : spec.params

    function setEnabled(on) {
        if (onTrack) project.setTrackAudioEffectEnabled(trackId, stackIndex, on)
        else if (audio) project.setAudioEffectEnabled(clipId, stackIndex, on)
        else project.setClipEffectEnabled(clipId, stackIndex, on)
    }
    function remove() {
        if (onTrack) project.removeTrackAudioEffect(trackId, stackIndex)
        else if (audio) project.removeAudioEffect(clipId, stackIndex)
        else project.removeClipEffect(clipId, stackIndex)
    }
    function commit(param, value) {
        if (onTrack) project.setTrackAudioEffectParam(trackId, stackIndex, param, value)
        else if (audio) project.setAudioEffectParam(clipId, stackIndex, param, value)
        else project.setClipEffectParam(clipId, stackIndex, param, value)
    }

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
            Icon { name: card.audio || card.onTrack ? "audio" : "effects"; size: Theme.size.iconSmall; color: card.entry.enabled ? Theme.color.accentText : Theme.color.textSecondary }
            Text {
                text: card.spec.title
                font: Theme.type.heading
                color: Theme.color.textPrimary
                Layout.fillWidth: true
                elide: Text.ElideRight
            }
            TextButton {
                objectName: card.prefix + "Window:" + card.stackIndex
                visible: card.plugin && card.pluginParams.length >= 0
                text: "Window"
                variant: "ghost"
                tip: "Open the plug-in's own window; what you change there is one undo step"
                onClicked: card.project.openPluginWindow(card.onTrack ? -1 : card.clipId, card.onTrack ? card.trackId : -1, card.stackIndex)
            }
            ToggleSwitch {
                objectName: card.prefix + ":" + card.stackIndex
                checked: card.entry.enabled
                onToggled: card.setEnabled(checked)
            }
            IconButton {
                objectName: (card.onTrack ? "removeTrackEffect:" : card.audio ? "removeAudioEffect:" : "removeEffect:") + card.stackIndex
                iconName: "delete"
                tip: "Remove effect"
                buttonSize: 24
                onClicked: card.remove()
            }
        }
        Text {
            objectName: card.prefix + "PluginNote:" + card.stackIndex
            visible: card.videoPlugin
            Layout.fillWidth: true
            text: card.spec.params && card.spec.params.length >= 0 && card.spec.id
                  ? "A plug-in: it works on the clip's own picture, before its colour and our effects."
                  : "Not available on this Mac: it's passed over until it's installed."
            font: Theme.type.caption
            color: card.spec.id ? Theme.color.textSecondary : Theme.color.warning
            wrapMode: Text.WordWrap
        }
        Text {
            visible: card.plugin && card.pluginParams.length === 0
            Layout.fillWidth: true
            text: "Not available on this Mac: it's passed over until it's installed."
            font: Theme.type.caption
            color: Theme.color.warning
            wrapMode: Text.WordWrap
        }

        Repeater {
            model: card.entry.enabled ? card.params : []
            delegate: ParamControl {
                required property var modelData
                readonly property string channel: (card.audio ? "audioEffect:" : "effect:") + card.stackIndex + ":" + modelData.id
                Layout.fillWidth: true
                objectName: (card.onTrack ? "trackEffectParam:" : card.audio ? "audioEffectParam:" : "effectParam:")
                            + card.stackIndex + ":" + modelData.id
                // Keyframes: video effects, and a clip's audio plug-ins.
                keyState: modelData.keyframable && !card.onTrack && (!card.audio || card.plugin)
                          ? (card.keyStates[channel] || "none") : ""
                onKeyframeToggled: keyState === "key" ? card.project.removeKeyframes(card.clipId, [channel])
                                                      : card.project.addKeyframes(card.clipId, [channel])
                // Show the stack's value when set, else the manifest default (a plug-in's own now).
                param: Object.assign({}, modelData, card.entry.values[modelData.id] !== undefined
                                                    ? { default: card.entry.values[modelData.id] } : {})
                format: card.plugin ? (v) => card.project.pluginValueText(card.onTrack ? -1 : card.clipId,
                                                                           card.onTrack ? card.trackId : -1,
                                                                           card.stackIndex, modelData.id, v)
                                    : null
                onValueEdited: (value) => {
                    if (!card.audio && !card.onTrack)
                        card.project.previewClipEffectParam(card.clipId, card.stackIndex, modelData.id, value)
                }
                onValueCommitted: (value) => card.commit(modelData.id, value)
            }
        }
    }
}
