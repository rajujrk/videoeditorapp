import QtQuick
import QtQuick.Controls
import TechieVideoEditor.Theme

// ◆ beside an animatable value: filled when it has a keyframe at the playhead, outlined in
// accent while it is animated, faint otherwise. Clicking asks to add or remove that keyframe.
AbstractButton {
    id: toggle
    property string keyState: "none" // "key", "animated" or "none"

    implicitWidth: 18
    implicitHeight: 18
    hoverEnabled: true
    focusPolicy: Qt.TabFocus

    ToolTip {
        text: toggle.keyState === "key" ? "Remove this keyframe" : "Add a keyframe at the playhead"
        visible: toggle.hovered
        delay: 600
    }
    background: Rectangle { radius: Theme.radius.s; color: toggle.hovered ? Theme.color.surfaceHover : "transparent" }
    contentItem: Item {
        Rectangle {
            anchors.centerIn: parent
            width: 8
            height: 8
            rotation: 45
            color: toggle.keyState === "key" ? Theme.color.accentText : "transparent"
            border.width: 1.5
            border.color: toggle.keyState !== "none" ? Theme.color.accentText
                        : toggle.hovered ? Theme.color.textSecondary : Theme.color.textDisabled
        }
    }
}
