import QtQuick
import QtTest
import TechieVideoEditor.Theme
import TechieVideoEditor.Player
import TechieVideoEditor.Shell
import "screens.js" as Screens

Item {
    id: root
    width: 1280
    height: 720
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
        name: "ShotImageSequence"
        when: windowShown
        function still(name) { return "file://" + stillsDir + "/" + name }
        function test_shots() {
            for (const dark of [true, false]) {
                Theme.dark = dark
                const tag = dark ? "dark" : "light"
                Screens.palette(root.Window.window, Theme)
                const f = createTemporaryObject(editorComponent, root)
                waitForRendering(f)
                const p = f.project
                const id = p.importImageSequence(still("sequence/shot_0001.png"), 24, 1)
                verify(id >= 0)
                compare(p.importMedia([still("photo.dng"), still("drawing.svg"), still("halves.exr")]), 3)
                verify(p.addClip(id))
                verify(p.addClip(p.media.find((m) => m.name === "photo.dng").id))
                p.seek(0.35)
                wait(800)
                const dialog = findChild(f.editor, "imageSequenceDialog")
                dialog.openFor(still("sequence/shot_0004.png"))
                tryVerify(() => dialog.opened)
                findChild(dialog.contentItem, "imageSequenceRate").currentIndex = 1
                wait(500)
                const window = root.Window.window
                grabImage(window.contentItem).save(screensDir + "/image-sequence-" + tag + ".png")
                dialog.close()
                f.destroy(); wait(100)
            }
            Theme.dark = true
        }
    }
}
