import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import TechieVideoEditor.Theme
import TechieVideoEditor.Player

// Start screen: pick a canvas shape for a new project, or drop media to start one.
Rectangle {
    id: home
    // For the list of unsaved work to recover (none shown without it).
    property var project: null
    signal newProject(string name, int width, int height)
    signal openRequested()
    signal recordRequested()  // a new project with the screen recorder open
    signal instantRequested() // a new project with instant video open
    signal mediaDropped(url file)
    signal recoverRequested(string id)
    signal recentRequested(string path) // a recent project's card clicked: open it

    // "home" or "projects" (every recent project, to search and sort).
    property string page: "home"
    // The Projects page: its search text and order ("date": last opened first, "name").
    property string projectSearch: ""
    property string projectSort: "date"
    readonly property var recents: project ? project.recentProjects : []
    // How many of them the Home page shows (the Projects page has the rest).
    readonly property int recentsShown: 8
    readonly property var listedProjects: {
        const text = projectSearch.trim().toLowerCase()
        const list = recents.filter((p) => text === "" || p.name.toLowerCase().indexOf(text) >= 0
                                            || p.path.toLowerCase().indexOf(text) >= 0)
        if (projectSort === "name")
            list.sort((a, b) => a.name.localeCompare(b.name, Qt.locale().name) || b.lastOpened - a.lastOpened)
        return list
    }

    // Files moved or deleted while the app was elsewhere show as missing when it is back.
    onVisibleChanged: if (visible && project) project.refreshRecentProjects()
    Connections {
        target: Qt.application
        function onStateChanged() {
            if (Qt.application.state === Qt.ApplicationActive && home.visible && home.project)
                home.project.refreshRecentProjects()
        }
    }

    color: Theme.color.canvas

    // The models dialog (offered by the app once, when it is first opened).
    function openModels() {
        if (home.project)
            modelsLoader.active = true
        if (modelsLoader.item)
            modelsLoader.item.open()
    }
    Loader {
        id: modelsLoader
        active: false
        sourceComponent: ModelsDialog { objectName: "modelsDialog"; project: home.project }
    }

    readonly property var ratios: [
        { name: qsTr("Landscape"), ratio: "16:9", w: 1920, h: 1080 },
        { name: qsTr("Vertical"), ratio: "9:16", w: 1080, h: 1920 },
        { name: qsTr("Square"), ratio: "1:1", w: 1080, h: 1080 },
        { name: qsTr("Standard"), ratio: "4:3", w: 1440, h: 1080 },
        { name: qsTr("Cinema"), ratio: "21:9", w: 2560, h: 1080 }
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
                        text: qsTr("TechieVideoEditor")
                        font: Theme.type.heading
                        color: Theme.color.textPrimary
                    }
                }

                // Home and Projects; Templates (the editor's library has them) and Recordings
                // are placeholders still.
                Repeater {
                    model: [
                        { icon: "home", label: qsTr("Home"), page: "home" },
                        { icon: "folder", label: qsTr("Projects"), page: "projects" },
                        { icon: "templates", label: qsTr("Templates"), page: "" },
                        { icon: "record", label: qsTr("Recordings"), page: "" }
                    ]
                    delegate: AbstractButton {
                        id: railEntry
                        required property var modelData
                        readonly property bool current: modelData.page === home.page
                        objectName: "homeRail:" + (modelData.page || modelData.icon)
                        enabled: modelData.page !== ""
                        hoverEnabled: true
                        Layout.fillWidth: true
                        implicitHeight: Theme.size.control + Theme.space.s
                        onClicked: home.page = modelData.page

                        background: Rectangle {
                            radius: Theme.radius.m
                            color: railEntry.current ? Theme.color.accentSubtle
                                 : railEntry.hovered ? Theme.color.surfaceHover : "transparent"
                        }
                        contentItem: RowLayout {
                            spacing: Theme.space.s
                            Icon {
                                Layout.leftMargin: Theme.space.s
                                name: railEntry.modelData.icon
                                size: Theme.size.iconSmall + 2
                                color: railEntry.current ? Theme.color.accentText
                                     : railEntry.enabled ? Theme.color.textSecondary : Theme.color.textDisabled
                            }
                            Text {
                                text: railEntry.modelData.label
                                font: Theme.type.body
                                color: railEntry.current ? Theme.color.textPrimary
                                     : railEntry.enabled ? Theme.color.textSecondary : Theme.color.textDisabled
                                Layout.fillWidth: true
                            }
                        }
                    }
                }

                Item { Layout.fillHeight: true }

                // The models on-device features use: which are here, and getting them.
                TextButton {
                    objectName: "homeModels"
                    visible: home.project !== null
                    Layout.fillWidth: true
                    Layout.bottomMargin: Theme.space.m
                    text: qsTr("On-device models…")
                    iconName: "import"
                    variant: "secondary"
                    tip: qsTr("Download the models for captions, speech cleanup and depth of field")
                    onClicked: home.openModels()
                }

                // The app's language: this Mac's, or one chosen here (switches at once).
                Text {
                    text: qsTr("Language")
                    font: Theme.type.label
                    color: Theme.color.textSecondary
                }
                AppComboBox {
                    objectName: "languagePicker"
                    Layout.fillWidth: true
                    Layout.bottomMargin: Theme.space.s
                    model: Language.languages
                    textRole: "label"
                    valueRole: "id"
                    currentIndex: Math.max(0, Language.languages.findIndex((l) => l.id === Language.language))
                    onActivated: Language.language = currentValue
                }

                IconButton {
                    objectName: "themeToggle"
                    iconName: Theme.dark ? "sun" : "moon"
                    tip: Theme.dark ? qsTr("Light theme") : qsTr("Dark theme")
                    onClicked: Theme.dark = !Theme.dark
                }
            }

            Divider { anchors.right: parent.right; height: parent.height; width: 1 }
        }

        // Content: the Home page, or every project
        Item {
        Layout.fillWidth: true
        Layout.fillHeight: true

        Flickable {
            id: homePage
            objectName: "homePage"
            anchors.fill: parent
            visible: home.page === "home"
            contentHeight: content.implicitHeight + Theme.space.xxl * 2
            clip: true
            ScrollBar.vertical: ScrollBar {}

            // A video dropped anywhere here starts a project with it.
            DropArea {
                id: drop
                width: homePage.width
                height: Math.max(homePage.height, homePage.contentHeight)
                onDropped: (event) => {
                    if (event.hasUrls)
                        home.mediaDropped(event.urls[0])
                }
            }

            ColumnLayout {
                id: content
                x: Theme.space.xxl
                y: Theme.space.xxl
                width: parent.width - Theme.space.xxl * 2
                spacing: Theme.space.xl

                Text {
                    text: qsTr("Start creating")
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

                    Text { text: qsTr("Unsaved work"); font: Theme.type.title; color: Theme.color.textPrimary }
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("TechieVideoEditor kept these changes when it last closed without saving.")
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
                                        text: (modelData.clipCount === 1 ? qsTr("Last saved automatically %1 · 1 clip").arg(Fmt.dateTime(modelData.savedAt))
                                               : qsTr("Last saved automatically %1 · %2 clips").arg(Fmt.dateTime(modelData.savedAt)).arg(Fmt.num(modelData.clipCount)))
                                              + (modelData.originalPath ? " · " + modelData.originalPath : "")
                                        font: Theme.type.label
                                        color: Theme.color.textSecondary
                                        elide: Text.ElideMiddle
                                        Layout.fillWidth: true
                                    }
                                }
                                TextButton {
                                    objectName: "discardRecovery:" + modelData.id
                                    text: qsTr("Discard")
                                    variant: "ghost"
                                    onClicked: home.project.discardRecovery(modelData.id)
                                }
                                TextButton {
                                    objectName: "recover:" + modelData.id
                                    text: qsTr("Recover")
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
                    Text { objectName: "newProjectHeading"; text: qsTr("New project"); font: Theme.type.title; color: Theme.color.textPrimary }

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
                    TextButton { objectName: "openProject"; text: qsTr("Open project…"); iconName: "folder"
                                 tip: qsTr("A project, or a timeline from another editor (OpenTimelineIO, Final Cut Pro XML)")
                                 onClicked: home.openRequested() }
                    TextButton { objectName: "homeRecord"; text: qsTr("Record screen"); iconName: "record"
                                 tip: qsTr("A new project with the screen and camera recorder open")
                                 onClicked: home.recordRequested() }
                    TextButton { objectName: "homeInstant"; text: qsTr("Instant mode"); iconName: "effects"
                                 tip: qsTr("A new project that makes a video from your pictures and clips for you")
                                 onClicked: home.instantRequested() }
                }

                // Recent projects: the latest few as cards (all of them on the Projects page);
                // with none yet, a drop target.
                ColumnLayout {
                    objectName: "recentSection"
                    Layout.fillWidth: true
                    spacing: Theme.space.m
                    RowLayout {
                        Layout.fillWidth: true
                        Text { text: qsTr("Recent projects"); font: Theme.type.title; color: Theme.color.textPrimary }
                        Item { Layout.fillWidth: true }
                        TextButton {
                            objectName: "allProjects"
                            visible: home.recents.length > home.recentsShown
                            text: qsTr("All projects (%1)").arg(Fmt.num(home.recents.length))
                            variant: "ghost"
                            onClicked: home.page = "projects"
                        }
                    }

                    Flow {
                        visible: home.recents.length > 0
                        Layout.fillWidth: true
                        spacing: Theme.space.m
                        Repeater {
                            model: home.recents.slice(0, home.recentsShown)
                            delegate: RecentProjectCard {
                                required property var modelData
                                entry: modelData
                                onOpenRequested: (path) => home.recentRequested(path)
                                onRevealRequested: (path) => home.project.revealRecentProject(path)
                                onRemoveRequested: (path) => home.project.removeRecentProject(path)
                            }
                        }
                    }
                    Text {
                        visible: home.recents.length > 0
                        text: qsTr("Drop a video anywhere here to start a project with it")
                        font: Theme.type.label
                        color: drop.containsDrag ? Theme.color.accentText : Theme.color.textSecondary
                    }

                    Rectangle {
                        objectName: "recentEmpty"
                        visible: home.recents.length === 0
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
                                text: qsTr("Recent projects appear here")
                                font: Theme.type.heading
                                color: Theme.color.textPrimary
                            }
                            Text {
                                anchors.horizontalCenter: parent.horizontalCenter
                                text: qsTr("Drop a video here to start a project with it")
                                font: Theme.type.label
                                color: Theme.color.textSecondary
                            }
                        }
                    }
                }
            }
        }

        // Projects: every recent project, searched by name or folder, by date or name.
        ColumnLayout {
            id: projectsPage
            objectName: "projectsPage"
            anchors.fill: parent
            anchors.margins: Theme.space.xxl
            anchors.bottomMargin: 0
            visible: home.page === "projects"
            spacing: Theme.space.l

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.space.m
                ColumnLayout {
                    spacing: Theme.space.xxs
                    Text { text: qsTr("Projects"); font: Theme.type.display; color: Theme.color.textPrimary }
                    Text {
                        objectName: "projectsCount"
                        text: home.recents.length === 1 ? qsTr("1 project opened or saved lately")
                                                        : qsTr("%1 projects opened or saved lately").arg(Fmt.num(home.recents.length))
                        font: Theme.type.label
                        color: Theme.color.textSecondary
                    }
                }
                Item { Layout.fillWidth: true }
                SearchField {
                    objectName: "projectSearch"
                    Layout.preferredWidth: 260
                    placeholderText: qsTr("Search projects")
                    text: home.projectSearch
                    onTextChanged: home.projectSearch = text
                }
                AppComboBox {
                    objectName: "projectSort"
                    Layout.preferredWidth: 170
                    model: [{ id: "date", label: qsTr("Last opened") }, { id: "name", label: qsTr("Name") }]
                    textRole: "label"
                    valueRole: "id"
                    currentIndex: home.projectSort === "name" ? 1 : 0
                    onActivated: home.projectSort = currentValue
                }
                TextButton { text: qsTr("Open project…"); iconName: "folder"; onClicked: home.openRequested() }
            }

            Flickable {
                Layout.fillWidth: true
                Layout.fillHeight: true
                contentHeight: grid.implicitHeight + Theme.space.xxl
                clip: true
                ScrollBar.vertical: ScrollBar {}

                Flow {
                    id: grid
                    objectName: "projectsGrid"
                    width: parent.width
                    spacing: Theme.space.m
                    Repeater {
                        model: home.listedProjects
                        delegate: RecentProjectCard {
                            required property var modelData
                            objectName: "projectCard:" + modelData.path
                            entry: modelData
                            onOpenRequested: (path) => home.recentRequested(path)
                            onRevealRequested: (path) => home.project.revealRecentProject(path)
                            onRemoveRequested: (path) => home.project.removeRecentProject(path)
                        }
                    }
                }

                // None yet, or none matching the search.
                Column {
                    visible: home.listedProjects.length === 0
                    anchors.horizontalCenter: parent.horizontalCenter
                    y: Theme.space.xxl * 2
                    spacing: Theme.space.s
                    Icon { anchors.horizontalCenter: parent.horizontalCenter; name: "folder"; size: 28 }
                    Text {
                        objectName: "projectsEmpty"
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: home.recents.length === 0 ? qsTr("No projects yet")
                                                        : qsTr("No projects match “%1”").arg(home.projectSearch.trim())
                        font: Theme.type.heading
                        color: Theme.color.textPrimary
                    }
                    Text {
                        visible: home.recents.length === 0
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: qsTr("Projects you open or save appear here")
                        font: Theme.type.label
                        color: Theme.color.textSecondary
                    }
                }
            }
        }
        }
    }
}
