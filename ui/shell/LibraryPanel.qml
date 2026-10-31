import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import TechieVideoEditor.Theme
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
    onCurrentTabChanged: searchField.text = ""

    readonly property var effectCategories: [
        { id: "", label: "All" }, { id: "blur", label: "Blur" }, { id: "color", label: "Colour" },
        { id: "stylize", label: "Stylize" }, { id: "distort", label: "Distort" }, { id: "glitch", label: "Glitch" },
        { id: "light", label: "Light" }, { id: "frame", label: "Frame" }, { id: "overlay", label: "Overlay" }
    ]
    function matchesSearch(title) {
        const text = searchField.text.trim().toLowerCase()
        return text === "" || title.toLowerCase().indexOf(text) >= 0
    }

    color: Theme.color.surface

    // Title templates (original designs, TitleTemplates.js): {name, category, style}; the
    // Titles tab's category ("" for all).
    readonly property var titleStyles: TitleTemplates.templates
    property string titleCategory: ""
    // The Stickers tab's category ("" for all).
    property string stickerCategory: ""
    readonly property var stickerCategories: [
        { id: "", label: "All" }, { id: "shapes", label: "Shapes" }, { id: "marks", label: "Arrows & marks" },
        { id: "nature", label: "Nature" }, { id: "emoji", label: "Emoji" }, { id: "party", label: "Party" }
    ]
    // The Transitions tab's category ("" for all).
    property string transitionCategory: ""
    readonly property var transitionCategories: [
        { id: "", label: "All" }, { id: "basic", label: "Basic" }, { id: "slide", label: "Slide" },
        { id: "wipe", label: "Wipe" }, { id: "shape", label: "Shape" }, { id: "zoom", label: "Zoom" },
        { id: "3d", label: "3D" }, { id: "distort", label: "Distort" }, { id: "luma", label: "Luma" },
        { id: "stylize", label: "Stylize" }, { id: "blur", label: "Blur" }, { id: "light", label: "Light" }
    ]

    readonly property var tabs: [
        { icon: "media", label: "Media", phase: "" },
        { icon: "audio", label: "Audio", phase: "Music and sound library — Phase 4" },
        { icon: "titles", label: "Titles", phase: "" },
        { icon: "transitions", label: "Transitions", phase: "" },
        { icon: "effects", label: "Effects", phase: "" },
        { icon: "filters", label: "Filters", phase: "" },
        { icon: "stickers", label: "Stickers", phase: "" },
        { icon: "captions", label: "Captions", phase: "" },
        { icon: "transcript", label: "Transcript", phase: "" },
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
                id: searchField
                objectName: "librarySearch"
                visible: library.tabs[library.currentTab].label !== "Captions" && library.tabs[library.currentTab].label !== "Transcript"
                Layout.fillWidth: true
                Layout.margins: Theme.space.m
                placeholderText: "Search " + library.tabs[library.currentTab].label.toLowerCase()
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
                    text: "Random on every cut"
                    variant: "ghost"
                    tip: "Put a different random transition on each cut between clips"
                    onClicked: library.project.applyRandomTransitions()
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
                visible: library.tabs[library.currentTab].label === "Transcript"
                project: library.project
                Layout.fillWidth: true
                Layout.fillHeight: true
            }

            CaptionsPage {
                id: captionsPage
                objectName: "captionsPage"
                visible: library.tabs[library.currentTab].label === "Captions"
                project: library.project
                Layout.fillWidth: true
                Layout.fillHeight: true
            }

            // Stickers tab: animated stickers (Lottie), playing on hover; click to add one at the
            // playhead.
            GridView {
                id: stickersGrid
                objectName: "stickersGrid"
                visible: library.tabs[library.currentTab].label === "Stickers"
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.leftMargin: Theme.space.m
                clip: true
                cellWidth: Math.floor(width / 3)
                cellHeight: cellWidth
                model: library.project.stickers.filter(
                           (s) => (library.stickerCategory === "" || s.category === library.stickerCategory)
                                  && library.matchesSearch(s.title))

                delegate: Item {
                    id: stickerTile
                    required property var modelData
                    width: stickersGrid.cellWidth
                    height: stickersGrid.cellHeight

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
                        ToolTip.text: stickerTile.modelData.title + " — click to add at the playhead"
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
                    model: TitleTemplates.categories
                    delegate: AbstractButton {
                        id: titleChip
                        required property var modelData
                        objectName: "titleCategory:" + (modelData.id || "all")
                        checked: library.titleCategory === modelData.id
                        hoverEnabled: true
                        implicitWidth: titleChipLabel.implicitWidth + Theme.space.m * 2
                        implicitHeight: 24
                        onClicked: library.titleCategory = modelData.id
                        background: Rectangle {
                            radius: height / 2
                            color: titleChip.checked ? Theme.color.accentSubtle : titleChip.hovered ? Theme.color.surfaceHover : "transparent"
                            border.color: titleChip.checked ? Theme.color.accent : Theme.color.borderSubtle
                        }
                        contentItem: Text {
                            id: titleChipLabel
                            text: titleChip.modelData.label
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
            GridView {
                id: effectsGrid
                objectName: "effectsGrid"
                visible: library.tabs[library.currentTab].label === "Effects"
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.leftMargin: Theme.space.m
                clip: true
                cellWidth: Math.floor(width / 2)
                cellHeight: 112
                model: (library.player ? library.player.availableEffects : []).filter(
                           (e) => (library.effectCategory === "" || e.categories.indexOf(library.effectCategory) >= 0)
                                  && library.matchesSearch(e.title))

                delegate: Item {
                    id: tile
                    required property var modelData
                    width: effectsGrid.cellWidth
                    height: effectsGrid.cellHeight
                    // Highlighted when the selected clip uses this effect.
                    readonly property bool active: (library.project.selectedClipInfo.effects || [])
                                                   .some((e) => e.effectId === modelData.id)
                    readonly property bool hovered: tileButton.hovered

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
                                        text: "Overlay"
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
            GridView {
                id: looksGrid
                objectName: "looksGrid"
                visible: library.tabs[library.currentTab].label === "Filters"
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.leftMargin: Theme.space.m
                clip: true
                cellWidth: Math.floor(width / 2)
                cellHeight: 112
                model: [{ id: "", title: "Adjustment layer", category: "Layer" }].concat(library.project.looks)

                delegate: Item {
                    id: lookTile
                    required property var modelData
                    readonly property bool adjustment: modelData.id === ""
                    readonly property bool active: !adjustment && library.project.selectedClipInfo.look !== undefined
                                                   && library.project.selectedClipInfo.look.id === modelData.id
                    width: looksGrid.cellWidth
                    height: looksGrid.cellHeight

                    AbstractButton {
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
                            text: lookTile.adjustment ? "Add an adjustment layer at the playhead: its look, grade and effects change everything below it"
                                                      : "Put this look on the selected clip"
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
            GridView {
                id: titlesGrid
                objectName: "titlesGrid"
                visible: library.tabs[library.currentTab].label === "Titles"
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.leftMargin: Theme.space.m
                clip: true
                cellWidth: Math.floor(width / 2)
                cellHeight: 112
                model: library.titleStyles.filter((t) => (library.titleCategory === "" || t.category === library.titleCategory)
                                                         && library.matchesSearch(t.name))

                delegate: Item {
                    id: titleTile
                    required property var modelData
                    readonly property var titleStyle: modelData.style // read by the timeline's drop area
                    readonly property bool animated: !!(modelData.style.animationIn || modelData.style.animationOut
                                                        || modelData.style.animationLoop || modelData.style.scroll)
                    width: titlesGrid.cellWidth
                    height: titlesGrid.cellHeight

                    Drag.active: titleMouse.drag.active
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
                                Image {
                                    id: titlePreview
                                    objectName: "titlePreview:" + titleTile.modelData.name
                                    anchors.fill: parent
                                    fillMode: Image.PreserveAspectCrop
                                    asynchronous: true
                                    readonly property real length: titleTile.modelData.style.duration || 5
                                    property real seconds: length / 2
                                    source: "image://tvetitle/" + encodeURIComponent(JSON.stringify(
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
                                text: titleTile.modelData.name
                                font: Theme.type.label
                                color: Theme.color.textPrimary
                                elide: Text.ElideRight
                                Layout.maximumWidth: titleTile.width - Theme.space.xl
                            }
                        }
                    }

                    MouseArea {
                        id: titleMouse
                        objectName: "libraryTitle:" + titleTile.modelData.name
                        anchors.fill: parent
                        anchors.rightMargin: Theme.space.m
                        anchors.bottomMargin: Theme.space.m
                        hoverEnabled: true
                        drag.target: titleDragProxy
                        onClicked: library.project.addTitle(titleTile.modelData.style)
                    }
                    Item { id: titleDragProxy }

                    ToolTip {
                        visible: titleMouse.containsMouse && !titleMouse.drag.active
                        delay: 800
                        text: "Click to add at the playhead, or drag onto a track"
                    }
                }
            }

            // Transitions tab: click to put one on the cut nearest the playhead, or drag it
            // onto a cut.
            GridView {
                id: transitionsGrid
                objectName: "transitionsGrid"
                visible: library.tabs[library.currentTab].label === "Transitions"
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.leftMargin: Theme.space.m
                clip: true
                cellWidth: Math.floor(width / 2)
                cellHeight: 112
                model: library.project.availableTransitions.filter(
                           (t) => (library.transitionCategory === "" || t.categories.indexOf(library.transitionCategory) >= 0)
                                  && library.matchesSearch(t.title))

                delegate: Item {
                    id: transitionTile
                    required property var modelData
                    readonly property string transitionId: modelData.id // read by the timeline's drop area
                    width: transitionsGrid.cellWidth
                    height: transitionsGrid.cellHeight
                    readonly property bool active: library.project.selectedTransitionInfo.transitionId === modelData.id

                    Drag.active: transitionMouse.drag.active
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
                                    source: "image://tvetransition/" + transitionTile.modelData.id + "@" + transitionProgress.toFixed(1)
                                    Timer {
                                        running: transitionMouse.containsMouse
                                        repeat: true
                                        interval: 120
                                        onTriggered: preview.transitionProgress = preview.transitionProgress >= 1.0 ? 0.0 : preview.transitionProgress + 0.1
                                        onRunningChanged: if (!running) preview.transitionProgress = 0.3
                                    }
                                }
                            }
                            Text {
                                Layout.leftMargin: Theme.space.s
                                Layout.bottomMargin: Theme.space.s
                                text: transitionTile.modelData.title
                                font: Theme.type.label
                                color: Theme.color.textPrimary
                                elide: Text.ElideRight
                                Layout.maximumWidth: transitionTile.width - Theme.space.xl
                            }
                        }
                    }

                    MouseArea {
                        id: transitionMouse
                        objectName: "libraryTransition:" + transitionTile.modelData.id
                        anchors.fill: parent
                        anchors.rightMargin: Theme.space.m
                        anchors.bottomMargin: Theme.space.m
                        hoverEnabled: true
                        drag.target: transitionDragProxy
                        onClicked: library.project.addTransition(transitionTile.modelData.id)
                    }
                    Item { id: transitionDragProxy }

                    ToolTip {
                        visible: transitionMouse.containsMouse && !transitionMouse.drag.active
                        delay: 800
                        text: "Click to add at the cut nearest the playhead, or drag onto a cut"
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
                                            text: mediaTile.modelData.missing ? "Missing"
                                                  : mediaTile.modelData.sticker ? "Sticker" : mediaTile.modelData.still ? "Image"
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
                                          ? "Audio only"
                                          : mediaTile.modelData.sticker
                                          ? "Animated sticker"
                                          : mediaTile.modelData.still
                                          ? mediaTile.modelData.width + "×" + mediaTile.modelData.height + " · image"
                                          : mediaTile.modelData.width + "×" + mediaTile.modelData.height
                                            + " · " + mediaTile.modelData.frameRate.toFixed(mediaTile.modelData.frameRate % 1 ? 2 : 0) + " fps"
                                            + (mediaTile.modelData.alpha ? " · transparent" : "")
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
                        }
                        Item { id: dragProxy }

                        ToolTip {
                            visible: tileMouse.containsMouse && !tileMouse.drag.active
                            delay: 800
                            text: "Double-click to add to the timeline, or drag onto a track"
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
                                text: mediaTile.proxy.state === "making" ? "Proxy " + Math.round(mediaTile.proxy.progress * 100) + "%"
                                    : mediaTile.proxy.state === "queued" ? "Proxy queued"
                                    : mediaTile.proxy.state === "failed" ? "Proxy failed" : "Proxy"
                                font: Theme.type.caption
                                color: "#FFFFFF"
                            }
                            ToolTip {
                                visible: proxyBadgeMouse.containsMouse
                                text: mediaTile.proxy.state === "ready" ? "A lighter copy plays in the preview; export uses the original"
                                    : mediaTile.proxy.state === "failed" ? mediaTile.proxy.error : "Making a lighter copy for the preview"
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
                                text: mediaTile.proxy.state === "ready" ? "Remove proxy" : "Make proxy"
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
                visible: !effectsGrid.visible && !looksGrid.visible && !transitionsGrid.visible && !titlesGrid.visible
                         && !mediaBin.visible && !stickersGrid.visible && !captionsPage.visible && !transcriptPage.visible
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
