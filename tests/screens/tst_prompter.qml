import QtQuick
import QtTest
import TechieVideoEditor.Theme
import TechieVideoEditor.Player
import TechieVideoEditor.Shell
import "screens.js" as Screens

Item {
    id: root
    width: 1400
    height: 780
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
        name: "ShotPrompter"
        when: windowShown
        function test_shots() {
            for (const dark of [true, false]) {
                Theme.dark = dark
                const tag = dark ? "dark" : "light"
                Screens.palette(root.Window.window, Theme)
                const f = createTemporaryObject(editorComponent, root)
                waitForRendering(f)
                const p = f.project
                p.teleprompter.text = "Welcome back to the channel.\n\nToday we're cutting a trailer from an afternoon of holiday clips — three minutes of footage down to thirty seconds.\n\nFirst, let's bring everything into the library and sort out the best moments.\n\nThen we'll lay them on the beat of the music and add a title at the end."
                p.teleprompter.fontSize = 44
                p.teleprompter.speed = 4
                f.editor.openTeleprompter()
                const w = f.editor.teleprompter
                w.width = 900; w.height = 520
                wait(400)
                findChild(w.contentItem, "teleprompterScroll").contentY = 120
                wait(600)
                const image = grabImage(w.contentItem)
                image.save(screensDir + "/teleprompter-" + tag + ".png")
                w.close()
                f.destroy(); wait(100)
            }
            Theme.dark = true
        }
    }
}
