import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import TechieVideoEditor.Theme

// The library's Store tab: content packs — templates, music and sounds, stickers, effects
// and transitions — from the pack catalogue. Each pack is checked against the catalogue's
// signature and every file against its SHA-256 before it is installed; updates replace a
// pack whole. What a pack brings shows up in the other tabs (effects and transitions the
// next time the app opens).
ScrollView {
    id: page
    required property var project
    readonly property var store: project.store
    property string filter: "" // "" or "installed"
    readonly property var shown: store.packs.filter((p) => filter === "" || p.installedVersion > 0)

    readonly property var kindNames: ({ sounds: "Music & sounds", templates: "Templates", stickers: "Stickers",
                                        effects: "Effects", transitions: "Transitions" })

    contentWidth: availableWidth
    clip: true

    Component.onCompleted: if (!store.checked && !store.busy && store.hasTrustedKey) store.refresh()

    ColumnLayout {
        width: page.availableWidth
        spacing: Theme.space.s

        Text {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.space.m
            Layout.rightMargin: Theme.space.m
            Layout.topMargin: Theme.space.s
            text: "Packs of templates, music, stickers and effects. Each is checked against our signature before it is installed."
            font: Theme.type.body
            color: Theme.color.textSecondary
            wrapMode: Text.WordWrap
        }

        // No key to check catalogues with: nothing can be trusted, so nothing is fetched.
        Text {
            objectName: "storeNoKey"
            visible: !page.store.hasTrustedKey
            Layout.fillWidth: true
            Layout.leftMargin: Theme.space.m
            Layout.rightMargin: Theme.space.m
            text: "This copy of the app trusts no signing key yet, so it can't check packs. Whoever publishes them adds theirs (tools/packs/README.md)."
            font: Theme.type.caption
            color: Theme.color.warning
            wrapMode: Text.WordWrap
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.space.m
            Layout.rightMargin: Theme.space.m
            spacing: Theme.space.xs
            Repeater {
                model: [{ id: "", label: "All" }, { id: "installed", label: "Installed" }]
                delegate: AbstractButton {
                    id: chip
                    required property var modelData
                    objectName: "storeFilter:" + (modelData.id || "all")
                    checked: page.filter === modelData.id
                    hoverEnabled: true
                    implicitWidth: chipLabel.implicitWidth + Theme.space.m * 2
                    implicitHeight: 24
                    onClicked: page.filter = modelData.id
                    background: Rectangle {
                        radius: height / 2
                        color: chip.checked ? Theme.color.accentSubtle : chip.hovered ? Theme.color.surfaceHover : "transparent"
                        border.color: chip.checked ? Theme.color.accent : Theme.color.borderSubtle
                    }
                    contentItem: Text {
                        id: chipLabel
                        text: chip.modelData.label
                        font: Theme.type.caption
                        color: chip.checked ? Theme.color.accentText : Theme.color.textSecondary
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }
            }
            Item { Layout.fillWidth: true }
            TextButton {
                objectName: "storeRefresh"
                visible: !page.store.busy
                text: page.store.checked ? "Check again" : "Check for packs"
                variant: "ghost"
                enabled: page.store.hasTrustedKey
                onClicked: page.store.refresh()
            }
            TextButton {
                objectName: "storeStop"
                visible: page.store.busy
                text: "Stop"
                variant: "ghost"
                onClicked: page.store.cancel()
            }
        }

        Text {
            objectName: "storeStatus"
            Layout.fillWidth: true
            Layout.leftMargin: Theme.space.m
            Layout.rightMargin: Theme.space.m
            visible: text !== ""
            text: page.store.lastError !== "" ? page.store.lastError
                : page.store.status !== "" ? page.store.status
                : page.store.checked ? "Catalogue of " + (page.store.published || "today") + " · checked" : ""
            font: Theme.type.caption
            color: page.store.lastError !== "" ? Theme.color.danger : Theme.color.textSecondary
            wrapMode: Text.WordWrap
        }
        Text {
            objectName: "storeRestart"
            visible: page.store.restartNeeded
            Layout.fillWidth: true
            Layout.leftMargin: Theme.space.m
            Layout.rightMargin: Theme.space.m
            text: "Effects and transitions from packs change the next time you open the app."
            font: Theme.type.caption
            color: Theme.color.warning
            wrapMode: Text.WordWrap
        }

        Repeater {
            model: page.shown
            delegate: Rectangle {
                id: card
                required property var modelData
                objectName: "pack:" + modelData.id
                Layout.fillWidth: true
                Layout.leftMargin: Theme.space.m
                Layout.rightMargin: Theme.space.m
                implicitHeight: cardColumn.implicitHeight + Theme.space.s * 2
                radius: Theme.radius.m
                color: Theme.color.surfaceRaised
                border.color: Theme.color.borderSubtle

                ColumnLayout {
                    id: cardColumn
                    anchors.fill: parent
                    anchors.margins: Theme.space.s
                    spacing: Theme.space.xs

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: Math.min(width * 9 / 16, 84)
                        visible: card.modelData.preview !== ""
                        radius: Theme.radius.s
                        color: Theme.color.surfaceSunken
                        clip: true
                        Image {
                            objectName: "packPreview:" + card.modelData.id
                            anchors.fill: parent
                            source: card.modelData.preview
                            fillMode: Image.PreserveAspectCrop
                            asynchronous: true
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.space.s
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 0
                            Text {
                                Layout.fillWidth: true
                                text: card.modelData.title
                                font: Theme.type.label
                                color: Theme.color.textPrimary
                                elide: Text.ElideRight
                            }
                            Text {
                                Layout.fillWidth: true
                                text: [card.modelData.author, card.modelData.sizeText, "v" + card.modelData.version]
                                          .filter((s) => s).join(" · ")
                                font: Theme.type.caption
                                color: Theme.color.textSecondary
                                elide: Text.ElideRight
                            }
                        }
                    }
                    // What can be done with it.
                    RowLayout {
                        Layout.fillWidth: true
                        visible: card.modelData.state !== "installing"
                        spacing: Theme.space.xs
                        TextButton {
                            objectName: "packInstall:" + card.modelData.id
                            visible: card.modelData.state === "available" || card.modelData.state === "update"
                            text: card.modelData.state === "update" ? "Update" : "Get"
                            variant: "primary"
                            enabled: !page.store.busy
                            onClicked: page.store.install(card.modelData.id)
                        }
                        TextButton {
                            objectName: "packRemove:" + card.modelData.id
                            visible: card.modelData.installedVersion > 0 && card.modelData.state !== "installing"
                            text: "Remove"
                            variant: "ghost"
                            enabled: !page.store.busy
                            onClicked: page.store.remove(card.modelData.id)
                        }
                        Item { Layout.fillWidth: true }
                    }
                    ProgressBar {
                        objectName: "packProgress:" + card.modelData.id
                        visible: card.modelData.state === "installing"
                        Layout.fillWidth: true
                        from: 0
                        to: 1
                        value: card.modelData.progress
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: card.modelData.description !== ""
                        text: card.modelData.description
                        font: Theme.type.body
                        color: Theme.color.textSecondary
                        wrapMode: Text.WordWrap
                    }
                    Flow {
                        Layout.fillWidth: true
                        spacing: Theme.space.xs
                        Repeater {
                            model: card.modelData.contents
                            delegate: Rectangle {
                                required property string modelData
                                implicitWidth: kindLabel.implicitWidth + Theme.space.s * 2
                                implicitHeight: 20
                                radius: height / 2
                                color: Theme.color.surfaceSunken
                                Text {
                                    id: kindLabel
                                    anchors.centerIn: parent
                                    text: page.kindNames[parent.modelData] || parent.modelData
                                    font: Theme.type.caption
                                    color: Theme.color.textSecondary
                                }
                            }
                        }
                        Text {
                            visible: card.modelData.state === "installed"
                            text: "Installed"
                            font: Theme.type.caption
                            color: Theme.color.success
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: card.modelData.license !== ""
                        text: "Licence: " + card.modelData.license
                        font: Theme.type.caption
                        color: Theme.color.textDisabled
                        wrapMode: Text.WordWrap
                    }
                }
            }
        }

        Text {
            objectName: "storeEmpty"
            visible: page.shown.length === 0 && !page.store.busy
            Layout.fillWidth: true
            Layout.leftMargin: Theme.space.m
            Layout.rightMargin: Theme.space.m
            text: page.filter === "installed" ? "No packs installed yet." : page.store.checked ? "The catalogue has no packs yet." : ""
            font: Theme.type.body
            color: Theme.color.textDisabled
            wrapMode: Text.WordWrap
        }

        // Where the catalogue comes from.
        Section {
            objectName: "storeSource"
            Layout.fillWidth: true
            expanded: false
            title: "Catalogue address"
            TextField {
                id: address
                objectName: "storeAddress"
                Layout.fillWidth: true
                text: page.store.catalogueUrl
                color: Theme.color.textPrimary
                selectByMouse: true
                onEditingFinished: page.store.catalogueUrl = text
                background: Rectangle {
                    implicitHeight: 30
                    radius: Theme.radius.s
                    color: Theme.color.surfaceSunken
                    border.color: address.activeFocus ? Theme.color.accent : Theme.color.borderSubtle
                }
            }
            TextButton {
                objectName: "storeAddressDefault"
                visible: page.store.catalogueUrl !== page.store.defaultCatalogueUrl
                text: "Use the default"
                variant: "ghost"
                onClicked: page.store.catalogueUrl = ""
            }
        }
        Item { Layout.preferredHeight: Theme.space.m }
    }
}
