import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import TechieVideoEditor.Theme

// Import from the Photos library, the Music library or a device (iPhone, camera, memory
// card): pick an album, playlist or device, choose items, and import them into the media
// bin (Photos and devices copy them into the project's Imports folder first).
Popup {
    id: panel
    required property var project
    readonly property var imports: project.imports

    property string source: "photos"
    property string access: "undetermined"
    property var collections: []
    property string collection: ""
    property var items: []
    // Chosen item ids, as an object used as a set.
    property var chosen: ({})
    readonly property int chosenCount: Object.keys(chosen).length

    readonly property var sources: [
        { id: "photos", label: "Photos", noun: "album" },
        { id: "music", label: "Music", noun: "playlist" },
        { id: "devices", label: "Devices", noun: "device" }
    ]

    function show(name) {
        source = name
        open()
        refresh()
    }
    function refresh() {
        access = imports.access(source)
        collections = access === "granted" ? imports.collections(source) : []
        if (!collections.some((c) => c.id === collection))
            collection = collections.length > 0 ? collections[0].id : ""
        loadItems()
    }
    function loadItems() {
        items = collection !== "" ? imports.items(source, collection) : []
        chosen = ({})
    }
    function toggle(id) {
        const next = Object.assign({}, chosen)
        if (next[id])
            delete next[id]
        else
            next[id] = true
        chosen = next
    }
    function chooseAll() {
        const next = {}
        for (const item of items)
            next[item.id] = true
        chosen = next
    }
    function importChosen() {
        // In the order shown.
        const ids = items.filter((i) => chosen[i.id]).map((i) => i.id)
        if (project.importFrom(source, ids))
            chosen = ({})
    }
    function duration(seconds) {
        const whole = Math.round(seconds)
        return Math.floor(whole / 60) + ":" + String(whole % 60).padStart(2, "0")
    }

    onSourceChanged: { collection = ""; if (opened) refresh() }
    onCollectionChanged: if (opened) loadItems()

    Connections {
        target: panel.imports
        function onChanged() { if (panel.opened) panel.refresh() }
        function onFetched(paths, errors) { if (panel.opened && paths.length > 0 && errors.length === 0) panel.close() }
    }

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(920, parent ? parent.width - Theme.space.xl * 2 : 920)
    height: Math.min(620, parent ? parent.height - Theme.space.xl * 2 : 620)
    modal: true
    padding: Theme.space.l
    closePolicy: imports.fetching ? Popup.NoAutoClose : Popup.CloseOnEscape | Popup.CloseOnPressOutside

    background: Rectangle {
        radius: Theme.radius.l
        color: Theme.color.surfaceRaised
        border.color: Theme.color.borderStrong
    }

    contentItem: ColumnLayout {
        spacing: Theme.space.m

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.space.m
            Text { text: "Import from"; font: Theme.type.title; color: Theme.color.textPrimary }
            Row {
                spacing: Theme.space.xxs
                Repeater {
                    model: panel.sources
                    delegate: AbstractButton {
                        id: sourceButton
                        required property var modelData
                        objectName: "importSource:" + modelData.id
                        checked: panel.source === modelData.id
                        hoverEnabled: true
                        implicitWidth: sourceLabel.implicitWidth + Theme.space.l * 2
                        implicitHeight: 28
                        enabled: !panel.imports.fetching
                        onClicked: panel.source = modelData.id
                        background: Rectangle {
                            radius: Theme.radius.s
                            color: sourceButton.checked ? Theme.color.accentSubtle
                                 : sourceButton.hovered ? Theme.color.surfaceHover : "transparent"
                            border.color: sourceButton.checked ? Theme.color.accent : Theme.color.borderSubtle
                        }
                        contentItem: Text {
                            id: sourceLabel
                            text: sourceButton.modelData.label
                            font: Theme.type.label
                            color: sourceButton.checked ? Theme.color.accentText : Theme.color.textSecondary
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                }
            }
            Item { Layout.fillWidth: true }
            IconButton {
                objectName: "closeImport"
                iconName: "close"
                tip: "Close"
                enabled: !panel.imports.fetching
                onClicked: panel.close()
            }
        }

        // Asking for access
        ColumnLayout {
            visible: panel.access !== "granted"
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Theme.space.m
            Item { Layout.fillHeight: true }
            Text {
                objectName: "importAccessText"
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                text: panel.access === "denied"
                      ? "TechieVideoEditor isn't allowed to read your " + (panel.source === "music" ? "Music" : "Photos")
                        + " library. Allow it in System Settings › Privacy & Security."
                      : "To import from your " + (panel.source === "music" ? "Music" : "Photos")
                        + " library, allow TechieVideoEditor to read it. Only what you choose is imported."
                font: Theme.type.body
                color: Theme.color.textSecondary
            }
            TextButton {
                objectName: "allowImportAccess"
                Layout.alignment: Qt.AlignHCenter
                text: panel.access === "denied" ? "Open System Settings" : "Allow access"
                variant: "primary"
                onClicked: panel.access === "denied" ? panel.imports.openPrivacySettings(panel.source)
                                                     : panel.imports.requestAccess(panel.source)
            }
            Item { Layout.fillHeight: true }
        }

        RowLayout {
            visible: panel.access === "granted"
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Theme.space.m

            // Albums, playlists or devices
            ListView {
                id: collectionList
                objectName: "importCollections"
                Layout.preferredWidth: 200
                Layout.fillHeight: true
                clip: true
                spacing: Theme.space.xxs
                model: panel.collections
                delegate: AbstractButton {
                    id: collectionButton
                    required property var modelData
                    objectName: "importCollection:" + modelData.id
                    width: ListView.view.width
                    implicitHeight: 32
                    checked: panel.collection === modelData.id
                    hoverEnabled: true
                    enabled: !panel.imports.fetching
                    onClicked: panel.collection = modelData.id
                    background: Rectangle {
                        radius: Theme.radius.s
                        color: collectionButton.checked ? Theme.color.accentSubtle
                             : collectionButton.hovered ? Theme.color.surfaceHover : "transparent"
                    }
                    contentItem: RowLayout {
                        spacing: Theme.space.xs
                        Text {
                            Layout.fillWidth: true
                            leftPadding: Theme.space.s
                            text: collectionButton.modelData.title
                            font: Theme.type.label
                            color: collectionButton.checked ? Theme.color.accentText : Theme.color.textPrimary
                            elide: Text.ElideRight
                        }
                        Text {
                            rightPadding: Theme.space.s
                            text: collectionButton.modelData.count >= 0 ? collectionButton.modelData.count : "…"
                            font: Theme.type.caption
                            color: Theme.color.textSecondary
                        }
                    }
                }
                Text {
                    anchors.fill: parent
                    anchors.margins: Theme.space.s
                    visible: collectionList.count === 0
                    text: panel.source === "devices" ? "Connect an iPhone, a camera or a memory card"
                                                     : "Nothing here yet"
                    font: Theme.type.body
                    color: Theme.color.textSecondary
                    wrapMode: Text.WordWrap
                }
            }

            Divider { Layout.fillHeight: true; implicitWidth: 1 }

            // Items
            GridView {
                id: itemGrid
                objectName: "importItems"
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                cellWidth: Math.floor(width / Math.max(1, Math.floor(width / 150)))
                cellHeight: panel.source === "music" ? 64 : cellWidth * 0.8 + 24
                model: panel.items
                ScrollBar.vertical: ScrollBar {}

                delegate: Item {
                    id: tile
                    required property var modelData
                    readonly property bool picked: !!panel.chosen[modelData.id]
                    width: itemGrid.cellWidth
                    height: itemGrid.cellHeight

                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: Theme.space.xxs
                        radius: Theme.radius.m
                        color: tileMouse.containsMouse ? Theme.color.surfaceHover : Theme.color.surfaceSunken
                        border.width: tile.picked ? 2 : 1
                        border.color: tile.picked ? Theme.color.accent : Theme.color.borderSubtle

                        // Pictures: a thumbnail with its length; songs: a row with the artist.
                        Item {
                            visible: panel.source !== "music"
                            anchors.fill: parent
                            anchors.margins: Theme.space.xxs
                            Icon {
                                anchors.centerIn: picture
                                name: tile.modelData.kind === "video" ? "media" : "stickers"
                                color: Theme.color.textDisabled
                            }
                            Image {
                                id: picture
                                anchors { left: parent.left; right: parent.right; top: parent.top; bottom: caption.top; bottomMargin: Theme.space.xxs }
                                asynchronous: true
                                fillMode: Image.PreserveAspectFit
                                source: panel.source !== "music" ? tile.modelData.thumbnail : ""
                            }
                            Rectangle {
                                visible: tile.modelData.kind === "video"
                                anchors { right: picture.right; bottom: picture.bottom; margins: Theme.space.xs }
                                width: lengthText.implicitWidth + Theme.space.s
                                height: lengthText.implicitHeight + Theme.space.xxs
                                radius: Theme.radius.s
                                color: Theme.color.overlay
                                Text { id: lengthText; anchors.centerIn: parent; text: panel.duration(tile.modelData.duration); font: Theme.type.caption; color: "#FFFFFF" }
                            }
                            Text {
                                id: caption
                                anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                                height: 18
                                leftPadding: Theme.space.xs
                                text: tile.modelData.created > 0 ? new Date(tile.modelData.created).toLocaleDateString(Qt.locale(), Locale.ShortFormat)
                                                                 : tile.modelData.title
                                font: Theme.type.caption
                                color: Theme.color.textSecondary
                                elide: Text.ElideRight
                            }
                        }
                        RowLayout {
                            visible: panel.source === "music"
                            anchors.fill: parent
                            anchors.margins: Theme.space.s
                            spacing: Theme.space.s
                            Rectangle {
                                Layout.preferredWidth: 40
                                Layout.preferredHeight: 40
                                radius: Theme.radius.s
                                color: Theme.color.surfaceRaised
                                Icon { anchors.centerIn: parent; name: "audio"; color: Theme.color.textDisabled }
                                Image {
                                    anchors.fill: parent
                                    asynchronous: true
                                    fillMode: Image.PreserveAspectCrop
                                    source: panel.source === "music" ? tile.modelData.thumbnail : ""
                                }
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2
                                Text { Layout.fillWidth: true; text: tile.modelData.title; font: Theme.type.label; color: Theme.color.textPrimary; elide: Text.ElideRight }
                                Text {
                                    Layout.fillWidth: true
                                    text: (tile.modelData.detail ? tile.modelData.detail + " · " : "") + panel.duration(tile.modelData.duration)
                                    font: Theme.type.caption
                                    color: Theme.color.textSecondary
                                    elide: Text.ElideRight
                                }
                            }
                        }

                        // The check mark of a chosen item.
                        Rectangle {
                            visible: tile.picked
                            anchors { right: parent.right; top: parent.top; margins: Theme.space.xs }
                            width: 18
                            height: 18
                            radius: 9
                            color: Theme.color.accent
                            Text { anchors.centerIn: parent; text: "✓"; font: Theme.type.caption; color: "#FFFFFF" }
                        }
                    }
                    MouseArea {
                        id: tileMouse
                        objectName: "importItem:" + tile.modelData.id
                        anchors.fill: parent
                        hoverEnabled: true
                        enabled: !panel.imports.fetching
                        onClicked: panel.toggle(tile.modelData.id)
                    }
                }

                Text {
                    anchors.centerIn: parent
                    visible: itemGrid.count === 0 && collectionList.count > 0
                    text: "No photos, videos or songs here"
                    font: Theme.type.body
                    color: Theme.color.textSecondary
                }
            }
        }

        // Choosing and importing
        RowLayout {
            visible: panel.access === "granted"
            Layout.fillWidth: true
            spacing: Theme.space.s
            TextButton {
                objectName: "chooseAllImports"
                text: panel.chosenCount === panel.items.length && panel.items.length > 0 ? "Choose none" : "Choose all"
                variant: "ghost"
                enabled: panel.items.length > 0 && !panel.imports.fetching
                onClicked: panel.chosenCount === panel.items.length ? panel.chosen = ({}) : panel.chooseAll()
            }
            Text {
                objectName: "importStatus"
                Layout.fillWidth: true
                text: panel.imports.fetching
                      ? "Importing " + Math.min(panel.imports.fetchedCount + 1, panel.imports.fetchTotal) + " of " + panel.imports.fetchTotal + "…"
                      : panel.chosenCount === 0 ? "Choose what to import"
                      : panel.chosenCount + (panel.chosenCount === 1 ? " item" : " items") + " chosen"
                font: Theme.type.label
                color: Theme.color.textSecondary
                elide: Text.ElideRight
            }
            ProgressBar {
                visible: panel.imports.fetching
                Layout.preferredWidth: 160
                from: 0
                to: Math.max(1, panel.imports.fetchTotal)
                value: panel.imports.fetchedCount
            }
            TextButton {
                objectName: "importChosen"
                text: panel.source === "music" ? "Add to media" : "Import"
                variant: "primary"
                enabled: panel.chosenCount > 0 && !panel.imports.fetching
                onClicked: panel.importChosen()
            }
        }
    }
}
