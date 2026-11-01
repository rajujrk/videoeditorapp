import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import TechieVideoEditor.Theme

// The library's Templates tab. When the project was made from a template, its storyboard:
// the media slots in order (choose what goes in each, where a video starts), the texts and
// the music. Below, the templates: categories, favourites, animated previews; using one
// asks for the shape (six) and makes a new project. Save as template marks this project's
// slots, texts and music (SaveTemplateDialog.qml).
ScrollView {
    id: page
    required property var project
    readonly property var library: project.templates
    readonly property var info: project.templateInfo
    readonly property bool hasTemplate: info.id !== undefined && info.id !== ""
    readonly property var slotList: info.slots || []
    readonly property var textList: info.texts || []
    readonly property var music: info.music || ({})
    readonly property var media: project.media
    property string category: ""
    // The slot a file is being imported for.
    property int importingFor: -1

    function shown() {
        return library.templates.filter((t) => category === "" ? true
                                              : category === "favorites" ? t.favorite
                                              : category === "mine" ? !t.builtIn && !t.pack
                                              : t.category === category)
    }
    function clock(seconds) { return seconds < 60 ? seconds.toFixed(0) + " s" : Math.floor(seconds / 60) + ":" + String(Math.round(seconds % 60)).padStart(2, "0") }

    contentWidth: availableWidth
    clip: true

    FileDialog {
        id: importDialog
        title: "Choose a picture or video for the slot"
        fileMode: FileDialog.OpenFile
        nameFilters: ["Pictures and videos (*.mp4 *.mov *.m4v *.mkv *.avi *.webm *.png *.jpg *.jpeg *.heic *.heif *.webp *.tif *.tiff)"]
        onAccepted: {
            if (page.project.importMedia([selectedFile]) === 1 && page.importingFor >= 0)
                page.project.fillTemplateSlot(page.importingFor, page.project.media[page.project.media.length - 1].id)
            page.importingFor = -1
        }
    }
    SaveTemplateDialog { id: saveDialog; objectName: "saveTemplateDialog"; project: page.project }
    AutoEditPanel { id: autoEdit; objectName: "autoEditPanel"; project: page.project }
    ShapePicker { id: shapePicker; objectName: "shapePicker"; project: page.project }

    component Chip: AbstractButton {
        id: chip
        property string label
        hoverEnabled: true
        leftPadding: Theme.space.m
        rightPadding: Theme.space.m
        implicitHeight: 26
        background: Rectangle {
            radius: height / 2
            color: chip.checked ? Theme.color.accentSubtle : chip.hovered ? Theme.color.surfaceHover : "transparent"
            border.color: chip.checked ? Theme.color.accent : Theme.color.borderSubtle
        }
        contentItem: Text {
            text: chip.label
            font: Theme.type.caption
            color: chip.checked ? Theme.color.accentText : Theme.color.textSecondary
            verticalAlignment: Text.AlignVCenter
        }
    }

    ColumnLayout {
        width: page.availableWidth - Theme.space.m * 2
        x: Theme.space.m
        spacing: Theme.space.s

        // --- Made for you ---
        Text { text: "Make it for me"; font: Theme.type.heading; color: Theme.color.textPrimary }
        GridLayout {
            Layout.fillWidth: true
            columns: 3
            columnSpacing: Theme.space.xs
            Repeater {
                model: [{ id: "instant", label: "Instant video", icon: "generate" }, { id: "montage", label: "Highlights", icon: "effects" },
                        { id: "shorts", label: "Shorts", icon: "screen-record" }]
                delegate: AbstractButton {
                    id: makeButton
                    required property var modelData
                    objectName: "makeFor:" + modelData.id
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    implicitHeight: 56
                    hoverEnabled: true
                    onClicked: autoEdit.show(modelData.id)
                    background: Rectangle {
                        radius: Theme.radius.m
                        color: makeButton.hovered ? Theme.color.surfaceHover : Theme.color.surfaceRaised
                        border.color: Theme.color.borderSubtle
                    }
                    contentItem: ColumnLayout {
                        spacing: 2
                        Icon { Layout.alignment: Qt.AlignHCenter; name: makeButton.modelData.icon; size: Theme.size.iconSmall; color: Theme.color.accent }
                        Text {
                            Layout.alignment: Qt.AlignHCenter
                            text: makeButton.modelData.label
                            font: Theme.type.caption
                            color: Theme.color.textPrimary
                        }
                    }
                }
            }
        }

        // --- The storyboard ---
        ColumnLayout {
            objectName: "storyboard"
            visible: page.hasTemplate
            Layout.fillWidth: true
            spacing: Theme.space.s

            Text {
                Layout.fillWidth: true
                text: (page.info.name || "") + " · " + page.slotList.filter((s) => s.filled).length + " of " + page.slotList.length + " filled"
                font: Theme.type.heading
                color: Theme.color.textPrimary
                elide: Text.ElideRight
            }
            Text {
                Layout.fillWidth: true
                text: "Choose what goes in each slot; it is cropped to fill the frame, and a short video is slowed to fit."
                font: Theme.type.caption
                color: Theme.color.textSecondary
                wrapMode: Text.WordWrap
            }
            Repeater {
                model: page.slotList
                delegate: Rectangle {
                    id: slotCard
                    required property var modelData
                    readonly property alias menu: slotMenu
                    objectName: "templateSlot:" + modelData.index
                    Layout.fillWidth: true
                    implicitHeight: slotColumn.implicitHeight + Theme.space.s * 2
                    radius: Theme.radius.m
                    color: Theme.color.surfaceRaised
                    border.color: modelData.filled ? Theme.color.borderSubtle : Theme.color.accent
                    ColumnLayout {
                        id: slotColumn
                        anchors { left: parent.left; right: parent.right; top: parent.top; margins: Theme.space.s }
                        spacing: Theme.space.xs
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Theme.space.s
                            Rectangle {
                                Layout.preferredWidth: 72
                                Layout.preferredHeight: 44
                                radius: Theme.radius.s
                                color: Theme.color.surfaceSunken
                                clip: true
                                Image {
                                    anchors.fill: parent
                                    asynchronous: true
                                    fillMode: Image.PreserveAspectCrop
                                    source: slotCard.modelData.path
                                            ? "image://tvethumb/" + encodeURIComponent(slotCard.modelData.path) + "@"
                                              + (slotCard.modelData.sourceIn + 0.1).toFixed(2) + "@88" : ""
                                }
                                Rectangle {
                                    anchors { left: parent.left; top: parent.top; margins: 3 }
                                    width: 18
                                    height: 18
                                    radius: 9
                                    color: slotCard.modelData.filled ? Theme.color.overlay : Theme.color.accent
                                    Text { anchors.centerIn: parent; text: slotCard.modelData.index + 1; font: Theme.type.caption; color: "#FFFFFF" }
                                }
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2
                                Text { Layout.fillWidth: true; text: slotCard.modelData.label; font: Theme.type.label; color: Theme.color.textPrimary; elide: Text.ElideRight }
                                Text {
                                    Layout.fillWidth: true
                                    text: slotCard.modelData.duration.toFixed(1) + " s · "
                                          + (slotCard.modelData.filled ? slotCard.modelData.name
                                             + (slotCard.modelData.speed < 0.999 ? " · slowed to " + Math.round(slotCard.modelData.speed * 100) + "%" : "")
                                             : "empty")
                                    font: Theme.type.caption
                                    color: Theme.color.textSecondary
                                    elide: Text.ElideRight
                                }
                            }
                            TextButton {
                                objectName: "chooseSlot:" + slotCard.modelData.index
                                text: slotCard.modelData.filled ? "Change" : "Choose"
                                variant: slotCard.modelData.filled ? "ghost" : "secondary"
                                onClicked: slotMenu.popup()
                                Menu {
                                    id: slotMenu
                                    objectName: "slotMenu:" + slotCard.modelData.index
                                    Repeater {
                                        model: page.media.filter((m) => m.hasVideo && !m.sticker && !m.missing && !m.placeholder)
                                        delegate: MenuItem {
                                            required property var modelData
                                            objectName: "slotMedia:" + modelData.name
                                            text: modelData.name
                                            onTriggered: page.project.fillTemplateSlot(slotCard.modelData.index, modelData.id)
                                        }
                                    }
                                    MenuItem {
                                        text: "Import a file…"
                                        onTriggered: { page.importingFor = slotCard.modelData.index; importDialog.open() }
                                    }
                                }
                            }
                        }
                        // Where a video starts.
                        RowLayout {
                            visible: slotCard.modelData.filled && !slotCard.modelData.still
                                     && slotCard.modelData.mediaDuration > slotCard.modelData.duration + 0.05
                            Layout.fillWidth: true
                            Text { text: "Starts at"; font: Theme.type.caption; color: Theme.color.textSecondary }
                            Slider {
                                id: startSlider
                                objectName: "slotStart:" + slotCard.modelData.index
                                Layout.fillWidth: true
                                from: 0
                                to: Math.max(0.01, slotCard.modelData.mediaDuration - slotCard.modelData.duration)
                                value: slotCard.modelData.sourceIn
                                onPressedChanged: if (!pressed) page.project.setTemplateSlotStart(slotCard.modelData.index, value)
                            }
                            Text { text: startSlider.value.toFixed(1) + " s"; font: Theme.type.caption; color: Theme.color.textPrimary }
                        }
                    }
                }
            }

            // Texts
            Repeater {
                model: page.textList
                delegate: ColumnLayout {
                    id: textRow
                    required property var modelData
                    Layout.fillWidth: true
                    spacing: 2
                    Text { text: textRow.modelData.label; font: Theme.type.caption; color: Theme.color.textSecondary }
                    TextField {
                        objectName: "templateText:" + textRow.modelData.index
                        Layout.fillWidth: true
                        text: textRow.modelData.text
                        color: Theme.color.textPrimary
                        selectByMouse: true
                        background: Rectangle {
                            implicitHeight: 32
                            radius: Theme.radius.s
                            color: Theme.color.surfaceSunken
                            border.color: parent.activeFocus ? Theme.color.accent : Theme.color.borderSubtle
                        }
                        onEditingFinished: if (text !== textRow.modelData.text) page.project.setTemplateText(textRow.modelData.index, text)
                    }
                }
            }

            // Music
            ColumnLayout {
                visible: page.hasTemplate && page.music.name !== undefined
                Layout.fillWidth: true
                spacing: 2
                RowLayout {
                    Layout.fillWidth: true
                    Icon { name: "audio"; size: Theme.size.iconSmall; color: Theme.color.textSecondary }
                    Text {
                        objectName: "templateMusicName"
                        Layout.fillWidth: true
                        text: page.music.name || ""
                        font: Theme.type.label
                        color: Theme.color.textPrimary
                        elide: Text.ElideMiddle
                    }
                    TextButton {
                        objectName: "changeMusic"
                        text: "Change"
                        variant: "ghost"
                        onClicked: musicMenu.popup()
                        Menu {
                            id: musicMenu
                            Repeater {
                                model: page.media.filter((m) => m.hasAudio && !m.hasVideo && !m.missing)
                                delegate: MenuItem {
                                    required property var modelData
                                    objectName: "musicMedia:" + modelData.name
                                    text: modelData.name
                                    onTriggered: page.project.setTemplateMusic(modelData.id)
                                }
                            }
                        }
                    }
                }
                RowLayout {
                    visible: (page.music.mediaDuration || 0) > (page.music.duration || 0) + 0.05
                    Layout.fillWidth: true
                    Text { text: "Music starts at"; font: Theme.type.caption; color: Theme.color.textSecondary }
                    Slider {
                        id: musicStart
                        objectName: "musicStart"
                        Layout.fillWidth: true
                        from: 0
                        to: Math.max(0.01, (page.music.mediaDuration || 0) - (page.music.duration || 0))
                        value: page.music.sourceIn || 0
                        onPressedChanged: if (!pressed) page.project.setTemplateMusicStart(value)
                    }
                    Text { text: musicStart.value.toFixed(1) + " s"; font: Theme.type.caption; color: Theme.color.textPrimary }
                }
            }
            Divider { Layout.fillWidth: true; implicitHeight: 1 }
        }

        // --- The templates ---
        RowLayout {
            Layout.fillWidth: true
            Text { Layout.fillWidth: true; text: "Browse"; font: Theme.type.heading; color: Theme.color.textPrimary }
            TextButton {
                objectName: "saveAsTemplate"
                text: "Save as template…"
                variant: "ghost"
                enabled: page.project.duration > 0
                onClicked: saveDialog.open()
            }
        }
        Flow {
            Layout.fillWidth: true
            spacing: Theme.space.xs
            Repeater {
                model: page.library.categories
                delegate: Chip {
                    required property var modelData
                    objectName: "templateCategory:" + (modelData.id || "all")
                    label: modelData.label
                    checked: page.category === modelData.id
                    onClicked: page.category = modelData.id
                }
            }
        }
        GridLayout {
            Layout.fillWidth: true
            columns: 2
            rowSpacing: Theme.space.s
            columnSpacing: Theme.space.s
            Repeater {
                model: page.shown()
                delegate: Rectangle {
                    id: card
                    required property var modelData
                    objectName: "template:" + modelData.id
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    implicitHeight: cardColumn.implicitHeight + Theme.space.xs * 2
                    radius: Theme.radius.m
                    color: Theme.color.surfaceRaised
                    border.color: cardMouse.containsMouse ? Theme.color.borderStrong : Theme.color.borderSubtle
                    property real seconds: modelData.seconds * 0.15
                    Timer {
                        running: cardMouse.containsMouse
                        repeat: true
                        interval: 180
                        onTriggered: card.seconds = (card.seconds + 0.5) % Math.max(1, card.modelData.seconds)
                        onRunningChanged: if (!running) card.seconds = card.modelData.seconds * 0.15
                    }
                    ColumnLayout {
                        id: cardColumn
                        anchors { left: parent.left; right: parent.right; top: parent.top; margins: Theme.space.xs }
                        spacing: 2
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 84
                            radius: Theme.radius.s
                            color: Theme.color.surfaceSunken
                            clip: true
                            Image {
                                objectName: "templatePreview:" + card.modelData.id
                                anchors.fill: parent
                                asynchronous: true
                                fillMode: Image.PreserveAspectFit
                                source: "image://tvetemplate/" + encodeURIComponent(card.modelData.id) + "@" + card.seconds.toFixed(1) + "@168"
                            }
                            AbstractButton {
                                objectName: "favorite:" + card.modelData.id
                                anchors { right: parent.right; top: parent.top; margins: 2 }
                                implicitWidth: 22
                                implicitHeight: 22
                                onClicked: page.library.toggleFavorite(card.modelData.id)
                                background: Rectangle { radius: 11; color: Theme.color.overlay }
                                contentItem: Text {
                                    text: card.modelData.favorite ? "★" : "☆"
                                    horizontalAlignment: Text.AlignHCenter
                                    verticalAlignment: Text.AlignVCenter
                                    font: Theme.type.label
                                    color: card.modelData.favorite ? "#FFD34D" : "#FFFFFF"
                                }
                            }
                        }
                        Text { Layout.fillWidth: true; text: card.modelData.name; font: Theme.type.label; color: Theme.color.textPrimary; elide: Text.ElideRight }
                        Text {
                            Layout.fillWidth: true
                            text: card.modelData.slots + " slots · " + page.clock(card.modelData.seconds) + " · " + card.modelData.shape
                            font: Theme.type.caption
                            color: Theme.color.textSecondary
                            elide: Text.ElideRight
                        }
                    }
                    MouseArea {
                        id: cardMouse
                        anchors.fill: parent
                        anchors.topMargin: 26 // (the star stays clickable)
                        hoverEnabled: true
                        onClicked: shapePicker.show(card.modelData)
                    }
                    ToolTip {
                        visible: cardMouse.containsMouse
                        delay: 800
                        text: card.modelData.description
                    }
                }
            }
        }
        Item { implicitHeight: Theme.space.m }
    }
}
