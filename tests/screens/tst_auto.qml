import QtQuick
import QtTest
import TechieVideoEditor.Theme
import TechieVideoEditor.Player
import TechieVideoEditor.Shell
import "screens.js" as Screens

// Templates and edits made for you: browsing templates, a template filled in (template mode),
// an instant video from pictures and videos, and shorts cut from a long video.
Item {
    id: root
    width: 1470
    height: 790
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
        name: "ScreensAuto"
        when: windowShown
        readonly property string media: "file://" + screensMediaDir + "/"
        readonly property string analysis: "file://" + analysisMediaDir + "/"

        function editor() {
            const f = createTemporaryObject(editorComponent, root)
            waitForRendering(f)
            f.project.cloud.removeKey("anthropic")
            return f
        }
        function shot(name, dark, settle) {
            mouseMove(root, root.width - 4, root.height - 4)
            wait(settle === undefined ? 1500 : settle)
            grabImage(root.Window.window.contentItem).save(Screens.file(screensDir, name, dark))
        }
        function settled(parent, name) {
            tryVerify(() => { const i = findChild(parent, name); return i !== null && i.visible }, 5000, name)
            return findChild(parent, name)
        }
        function click(parent, name) {
            const item = settled(parent, name)
            mouseMove(item)
            mouseClick(item)
        }
        function mediaNamed(p, name) { return p.media.find((m) => m.name === name) }
        function finished(p) {
            let result = null
            p.autoEditFinished.connect((kind, ok, message) => result = { kind: kind, ok: ok, message: message })
            return () => result
        }
        function templatesTab(f) {
            settled(f.editor, "libraryTab:Templates").clicked()
            return settled(f.editor, "templatesPage")
        }

        function test_scenes() {
            for (const dark of [true, false]) {
                Theme.dark = dark
                EditingPreferences.reset()
                Screens.palette(root.Window.window, Theme)

                // The Templates tab: make it for me, and templates to browse.
                let f = editor()
                let p = f.project
                verify(p.importAndAppend(media + "night-street.mp4"))
                verify(p.importAndAppend(media + "portrait-wide.mp4"))
                p.seek(1)
                templatesTab(f)
                shot("templates", dark)
                f.destroy(); wait(200)

                // A template filled in: three shots chosen, the title changed.
                f = editor()
                p = f.project
                verify(p.newFromTemplate("tve.template.postcard"), "" + p.lastError)
                compare(p.importMedia([media + "night-street.mp4", media + "portrait-wide.mp4", media + "photo.png"]), 3)
                verify(p.fillTemplateSlot(0, mediaNamed(p, "night-street.mp4").id))
                verify(p.fillTemplateSlot(1, mediaNamed(p, "portrait-wide.mp4").id))
                verify(p.fillTemplateSlot(2, mediaNamed(p, "photo.png").id))
                verify(p.setTemplateText(0, "AUTUMN IN KYOTO"))
                const page = templatesTab(f)
                p.seek(1.2)
                shot("template-mode", dark)
                f.destroy(); wait(200)

                // An instant video: the panel, with pictures and videos chosen and a style.
                f = editor()
                p = f.project
                compare(p.importMedia([media + "night-street.mp4", media + "portrait-wide.mp4", media + "photo.png",
                                       analysis + "scenes.mov"]), 4)
                settled(templatesTab(f), "makeFor:instant").clicked()
                const panel = findChild(f.editor, "autoEditPanel")
                tryCompare(panel, "opened", true)
                click(panel.contentItem, "autoStyle:cinematic")
                click(panel.contentItem, "autoSeconds:30")
                settled(panel.contentItem, "autoTitle").text = "A weekend away"
                shot("auto-edit", dark)
                panel.close()
                f.destroy(); wait(200)

                // Shorts cut from a long video, in a tall frame, each a slot to change.
                f = editor()
                p = f.project
                compare(p.importMedia([analysis + "long.mov"]), 1)
                const made = finished(p)
                verify(p.makeShorts({ media: mediaNamed(p, "long.mov").id, count: 3, seconds: 15 }), "" + p.lastError)
                tryVerify(() => made() !== null, 90000)
                verify(made().ok, made().message)
                templatesTab(f)
                p.seek(3)
                shot("shorts", dark)
                f.destroy(); wait(200)
            }
            EditingPreferences.reset()
            Theme.dark = true
        }
    }
}
