import QtQuick
import TechieVideoEditor.Theme

// Before and after a grade, over the player: the divider between the picture as it was (left)
// and as graded (right), dragged to either side; its handle, and the two sides named.
Item {
    id: overlay
    required property var project
    required property var player // the VideoView comparing
    visible: player.compareGrade

    readonly property real canvasScale: Math.min(width / project.canvasWidth, height / project.canvasHeight)
    readonly property rect canvasRect: Qt.rect((width - project.canvasWidth * canvasScale) / 2,
                                               (height - project.canvasHeight * canvasScale) / 2,
                                               project.canvasWidth * canvasScale, project.canvasHeight * canvasScale)
    readonly property real lineX: canvasRect.x + canvasRect.width * player.compareSplit

    // The divider, a thin line on a dark edge so it shows on any picture.
    Rectangle {
        objectName: "compareLine"
        x: overlay.lineX - 1.5
        y: overlay.canvasRect.y
        width: 3
        height: overlay.canvasRect.height
        color: "#66000000"
        Rectangle { anchors.centerIn: parent; width: 1; height: parent.height; color: "white" }
    }

    // "Before" and "After", at the top of each side (hidden when that side is too narrow).
    Repeater {
        model: [{ key: "before", text: qsTr("Before"), left: true }, { key: "after", text: qsTr("After"), left: false }]
        delegate: Rectangle {
            required property var modelData
            objectName: "compareLabel:" + modelData.key
            readonly property real room: modelData.left ? overlay.lineX - overlay.canvasRect.x
                                                        : overlay.canvasRect.x + overlay.canvasRect.width - overlay.lineX
            visible: room > width + Theme.space.m * 2
            x: modelData.left ? overlay.lineX - width - Theme.space.s : overlay.lineX + Theme.space.s
            y: overlay.canvasRect.y + Theme.space.s
            width: labelText.implicitWidth + Theme.space.m
            height: labelText.implicitHeight + Theme.space.xs
            radius: Theme.radius.s
            color: Theme.color.overlay
            Text {
                id: labelText
                anchors.centerIn: parent
                text: parent.modelData.text
                font: Theme.type.caption
                color: "white" // always on the dark scrim, in both themes
            }
        }
    }

    // The handle, and a strip along the line to drag it by (elsewhere the overlays below work).
    Rectangle {
        id: handle
        objectName: "compareHandle"
        x: overlay.lineX - width / 2
        y: overlay.canvasRect.y + (overlay.canvasRect.height - height) / 2
        width: 28
        height: 28
        radius: width / 2
        color: dragArea.pressed ? Theme.color.accent : "white"
        border.color: "#66000000"
        border.width: 1
        Row {
            anchors.centerIn: parent
            spacing: 4
            Repeater {
                model: ["‹", "›"]
                delegate: Text {
                    required property string modelData
                    text: modelData
                    font.pixelSize: 15
                    font.bold: true
                    color: dragArea.pressed ? "white" : "#1A1A1A"
                }
            }
        }
    }
    MouseArea {
        id: dragArea
        objectName: "compareDrag"
        x: overlay.lineX - 10
        y: overlay.canvasRect.y
        width: 20
        height: overlay.canvasRect.height
        cursorShape: Qt.SplitHCursor
        preventStealing: true
        function follow(mouse) {
            const at = mapToItem(overlay, mouse.x, mouse.y).x
            overlay.player.compareSplit = Math.max(0, Math.min(1, (at - overlay.canvasRect.x) / Math.max(1, overlay.canvasRect.width)))
        }
        onPressed: (mouse) => follow(mouse)
        onPositionChanged: (mouse) => { if (pressed) follow(mouse) }
        onDoubleClicked: overlay.player.compareSplit = 0.5
    }
}
