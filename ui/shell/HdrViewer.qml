import QtQuick
import QtQuick.Window
import TechieVideoEditor.Theme
import TechieVideoEditor.Player

// The HDR viewer: the player's picture in a window of its own, shown as HDR on an EDR
// (XDR) display — extended linear light, highlights rolled off to what the display can show
// at its brightness now. macOS makes HDR per window, so the editor itself stays SDR. It
// follows the player: the same frames, live previews and output effects.
Window {
    id: viewer
    required property var leader // the player's VideoView
    required property var project
    objectName: "hdrViewer"
    title: qsTr("HDR Viewer — %1").arg(project.name)
    width: 960
    height: 600
    minimumWidth: 320
    minimumHeight: 200
    color: "black" // (the window is linear light: black and white are the same either way)
    visible: true

    VideoView {
        id: hdrView
        objectName: "hdrView"
        anchors { left: parent.left; right: parent.right; top: parent.top; bottom: status.top }
        follows: viewer.leader
        hdrOutput: true
    }

    Item {
        id: status
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        height: 26
        Text {
            objectName: "hdrViewerStatus"
            anchors { left: parent.left; right: parent.right; verticalCenter: parent.verticalCenter; margins: Theme.space.s }
            elide: Text.ElideRight
            font: Theme.type.caption
            color: "white" // SDR white in linear light too
            text: viewer.project.projectColour === "sdr"
                  ? qsTr("An SDR project: shown as in the player. Make the project HDR in Color to work in HDR.")
                  : hdrView.hdrActive
                  ? qsTr("%1 — this display shows up to %2× SDR white now; brighter highlights are rolled off")
                    .arg(viewer.project.projectColour === "hdr-pq" ? qsTr("HDR · PQ") : qsTr("HDR · HLG"))
                    .arg(Fmt.num(hdrView.displayHeadroom, 1))
                  : qsTr("This display shows SDR: the picture is tone-mapped. Move the window to an HDR (XDR) display.")
        }
    }
}
