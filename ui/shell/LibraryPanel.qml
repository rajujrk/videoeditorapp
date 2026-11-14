import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import TechieVideoEditor.Theme
import TechieVideoEditor.Player
import "TitleTemplates.js" as TitleTemplates

// Left panel: a vertical tab rail of content types and the selected type's browser.
Rectangle {
    id: library
    required property var player  // VideoView, for the live effects list
    required property var project // ProjectController
    property int currentTab: 0
    // The Effects tab's category ("" for all) and the search text (Effects and Transitions).
    property string effectCategory: ""
    property alias searchText: searchField.text
    signal importRequested()
    // "photos", "music" or "devices"
    signal importFromRequested(string source)
    signal sourceRequested(var media) // open it in the player's source monitor
    signal momentRequested(var media, real start, real end) // the same, In and Out around a moment
    signal cloudSettingsRequested()
    onCurrentTabChanged: {
        searchField.text = ""
        favouriteTag = ""
    }
    // Favourites (Favourites.qml's store): the tag chosen among a tab's favourites ("" for all),
    // the Filters tab's choice ("" or "favourites"), and the tile whose tags are being edited.
    property string favouriteTag: ""
    property string lookCategory: ""
    // Whether a tile shows: among the favourites (with the tag) when "favourites" is chosen,
    // else in its category; and found by the search in its title or its tags.
    function shows(kind, id, chosen, inCategory, title) {
        const items = Favourites.items // (read here, so the grids follow changes)
        const key = kind + ":" + id
        if (chosen === "favourites") {
            if (items[key] === undefined || (favouriteTag !== "" && items[key].indexOf(favouriteTag) < 0))
                return false
        } else if (!inCategory) {
            return false
        }
        const text = searchField.text.trim().toLowerCase()
        return text === "" || title.toLowerCase().indexOf(text) >= 0
               || (items[key] || []).some((tag) => tag.indexOf(text) >= 0)
    }
    // Every tag on the favourites of a kind, sorted (from Favourites.items, so bindings follow it).
    function tagsIn(items, kind) {
        const tags = []
        for (const key in items)
            if (key.startsWith(kind + ":"))
                for (const tag of items[key])
                    if (tags.indexOf(tag) < 0)
                        tags.push(tag)
        return tags.sort()
    }
    function editTags(kind, id, title) {
        tagEditor.kind = kind
        tagEditor.itemId = id
        tagEditor.itemTitle = title
        tagEditor.open()
    }

    readonly property var effectCategories: [
        { id: "", label: qsTr("All") }, { id: "favourites", label: "★" }, { id: "blur", label: qsTr("Blur") }, { id: "color", label: qsTr("Colour") },
        { id: "stylize", label: qsTr("Stylize") }, { id: "distort", label: qsTr("Distort") }, { id: "glitch", label: qsTr("Glitch") },
        { id: "light", label: qsTr("Light") }, { id: "frame", label: qsTr("Frame") }, { id: "overlay", label: qsTr("Overlay") }
    ]
    function matchesSearch(title) {
        const text = searchField.text.trim().toLowerCase()
        return text === "" || title.toLowerCase().indexOf(text) >= 0
    }

    color: Theme.color.surface

    // A tab's favourites, by tag: shown when its ★ Favourites chip is chosen.
    component TagChips: Flow {
        id: tagChips
        required property string kind
        required property var panel // the library
        property bool shown: false
        readonly property var tags: tagChips.panel.tagsIn(Favourites.items, kind)
        visible: shown && tags.length > 0
        Layout.fillWidth: true
        Layout.leftMargin: Theme.space.m
        Layout.rightMargin: Theme.space.m
        Layout.bottomMargin: Theme.space.s
        spacing: Theme.space.xs
        Repeater {
            model: [""].concat(tagChips.tags)
            delegate: AbstractButton {
                id: tagChip
                required property string modelData
                objectName: "favouriteTag:" + tagChips.kind + ":" + (modelData || "all")
                checked: tagChips.panel.favouriteTag === modelData
                hoverEnabled: true
                implicitWidth: tagChipLabel.implicitWidth + Theme.space.m * 2
                implicitHeight: 22
                onClicked: tagChips.panel.favouriteTag = modelData
                background: Rectangle {
                    radius: height / 2
                    color: tagChip.checked ? Theme.color.accentSubtle : tagChip.hovered ? Theme.color.surfaceHover : "transparent"
                    border.color: tagChip.checked ? Theme.color.accent : Theme.color.borderSubtle
                }
                contentItem: Text {
                    id: tagChipLabel
                    text: tagChip.modelData === "" ? qsTr("Every tag") : "#" + tagChip.modelData
                    font: Theme.type.caption
                    color: tagChip.checked ? Theme.color.accentText : Theme.color.textSecondary
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }
    }

    // Title templates (original designs, TitleTemplates.js): {name, category, style}; the
    // Titles tab's category ("" for all).
    // Each with its title (the name, translated) and its sample text in the app's language.
    readonly property var titleStyles: TitleTemplates.templates.map((t) => Object.assign({}, t, {
        title: qsTranslate("TitleTemplates", t.name),
        style: Object.assign({}, t.style, { text: qsTranslate("TitleTemplates", t.style.text) })
    }))
    property string titleCategory: ""
    // The Titles tab's tiles: the user's own presets, the templates, then logo reveals (each
    // asks for the logo picture).
    readonly property var titleTiles: library.project.titlePresets.map((p) => ({ name: p.name, title: p.name, category: "mine", style: p.style, preset: true }))
        .concat(library.titleStyles)
        .concat(library.project.logoReveals.map((r) => ({ name: r.id, title: r.title, category: "logos", style: ({}), reveal: r.id })))
    // The logo reveal waiting for its picture.
    property string pendingReveal: ""
    // The Stickers tab's category ("" for all).
    property string stickerCategory: ""
    readonly property var stickerCategories: [
        { id: "", label: qsTr("All") }, { id: "favourites", label: "★" }, { id: "shapes", label: qsTr("Shapes") }, { id: "marks", label: qsTr("Arrows & marks") },
        { id: "nature", label: qsTr("Nature") }, { id: "emoji", label: qsTr("Emoji") }, { id: "party", label: qsTr("Party") }
    ]
    // The Transitions tab's category ("" for all).
    property string transitionCategory: ""
    readonly property var transitionCategories: [
        { id: "", label: qsTr("All") }, { id: "favourites", label: "★" }, { id: "basic", label: qsTr("Basic") }, { id: "slide", label: qsTr("Slide") },
        { id: "wipe", label: qsTr("Wipe") }, { id: "shape", label: qsTr("Shape") }, { id: "zoom", label: qsTr("Zoom") },
        { id: "3d", label: qsTr("3D") }, { id: "distort", label: qsTr("Distort") }, { id: "luma", label: qsTr("Luma") },
        { id: "stylize", label: qsTr("Stylize") }, { id: "blur", label: qsTr("Blur") }, { id: "light", label: qsTr("Light") },
        { id: "overlays", label: qsTr("Overlays") }
    ]

    readonly property var tabs: [
        { icon: "media", key: "Media", label: qsTr("Media"), phase: "" },
        { icon: "audio", key: "Audio", label: qsTr("Audio"), phase: "" },
        { icon: "generate", key: "Generate", label: qsTr("Generate"), phase: "" },
        { icon: "assistant", key: "Copilot", label: qsTr("Copilot"), phase: "" },
        { icon: "titles", key: "Titles", label: qsTr("Titles"), phase: "" },
        { icon: "transitions", key: "Transitions", label: qsTr("Transitions"), phase: "" },
        { icon: "effects", key: "Effects", label: qsTr("Effects"), phase: "" },
        { icon: "filters", key: "Filters", label: qsTr("Filters"), phase: "" },
        { icon: "stickers", key: "Stickers", label: qsTr("Stickers"), phase: "" },
        { icon: "captions", key: "Captions", label: qsTr("Captions"), phase: "" },
        { icon: "transcript", key: "Transcript", label: qsTr("Transcript"), phase: "" },
        { icon: "templates", key: "Templates", label: qsTr("Templates"), phase: "" },
        { icon: "store", key: "Store", label: qsTr("Store"), phase: "" }
    ]

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // Tab rail
        // (Scrolls when the window is too short for every tab, rather than stretching the panel.)
        Flickable {
            id: rail
            objectName: "libraryTabRail"
            Layout.preferredWidth: Theme.size.tabRail
            Layout.maximumWidth: Theme.size.tabRail
            Layout.fillWidth: false
            Layout.fillHeight: true
            contentWidth: width
            contentHeight: railColumn.implicitHeight + Theme.space.s * 2
            boundsBehavior: Flickable.StopAtBounds
            clip: true
            ScrollIndicator.vertical: ScrollIndicator {}

        ColumnLayout {
            id: railColumn
            y: Theme.space.s
            width: rail.width
            spacing: Theme.space.xxs

            Repeater {
                model: library.tabs
                delegate: AbstractButton {
                    id: tab
                    required property var modelData
                    required property int index
                    objectName: "libraryTab:" + modelData.key
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
        }
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
                    tip: qsTr("Import media (⌘I)")
                    visible: library.currentTab === 0
                    onClicked: library.importRequested()
                }
                IconButton {
                    objectName: "colourClipButton"
                    iconName: "colour-clip"
                    tip: qsTr("Add a colour clip at the playhead (choose its colours or gradient in the inspector)")
                    visible: library.currentTab === 0
                    onClicked: library.project.addColorClip({ kind: "solid", from: [0.12, 0.44, 0.92, 1] })
                }
                IconButton {
                    objectName: "importFromButton"
                    iconName: "chevron-down"
                    tip: qsTr("Import from Photos, Music or a device")
                    visible: library.currentTab === 0
                    onClicked: importFromMenu.popup(this, 0, height)
                    Menu {
                        id: importFromMenu
                        MenuItem { objectName: "importFrom:photos"; text: qsTr("From Photos…"); onTriggered: library.importFromRequested("photos") }
                        MenuItem { objectName: "importFrom:music"; text: qsTr("From Music…"); onTriggered: library.importFromRequested("music") }
                        MenuItem { objectName: "importFrom:devices"; text: qsTr("From an iPhone, camera or card…"); onTriggered: library.importFromRequested("devices") }
                        MenuItem { objectName: "importFrom:project"; text: qsTr("A project, as a compound clip…"); onTriggered: projectDialog.open() }
                        MenuItem { objectName: "importFrom:sequence"; text: qsTr("An image sequence…"); onTriggered: sequenceFrameDialog.open() }
                    }
                }
            }

            SearchField {
                id: searchField
                objectName: "librarySearch"
                visible: library.tabs[library.currentTab].key !== "Captions" && library.tabs[library.currentTab].key !== "Transcript"
                         && library.tabs[library.currentTab].key !== "Generate" && library.tabs[library.currentTab].key !== "Templates"
                         && library.tabs[library.currentTab].key !== "Copilot"
                         && library.tabs[library.currentTab].key !== "Store"
                Layout.fillWidth: true
                Layout.margins: Theme.space.m
                placeholderText: qsTr("Search %1").arg(library.tabs[library.currentTab].label.toLowerCase())
            }

            // Transitions tab: category chips, and random transitions on every cut.
            Flow {
                visible: transitionsGrid.visible
                Layout.fillWidth: true
                Layout.leftMargin: Theme.space.m
                Layout.rightMargin: Theme.space.m
                Layout.bottomMargin: Theme.space.s
                spacing: Theme.space.xs
                Repeater {
                    model: library.transitionCategories
                    delegate: AbstractButton {
                        id: transitionChip
                        required property var modelData
                        objectName: "transitionCategory:" + (modelData.id || "all")
                        checked: library.transitionCategory === modelData.id
                        hoverEnabled: true
                        implicitWidth: transitionChipLabel.implicitWidth + Theme.space.m * 2
                        implicitHeight: 24
                        onClicked: library.transitionCategory = modelData.id
                        // (★: the favourites, named in its tooltip to keep the chips short)
                        ToolTip.visible: hovered && modelData.id === "favourites"
                        ToolTip.delay: 400
                        ToolTip.text: qsTr("Favourites")
                        background: Rectangle {
                            radius: height / 2
                            color: transitionChip.checked ? Theme.color.accentSubtle
                                 : transitionChip.hovered ? Theme.color.surfaceHover : "transparent"
                            border.color: transitionChip.checked ? Theme.color.accent : Theme.color.borderSubtle
                        }
                        contentItem: Text {
                            id: transitionChipLabel
                            text: transitionChip.modelData.label
                            font: Theme.type.caption
                            color: transitionChip.checked ? Theme.color.accentText : Theme.color.textSecondary
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                }
                TextButton {
                    objectName: "randomTransitions"
                    text: qsTr("Random on every cut")
                    variant: "ghost"
                    tip: qsTr("Put a different random transition on each cut between clips")
                    onClicked: library.project.applyRandomTransitions()
                }
                // How long a new transition lasts (the default one, ⌘T, and those dropped on a cut).
                RowLayout {
                    spacing: Theme.space.xs
                    height: 24
                    Text { text: qsTr("New ones last"); font: Theme.type.caption; color: Theme.color.textSecondary }
                    TextField {
                        id: defaultLengthField
                        objectName: "defaultTransitionLength"
                        implicitWidth: 52
                        implicitHeight: 24
                        horizontalAlignment: Text.AlignRight
                        font: Theme.type.caption
                        color: Theme.color.textPrimary
                        selectByMouse: true
                        validator: DoubleValidator { bottom: 0.1; top: 10; decimals: 2; notation: DoubleValidator.StandardNotation }
                        text: Fmt.brief(EditingPreferences.defaultTransitionLength, 2)
                        background: Rectangle {
                            radius: Theme.radius.s
                            color: Theme.color.surfaceSunken
                            border.color: defaultLengthField.activeFocus ? Theme.color.accent : Theme.color.borderSubtle
                        }
                        onEditingFinished: {
                            const value = Number.fromLocaleString(Qt.locale(), text)
                            if (!isNaN(value))
                                EditingPreferences.defaultTransitionLength = value
                            text = Fmt.brief(EditingPreferences.defaultTransitionLength, 2)
                        }
                        ToolTip.visible: hovered
                        ToolTip.delay: 600
                        ToolTip.text: qsTr("Seconds, from 0.1 to 10")
                    }
                    Text { text: qsTr("s"); font: Theme.type.caption; color: Theme.color.textSecondary }
                }
            }

            // Stickers tab: category chips over the stickers.
            Flow {
                visible: stickersGrid.visible
                Layout.fillWidth: true
                Layout.leftMargin: Theme.space.m
                Layout.rightMargin: Theme.space.m
                Layout.bottomMargin: Theme.space.s
                spacing: Theme.space.xs
                Repeater {
                    model: library.stickerCategories
                    delegate: AbstractButton {
                        id: stickerChip
                        required property var modelData
                        objectName: "stickerCategory:" + (modelData.id || "all")
                        checked: library.stickerCategory === modelData.id
                        hoverEnabled: true
                        implicitWidth: stickerChipLabel.implicitWidth + Theme.space.m * 2
                        implicitHeight: 24
                        onClicked: library.stickerCategory = modelData.id
                        // (★: the favourites, named in its tooltip to keep the chips short)
                        ToolTip.visible: hovered && modelData.id === "favourites"
                        ToolTip.delay: 400
                        ToolTip.text: qsTr("Favourites")
                        background: Rectangle {
                            radius: height / 2
                            color: stickerChip.checked ? Theme.color.accentSubtle
                                 : stickerChip.hovered ? Theme.color.surfaceHover : "transparent"
                            border.color: stickerChip.checked ? Theme.color.accent : Theme.color.borderSubtle
                        }
                        contentItem: Text {
                            id: stickerChipLabel
                            text: stickerChip.modelData.label
                            font: Theme.type.caption
                            color: stickerChip.checked ? Theme.color.accentText : Theme.color.textSecondary
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                }
            }

            TranscriptPage {
                id: transcriptPage
                objectName: "transcriptPage"
                visible: library.tabs[library.currentTab].key === "Transcript"
                project: library.project
                Layout.fillWidth: true
                Layout.fillHeight: true
            }

            // Made the first time the tab is opened (they read the whole media list).
            Loader {
                id: templatesPage
                active: visible || item !== null
                visible: library.tabs[library.currentTab].key === "Templates"
                Layout.fillWidth: true
                Layout.fillHeight: true
                sourceComponent: TemplatesPage {
                    objectName: "templatesPage"
                    project: library.project
                    onImportRequested: library.importRequested()
                }
            }
            Loader {
                id: storePage
                active: visible || item !== null
                visible: library.tabs[library.currentTab].key === "Store"
                Layout.fillWidth: true
                Layout.fillHeight: true
                sourceComponent: StorePage {
                    objectName: "storePage"
                    project: library.project
                }
            }
            Loader {
                id: copilotPage
                active: visible || item !== null
                visible: library.tabs[library.currentTab].key === "Copilot"
                Layout.fillWidth: true
                Layout.fillHeight: true
                sourceComponent: CopilotPage {
                    objectName: "copilotPage"
                    project: library.project
                    onSettingsRequested: library.cloudSettingsRequested()
                }
            }
            Loader {
                id: generatePage
                active: visible || item !== null
                visible: library.tabs[library.currentTab].key === "Generate"
                Layout.fillWidth: true
                Layout.fillHeight: true
                sourceComponent: GeneratePage {
                    objectName: "generatePage"
                    project: library.project
                    onSettingsRequested: library.cloudSettingsRequested()
                }
            }

            AudioPage {
                id: audioPage
                objectName: "audioPage"
                visible: library.tabs[library.currentTab].key === "Audio"
                project: library.project
                searchText: searchField.text
                Layout.fillWidth: true
                Layout.fillHeight: true
            }

            CaptionsPage {
                id: captionsPage
                objectName: "captionsPage"
                visible: library.tabs[library.currentTab].key === "Captions"
                project: library.project
                Layout.fillWidth: true
                Layout.fillHeight: true
            }

            // Stickers tab: animated stickers (Lottie), playing on hover; click to add one at the
            // playhead.
            TagChips {
                panel: library
                objectName: "stickerTags"
                kind: "sticker"
                shown: stickersGrid.visible && library.stickerCategory === "favourites"
            }
            GridView {
                id: stickersGrid
                objectName: "stickersGrid"
                visible: library.tabs[library.currentTab].key === "Stickers"
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.leftMargin: Theme.space.m
                clip: true
                cellWidth: Math.floor(width / 3)
                cellHeight: cellWidth
                model: library.project.stickers.filter(
                           (s) => library.shows("sticker", s.id, library.stickerCategory,
                                                library.stickerCategory === "" || s.category === library.stickerCategory, s.title))

                delegate: Item {
                    id: stickerTile
                    required property var modelData
                    width: stickersGrid.cellWidth
                    height: stickersGrid.cellHeight

                    FavouriteBadge {
                        z: 10
                        anchors.top: parent.top
                        anchors.right: parent.right
                        anchors.topMargin: Theme.space.xs
                        anchors.rightMargin: Theme.space.s + Theme.space.xs
                        kind: "sticker"
                        itemId: stickerTile.modelData.id
                        hovered: stickerButton.hovered
                        onTagsRequested: library.editTags("sticker", stickerTile.modelData.id, stickerTile.modelData.title)
                    }

                    AbstractButton {
                        id: stickerButton
                        objectName: "librarySticker:" + stickerTile.modelData.id
                        anchors.fill: parent
                        anchors.rightMargin: Theme.space.s
                        anchors.bottomMargin: Theme.space.s
                        hoverEnabled: true
                        onClicked: library.project.addSticker(stickerTile.modelData.id)
                        ToolTip.visible: hovered
                        ToolTip.delay: 600
                        ToolTip.text: qsTr("%1 — click to add at the playhead").arg(stickerTile.modelData.title)
                        background: Rectangle {
                            radius: Theme.radius.m
                            color: stickerButton.hovered ? Theme.color.surfaceHover : Theme.color.surfaceRaised
                            border.color: stickerButton.hovered ? Theme.color.borderStrong : Theme.color.borderSubtle
                        }
                        contentItem: Image {
                            id: stickerPreview
                            objectName: "stickerPreview:" + stickerTile.modelData.id
                            fillMode: Image.PreserveAspectFit
                            asynchronous: true
                            property real seconds: 0.4
                            source: "image://tvesticker/" + stickerTile.modelData.id + "@" + seconds.toFixed(3)
                            anchors.margins: Theme.space.s
                            Timer {
                                running: stickerButton.hovered
                                repeat: true
                                interval: 66
                                onTriggered: stickerPreview.seconds = (stickerPreview.seconds + 1 / 15) % 2
                                onRunningChanged: if (!running) stickerPreview.seconds = 0.4
                            }
                        }
                    }
                }
            }

            // Titles tab: category chips over the templates.
            Flow {
                visible: titlesGrid.visible
                Layout.fillWidth: true
                Layout.leftMargin: Theme.space.m
                Layout.rightMargin: Theme.space.m
                Layout.bottomMargin: Theme.space.s
                spacing: Theme.space.xs
                Repeater {
                    model: [TitleTemplates.categories[0], { id: "favourites", label: "★", own: true }]
                           .concat(TitleTemplates.categories.slice(1))
                    delegate: AbstractButton {
                        id: titleChip
                        required property var modelData
                        objectName: "titleCategory:" + (modelData.id || "all")
                        checked: library.titleCategory === modelData.id
                        hoverEnabled: true
                        implicitWidth: titleChipLabel.implicitWidth + Theme.space.m * 2
                        implicitHeight: 24
                        onClicked: library.titleCategory = modelData.id
                        // (★: the favourites, named in its tooltip to keep the chips short)
                        ToolTip.visible: hovered && modelData.id === "favourites"
                        ToolTip.delay: 400
                        ToolTip.text: qsTr("Favourites")
                        background: Rectangle {
                            radius: height / 2
                            color: titleChip.checked ? Theme.color.accentSubtle : titleChip.hovered ? Theme.color.surfaceHover : "transparent"
                            border.color: titleChip.checked ? Theme.color.accent : Theme.color.borderSubtle
                        }
                        contentItem: Text {
                            id: titleChipLabel
                            text: titleChip.modelData.own ? titleChip.modelData.label : qsTranslate("TitleTemplates", titleChip.modelData.label)
                            font: Theme.type.caption
                            color: titleChip.checked ? Theme.color.accentText : Theme.color.textSecondary
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                }
            }

            // Effects tab: category chips over the effects.
            Flow {
                visible: effectsGrid.visible
                Layout.fillWidth: true
                Layout.leftMargin: Theme.space.m
                Layout.rightMargin: Theme.space.m
                Layout.bottomMargin: Theme.space.s
                spacing: Theme.space.xs
                Repeater {
                    model: library.effectCategories
                    delegate: AbstractButton {
                        id: chip
                        required property var modelData
                        objectName: "effectCategory:" + (modelData.id || "all")
                        checked: library.effectCategory === modelData.id
                        hoverEnabled: true
                        implicitWidth: chipLabel.implicitWidth + Theme.space.m * 2
                        implicitHeight: 24
                        onClicked: library.effectCategory = modelData.id
                        // (★: the favourites, named in its tooltip to keep the chips short)
                        ToolTip.visible: hovered && modelData.id === "favourites"
                        ToolTip.delay: 400
                        ToolTip.text: qsTr("Favourites")
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
            }

            // Effects tab: the built-in effects with previews, by category, filtered by the
            // chips and the search.
            TagChips {
                panel: library
                objectName: "effectTags"
                kind: "effect"
                shown: effectsGrid.visible && library.effectCategory === "favourites"
            }
            GridView {
                id: effectsGrid
                objectName: "effectsGrid"
                visible: library.tabs[library.currentTab].key === "Effects"
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.leftMargin: Theme.space.m
                clip: true
                cellWidth: Math.floor(width / 2)
                cellHeight: 112
                model: (library.player ? library.player.availableEffects : []).filter(
                           (e) => library.shows("effect", e.id, library.effectCategory,
                                                library.effectCategory === "" || e.categories.indexOf(library.effectCategory) >= 0, e.title))

                delegate: Item {
                    id: tile
                    required property var modelData
                    width: effectsGrid.cellWidth
                    height: effectsGrid.cellHeight
                    // Highlighted when the selected clip uses this effect.
                    readonly property bool active: (library.project.selectedClipInfo.effects || [])
                                                   .some((e) => e.effectId === modelData.id)
                    readonly property bool hovered: tileButton.hovered

                    FavouriteBadge {
                        z: 10
                        anchors.top: parent.top
                        anchors.right: parent.right
                        anchors.topMargin: Theme.space.xs
                        anchors.rightMargin: Theme.space.m + Theme.space.xs
                        kind: "effect"
                        itemId: tile.modelData.id
                        hovered: tileButton.hovered
                        onTagsRequested: library.editTags("effect", tile.modelData.id, tile.modelData.title)
                    }

                    AbstractButton {
                        id: tileButton
                        anchors.fill: parent
                        anchors.rightMargin: Theme.space.m
                        anchors.bottomMargin: Theme.space.m
                        hoverEnabled: true
                        objectName: "libraryEffect:" + tile.modelData.id
                        onClicked: library.project.addClipEffect(library.project.selectedClip, tile.modelData.id)
                        ToolTip {
                            visible: parent.hovered
                            delay: 600
                            text: qsTr("Add to the selected clip")
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
                                clip: true
                                Icon { anchors.centerIn: parent; name: "effects"; color: Theme.color.textDisabled }
                                // The effect on a sample scene (rendered on the GPU); animates on hover.
                                Image {
                                    id: effectPreview
                                    objectName: "effectPreview:" + tile.modelData.id
                                    anchors.fill: parent
                                    asynchronous: true
                                    fillMode: Image.PreserveAspectCrop
                                    property real seconds: 0.5
                                    source: "image://tveeffect/" + tile.modelData.id + "@" + seconds.toFixed(3)
                                    Timer {
                                        running: tile.hovered
                                        repeat: true
                                        interval: 125
                                        onTriggered: effectPreview.seconds = effectPreview.seconds >= 1.875 ? 0 : effectPreview.seconds + 0.125
                                        onRunningChanged: if (!running) effectPreview.seconds = 0.5
                                    }
                                }
                                Rectangle { // overlays are marked
                                    visible: (tile.modelData.categories || []).indexOf("overlay") >= 0
                                    x: Theme.space.xs
                                    y: Theme.space.xs
                                    width: overlayBadge.implicitWidth + Theme.space.s
                                    height: overlayBadge.implicitHeight + 2
                                    radius: Theme.radius.s
                                    color: Qt.alpha(Theme.color.trackEffect, 0.85)
                                    Text {
                                        id: overlayBadge
                                        anchors.centerIn: parent
                                        text: qsTr("Overlay")
                                        font: Theme.type.caption
                                        color: "white"
                                    }
                                }
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

            // Filters tab: an adjustment layer, then the looks (click to put one on the selected
            // clip). Previews are a sample scene graded on the CPU with the same maths.
            // Filters tab: all the looks, or the favourites.
            Flow {
                visible: looksGrid.visible
                Layout.fillWidth: true
                Layout.leftMargin: Theme.space.m
                Layout.rightMargin: Theme.space.m
                Layout.bottomMargin: Theme.space.s
                spacing: Theme.space.xs
                Repeater {
                    model: [{ id: "", label: qsTr("All") }, { id: "favourites", label: qsTr("★ Favourites") }]
                    delegate: AbstractButton {
                        id: lookChip
                        required property var modelData
                        objectName: "lookCategory:" + (modelData.id || "all")
                        checked: library.lookCategory === modelData.id
                        hoverEnabled: true
                        implicitWidth: lookChipLabel.implicitWidth + Theme.space.m * 2
                        implicitHeight: 24
                        onClicked: library.lookCategory = modelData.id
                        background: Rectangle {
                            radius: height / 2
                            color: lookChip.checked ? Theme.color.accentSubtle : lookChip.hovered ? Theme.color.surfaceHover : "transparent"
                            border.color: lookChip.checked ? Theme.color.accent : Theme.color.borderSubtle
                        }
                        contentItem: Text {
                            id: lookChipLabel
                            text: lookChip.modelData.label
                            font: Theme.type.caption
                            color: lookChip.checked ? Theme.color.accentText : Theme.color.textSecondary
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                }
            }
            TagChips {
                panel: library
                objectName: "lookTags"
                kind: "look"
                shown: looksGrid.visible && library.lookCategory === "favourites"
            }
            GridView {
                id: looksGrid
                objectName: "looksGrid"
                visible: library.tabs[library.currentTab].key === "Filters"
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.leftMargin: Theme.space.m
                clip: true
                cellWidth: Math.floor(width / 2)
                cellHeight: 112
                model: (library.lookCategory === "favourites" ? [] : [{ id: "", title: qsTr("Adjustment layer"), category: "Layer" }])
                       .concat(library.project.looks.filter((l) => library.shows("look", l.id, library.lookCategory, true, l.title)))

                delegate: Item {
                    id: lookTile
                    required property var modelData
                    readonly property bool adjustment: modelData.id === ""
                    readonly property bool active: !adjustment && library.project.selectedClipInfo.look !== undefined
                                                   && library.project.selectedClipInfo.look.id === modelData.id
                    width: looksGrid.cellWidth
                    height: looksGrid.cellHeight

                    FavouriteBadge {
                        z: 10
                        anchors.top: parent.top
                        anchors.right: parent.right
                        anchors.topMargin: Theme.space.xs
                        anchors.rightMargin: Theme.space.m + Theme.space.xs
                        kind: "look"
                        itemId: lookTile.modelData.id
                        hovered: lookButton.hovered
                        visible: !lookTile.adjustment && (hovered || favourite)
                        onTagsRequested: library.editTags("look", lookTile.modelData.id, lookTile.modelData.title)
                    }

                    AbstractButton {
                        id: lookButton
                        anchors.fill: parent
                        anchors.rightMargin: Theme.space.m
                        anchors.bottomMargin: Theme.space.m
                        hoverEnabled: true
                        objectName: lookTile.adjustment ? "libraryAdjustmentLayer" : "libraryLook:" + lookTile.modelData.id
                        onClicked: lookTile.adjustment ? library.project.addAdjustmentLayer()
                                                       : library.project.applyLook(library.project.selectedClip, lookTile.modelData.id)
                        ToolTip {
                            visible: parent.hovered
                            delay: 600
                            text: lookTile.adjustment ? qsTr("Add an adjustment layer at the playhead: its look, grade and effects change everything below it")
                                                      : qsTr("Put this look on the selected clip")
                        }
                        background: Rectangle {
                            radius: Theme.radius.m
                            color: Theme.color.surfaceRaised
                            border.width: lookTile.active ? 2 : 1
                            border.color: lookTile.active ? Theme.color.accent
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
                                clip: true
                                Image {
                                    objectName: "lookPreview:" + lookTile.modelData.id
                                    visible: !lookTile.adjustment
                                    anchors.fill: parent
                                    fillMode: Image.PreserveAspectCrop
                                    asynchronous: true
                                    source: lookTile.adjustment ? "" : "image://tvelook/" + lookTile.modelData.id
                                }
                                Rectangle { // an adjustment layer: a band over the layers below
                                    visible: lookTile.adjustment
                                    anchors.centerIn: parent
                                    width: parent.width * 0.6
                                    height: 14
                                    radius: 3
                                    color: Theme.color.trackEffect
                                }
                            }
                            Text {
                                Layout.leftMargin: Theme.space.s
                                Layout.bottomMargin: Theme.space.s
                                text: lookTile.modelData.title
                                font: Theme.type.label
                                color: Theme.color.textPrimary
                                elide: Text.ElideRight
                                Layout.maximumWidth: lookTile.width - Theme.space.xl
                            }
                        }
                    }
                }
            }

            // Titles tab: click to add one at the playhead, or drag it onto a track.
            TagChips {
                panel: library
                objectName: "titleTags"
                kind: "title"
                shown: titlesGrid.visible && library.titleCategory === "favourites"
            }
            GridView {
                id: titlesGrid
                objectName: "titlesGrid"
                visible: library.tabs[library.currentTab].key === "Titles"
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.leftMargin: Theme.space.m
                clip: true
                cellWidth: Math.floor(width / 2)
                cellHeight: 112
                model: library.titleTiles.filter((t) => library.shows("title", t.name, library.titleCategory,
                                                                      library.titleCategory === "" || t.category === library.titleCategory,
                                                                      t.title + " " + t.name))

                delegate: Item {
                    id: titleTile
                    required property var modelData
                    // Read by the timeline's drop area (a logo reveal is not dropped: it asks for the logo).
                    readonly property var titleStyle: modelData.reveal ? undefined : modelData.style
                    readonly property bool reveal: !!modelData.reveal
                    readonly property bool animated: !!(modelData.style.animationIn || modelData.style.animationOut
                                                        || modelData.style.animationLoop || modelData.style.scroll
                                                        || modelData.style.chart || modelData.style.visualiser)
                    width: titlesGrid.cellWidth
                    height: titlesGrid.cellHeight

                    FavouriteBadge {
                        z: 10
                        anchors.top: parent.top
                        anchors.right: parent.right
                        anchors.topMargin: Theme.space.xs
                        anchors.rightMargin: Theme.space.m + Theme.space.xs
                        kind: "title"
                        itemId: titleTile.modelData.name
                        hovered: titleMouse.containsMouse
                        onTagsRequested: library.editTags("title", titleTile.modelData.name, titleTile.modelData.title)
                    }

                    Drag.active: titleMouse.drag.active && !reveal
                    Drag.dragType: Drag.Automatic
                    Drag.supportedActions: Qt.CopyAction
                    Drag.keys: ["application/x-tve-title"]
                    Drag.mimeData: { "application/x-tve-title": JSON.stringify(modelData.style) }
                    // The tile's own preview (grabbing the tile on press could outlive it).
                    Drag.imageSource: "image://tvetitle/" + encodeURIComponent(JSON.stringify(modelData.style))

                    Rectangle {
                        anchors.fill: parent
                        anchors.rightMargin: Theme.space.m
                        anchors.bottomMargin: Theme.space.m
                        radius: Theme.radius.m
                        color: Theme.color.surfaceRaised
                        border.color: titleMouse.containsMouse ? Theme.color.borderStrong : Theme.color.borderSubtle

                        ColumnLayout {
                            anchors.fill: parent
                            spacing: Theme.space.xs
                            // The title drawn by the app's own text renderer, over a dusk sky.
                            Rectangle {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                Layout.margins: Theme.space.xs
                                radius: Theme.radius.s
                                clip: true
                                gradient: Gradient {
                                    GradientStop { position: 0; color: "#3B4E7A" }
                                    GradientStop { position: 1; color: "#B0675A" }
                                }
                                // Drawn by the app's text renderer; an animated one plays on hover.
                                // A logo reveal: the reveal's mark over the sky.
                                Icon {
                                    visible: titleTile.reveal
                                    anchors.centerIn: parent
                                    name: "logo-reveal"
                                    size: 36
                                    color: "#FFFFFF"
                                }
                                Image {
                                    id: titlePreview
                                    objectName: "titlePreview:" + titleTile.modelData.name
                                    visible: !titleTile.reveal
                                    anchors.fill: parent
                                    fillMode: Image.PreserveAspectCrop
                                    asynchronous: true
                                    readonly property real length: titleTile.modelData.style.duration || 5
                                    property real seconds: length / 2
                                    source: titleTile.reveal ? "" : "image://tvetitle/" + encodeURIComponent(JSON.stringify(
                                                Object.assign({ t: Math.round(seconds * 10) / 10 }, titleTile.modelData.style)))
                                    Timer {
                                        running: titleMouse.containsMouse && titleTile.animated
                                        repeat: true
                                        interval: 100
                                        onTriggered: titlePreview.seconds = titlePreview.seconds >= titlePreview.length
                                                     ? 0 : titlePreview.seconds + Math.max(0.1, titlePreview.length / 40)
                                        onRunningChanged: if (!running) titlePreview.seconds = titlePreview.length / 2
                                    }
                                }
                            }
                            Text {
                                Layout.leftMargin: Theme.space.s
                                Layout.bottomMargin: Theme.space.s
                                text: titleTile.modelData.title
                                font: Theme.type.label
                                color: Theme.color.textPrimary
                                elide: Text.ElideRight
                                Layout.maximumWidth: titleTile.width - Theme.space.xl
                            }
                        }
                    }

                    MouseArea {
                        id: titleMouse
                        objectName: (titleTile.reveal ? "logoReveal:" : titleTile.modelData.preset ? "titlePreset:" : "libraryTitle:")
                                    + titleTile.modelData.name
                        anchors.fill: parent
                        anchors.rightMargin: Theme.space.m
                        anchors.bottomMargin: Theme.space.m
                        hoverEnabled: true
                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                        drag.target: titleTile.reveal ? null : titleDragProxy
                        onClicked: (mouse) => {
                            if (mouse.button === Qt.RightButton) {
                                if (titleTile.modelData.preset) {
                                    titlePresetMenu.preset = titleTile.modelData.name
                                    titlePresetMenu.popup()
                                }
                            } else if (titleTile.reveal) {
                                library.pendingReveal = titleTile.modelData.reveal
                                logoDialog.open()
                            } else if (titleTile.modelData.style.visualiser) {
                                library.project.addVisualiser(titleTile.modelData.style) // over the music, as long as it
                            } else {
                                library.project.addTitle(titleTile.modelData.style)
                            }
                        }
                    }
                    Item { id: titleDragProxy }

                    ToolTip {
                        visible: titleMouse.containsMouse && !titleMouse.drag.active
                        delay: 800
                        text: titleTile.reveal ? qsTr("Click to choose your logo; it is revealed at the playhead")
                            : titleTile.modelData.style.visualiser ? qsTr("Click to add it over the music at the playhead, as long as the music")
                            : titleTile.modelData.preset ? qsTr("Click to add at the playhead, or drag onto a track. Right-click to delete.")
                            : qsTr("Click to add at the playhead, or drag onto a track")
                    }
                }
            }

            // Transitions tab: click to put one on the cut nearest the playhead, or drag it
            // onto a cut.
            TagChips {
                panel: library
                objectName: "transitionTags"
                kind: "transition"
                shown: transitionsGrid.visible && library.transitionCategory === "favourites"
            }
            GridView {
                id: transitionsGrid
                objectName: "transitionsGrid"
                visible: library.tabs[library.currentTab].key === "Transitions"
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.leftMargin: Theme.space.m
                clip: true
                cellWidth: Math.floor(width / 2)
                cellHeight: 112
                // Overlay transitions (laid over a cut on a track above) have a category of their own.
                model: library.transitionCategory === "favourites"
                       ? library.project.availableTransitions.filter((t) => library.shows("transition", t.id, "favourites", true, t.title))
                             .concat(library.project.overlayTransitions.filter((t) => library.shows("transition", t.id, "favourites", true, t.title))
                                         .map((t) => Object.assign({ overlay: true }, t)))
                       : library.transitionCategory === "overlays"
                       ? library.project.overlayTransitions.filter((t) => library.shows("transition", t.id, "", true, t.title))
                                 .map((t) => Object.assign({ overlay: true }, t))
                       : library.project.availableTransitions.filter(
                           (t) => library.shows("transition", t.id, library.transitionCategory,
                                                library.transitionCategory === "" || t.categories.indexOf(library.transitionCategory) >= 0, t.title))

                delegate: Item {
                    id: transitionTile
                    required property var modelData
                    readonly property string transitionId: modelData.id // read by the timeline's drop area
                    width: transitionsGrid.cellWidth
                    height: transitionsGrid.cellHeight
                    readonly property bool overlay: modelData.overlay === true
                    readonly property bool active: !overlay && library.project.selectedTransitionInfo.transitionId === modelData.id
                    readonly property bool isDefault: !overlay && EditingPreferences.defaultTransition === modelData.id

                    FavouriteBadge {
                        z: 10
                        anchors.top: parent.top
                        anchors.right: parent.right
                        anchors.topMargin: Theme.space.xs
                        anchors.rightMargin: Theme.space.m + Theme.space.xs
                        kind: "transition"
                        itemId: transitionTile.modelData.id
                        hovered: transitionMouse.containsMouse
                        onTagsRequested: library.editTags("transition", transitionTile.modelData.id, transitionTile.modelData.title)
                    }

                    Drag.active: !overlay && transitionMouse.drag.active
                    Drag.dragType: Drag.Automatic
                    Drag.supportedActions: Qt.CopyAction
                    Drag.keys: ["application/x-tve-transition"]
                    Drag.mimeData: { "application/x-tve-transition": modelData.id }
                    Drag.imageSource: "image://tvetransition/" + modelData.id + "@0.5"

                    Rectangle {
                        anchors.fill: parent
                        anchors.rightMargin: Theme.space.m
                        anchors.bottomMargin: Theme.space.m
                        radius: Theme.radius.m
                        color: Theme.color.surfaceRaised
                        border.width: transitionTile.active ? 2 : 1
                        border.color: transitionTile.active ? Theme.color.accent
                                    : transitionMouse.containsMouse ? Theme.color.borderStrong : Theme.color.borderSubtle

                        ColumnLayout {
                            anchors.fill: parent
                            spacing: Theme.space.xs
                            Rectangle {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                Layout.margins: Theme.space.xs
                                radius: Theme.radius.s
                                color: Theme.color.surfaceSunken
                                clip: true
                                Icon { anchors.centerIn: parent; name: "transitions"; color: Theme.color.textDisabled }
                                // A still of the transition under way (rendered on the GPU); plays on hover.
                                Image {
                                    id: preview
                                    objectName: "transitionPreview:" + transitionTile.modelData.id
                                    anchors.fill: parent
                                    asynchronous: true
                                    fillMode: Image.PreserveAspectCrop
                                    property real transitionProgress: 0.3
                                    source: (transitionTile.overlay ? "image://tveoverlay/" : "image://tvetransition/")
                                            + transitionTile.modelData.id + "@" + transitionProgress.toFixed(1)
                                    Timer {
                                        running: transitionMouse.containsMouse
                                        repeat: true
                                        interval: 120
                                        onTriggered: preview.transitionProgress = preview.transitionProgress >= 1.0 ? 0.0 : preview.transitionProgress + 0.1
                                        onRunningChanged: if (!running) preview.transitionProgress = 0.3
                                    }
                                }
                            }
                            RowLayout {
                                Layout.leftMargin: Theme.space.s
                                Layout.rightMargin: Theme.space.s
                                Layout.bottomMargin: Theme.space.s
                                spacing: Theme.space.xs
                                Text {
                                    Layout.fillWidth: true
                                    text: transitionTile.modelData.title
                                    font: Theme.type.label
                                    color: Theme.color.textPrimary
                                    elide: Text.ElideRight
                                }
                                // The one ⌘T adds.
                                Text {
                                    objectName: "defaultTransitionBadge"
                                    visible: transitionTile.isDefault
                                    text: qsTr("Default")
                                    font: Theme.type.caption
                                    color: Theme.color.accentText
                                }
                            }
                        }
                    }

                    MouseArea {
                        id: transitionMouse
                        objectName: (transitionTile.overlay ? "libraryOverlay:" : "libraryTransition:") + transitionTile.modelData.id
                        anchors.fill: parent
                        anchors.rightMargin: Theme.space.m
                        anchors.bottomMargin: Theme.space.m
                        hoverEnabled: true
                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                        drag.target: transitionTile.overlay ? null : transitionDragProxy
                        onClicked: (mouse) => {
                            if (mouse.button === Qt.RightButton) {
                                if (!transitionTile.overlay)
                                    transitionMenu.popup()
                                return
                            }
                            transitionTile.overlay ? library.project.addOverlayTransition(transitionTile.modelData.id)
                                                   : library.project.addTransition(transitionTile.modelData.id)
                        }
                    }
                    AppMenu {
                        id: transitionMenu
                        objectName: "transitionTileMenu:" + transitionTile.modelData.id
                        AppMenuItem {
                            objectName: "makeDefaultTransition"
                            text: qsTr("Make it the default")
                            shortcut: KeyMap.text("defaultTransition")
                            checkable: true
                            checked: transitionTile.isDefault
                            onTriggered: EditingPreferences.defaultTransition = transitionTile.modelData.id
                        }
                        AppMenuItem {
                            objectName: "applyTransitionToAll"
                            text: qsTr("Put it on every cut")
                            onTriggered: library.project.applyTransitionToAll(transitionTile.modelData.id)
                        }
                    }
                    Item { id: transitionDragProxy }

                    ToolTip {
                        visible: transitionMouse.containsMouse && !transitionMouse.drag.active
                        delay: 800
                        text: transitionTile.overlay ? qsTr("Click to lay it over the cut nearest the playhead, on a track above")
                                                     : qsTr("Click to add at the cut nearest the playhead, or drag onto a cut; right-click to make it the default")
                    }
                }
            }

            // Media tab: smart search over the media bin (its moments), then the bin itself
            // (while searching, the media named so or with a moment found).
            MediaSearch {
                id: mediaSearch
                objectName: "mediaSearch"
                visible: library.currentTab === 0
                Layout.fillWidth: true
                Layout.fillHeight: false // (a layout fills by default: the bin below takes the room)
                Layout.bottomMargin: Theme.space.s
                project: library.project
                query: library.currentTab === 0 ? searchField.text : ""
                onMomentRequested: (media, start, end) => library.momentRequested(media, start, end)
            }
            Item {
                id: mediaBin
                visible: library.currentTab === 0
                Layout.fillWidth: true
                Layout.fillHeight: true

                GridView {
                    id: mediaGrid
                    objectName: "mediaGrid"
                    anchors.fill: parent
                    anchors.leftMargin: Theme.space.m
                    clip: true
                    cellWidth: Math.floor(width / 2)
                    cellHeight: 118
                    model: mediaSearch.trimmed === ""
                           ? library.project.media
                           : library.project.media.filter((m) => m.name.toLowerCase().indexOf(mediaSearch.trimmed.toLowerCase()) >= 0
                                                                 || mediaSearch.hitIds.indexOf(m.id) >= 0)

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
                        // The tile's thumbnail (grabbing the tile on press could outlive it).
                        Drag.imageSource: modelData.hasVideo
                                          ? "image://tvethumb/!" + encodeURIComponent(modelData.path) + "@"
                                            + (modelData.duration * 0.1).toFixed(2) + "@90"
                                          : ""

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
                                        source: mediaTile.modelData.hasVideo && !mediaTile.modelData.missing && height >= 4
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
                                        color: mediaTile.modelData.missing ? Theme.color.danger : Theme.color.overlay
                                        Text {
                                            id: durationText
                                            anchors.centerIn: parent
                                            objectName: "mediaDuration:" + mediaTile.modelData.name
                                            // A still has no length of its own.
                                            text: mediaTile.modelData.missing ? qsTr("Missing")
                                                  : mediaTile.modelData.sticker ? qsTr("Sticker") : mediaTile.modelData.still ? qsTr("Image")
                                                  : library.project.timecode(mediaTile.modelData.duration).substring(3, 8)
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
                                          ? qsTr("Audio only")
                                          : mediaTile.modelData.sticker
                                          ? qsTr("Animated sticker")
                                          : mediaTile.modelData.still
                                          ? qsTr("%1×%2 · image").arg(mediaTile.modelData.width).arg(mediaTile.modelData.height)
                                          : mediaTile.modelData.width + "×" + mediaTile.modelData.height
                                            + " · " + qsTr("%1 fps").arg(Fmt.num(mediaTile.modelData.frameRate, mediaTile.modelData.frameRate % 1 ? 2 : 0))
                                            + (mediaTile.modelData.alpha ? qsTr(" · transparent") : "")
                                            + (mediaTile.modelData.colour && !mediaTile.modelData.hdr ? " · " + mediaTile.modelData.colour : "")
                                            + (mediaTile.modelData.hasAudio ? qsTr(" · sound") : "")
                                    font: Theme.type.caption
                                    color: Theme.color.textSecondary
                                }
                            }
                        }

                        MouseArea {
                            id: tileMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            acceptedButtons: Qt.LeftButton | Qt.RightButton
                            drag.target: pressedButtons & Qt.RightButton ? null : dragProxy
                            onPressed: (mouse) => {
                                if (mouse.button === Qt.RightButton)
                                    library.openMediaMenu(mediaTile.modelData, tileMouse, mouse.x, mouse.y)
                            }
                            onDoubleClicked: (mouse) => {
                                if (mouse.button !== Qt.LeftButton)
                                    return
                                // ⌥: into the source monitor, to choose a part (In and Out) first.
                                if (mouse.modifiers & Qt.AltModifier)
                                    library.sourceRequested(mediaTile.modelData)
                                else
                                    library.project.addClip(mediaTile.modelData.id)
                            }
                        }
                        Item { id: dragProxy }

                        ToolTip {
                            visible: tileMouse.containsMouse && !tileMouse.drag.active
                            delay: 800
                            // (What it shows is read only on hover.)
                            readonly property var labels: tileMouse.containsMouse ? library.project.mediaLabels(mediaTile.modelData.id, 5) : []
                            text: (mediaTile.modelData.generated
                                   ? qsTr("Made by %1: “%2”").arg(mediaTile.modelData.generated.model).arg(mediaTile.modelData.generated.prompt) + "\n" : "")
                                  + (labels.length > 0 ? qsTr("Shows: %1").arg(labels.map((l) => l.label).join(", ")) + "\n" : "")
                                  + qsTr("Double-click to add to the timeline (⌥: open in the source monitor), drag onto a track or the player, or right-click for more")
                        }
                        // Generated media: marked, its prompt and maker kept.
                        Rectangle {
                            objectName: "generatedBadge:" + mediaTile.modelData.name
                            visible: !!mediaTile.modelData.generated
                            anchors { left: parent.left; bottom: parent.bottom; leftMargin: Theme.space.s; bottomMargin: Theme.space.m + 44 }
                            width: aiLabel.implicitWidth + Theme.space.s
                            height: aiLabel.implicitHeight + Theme.space.xxs
                            radius: Theme.radius.s
                            color: Theme.color.accent
                            Text { id: aiLabel; anchors.centerIn: parent; text: qsTr("AI"); font: Theme.type.caption; color: "#FFFFFF" }
                        }

                        // 360° media: marked.
                        Rectangle {
                            objectName: "sphereBadge:" + mediaTile.modelData.name
                            visible: mediaTile.modelData.spherical === true
                            anchors { right: parent.right; top: parent.top; margins: Theme.space.s }
                            width: sphereLabel.implicitWidth + Theme.space.s
                            height: sphereLabel.implicitHeight + Theme.space.xxs
                            radius: Theme.radius.s
                            color: Theme.color.overlay
                            Text { id: sphereLabel; anchors.centerIn: parent; text: "360°"; font: Theme.type.caption; color: "#FFFFFF" }
                        }

                        // HDR media: marked, with how it is encoded (PQ or HLG).
                        Rectangle {
                            objectName: "hdrBadge:" + mediaTile.modelData.name
                            visible: mediaTile.modelData.hdr === true
                            anchors { right: parent.right; top: parent.top; margins: Theme.space.s }
                            // (Below the 360° badge when it is both.)
                            anchors.topMargin: Theme.space.s + (mediaTile.modelData.spherical === true ? 22 : 0)
                            width: hdrLabel.implicitWidth + Theme.space.s
                            height: hdrLabel.implicitHeight + Theme.space.xxs
                            radius: Theme.radius.s
                            color: Theme.color.overlay
                            Text { id: hdrLabel; anchors.centerIn: parent; text: mediaTile.modelData.colour || "HDR"; font: Theme.type.caption; color: "#FFFFFF" }
                            ToolTip {
                                visible: hdrBadgeMouse.containsMouse
                                text: library.project.projectColour === "sdr"
                                      ? qsTr("HDR media: shown tone-mapped in this SDR project. Make the project HDR in Color to keep its highlights.")
                                      : qsTr("HDR media, in this HDR project as it is")
                            }
                            MouseArea { id: hdrBadgeMouse; anchors.fill: parent; hoverEnabled: true }
                        }

                        // Proxy: its state, and (on hover) making or removing one.
                        readonly property var proxy: library.project.proxyStates[modelData.path] || ({ state: "none" })
                        Rectangle {
                            objectName: "proxyBadge:" + mediaTile.modelData.name
                            visible: mediaTile.proxy.state !== "none"
                            x: Theme.space.s
                            y: Theme.space.s
                            width: proxyLabel.implicitWidth + Theme.space.s
                            height: proxyLabel.implicitHeight + Theme.space.xxs
                            radius: Theme.radius.s
                            color: mediaTile.proxy.state === "ready" ? Theme.color.accent
                                 : mediaTile.proxy.state === "failed" ? Theme.color.danger : Theme.color.overlay
                            Text {
                                id: proxyLabel
                                anchors.centerIn: parent
                                text: mediaTile.proxy.state === "making" ? qsTr("Proxy %1").arg(Fmt.percent(mediaTile.proxy.progress))
                                    : mediaTile.proxy.state === "queued" ? qsTr("Proxy queued")
                                    : mediaTile.proxy.state === "failed" ? qsTr("Proxy failed") : qsTr("Proxy")
                                font: Theme.type.caption
                                color: "#FFFFFF"
                            }
                            ToolTip {
                                visible: proxyBadgeMouse.containsMouse
                                text: mediaTile.proxy.state === "ready" ? qsTr("A lighter copy plays in the preview; export uses the original")
                                    : mediaTile.proxy.state === "failed" ? mediaTile.proxy.error : qsTr("Making a lighter copy for the preview")
                            }
                            MouseArea { id: proxyBadgeMouse; anchors.fill: parent; hoverEnabled: true }
                        }
                        AbstractButton {
                            objectName: "proxyAction:" + mediaTile.modelData.name
                            visible: mediaTile.modelData.hasVideo && !mediaTile.modelData.still && (tileMouse.containsMouse || hovered)
                                     && (mediaTile.proxy.state === "none" || mediaTile.proxy.state === "ready"
                                         || mediaTile.proxy.state === "failed")
                            anchors { right: parent.right; top: parent.top; rightMargin: Theme.space.m + Theme.space.s; topMargin: Theme.space.s }
                            hoverEnabled: true
                            padding: Theme.space.xxs
                            leftPadding: Theme.space.s
                            rightPadding: Theme.space.s
                            onClicked: mediaTile.proxy.state === "ready" ? library.project.deleteProxy(mediaTile.modelData.id)
                                                                         : library.project.createProxy(mediaTile.modelData.id)
                            background: Rectangle {
                                radius: Theme.radius.s
                                color: parent.hovered ? Theme.color.accent : Theme.color.overlay
                            }
                            contentItem: Text {
                                text: mediaTile.proxy.state === "ready" ? qsTr("Remove proxy") : qsTr("Make proxy")
                                font: Theme.type.caption
                                color: "#FFFFFF"
                            }
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
                        text: qsTr("Import or drop video files here")
                        font: Theme.type.heading
                        color: Theme.color.textPrimary
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                    }
                    TextButton {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: qsTr("Import media…")
                        iconName: "import"
                        variant: "primary"
                        onClicked: library.importRequested()
                    }
                    Flow {
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: Math.min(parent.width, implicitWidth)
                        spacing: Theme.space.xxs
                        Repeater {
                            model: [{ id: "photos", label: qsTr("Photos") }, { id: "music", label: qsTr("Music") }, { id: "devices", label: qsTr("A device") }]
                            delegate: TextButton {
                                required property var modelData
                                objectName: "emptyImportFrom:" + modelData.id
                                text: qsTr("From %1").arg(modelData.label)
                                variant: "ghost"
                                onClicked: library.importFromRequested(modelData.id)
                            }
                        }
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
                visible: !effectsGrid.visible && !looksGrid.visible && !transitionsGrid.visible && !titlesGrid.visible
                         && !mediaBin.visible && !stickersGrid.visible && !captionsPage.visible && !transcriptPage.visible
                         && !audioPage.visible && !generatePage.visible && !copilotPage.visible && !templatesPage.visible
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

    // Instant mode: the Templates tab, with its instant-video panel open.
    function openInstantMode() {
        currentTab = tabs.findIndex((t) => t.key === "Templates")
        Qt.callLater(() => { if (templatesPage.item) templatesPage.item.makeFor("instant") })
    }

    // A media item's right-click menu.
    function openMediaMenu(media, item, x, y) {
        mediaMenu.media = media
        mediaMenu.popup(item, x, y)
    }
    AppMenu {
        id: mediaMenu
        objectName: "mediaMenu"
        property var media: null
        AppMenuItem {
            objectName: "mediaMenu:atPlayhead"
            text: qsTr("Add at the playhead")
            enabled: mediaMenu.media !== null && mediaMenu.media.missing !== true
            onTriggered: library.project.addMediaAt(mediaMenu.media.id)
        }
        AppMenuItem {
            objectName: "mediaMenu:append"
            text: qsTr("Add to the end of the timeline")
            enabled: mediaMenu.media !== null && mediaMenu.media.missing !== true
            onTriggered: library.project.addClip(mediaMenu.media.id)
        }
        AppMenuItem {
            objectName: "mediaMenu:source"
            text: qsTr("Open in the source monitor")
            enabled: mediaMenu.media !== null && mediaMenu.media.missing !== true && mediaMenu.media.placeholder !== true
            onTriggered: library.sourceRequested(mediaMenu.media)
        }
        AppMenuItem {
            objectName: "mediaMenu:reveal"
            text: qsTr("Show in Finder")
            enabled: mediaMenu.media !== null && mediaMenu.media.missing !== true && mediaMenu.media.placeholder !== true
            onTriggered: library.project.revealMedia(mediaMenu.media.id)
        }
        AppMenuSeparator {}
        AppMenuItem {
            objectName: "mediaMenu:remove"
            text: qsTr("Remove from the project")
            onTriggered: {
                const media = mediaMenu.media
                const used = library.project.clipsUsingMedia(media.id)
                if (used === 0) {
                    library.project.removeMedia(media.id)
                    return
                }
                mediaConfirm.ask(qsTr("Remove \"%1\"?").arg(media.name),
                                 used === 1 ? qsTr("A clip on the timeline uses it and goes with it. You can undo this.")
                                            : qsTr("%1 clips on the timeline use it and go with it. You can undo this.").arg(Fmt.num(used)),
                                 qsTr("Remove"), () => library.project.removeMedia(media.id))
            }
        }
    }
    ConfirmDialog { id: mediaConfirm; objectName: "mediaConfirm" }
    // A saved title preset's right-click menu.
    AppMenu {
        id: titlePresetMenu
        objectName: "titlePresetMenu"
        property string preset: ""
        AppMenuItem {
            objectName: "titlePresetMenu:delete"
            text: qsTr("Delete this preset")
            onTriggered: library.project.deleteTitlePreset(titlePresetMenu.preset)
        }
    }
    // The logo for a logo reveal.
    FileDialog {
        id: logoDialog
        title: qsTr("Choose your logo")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("Images (*.png *.jpg *.jpeg *.heic *.webp *.tif *.tiff *.bmp)")]
        onAccepted: library.project.addLogoReveal(library.pendingReveal, selectedFile, qsTr("Your tagline"))
    }
    // Another project's timeline, brought in as one compound clip at the playhead.
    FileDialog {
        id: projectDialog
        title: qsTr("Import a project as a compound clip")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("TechieVideoEditor projects (*.tve)")]
        onAccepted: library.project.importProjectAsCompound(selectedFile)
    }
    // Numbered pictures as one video: any one of them chosen, then the frame rate.
    FileDialog {
        id: sequenceFrameDialog
        title: qsTr("Choose one picture of the sequence")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("Pictures (*.png *.jpg *.jpeg *.tif *.tiff *.exr *.bmp *.tga *.webp *.heic)"), qsTr("All files (*)")]
        onAccepted: sequenceDialog.openFor(selectedFile)
    }
    ImageSequenceDialog { id: sequenceDialog; objectName: "imageSequenceDialog"; project: library.project }

    // A favourite's tags: those it has (× takes one off), new ones typed (commas between),
    // and the kind's other tags to add with a click.
    Popup {
        id: tagEditor
        objectName: "favouriteTagEditor"
        property string kind
        property string itemId
        property string itemTitle
        readonly property var tags: Favourites.items[kind + ":" + itemId] || []
        readonly property var others: library.tagsIn(Favourites.items, kind).filter((t) => tags.indexOf(t) < 0)
        function add(text) {
            Favourites.setTags(kind, itemId, tags.concat(text.split(",")))
            tagField.text = ""
        }
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        width: 360
        padding: Theme.space.l
        onAboutToShow: tagField.text = ""
        onOpened: tagField.forceActiveFocus()
        background: Rectangle {
            radius: Theme.radius.l
            color: Theme.color.surfaceRaised
            border.color: Theme.color.borderStrong
        }
        contentItem: ColumnLayout {
            spacing: Theme.space.s
            Text {
                Layout.fillWidth: true
                text: qsTr("Tags for %1").arg(tagEditor.itemTitle)
                font: Theme.type.title
                color: Theme.color.textPrimary
                elide: Text.ElideRight
            }
            Flow {
                Layout.fillWidth: true
                spacing: Theme.space.xs
                Repeater {
                    model: tagEditor.tags
                    delegate: TextButton {
                        required property string modelData
                        objectName: "favouriteTagRemove:" + modelData
                        text: "#" + modelData + "  ×"
                        variant: "secondary"
                        tip: qsTr("Take this tag off")
                        onClicked: Favourites.setTags(tagEditor.kind, tagEditor.itemId, tagEditor.tags.filter((t) => t !== modelData))
                    }
                }
            }
            TextField {
                id: tagField
                objectName: "favouriteTagField"
                Layout.fillWidth: true
                placeholderText: qsTr("Add tags, with commas between")
                color: Theme.color.textPrimary
                placeholderTextColor: Theme.color.textDisabled
                selectByMouse: true
                background: Rectangle {
                    implicitHeight: 32
                    radius: Theme.radius.s
                    color: Theme.color.surfaceSunken
                    border.color: tagField.activeFocus ? Theme.color.accent : Theme.color.borderSubtle
                }
                onAccepted: tagEditor.add(text)
            }
            Text {
                visible: tagEditor.others.length > 0
                text: qsTr("Your other tags")
                font: Theme.type.label
                color: Theme.color.textSecondary
            }
            Flow {
                visible: tagEditor.others.length > 0
                Layout.fillWidth: true
                spacing: Theme.space.xs
                Repeater {
                    model: tagEditor.others
                    delegate: TextButton {
                        required property string modelData
                        objectName: "favouriteTagAdd:" + modelData
                        text: "#" + modelData
                        variant: "ghost"
                        onClicked: tagEditor.add(modelData)
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                TextButton {
                    objectName: "favouriteTagsDone"
                    text: qsTr("Done")
                    variant: "primary"
                    onClicked: {
                        if (tagField.text.trim() !== "")
                            tagEditor.add(tagField.text)
                        tagEditor.close()
                    }
                }
            }
        }
    }
}
