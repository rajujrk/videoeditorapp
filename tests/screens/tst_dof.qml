import QtQuick
import QtTest
import TechieVideoEditor.Theme
import TechieVideoEditor.Player
import TechieVideoEditor.Shell
import "screens.js" as Screens

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
        name: "ShotDof"
        when: windowShown
        function scrollTo(item) {
            for (let a = item.parent; a; a = a.parent) {
                if (a.contentY === undefined || a.contentHeight === undefined)
                    continue
                const top = item.mapToItem(a.contentItem, 0, 0).y
                a.contentY = Math.max(0, Math.min(top - 12, a.contentHeight - a.height))
                return
            }
        }
        function test_shots() {
            const window = root.Window.window
            const tags = (typeof shotTags !== "undefined") ? shotTags : ["dark", "light"]
            for (const dark of [true, false]) {
                Theme.dark = dark
                const tag = dark ? "dark" : "light"
                Screens.palette(root.Window.window, Theme)
                const f = createTemporaryObject(editorComponent, root)
                waitForRendering(f)
                const p = f.project
                verify(p.importMedia(["file://" + screensMediaDir + "/portrait-wide.mp4", "file://" + screensMediaDir + "/night-street.mp4"]) === 2)
                verify(p.addClip(p.media[0].id))
                const id = Number(p.tracks.find((t) => t.main).clips[0].id)
                p.selectedClip = id
                p.seek(1)
                findChild(f.editor, "inspectorTab:Analyze").clicked()
                if (!p.depthModel.downloaded) {
                    verify(p.downloadSpeechModel(p.depthModel.id))
                    tryVerify(() => p.depthModel.downloaded === true, 60000)
                }
                wait(500)
                grabImage(window.contentItem).save(tempDir + "/tve-shot-dof-before-" + tag + ".png")
                verify(p.setDepthOfField(id, { enabled: true, blur: 1 }))
                wait(800)
                scrollTo(findChild(f.editor, "depthSection"))
                wait(700)
                grabImage(window.contentItem).save(screensDir + "/depth-of-field-" + tag + ".png")
                verify(p.setDepthOfField(id, { enabled: false }))
                // Night view on the people clip, darkened first.
                verify(p.addClip(p.media[1].id))
                const id2 = Number(p.tracks.find((t) => t.main).clips[1].id)
                p.selectedClip = id2
                p.seek(4.0)
                wait(600)
                grabImage(window.contentItem).save(tempDir + "/tve-shot-night-before-" + tag + ".png")
                verify(p.setNightView(id2, { enabled: true, strength: 0.8, denoise: 0.6 }))
                wait(800)
                scrollTo(findChild(f.editor, "nightSection"))
                wait(700)
                grabImage(window.contentItem).save(screensDir + "/night-view-" + tag + ".png")
                f.destroy(); wait(100)
            }
            Theme.dark = true
        }
    }
}
