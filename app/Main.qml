import QtCore
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import TechieVideoEditor.Theme
import TechieVideoEditor.Player
import TechieVideoEditor.Shell

ApplicationWindow {
    id: root
    // "home" or "editor"; set from the command line too.
    property string screen: "home"

    width: 1600
    height: 940
    minimumWidth: 1180
    minimumHeight: 700
    visible: true
    title: screen === "editor" ? (project.modified ? qsTr("%1 (edited) — TechieVideoEditor") : qsTr("%1 — TechieVideoEditor")).arg(project.name)
                               : qsTr("TechieVideoEditor")
    color: Theme.color.canvas

    // Stock controls (Slider, Switch, ComboBox, ToolTip…) pick their colors up from here.
    palette {
        window: Theme.color.surface
        windowText: Theme.color.textPrimary
        base: Theme.color.surfaceRaised
        alternateBase: Theme.color.surfaceHover
        text: Theme.color.textPrimary
        button: Theme.color.surfaceRaised
        buttonText: Theme.color.textPrimary
        brightText: Theme.color.textOnAccent
        highlight: Theme.color.accent
        highlightedText: Theme.color.textOnAccent
        light: Theme.color.surfaceHover
        midlight: Theme.color.borderSubtle
        mid: Theme.color.borderStrong
        dark: Theme.color.textDisabled
        shadow: "#000000"
        toolTipBase: Theme.color.surfaceRaised
        toolTipText: Theme.color.textPrimary
        placeholderText: Theme.color.textDisabled
        disabled.windowText: Theme.color.textDisabled
        disabled.text: Theme.color.textDisabled
        disabled.buttonText: Theme.color.textDisabled
    }
    font: Theme.type.body

    // Asked once, on the first start: the models on-device features need (main.cpp calls
    // this unless the app was started for a scripted run).
    Settings {
        id: firstRun
        category: "firstRun"
        property bool modelsOffered: false
    }
    function offerModelsOnce() {
        if (firstRun.modelsOffered)
            return
        firstRun.modelsOffered = true
        home.openModels()
    }
    // A file opened from Finder (double-clicked, dropped on the Dock icon, Open With): a
    // project or another editor's timeline replaces the one open; media starts a new project
    // from the Home screen, or joins the open one.
    function openFile(file) {
        const name = String(file).toLowerCase()
        if (/\.(tve|otio|fcpxml|fcpxmld)$/.test(name)) {
            project.pause()
            if (project.open(file))
                root.screen = "editor"
            return
        }
        if (root.screen !== "editor")
            project.newProject(1920, 1080)
        if (project.importAndAppend(file))
            root.screen = "editor"
    }

    ProjectController {
        id: project
        objectName: "project"
        view: editorScreen.player
    }

    StackLayout {
        anchors.fill: parent
        currentIndex: root.screen === "editor" ? 1 : 0

        HomeScreen {
            id: home
            project: project
            onRecoverRequested: (id) => {
                if (project.recoverProject(id))
                    root.screen = "editor"
            }
            onNewProject: (name, w, h) => {
                project.newProject(w, h, name)
                root.screen = "editor"
            }
            onOpenRequested: openDialog.open()
            onRecordRequested: {
                project.newProject(1920, 1080, qsTr("Screen recording"))
                root.screen = "editor"
                editorScreen.openRecorder()
            }
            onInstantRequested: {
                project.newProject(1920, 1080, qsTr("Instant video"))
                root.screen = "editor"
                editorScreen.openInstantMode()
            }
            onMediaDropped: (file) => {
                project.newProject(1920, 1080)
                project.importAndAppend(file)
                root.screen = "editor"
            }
        }

        EditorScreen {
            id: editorScreen
            project: project
            onHomeRequested: {
                project.pause()
                root.screen = "home"
            }
        }
    }

    FileDialog {
        id: openDialog
        title: qsTr("Open project")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("TechieVideoEditor projects (*.tve)"),
                      qsTr("Timelines from other editors (*.otio *.fcpxml *.fcpxmld)"), qsTr("All files (*)")]
        onAccepted: {
            if (project.open(selectedFile))
                root.screen = "editor"
        }
    }
}
