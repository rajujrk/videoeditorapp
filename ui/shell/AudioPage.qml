import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import TechieVideoEditor.Theme

// The library's Audio tab: our own music (by mood) and sound effects (by family), sound
// packs, and My music (the person's folders). Each sound previews with its play button and
// goes to the timeline at the playhead with its add button (or a double-click). Voice speaks a
// text (VoicePane.qml).
ColumnLayout {
    id: page
    required property var project
    // The library's search text.
    property string searchText: ""
    // "music", "sfx", "mine" or "voice"; and the category chip ("" for all).
    property string section: "music"
    property string category: ""
    onSectionChanged: category = ""

    readonly property var library: project.soundLibrary
    readonly property var categoryNames: ({
        calm: qsTr("Calm"), upbeat: qsTr("Upbeat"), cinematic: qsTr("Cinematic"), lofi: qsTr("Lo-fi"), corporate: qsTr("Corporate"),
        electronic: qsTr("Electronic"), transition: qsTr("Transitions"), hit: qsTr("Hits"), interface: qsTr("Interface"),
        texture: qsTr("Textures")
    })
    function categoryName(id) { return categoryNames[id] || (id.charAt(0).toUpperCase() + id.slice(1)) }
    readonly property var categories: {
        if (section === "mine" || section === "voice")
            return []
        const seen = []
        for (const sound of library.sounds)
            if (sound.kind === section && sound.category !== "" && seen.indexOf(sound.category) < 0)
                seen.push(sound.category)
        return [""].concat(seen)
    }
    function matches(title) {
        const text = searchText.trim().toLowerCase()
        return text === "" || title.toLowerCase().indexOf(text) >= 0
    }
    readonly property var shown: section === "voice" ? []
        : section === "mine"
        ? library.myMusic.filter((s) => matches(s.title))
        : library.sounds.filter((s) => s.kind === section && (category === "" || s.category === category) && matches(s.title))
    function duration(seconds) {
        const whole = Math.round(seconds)
        return Math.floor(whole / 60) + ":" + String(whole % 60).padStart(2, "0")
    }

    spacing: Theme.space.s
    // A preview stops when the tab is left.
    onVisibleChanged: if (!visible) library.stopPreview()

    FolderDialog {
        id: folderDialog
        title: qsTr("Add a music folder")
        onAccepted: page.library.addMusicFolder(selectedFolder)
    }
    FolderDialog {
        id: packDialog
        title: qsTr("Add a sound pack")
        onAccepted: if (!page.library.importPack(selectedFolder)) page.project.errorOccurred(page.library.lastError())
    }

    // Music · Sound effects · My music
    RowLayout {
        Layout.fillWidth: true
        Layout.leftMargin: Theme.space.m
        Layout.rightMargin: Theme.space.m
        spacing: 0
        Repeater {
            model: [{ id: "music", label: qsTr("Music") }, { id: "sfx", label: qsTr("Sound effects") }, { id: "mine", label: qsTr("My music") }, { id: "voice", label: qsTr("Voice") }]
            delegate: AbstractButton {
                id: sectionButton
                required property var modelData
                required property int index
                objectName: "audioSection:" + modelData.id
                Layout.fillWidth: true
                implicitHeight: 28
                checked: page.section === modelData.id
                hoverEnabled: true
                onClicked: page.section = modelData.id
                background: Rectangle {
                    radius: Theme.radius.s
                    color: sectionButton.checked ? Theme.color.accentSubtle
                         : sectionButton.hovered ? Theme.color.surfaceHover : Theme.color.surfaceSunken
                    border.color: sectionButton.checked ? Theme.color.accent : Theme.color.borderSubtle
                }
                contentItem: Text {
                    text: sectionButton.modelData.label
                    font: Theme.type.label
                    color: sectionButton.checked ? Theme.color.accentText : Theme.color.textSecondary
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                }
            }
        }
    }

    // Moods or families
    Flow {
        visible: page.categories.length > 2
        Layout.fillWidth: true
        Layout.leftMargin: Theme.space.m
        Layout.rightMargin: Theme.space.m
        spacing: Theme.space.xs
        Repeater {
            model: page.categories
            delegate: AbstractButton {
                id: chip
                required property var modelData
                objectName: "audioCategory:" + (modelData || "all")
                checked: page.category === modelData
                hoverEnabled: true
                implicitWidth: chipLabel.implicitWidth + Theme.space.m * 2
                implicitHeight: 24
                onClicked: page.category = modelData
                background: Rectangle {
                    radius: height / 2
                    color: chip.checked ? Theme.color.accentSubtle : chip.hovered ? Theme.color.surfaceHover : "transparent"
                    border.color: chip.checked ? Theme.color.accent : Theme.color.borderSubtle
                }
                contentItem: Text {
                    id: chipLabel
                    text: chip.modelData === "" ? qsTr("All") : page.categoryName(chip.modelData)
                    font: Theme.type.caption
                    color: chip.checked ? Theme.color.accentText : Theme.color.textSecondary
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }
    }

    // My music: the folders it comes from.
    ColumnLayout {
        visible: page.section === "mine"
        Layout.fillWidth: true
        Layout.leftMargin: Theme.space.m
        Layout.rightMargin: Theme.space.m
        spacing: Theme.space.xxs
        Repeater {
            model: page.library.musicFolders
            delegate: RowLayout {
                id: folderRow
                required property string modelData
                Layout.fillWidth: true
                spacing: Theme.space.xs
                Icon { name: "folder"; size: Theme.size.iconSmall; color: Theme.color.textSecondary }
                Text {
                    Layout.fillWidth: true
                    text: folderRow.modelData
                    font: Theme.type.caption
                    color: Theme.color.textSecondary
                    elide: Text.ElideMiddle
                }
                IconButton {
                    objectName: "removeMusicFolder:" + folderRow.modelData
                    iconName: "close"
                    buttonSize: 22
                    tip: qsTr("Stop showing this folder")
                    onClicked: page.library.removeMusicFolder(folderRow.modelData)
                }
            }
        }
        TextButton {
            objectName: "addMusicFolder"
            text: qsTr("Add a folder…")
            iconName: "folder"
            variant: "ghost"
            tip: qsTr("Show the music in a folder on this Mac")
            onClicked: folderDialog.open()
        }
    }

    // Made the first time Voice is opened (it lists the Mac's voices).
    Loader {
        active: page.section === "voice" || item !== null
        visible: page.section === "voice"
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.leftMargin: Theme.space.m
        Layout.rightMargin: Theme.space.m
        sourceComponent: VoicePane {
            objectName: "voicePane"
            project: page.project
        }
    }

    ListView {
        id: soundList
        objectName: "soundList"
        visible: page.section !== "voice"
        Layout.fillWidth: true
        Layout.fillHeight: true
        clip: true
        spacing: Theme.space.xxs
        model: page.shown
        ScrollBar.vertical: ScrollBar {}

        delegate: Rectangle {
            id: row
            required property var modelData
            readonly property bool playing: page.library.previewing === modelData.id
            objectName: "sound:" + modelData.id
            // Dragged to the timeline: placed where it is dropped (on that sound track if free).
            readonly property string libraryItem: modelData.id
            Drag.active: rowMouse.drag.active
            Drag.dragType: Drag.Automatic
            Drag.supportedActions: Qt.CopyAction
            Drag.keys: ["application/x-tve-sound"]
            Drag.mimeData: { "application/x-tve-sound": modelData.id }
            width: ListView.view.width - Theme.space.m * 2
            x: Theme.space.m
            height: 48
            radius: Theme.radius.m
            color: rowMouse.containsMouse || playing ? Theme.color.surfaceHover : Theme.color.surfaceRaised
            border.color: playing ? Theme.color.accent : Theme.color.borderSubtle

            MouseArea {
                id: rowMouse
                anchors.fill: parent
                hoverEnabled: true
                drag.target: soundDragProxy
                onDoubleClicked: page.project.addSound(row.modelData.id)
            }
            Item { id: soundDragProxy }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.space.xs
                anchors.rightMargin: Theme.space.xs
                spacing: Theme.space.s

                IconButton {
                    objectName: "previewSound:" + row.modelData.id
                    iconName: row.playing ? "pause" : "play"
                    checked: row.playing
                    tip: row.playing ? qsTr("Stop") : qsTr("Listen")
                    onClicked: if (!page.library.preview(row.modelData.id)) page.project.errorOccurred(page.library.lastError())
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    Text {
                        Layout.fillWidth: true
                        text: row.modelData.title
                        font: Theme.type.label
                        color: Theme.color.textPrimary
                        elide: Text.ElideRight
                    }
                    Text {
                        Layout.fillWidth: true
                        text: (page.section === "mine" ? row.modelData.folder.split("/").pop()
                                                       : page.categoryName(row.modelData.category)
                                                         + (row.modelData.pack ? " · " + row.modelData.pack : ""))
                              + " · " + page.duration(row.modelData.duration)
                        font: Theme.type.caption
                        color: Theme.color.textSecondary
                        elide: Text.ElideRight
                    }
                    // How far the preview has got.
                    Rectangle {
                        visible: row.playing
                        Layout.fillWidth: true
                        implicitHeight: 2
                        radius: 1
                        color: Theme.color.borderSubtle
                        Rectangle {
                            width: parent.width * page.library.previewProgress
                            height: parent.height
                            radius: 1
                            color: Theme.color.accent
                        }
                    }
                }
                IconButton {
                    objectName: "addSound:" + row.modelData.id
                    iconName: "plus"
                    tip: qsTr("Add at the playhead")
                    onClicked: page.project.addSound(row.modelData.id)
                }
            }
        }

        Text {
            anchors.centerIn: parent
            width: parent.width - Theme.space.xl * 2
            visible: soundList.count === 0
            text: page.section === "mine" && page.library.musicFolders.length === 0
                  ? qsTr("Add a folder to see your own music here")
                  : qsTr("Nothing matches")
            font: Theme.type.body
            color: Theme.color.textSecondary
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
        }
    }

    // Packs: adding one, and what was left out of those installed.
    ColumnLayout {
        visible: page.section === "music" || page.section === "sfx"
        Layout.fillWidth: true
        Layout.leftMargin: Theme.space.m
        Layout.rightMargin: Theme.space.m
        Layout.bottomMargin: Theme.space.s
        spacing: Theme.space.xxs
        Text {
            objectName: "packProblems"
            visible: page.library.packProblems.length > 0
            Layout.fillWidth: true
            text: qsTr("Left out (failed its check): %1").arg(page.library.packProblems.join("; "))
            font: Theme.type.caption
            color: Theme.color.danger
            wrapMode: Text.WordWrap
        }
        TextButton {
            objectName: "addSoundPack"
            text: qsTr("Add a sound pack…")
            iconName: "plus"
            variant: "ghost"
            tip: qsTr("Add a folder with a sound pack's manifest; every file is checked before use")
            onClicked: packDialog.open()
        }
    }
}
