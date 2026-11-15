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
        name: "ShotExportMore"
        when: windowShown
        function test_shots() {
            for (const dark of [true, false]) {
                Theme.dark = dark
                const tag = dark ? "dark" : "light"
                Screens.palette(root.Window.window, Theme)
                const f = createTemporaryObject(editorComponent, root)
                waitForRendering(f)
                const p = f.project
                verify(p.importAndAppend("file://" + analysisMediaDir + "/long.mov"))
                p.seek(12)
                wait(500)
                const dialog = findChild(f.editor, "exportDialog")
                dialog.open()
                tryVerify(() => dialog.opened)
                dialog.file = "file:///Users/me/Movies/Holiday.mp4"
                dialog.quality = "size"
                findChild(dialog.contentItem, "exportTargetSize").text = "8"
                dialog.alsoFormats = ["mp4-hevc", "gif", "mp3"]
                wait(500)
                const window = root.Window.window
                grabImage(window.contentItem).save(screensDir + "/export-size-" + tag + ".png")
                const formats = findChild(dialog.contentItem, "exportFormat")
                formats.currentIndex = p.exportFormats.findIndex((x) => x.id === "live-photo")
                dialog.formatChosen()
                findChild(dialog.contentItem, "exportAddToPhotos").checked = true
                wait(500)
                grabImage(window.contentItem).save(screensDir + "/export-live-photo-" + tag + ".png")
                dialog.close()
                f.destroy(); wait(100)
            }
            Theme.dark = true
        }
    }
}
