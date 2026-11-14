import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import TechieVideoEditor.Theme
import TechieVideoEditor.Player

// Editor title bar: back to Home, project name and canvas, history, save, export.
Rectangle {
    id: bar
    required property var project
    signal homeRequested()
    signal saveRequested()
    signal exportRequested()
    signal interchangeRequested()
    signal shortcutsRequested()
    signal cloudSettingsRequested()
    signal workspaceChosen(string id)
    signal saveWorkspaceRequested()
    signal projectSettingsRequested()

    implicitHeight: Theme.size.topBar
    color: Theme.color.surface

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.space.s
        anchors.rightMargin: Theme.space.m
        spacing: Theme.space.s

        IconButton { iconName: "back"; tip: qsTr("Home"); onClicked: bar.homeRequested() }
        Divider { implicitWidth: 1; implicitHeight: 20 }

        Text {
            objectName: "projectName"
            text: bar.project.name + (bar.project.modified ? "  •" : "")
            font: Theme.type.heading
            color: Theme.color.textPrimary
            Layout.leftMargin: Theme.space.xs
        }
        Text {
            objectName: "autosaveStatus"
            visible: bar.project.modified && bar.project.lastAutosave > 0
            text: qsTr("Autosaved %1").arg(new Date(bar.project.lastAutosave).toLocaleTimeString(Qt.locale(), Locale.ShortFormat))
            font: Theme.type.caption
            color: Theme.color.textSecondary
        }
        // The canvas: click for the project's settings (size, frame rate, archive).
        AbstractButton {
            id: canvasButton
            objectName: "canvasButton"
            implicitWidth: canvas.implicitWidth + Theme.space.m
            implicitHeight: 20
            hoverEnabled: true
            onClicked: bar.projectSettingsRequested()
            ToolTip.visible: hovered
            ToolTip.delay: 600
            ToolTip.text: qsTr("Project settings: change the size or frame rate, or archive the project with its media")
            background: Rectangle {
                radius: Theme.radius.round
                color: canvasButton.hovered ? Theme.color.surfaceHover : Theme.color.surfaceRaised
                border.color: canvasButton.hovered ? Theme.color.borderStrong : Theme.color.borderSubtle
            }
            contentItem: Text {
                id: canvas
                text: qsTr("%1 × %2 · %3 fps").arg(bar.project.canvasWidth).arg(bar.project.canvasHeight).arg(Fmt.brief(bar.project.frameRate, 3))
                font: Theme.type.caption
                color: Theme.color.textSecondary
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
        }

        Item { Layout.fillWidth: true }

        IconButton {
            objectName: "undoButton"
            iconName: "undo"
            tip: bar.project.canUndo ? qsTr("Undo %1").arg(bar.project.undoText) + KeyMap.hints["undo"] : qsTr("Nothing to undo")
            enabled: bar.project.canUndo
            onClicked: bar.project.undo()
        }
        IconButton {
            objectName: "redoButton"
            iconName: "redo"
            tip: bar.project.canRedo ? qsTr("Redo %1").arg(bar.project.redoText) + KeyMap.hints["redo"] : qsTr("Nothing to redo")
            enabled: bar.project.canRedo
            onClicked: bar.project.redo()
        }
        IconButton { iconName: "save"; tip: qsTr("Save project") + KeyMap.hints["save"]; onClicked: bar.saveRequested() }
        IconButton {
            objectName: "interchangeButton"
            iconName: "interchange"
            tip: qsTr("Export the timeline for another editor (OpenTimelineIO, Final Cut Pro XML, EDL)")
            onClicked: bar.interchangeRequested()
        }
        IconButton {
            objectName: "cloudSettingsButton"
            iconName: "assistant"
            tip: qsTr("AI settings: keys, models and use")
            onClicked: bar.cloudSettingsRequested()
        }
        // Workspaces: the panels laid out for a kind of work, and the person's own.
        IconButton {
            id: workspaceButton
            objectName: "workspaceButton"
            iconName: "workspace"
            tip: qsTr("Workspace: %1").arg(Workspaces.layout(Workspaces.current).name || "")
            onClicked: workspaceMenu.popup(workspaceButton, 0, workspaceButton.height)
            Menu {
                id: workspaceMenu
                objectName: "workspaceMenu"
                Repeater {
                    model: Workspaces.layouts
                    delegate: MenuItem {
                        required property var modelData
                        objectName: "workspace:" + modelData.id
                        text: modelData.name
                        checkable: true
                        checked: Workspaces.current === modelData.id
                        onTriggered: bar.workspaceChosen(modelData.id)
                    }
                }
                MenuSeparator {}
                MenuItem {
                    objectName: "workspaceSave"
                    text: qsTr("Save this layout…")
                    onTriggered: bar.saveWorkspaceRequested()
                }
                MenuItem {
                    objectName: "workspaceDelete"
                    readonly property var chosen: Workspaces.layout(Workspaces.current)
                    enabled: chosen.builtIn === false
                    text: chosen.builtIn === false ? qsTr("Delete “%1”").arg(chosen.name) : qsTr("Delete this layout")
                    onTriggered: Workspaces.remove(Workspaces.current)
                }
            }
        }
        IconButton {
            objectName: "shortcutsButton"
            iconName: "keyboard"
            tip: qsTr("Keyboard shortcuts") + KeyMap.hints["shortcuts"]
            onClicked: bar.shortcutsRequested()
        }
        IconButton {
            iconName: Theme.dark ? "sun" : "moon"
            tip: Theme.dark ? qsTr("Light theme") : qsTr("Dark theme")
            onClicked: Theme.dark = !Theme.dark
        }
        IconButton {
            id: queueButton
            objectName: "renderQueueButton"
            // Exports still to finish: the running one and those waiting.
            readonly property int pending: bar.project.exportJobs.filter((j) => j.state === "running" || j.state === "waiting").length
            visible: bar.project.exportJobs.length > 0
            iconName: "queue"
            tip: pending > 0 ? qsTr("Render queue · %1 to go").arg(Fmt.num(pending)) : qsTr("Render queue")
            checkable: true
            checked: queuePanel.visible
            onClicked: queuePanel.visible ? queuePanel.close() : queuePanel.open()

            Rectangle {
                visible: queueButton.pending > 0
                anchors { right: parent.right; top: parent.top; rightMargin: -2; topMargin: -2 }
                width: Math.max(16, badge.implicitWidth + 8)
                height: 16
                radius: 8
                color: Theme.color.accent
                Text {
                    id: badge
                    anchors.centerIn: parent
                    text: queueButton.pending
                    font: Theme.type.caption
                    color: Theme.color.textOnAccent
                }
            }

            RenderQueuePanel {
                id: queuePanel
                objectName: "renderQueuePanel"
                project: bar.project
                x: parent.width - width
                y: parent.height + Theme.space.s
            }
        }
        TextButton {
            objectName: "exportButton"
            text: bar.project.exporting ? (bar.project.exportPaused ? qsTr("Paused %1") : qsTr("Exporting %1")).arg(Fmt.percent(bar.project.exportProgress))
                                        : qsTr("Export")
            iconName: "export"
            variant: "primary"
            enabled: bar.project.duration > 0
            tip: qsTr("Export a video, a GIF or the sound%1; exports queue up and run while you edit").arg(KeyMap.hints["export"])
            Layout.leftMargin: Theme.space.s
            onClicked: bar.exportRequested()
        }
    }

    Divider { anchors.bottom: parent.bottom; width: parent.width }
}
