import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import TechieVideoEditor.Theme

// Left panel: a vertical tab rail of content types and the selected type's browser.
Rectangle {
    id: library
    required property var player  // VideoView, for the live effects list
    required property var project // ProjectController
    property int currentTab: 0
    signal importRequested()

    color: Theme.color.surface

    readonly property var tabs: [
        { icon: "media", label: "Media", phase: "" },
        { icon: "audio", label: "Audio", phase: "Music and sound library — Phase 4" },
        { icon: "titles", label: "Titles", phase: "Title templates — Phase 2" },
        { icon: "transitions", label: "Transitions", phase: "Transition library — Phase 1–2" },
        { icon: "effects", label: "Effects", phase: "" },
        { icon: "stickers", label: "Stickers", phase: "Stickers and elements — Phase 2" },
        { icon: "templates", label: "Templates", phase: "Template mode — Phase 4" }
    ]

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // Tab rail
        ColumnLayout {
            Layout.preferredWidth: Theme.size.tabRail
            Layout.maximumWidth: Theme.size.tabRail
            Layout.fillWidth: false
            Layout.fillHeight: true
            Layout.topMargin: Theme.space.s
            spacing: Theme.space.xxs

            Repeater {
                model: library.tabs
                delegate: AbstractButton {
                    id: tab
                    required property var modelData
                    required property int index
                    objectName: "libraryTab:" + modelData.label
                    Layout.alignment: Qt.AlignHCenter
                    implicitWidth: Theme.size.tabRail - Theme.space.s
                    implicitHeight: 48
                    hoverEnabled: true
                    checked: library.currentTab === index
                    onClicked: library.currentTab = index

                    background: Rectangle {
                        radius: Theme.radius.m
                        color: tab.checked ? Theme.color.accentSubtle
                             : tab.hovered ? Theme.color.surfaceHover : "transparent"
                    }
                    contentItem: ColumnLayout {
                        spacing: Theme.space.xxs
                        Icon {
                            Layout.alignment: Qt.AlignHCenter
                            name: tab.modelData.icon
                            size: Theme.size.icon
                            color: tab.checked ? Theme.color.accentText : Theme.color.textSecondary
                        }
                        Text {
                            Layout.alignment: Qt.AlignHCenter
                            text: tab.modelData.label
                            font: Theme.type.caption
                            color: tab.checked ? Theme.color.textPrimary : Theme.color.textSecondary
                            elide: Text.ElideRight
                            Layout.maximumWidth: tab.width - 2
                        }
                    }
                }
            }
            Item { Layout.fillHeight: true }
        }

        Divider { Layout.fillHeight: true; implicitWidth: 1 }

        // Browser for the selected tab
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            PanelHeader {
                Layout.fillWidth: true
                title: library.tabs[library.currentTab].label
                IconButton {
                    objectName: "importButton"
                    iconName: "import"
                    tip: "Import media (⌘I)"
                    visible: library.currentTab === 0
                    onClicked: library.importRequested()
                }
            }

            SearchField {
                Layout.fillWidth: true
                Layout.margins: Theme.space.m
                placeholderText: "Search " + library.tabs[library.currentTab].label.toLowerCase()
            }

            // Effects tab: the real built-in effects. Other tabs: placeholders.
            GridView {
                id: effectsGrid
                visible: library.tabs[library.currentTab].label === "Effects"
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.leftMargin: Theme.space.m
                clip: true
                cellWidth: Math.floor(width / 2)
                cellHeight: 112
                model: library.player ? library.player.availableEffects : []

                delegate: Item {
                    id: tile
                    required property var modelData
                    width: effectsGrid.cellWidth
                    height: effectsGrid.cellHeight
                    // Highlighted when the selected clip uses this effect.
                    readonly property bool active: (library.project.selectedClipInfo.effects || [])
                                                   .some((e) => e.effectId === modelData.id)

                    AbstractButton {
                        anchors.fill: parent
                        anchors.rightMargin: Theme.space.m
                        anchors.bottomMargin: Theme.space.m
                        hoverEnabled: true
                        objectName: "libraryEffect:" + tile.modelData.id
                        onClicked: library.project.addClipEffect(library.project.selectedClip, tile.modelData.id)
                        ToolTip {
                            visible: parent.hovered
                            delay: 600
                            text: "Add to the selected clip"
                        }

                        background: Rectangle {
                            radius: Theme.radius.m
                            color: Theme.color.surfaceRaised
                            border.width: tile.active ? 2 : 1
                            border.color: tile.active ? Theme.color.accent
                                        : parent.hovered ? Theme.color.borderStrong : Theme.color.borderSubtle
                        }
                        contentItem: ColumnLayout {
                            spacing: Theme.space.xs
                            Rectangle {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                Layout.margins: Theme.space.xs
                                radius: Theme.radius.s
                                color: Theme.color.surfaceSunken
                                Icon { anchors.centerIn: parent; name: "effects"; color: tile.active ? Theme.color.accentText : Theme.color.textDisabled }
                            }
                            Text {
                                Layout.leftMargin: Theme.space.s
                                Layout.bottomMargin: Theme.space.s
                                text: tile.modelData.title
                                font: Theme.type.label
                                color: Theme.color.textPrimary
                                elide: Text.ElideRight
                                Layout.maximumWidth: tile.width - Theme.space.xl
                            }
                        }
                    }
                }
            }

            // Media tab: the project's media bin
            Item {
                id: mediaBin
                visible: library.currentTab === 0
                Layout.fillWidth: true
                Layout.fillHeight: true

                GridView {
                    id: mediaGrid
                    anchors.fill: parent
                    anchors.leftMargin: Theme.space.m
                    clip: true
                    cellWidth: Math.floor(width / 2)
                    cellHeight: 118
                    model: library.project.media

                    delegate: Item {
                        id: mediaTile
                        required property var modelData
                        readonly property double mediaId: modelData.id // read by timeline drop targets
                        objectName: "media:" + modelData.name
                        width: mediaGrid.cellWidth
                        height: mediaGrid.cellHeight

                        Drag.active: tileMouse.drag.active
                        Drag.dragType: Drag.Automatic
                        Drag.supportedActions: Qt.CopyAction
                        Drag.keys: ["application/x-tve-media"]
                        Drag.mimeData: { "application/x-tve-media": String(modelData.id) }

                        Rectangle {
                            anchors.fill: parent
                            anchors.rightMargin: Theme.space.m
                            anchors.bottomMargin: Theme.space.m
                            radius: Theme.radius.m
                            color: Theme.color.surfaceRaised
                            border.color: tileMouse.containsMouse ? Theme.color.borderStrong : Theme.color.borderSubtle

                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: Theme.space.xs
                                spacing: Theme.space.xxs

                                Rectangle {
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    radius: Theme.radius.s
                                    color: Theme.color.surfaceSunken
                                    Icon { anchors.centerIn: parent; name: mediaTile.modelData.hasVideo ? "media" : "audio"; color: Theme.color.textDisabled }
                                    Image {
                                        objectName: "mediaThumb:" + mediaTile.modelData.name
                                        visible: mediaTile.modelData.hasVideo
                                        anchors.fill: parent
                                        anchors.margins: 1
                                        asynchronous: true
                                        fillMode: Image.PreserveAspectFit
                                        source: mediaTile.modelData.hasVideo && height >= 4
                                                ? "image://tvethumb/!" + encodeURIComponent(mediaTile.modelData.path) + "@"
                                                  + (mediaTile.modelData.duration * 0.1).toFixed(2) + "@" + Math.round(height * 2)
                                                : ""
                                    }
                                    // Duration on a scrim so it reads over the thumbnail.
                                    Rectangle {
                                        anchors { right: parent.right; bottom: parent.bottom; margins: Theme.space.xs }
                                        width: durationText.implicitWidth + Theme.space.s
                                        height: durationText.implicitHeight + Theme.space.xxs
                                        radius: Theme.radius.s
                                        color: Theme.color.overlay
                                        Text {
                                            id: durationText
                                            anchors.centerIn: parent
                                            text: library.project.timecode(mediaTile.modelData.duration).substring(3, 8)
                                            font: Theme.type.caption
                                            color: "#FFFFFF"
                                        }
                                    }
                                }
                                Text {
                                    Layout.fillWidth: true
                                    Layout.leftMargin: Theme.space.xs
                                    text: mediaTile.modelData.name
                                    font: Theme.type.label
                                    color: Theme.color.textPrimary
                                    elide: Text.ElideMiddle
                                }
                                Text {
                                    Layout.fillWidth: true
                                    Layout.leftMargin: Theme.space.xs
                                    Layout.rightMargin: Theme.space.xs
                                    Layout.bottomMargin: Theme.space.xxs
                                    elide: Text.ElideRight
                                    text: !mediaTile.modelData.hasVideo
                                          ? "Audio only"
                                          : mediaTile.modelData.width + "×" + mediaTile.modelData.height
                                            + " · " + mediaTile.modelData.frameRate.toFixed(mediaTile.modelData.frameRate % 1 ? 2 : 0) + " fps"
                                            + (mediaTile.modelData.hasAudio ? " · sound" : "")
                                    font: Theme.type.caption
                                    color: Theme.color.textSecondary
                                }
                            }
                        }

                        MouseArea {
                            id: tileMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            drag.target: dragProxy
                            onDoubleClicked: library.project.addClip(mediaTile.modelData.id)
                            onPressed: mediaTile.grabToImage((result) => mediaTile.Drag.imageSource = result.url, Qt.size(120, 90))
                        }
                        Item { id: dragProxy }

                        ToolTip {
                            visible: tileMouse.containsMouse && !tileMouse.drag.active
                            delay: 800
                            text: "Double-click to add to the timeline, or drag onto a track"
                        }
                    }
                }

                Column {
                    anchors.centerIn: parent
                    visible: library.project.media.length === 0
                    spacing: Theme.space.s
                    width: parent.width - Theme.space.xl * 2

                    Icon { anchors.horizontalCenter: parent.horizontalCenter; name: "import"; size: 28 }
                    Text {
                        width: parent.width
                        text: "Import or drop video files here"
                        font: Theme.type.heading
                        color: Theme.color.textPrimary
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                    }
                    TextButton {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: "Import media…"
                        iconName: "import"
                        variant: "primary"
                        onClicked: library.importRequested()
                    }
                }

                DropArea {
                    anchors.fill: parent
                    keys: ["text/uri-list"]
                    onDropped: (drop) => {
                        if (drop.hasUrls) {
                            library.project.importMedia(drop.urls)
                            drop.accept(Qt.CopyAction)
                        }
                    }
                }
            }

            Item {
                visible: !effectsGrid.visible && !mediaBin.visible
                Layout.fillWidth: true
                Layout.fillHeight: true

                GridLayout {
                    anchors.fill: parent
                    anchors.margins: Theme.space.m
                    columns: 2
                    rowSpacing: Theme.space.m
                    columnSpacing: Theme.space.m

                    Repeater {
                        model: 6
                        PlaceholderBox {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 96
                            label: index === 0 ? library.tabs[library.currentTab].phase : ""
                        }
                    }
                    Item { Layout.fillHeight: true; Layout.columnSpan: 2 }
                }
            }
        }
    }
}
