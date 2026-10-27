import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import TechieVideoEditor.Theme

// Left panel: a vertical tab rail of content types and the selected type's browser.
Rectangle {
    id: library
    required property var player // VideoView, for the live effects list
    property int currentTab: 0

    color: Theme.color.surface

    readonly property var tabs: [
        { icon: "media", label: "Media", phase: "Import and bins arrive in Phase 1" },
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
                IconButton { iconName: "import"; tip: "Import media (Phase 1)"; enabled: false; visible: library.currentTab === 0 }
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
                    readonly property bool active: library.player.enabledEffectCount >= 0
                                                   && library.player.isEffectEnabled(modelData.id)

                    AbstractButton {
                        anchors.fill: parent
                        anchors.rightMargin: Theme.space.m
                        anchors.bottomMargin: Theme.space.m
                        hoverEnabled: true
                        onClicked: library.player.setEffectEnabled(tile.modelData.id, !tile.active)

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

            Item {
                visible: !effectsGrid.visible
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
