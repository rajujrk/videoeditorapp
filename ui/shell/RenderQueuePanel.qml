import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import TechieVideoEditor.Theme

// The render queue: every export of this session, the running one first among those
// waiting. Pause or cancel the running export, cancel a waiting one, show a finished file,
// take finished ones off the list.
Popup {
    id: panel
    required property var project
    readonly property var jobs: project.exportJobs
    readonly property bool anyFinished: jobs.some((j) => j.state === "done" || j.state === "failed" || j.state === "cancelled")

    width: 420
    padding: Theme.space.l
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    function stateText(job) {
        if (job.state === "running")
            return panel.project.exportPaused ? qsTr("Paused · %1").arg(Fmt.percent(job.progress)) : Fmt.percent(job.progress)
        return job.state === "waiting" ? qsTr("Waiting")
             : job.state === "done" ? qsTr("Done")
             : job.state === "cancelled" ? qsTr("Cancelled") : qsTr("Failed: %1").arg(job.error)
    }
    function folderOf(path) { return "file://" + path.substring(0, path.lastIndexOf("/")) }

    background: Rectangle {
        radius: Theme.radius.l
        color: Theme.color.surfaceRaised
        border.color: Theme.color.borderStrong
    }

    contentItem: ColumnLayout {
        spacing: Theme.space.m

        RowLayout {
            Layout.fillWidth: true
            Text {
                Layout.fillWidth: true
                text: qsTr("Render queue")
                font: Theme.type.heading
                color: Theme.color.textPrimary
            }
            TextButton {
                objectName: "clearFinishedExports"
                visible: panel.anyFinished
                text: qsTr("Clear finished")
                variant: "ghost"
                onClicked: panel.project.clearFinishedExports()
            }
        }

        Text {
            visible: panel.jobs.length === 0
            Layout.fillWidth: true
            text: qsTr("Nothing exported yet. Exports wait here and run one after another while you keep editing.")
            font: Theme.type.body
            color: Theme.color.textSecondary
            wrapMode: Text.WordWrap
        }

        ListView {
            id: list
            objectName: "renderQueueList"
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(contentHeight, 360)
            clip: true
            spacing: Theme.space.s
            model: panel.jobs
            delegate: Rectangle {
                id: row
                required property var modelData
                readonly property var job: modelData
                objectName: "exportJob:" + job.id
                width: list.width
                implicitHeight: rowLayout.implicitHeight + Theme.space.m * 2
                radius: Theme.radius.m
                color: Theme.color.surface
                border.color: job.state === "running" ? Theme.color.accent : Theme.color.borderSubtle

                ColumnLayout {
                    id: rowLayout
                    anchors.fill: parent
                    anchors.margins: Theme.space.m
                    spacing: Theme.space.xs

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.space.xs
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            Text {
                                Layout.fillWidth: true
                                text: row.job.name
                                font: Theme.type.label
                                color: Theme.color.textPrimary
                                elide: Text.ElideMiddle
                            }
                            Text {
                                objectName: "exportJobState"
                                Layout.fillWidth: true
                                text: row.job.format + " · " + panel.project.timecode(row.job.duration).substring(0, 8)
                                      + " · " + panel.stateText(row.job)
                                font: Theme.type.caption
                                color: row.job.state === "failed" ? Theme.color.danger : Theme.color.textSecondary
                                elide: Text.ElideRight
                            }
                        }
                        IconButton {
                            objectName: "pauseExport"
                            visible: row.job.state === "running"
                            iconName: panel.project.exportPaused ? "play" : "pause"
                            tip: panel.project.exportPaused ? qsTr("Resume") : qsTr("Pause")
                            onClicked: panel.project.exportPaused ? panel.project.resumeExport() : panel.project.pauseExport()
                        }
                        IconButton {
                            objectName: "cancelExport"
                            visible: row.job.state === "running" || row.job.state === "waiting"
                            iconName: "close"
                            tip: row.job.state === "running" ? qsTr("Cancel (removes the partial file)") : qsTr("Take out of the queue")
                            onClicked: panel.project.cancelExport(row.job.id)
                        }
                        IconButton {
                            objectName: "revealExport"
                            visible: row.job.state === "done"
                            iconName: "folder"
                            tip: qsTr("Show in Finder")
                            onClicked: Qt.openUrlExternally(panel.folderOf(row.job.path))
                        }
                        IconButton {
                            objectName: "removeExport"
                            visible: row.job.state !== "running" && row.job.state !== "waiting"
                            iconName: "delete"
                            tip: qsTr("Remove from the list (the file stays)")
                            onClicked: panel.project.removeExportJob(row.job.id)
                        }
                    }
                    ProgressBar {
                        id: bar
                        visible: row.job.state === "running" || row.job.state === "waiting"
                        Layout.fillWidth: true
                        value: row.job.progress
                        background: Rectangle {
                            implicitHeight: 4
                            radius: 2
                            color: Theme.color.borderSubtle
                        }
                        contentItem: Item {
                            implicitHeight: 4
                            Rectangle {
                                width: parent.width * bar.visualPosition
                                height: parent.height
                                radius: 2
                                color: Theme.color.accent
                            }
                        }
                    }
                }
            }
        }
    }
}
