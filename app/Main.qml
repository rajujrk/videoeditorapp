import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import TechieVideoEditor.Theme
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
    title: screen === "editor" ? editorScreen.projectName + " — TechieVideoEditor" : "TechieVideoEditor"
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

    StackLayout {
        anchors.fill: parent
        currentIndex: root.screen === "editor" ? 1 : 0

        HomeScreen {
            onNewProject: (name, w, h) => {
                editorScreen.projectName = name
                editorScreen.canvasWidth = w
                editorScreen.canvasHeight = h
                root.screen = "editor"
            }
            onMediaDropped: (file) => {
                editorScreen.player.source = file
                root.screen = "editor"
            }
        }

        EditorScreen {
            id: editorScreen
            onHomeRequested: root.screen = "home"
        }
    }
}
