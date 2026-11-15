import QtQuick
import QtTest
import TechieVideoEditor.Theme
import TechieVideoEditor.Player
import TechieVideoEditor.Shell
import "screens.js" as Screens

Item {
    id: root
    width: 1280
    height: 800
    Component {
        id: editorComponent
        Item {
            property alias project: controller
            property alias editor: screen
            anchors.fill: parent
            ProjectController { id: controller; view: screen.player; autosaveEnabled: false }
            EditorScreen { id: screen; anchors.fill: parent; project: controller }
        }
    }
    TestCase {
        name: "ShotSetup"
        when: windowShown
        function test_shots() {
            const window = root.Window.window
            for (const dark of [true, false]) {
                Theme.dark = dark
                const tag = dark ? "dark" : "light"
                Screens.palette(root.Window.window, Theme)
                const f = createTemporaryObject(editorComponent, root)
                waitForRendering(f)
                const p = f.project
                verify(p.importAndAppend("file://" + analysisMediaDir + "/scenes.mov"))
                p.seek(0.5)
                // Favourites with tags in the Effects tab, the tag editor open.
                const library = f.editor.libraryView
                library.currentTab = library.tabs.findIndex((t) => t.key === "Effects")
                const effects = f.editor.player.availableEffects
                Favourites.setTags("effect", effects[0].id, ["wedding", "soft"])
                Favourites.setTags("effect", effects[2].id, ["soft"])
                Favourites.setTags("effect", effects[5].id, ["retro"])
                Favourites.setTags("effect", effects[7].id, ["wedding"])
                library.effectCategory = "favourites"
                wait(900)
                library.editTags("effect", effects[0].id, effects[0].title)
                wait(600)
                grabImage(window.contentItem).save(screensDir + "/favourites-" + tag + ".png")
                findChild(library, "favouriteTagEditor").close()
                for (const key of Object.keys(Favourites.items)) {
                    const at = key.indexOf(":")
                    Favourites.toggle(key.substring(0, at), key.substring(at + 1))
                }
                // The keyboard drawn: Premiere's keys with ⌘.
                KeyMap.applyPreset("premiere")
                const dialog = findChild(f.editor, "shortcutsDialog")
                dialog.open()
                tryVerify(() => dialog.opened)
                dialog.view = "keyboard"
                findChild(dialog.contentItem, "keyboardMap").modifiers = ["Ctrl"]
                wait(600)
                grabImage(window.contentItem).save(screensDir + "/keyboard-" + tag + ".png")
                dialog.close()
                KeyMap.resetAll()
                f.destroy(); wait(100)
            }
            Theme.dark = true
        }
    }
}
