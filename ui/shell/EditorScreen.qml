import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import TechieVideoEditor.Theme

// The editor workspace:
//   ┌──────────────────────── top bar ────────────────────────┐
//   │ library │          player          │      inspector      │
//   ├─────────┴──────────────────────────┴─────────────────────┤
//   │                        timeline                          │
//   └──────────────────────────────────────────────────────────┘
// All dividers are draggable; sizes come from Theme.size.
Rectangle {
    id: editor
    property string projectName: "Untitled project"
    property int canvasWidth: 1920
    property int canvasHeight: 1080
    property alias player: playerPanel.player
    signal homeRequested()

    color: Theme.color.canvas

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        TopBar {
            Layout.fillWidth: true
            projectName: editor.projectName
            canvasDescription: editor.canvasWidth + " × " + editor.canvasHeight + " · 30 fps"
            onHomeRequested: editor.homeRequested()
        }

        SplitView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            orientation: Qt.Vertical
            handle: Grip { vertical: false }

            SplitView {
                SplitView.fillHeight: true
                orientation: Qt.Horizontal
                handle: Grip { vertical: true }

                LibraryPanel {
                    SplitView.preferredWidth: Theme.size.libraryWidth
                    SplitView.minimumWidth: Theme.size.libraryMinWidth
                    player: playerPanel.player
                }
                PlayerPanel {
                    id: playerPanel
                    SplitView.fillWidth: true
                    SplitView.minimumWidth: Theme.size.playerMinWidth
                }
                InspectorPanel {
                    SplitView.preferredWidth: Theme.size.inspectorWidth
                    SplitView.minimumWidth: Theme.size.inspectorMinWidth
                    player: playerPanel.player
                }
            }

            TimelinePanel {
                SplitView.preferredHeight: Theme.size.timelineHeight
                SplitView.minimumHeight: Theme.size.timelineMinHeight
            }
        }
    }

    // 1px divider with a wider invisible grab area.
    component Grip: Rectangle {
        id: grip
        required property bool vertical
        implicitWidth: vertical ? 1 : 0
        implicitHeight: vertical ? 0 : 1
        color: SplitHandle.pressed ? Theme.color.accent
             : SplitHandle.hovered ? Theme.color.borderStrong : Theme.color.borderSubtle

        containmentMask: Item {
            x: grip.vertical ? -3 : 0
            y: grip.vertical ? 0 : -3
            width: grip.vertical ? 7 : grip.width
            height: grip.vertical ? grip.height : 7
        }
    }
}
