import QtQuick
import QtQuick.Controls

// A right-click (or button) menu in the app's look: compact rows, a raised surface, every
// entry (submenus' too) an AppMenuItem.
Menu {
    id: menu
    // Wide enough for the longest entry with its shortcut, within reason.
    width: Math.min(420, Math.max(220, contentWidthHint + leftPadding + rightPadding))
    readonly property real contentWidthHint: {
        let widest = 0
        for (let i = 0; i < count; ++i) {
            const item = itemAt(i)
            if (item && item.visible)
                widest = Math.max(widest, item.implicitWidth)
        }
        return widest
    }
    topPadding: Theme.space.xs
    bottomPadding: Theme.space.xs
    leftPadding: Theme.space.xs
    rightPadding: Theme.space.xs
    delegate: AppMenuItem {}
    // Opens with a quick fade and grow from where it was asked for.
    enter: Transition {
        ParallelAnimation {
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.motion.fast; easing.type: Easing.OutCubic }
            NumberAnimation { property: "scale"; from: 0.97; to: 1; duration: Theme.motion.fast; easing.type: Easing.OutCubic }
        }
    }
    background: Rectangle {
        implicitWidth: 220
        radius: Theme.radius.m
        color: Theme.color.surfaceRaised
        border.color: Theme.color.borderStrong
    }
}
