import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import TechieVideoEditor.Theme

// Import an image sequence: the numbered pictures one chosen picture is in (shot_0001.png,
// shot_0002.png…) played as one video, at a chosen frame rate.
Popup {
    id: dialog
    required property var project
    property url frame
    property var found: ({})
    // Frame rates offered; the project's is chosen first when it is one of them.
    readonly property var rates: [
        { label: "23.976", num: 24000, den: 1001 }, { label: "24", num: 24, den: 1 },
        { label: "25", num: 25, den: 1 }, { label: "29.97", num: 30000, den: 1001 },
        { label: "30", num: 30, den: 1 }, { label: "48", num: 48, den: 1 },
        { label: "50", num: 50, den: 1 }, { label: "59.94", num: 60000, den: 1001 },
        { label: "60", num: 60, den: 1 }
    ]
    signal imported(double mediaId)

    // Looks for the sequence `file` is one of and shows it.
    function openFor(file) {
        frame = file
        found = project.findImageSequence(file)
        let best = 4 // 30
        for (let i = 0; i < rates.length; ++i)
            if (Math.abs(rates[i].num / rates[i].den - project.frameRate) < 0.01)
                best = i
        rateBox.currentIndex = best
        open()
    }
    function importIt() {
        const rate = rates[rateBox.currentIndex]
        const id = project.importImageSequence(frame, rate.num, rate.den)
        if (id >= 0) {
            close()
            imported(id)
        }
    }

    parent: Overlay.overlay
    anchors.centerIn: parent
    modal: true
    width: 420
    padding: Theme.space.l
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    background: Rectangle {
        radius: Theme.radius.l
        color: Theme.color.surfaceRaised
        border.color: Theme.color.borderStrong
    }

    contentItem: ColumnLayout {
        spacing: Theme.space.m
        RowLayout {
            Layout.fillWidth: true
            Icon { name: "media"; size: Theme.size.icon; color: Theme.color.textPrimary }
            Text {
                Layout.fillWidth: true
                text: qsTr("Import an image sequence")
                font: Theme.type.title
                color: Theme.color.textPrimary
            }
            IconButton { objectName: "imageSequenceClose"; iconName: "close"; tip: qsTr("Close (Esc)"); onClicked: dialog.close() }
        }

        Text {
            objectName: "imageSequenceError"
            visible: !dialog.found.found
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: dialog.found.error || ""
            font: Theme.type.body
            color: Theme.color.textSecondary
        }

        GridLayout {
            visible: !!dialog.found.found
            Layout.fillWidth: true
            columns: 2
            columnSpacing: Theme.space.m
            rowSpacing: Theme.space.s

            Text { text: qsTr("Pictures"); font: Theme.type.label; color: Theme.color.textSecondary }
            Text {
                objectName: "imageSequenceName"
                Layout.fillWidth: true
                elide: Text.ElideMiddle
                text: dialog.found.name || ""
                font: Theme.type.mono
                color: Theme.color.textPrimary
            }
            Text { text: qsTr("Frames"); font: Theme.type.label; color: Theme.color.textSecondary }
            Text {
                objectName: "imageSequenceFrames"
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: dialog.found.found
                      ? qsTr("%1 frames, %2 to %3").arg(dialog.found.frames).arg(dialog.found.firstFile)
                        .arg(dialog.found.lastFile)
                      : ""
                font: Theme.type.body
                color: Theme.color.textPrimary
            }
            Text { text: qsTr("Size"); font: Theme.type.label; color: Theme.color.textSecondary }
            Text {
                objectName: "imageSequenceSize"
                text: dialog.found.found
                      ? (dialog.found.alpha ? qsTr("%1 × %2, with transparency") : qsTr("%1 × %2"))
                        .arg(dialog.found.width).arg(dialog.found.height)
                      : ""
                font: Theme.type.body
                color: Theme.color.textPrimary
            }
            Text { text: qsTr("Frame rate"); font: Theme.type.label; color: Theme.color.textSecondary }
            RowLayout {
                spacing: Theme.space.s
                ComboBox {
                    id: rateBox
                    objectName: "imageSequenceRate"
                    model: dialog.rates
                    textRole: "label"
                    implicitWidth: 110
                }
                Text {
                    objectName: "imageSequenceLength"
                    text: dialog.found.found
                          ? qsTr("%1 long").arg(dialog.project.timecode(dialog.found.frames
                                * dialog.rates[rateBox.currentIndex].den / dialog.rates[rateBox.currentIndex].num))
                          : ""
                    font: Theme.type.mono
                    color: Theme.color.textSecondary
                }
            }
        }

        Text {
            objectName: "imageSequenceGaps"
            visible: !!dialog.found.found && dialog.found.missing > 0
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: dialog.found.missing === 1 ? qsTr("1 number has no picture; the frame before it is shown there.")
                  : qsTr("%1 numbers have no picture; the frame before each is shown there.").arg(dialog.found.missing)
            font: Theme.type.caption
            color: Theme.color.warning
        }

        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            TextButton { text: qsTr("Cancel"); variant: "ghost"; onClicked: dialog.close() }
            TextButton {
                objectName: "imageSequenceImport"
                text: qsTr("Import")
                enabled: !!dialog.found.found
                onClicked: dialog.importIt()
            }
        }
    }
}
