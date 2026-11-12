import QtQuick
import QtQuick.Window
import TechieVideoEditor.Theme
import TechieVideoEditor.Player

// The picture alone, full screen on another display (or this one when there is no other):
// it follows the player — the same frames as they play, scrub or are edited. Space plays or
// pauses, ←/→ step a frame, Esc closes it. The timecode shows for a moment when the pointer
// moves.
Window {
    id: preview
    required property var leader // the player's VideoView
    required property var project
    objectName: "secondDisplayPreview"
    title: qsTr("Preview — %1").arg(project.name)
    color: "black"
    // Another display than the editor's, when there is one.
    property var editorScreen: null
    readonly property var otherScreen: {
        const screens = Qt.application.screens
        for (let i = 0; i < screens.length; ++i)
            if (!editorScreen || screens[i].name !== editorScreen.name)
                return screens[i]
        return screens.length > 0 ? screens[0] : null
    }
    readonly property bool onAnotherDisplay: !!editorScreen && !!otherScreen && otherScreen.name !== editorScreen.name
    screen: otherScreen
    x: otherScreen ? otherScreen.virtualX : 0
    y: otherScreen ? otherScreen.virtualY : 0
    width: Workspaces.previewWindowed ? 640 : (otherScreen ? otherScreen.width : 1280)
    height: Workspaces.previewWindowed ? 360 : (otherScreen ? otherScreen.height : 720)
    visibility: Workspaces.previewWindowed ? Window.Windowed : Window.FullScreen
    visible: true

    VideoView {
        id: view
        objectName: "secondDisplayView"
        anchors.fill: parent
        follows: preview.leader
    }

    // The timecode, for a moment after the pointer moves.
    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: hud.opacity > 0 ? Qt.ArrowCursor : Qt.BlankCursor
        onPositionChanged: {
            hud.opacity = 1
            hideHud.restart()
        }
    }
    Text {
        id: hud
        objectName: "secondDisplayTimecode"
        anchors { left: parent.left; bottom: parent.bottom; margins: 24 }
        opacity: 0
        Behavior on opacity { NumberAnimation { duration: 200 } }
        text: preview.project.timecode(preview.project.playhead) + "  ·  " + qsTr("Esc to close")
        font: Theme.type.mono
        color: "white"
        style: Text.Outline
        styleColor: "black"
    }
    Timer { id: hideHud; interval: 2000; onTriggered: hud.opacity = 0 }

    Shortcut { objectName: "secondDisplayKey:close"; sequence: "Esc"; onActivated: preview.close() }
    Shortcut { objectName: "secondDisplayKey:play"; sequence: "Space"; onActivated: preview.project.togglePlay() }
    Shortcut { sequence: "Left"; onActivated: preview.project.stepFrames(-1) }
    Shortcut { sequence: "Right"; onActivated: preview.project.stepFrames(1) }
}
