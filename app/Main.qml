import QtQuick
import TechieVideoEditor.Player

Window {
    id: root
    width: 1280
    height: 760
    visible: true
    title: "TechieVideoEditor — Playback Spike"
    color: "#111316"

    VideoView {
        id: player
        objectName: "player"
        anchors.fill: parent
    }

    DropArea {
        anchors.fill: parent
        onDropped: (drop) => {
            if (drop.hasUrls)
                player.source = drop.urls[0]
        }
    }

    Text {
        anchors.centerIn: parent
        visible: player.source === ""
        text: "Drop a video file here"
        color: "#8a9099"
        font.pixelSize: 20
    }

    Rectangle {
        anchors { left: parent.left; top: parent.top; margins: 12 }
        visible: player.source !== ""
        color: "#b3000000"
        radius: 6
        width: stats.implicitWidth + 20
        height: stats.implicitHeight + 14

        Text {
            id: stats
            anchors.centerIn: parent
            color: "white"
            font.family: "Menlo"
            font.pixelSize: 13
            text: player.errorString !== ""
                  ? player.errorString
                  : player.streamDescription + "\n"
                    + (player.hardwareAccelerated ? "VideoToolbox" : "software decode") + "\n"
                    + "decode " + player.decodeFps.toFixed(1) + " fps · present " + player.presentFps.toFixed(1) + " fps\n"
                    + "skipped " + player.skippedFrames
        }
    }
}
