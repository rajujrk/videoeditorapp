import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import TechieVideoEditor.Theme

// Start screen: pick a canvas shape for a new project, or drop media to start one.
Rectangle {
    id: home
    // For the list of unsaved work to recover (none shown without it).
    property var project: null
    signal newProject(string name, int width, int height)
    signal openRequested()
    signal mediaDropped(url file)
    signal recoverRequested(string id)

    color: Theme.color.canvas

    readonly property var ratios: [
        { name: "Landscape", ratio: "16:9", w: 1920, h: 1080 },
        { name: "Vertical", ratio: "9:16", w: 1080, h: 1920 },
        { name: "Square", ratio: "1:1", w: 1080, h: 1080 },
        { name: "Standard", ratio: "4:3", w: 1440, h: 1080 },
        { name: "Cinema", ratio: "21:9", w: 2560, h: 1080 }
    ]

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // Navigation rail
        Rectangle {
            Layout.preferredWidth: 220
            Layout.fillHeight: true
            color: Theme.color.surface

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: Theme.space.l
                spacing: Theme.space.xs

                RowLayout {
                    spacing: Theme.space.s
                    Layout.bottomMargin: Theme.space.xl

                    Rectangle {
                        width: 28
                        height: 28
                        radius: Theme.radius.m
                        color: Theme.color.accent
                        Icon { anchors.centerIn: parent; name: "play"; size: 14; color: Theme.color.textOnAccent }
                    }
                    Text {
                        text: "TechieVideoEditor"
                        font: Theme.type.heading
                        color: Theme.color.textPrimary
                    }
                }

                Repeater {
                    model: [
                        { icon: "home", label: "Home", ready: true },
                        { icon: "folder", label: "Projects", ready: false },
                        { icon: "templates", label: "Templates", ready: false },
                        { icon: "record", label: "Recordings", ready: false }
                    ]
                    delegate: Rectangle {
                        required property var modelData
                        required property int index
                        Layout.fillWidth: true
                        implicitHeight: Theme.size.control + Theme.space.s
                        radius: Theme.radius.m
                        color: index === 0 ? Theme.color.accentSubtle : "transparent"

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: Theme.space.s
                            spacing: Theme.space.s
                            Icon {
                                name: modelData.icon
                                size: Theme.size.iconSmall + 2
                                color: index === 0 ? Theme.color.accentText
                                     : modelData.ready ? Theme.color.textSecondary : Theme.color.textDisabled
                            }
                            Text {
                                text: modelData.label
                                font: Theme.type.body
                                color: index === 0 ? Theme.color.textPrimary
                                     : modelData.ready ? Theme.color.textSecondary : Theme.color.textDisabled
                                Layout.fillWidth: true
                            }
                        }
                    }
                }

                Item { Layout.fillHeight: true }

                IconButton {
                    objectName: "themeToggle"
                    iconName: Theme.dark ? "sun" : "moon"
                    tip: Theme.dark ? "Light theme" : "Dark theme"
                    onClicked: Theme.dark = !Theme.dark
                }
            }

            Divider { anchors.right: parent.right; height: parent.height; width: 1 }
        }

        // Content
        Flickable {
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentHeight: content.implicitHeight + Theme.space.xxl * 2
            clip: true

            ColumnLayout {
                id: content
                x: Theme.space.xxl
                y: Theme.space.xxl
                width: parent.width - Theme.space.xxl * 2
                spacing: Theme.space.xl

                Text {
                    text: "Start creating"
                    font: Theme.type.display
                    color: Theme.color.textPrimary
                }

                // Unsaved work from a session that crashed or closed without saving.
                ColumnLayout {
                    id: recovery
                    objectName: "recoverySection"
                    readonly property var items: home.project ? home.project.recoverableProjects : []
                    visible: items.length > 0
                    Layout.fillWidth: true
                    spacing: Theme.space.s

                    Text { text: "Unsaved work"; font: Theme.type.title; color: Theme.color.textPrimary }
                    Text {
                        Layout.fillWidth: true
                        text: "TechieVideoEditor kept these changes when it last closed without saving."
                        font: Theme.type.label
                        color: Theme.color.textSecondary
                        wrapMode: Text.WordWrap
                    }

                    Repeater {
                        model: recovery.items
                        delegate: Rectangle {
                            required property var modelData
                            Layout.fillWidth: true
                            Layout.maximumWidth: 640
                            implicitHeight: row.implicitHeight + Theme.space.m * 2
                            radius: Theme.radius.l
                            color: Theme.color.surface
                            border.color: Theme.color.warning

                            RowLayout {
                                id: row
                                anchors.fill: parent
                                anchors.margins: Theme.space.m
                                spacing: Theme.space.m
                                Icon { name: "save"; color: Theme.color.warning }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: Theme.space.xxs
                                    Text {
                                        text: modelData.name
                                        font: Theme.type.heading
                                        color: Theme.color.textPrimary
                                        elide: Text.ElideRight
                                        Layout.fillWidth: true
                                    }
                                    Text {
                                        text: "Last saved automatically "
                                              + new Date(modelData.savedAt).toLocaleString(Qt.locale(), Locale.ShortFormat)
                                              + " · " + modelData.clipCount + (modelData.clipCount === 1 ? " clip" : " clips")
                                              + (modelData.originalPath ? " · " + modelData.originalPath : "")
                                        font: Theme.type.label
                                        color: Theme.color.textSecondary
                                        elide: Text.ElideMiddle
                                        Layout.fillWidth: true
                                    }
                                }
                                TextButton {
                                    objectName: "discardRecovery:" + modelData.id
                                    text: "Discard"
                                    variant: "ghost"
                                    onClicked: home.project.discardRecovery(modelData.id)
                                }
                                TextButton {
                                    objectName: "recover:" + modelData.id
                                    text: "Recover"
                                    variant: "primary"
                                    onClicked: home.recoverRequested(modelData.id)
                                }
                            }
                        }
                    }
                }

                // New project: one card per canvas shape
                ColumnLayout {
                    spacing: Theme.space.m
                    Text { text: "New project"; font: Theme.type.title; color: Theme.color.textPrimary }

                    Flow {
                        Layout.fillWidth: true
                        spacing: Theme.space.m

                        Repeater {
                            model: home.ratios
                            delegate: AbstractButton {
                                id: card
                                required property var modelData
                                objectName: "newProject:" + modelData.ratio
                                width: 148
                                height: 140
                                padding: Theme.space.m
                                hoverEnabled: true
                                onClicked: home.newProject(modelData.name + " " + modelData.ratio, modelData.w, modelData.h)

                                background: Rectangle {
                                    radius: Theme.radius.l
                                    color: card.hovered ? Theme.color.surfaceHover : Theme.color.surface
                                    border.color: card.hovered ? Theme.color.accent : Theme.color.borderSubtle
                                    Behavior on color { ColorAnimation { duration: Theme.motion.fast } }
                                }
                                contentItem: ColumnLayout {
                                    spacing: Theme.space.s
                                    Item {
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: 64
                                        // The shape itself, scaled into a 64px box
                                        Rectangle {
                                            anchors.centerIn: parent
                                            readonly property real scale_: 60 / Math.max(card.modelData.w, card.modelData.h)
                                            width: card.modelData.w * scale_
                                            height: card.modelData.h * scale_
                                            radius: Theme.radius.s
                                            color: "transparent"
                                            border.width: 2
                                            border.color: card.hovered ? Theme.color.accentText : Theme.color.textSecondary
                                        }
                                    }
                                    Text {
                                        Layout.alignment: Qt.AlignHCenter
                                        text: card.modelData.ratio
                                        font: Theme.type.heading
                                        color: Theme.color.textPrimary
                                    }
                                    Text {
                                        Layout.alignment: Qt.AlignHCenter
                                        text: card.modelData.name
                                        font: Theme.type.caption
                                        color: Theme.color.textSecondary
                                    }
                                }
                            }
                        }
                    }
                }

                // Quick actions
                RowLayout {
                    spacing: Theme.space.s
                    TextButton { objectName: "openProject"; text: "Open project…"; iconName: "folder"; onClicked: home.openRequested() }
                    TextButton { text: "Record screen"; iconName: "record"; enabled: false; tip: "Recording arrives in Phase 4" }
                    TextButton { text: "Instant mode"; iconName: "effects"; enabled: false; tip: "Instant mode arrives in Phase 4" }
                }

                // Recent projects (empty state until projects are saved in Phase 1)
                ColumnLayout {
                    spacing: Theme.space.m
                    Text { text: "Recent projects"; font: Theme.type.title; color: Theme.color.textPrimary }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 180
                        radius: Theme.radius.l
                        color: drop.containsDrag ? Theme.color.accentSubtle : Theme.color.surface
                        border.color: drop.containsDrag ? Theme.color.accent : Theme.color.borderSubtle

                        Column {
                            anchors.centerIn: parent
                            spacing: Theme.space.s
                            Icon { anchors.horizontalCenter: parent.horizontalCenter; name: "import"; size: 28 }
                            Text {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: "Recent projects appear here"
                                font: Theme.type.heading
                                color: Theme.color.textPrimary
                            }
                            Text {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: "Drop a video here to start a project with it"
                                font: Theme.type.label
                                color: Theme.color.textSecondary
                            }
                        }

                        DropArea {
                            id: drop
                            anchors.fill: parent
                            onDropped: (event) => {
                                if (event.hasUrls)
                                    home.mediaDropped(event.urls[0])
                            }
                        }
                    }
                }
            }
        }
    }
}
