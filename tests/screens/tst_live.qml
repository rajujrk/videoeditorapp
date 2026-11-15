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
    SignalSpy { id: finished; signalName: "analysisFinished" }
    TestCase {
        name: "ShotLive"
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
            for (const dark of [true, false]) {
                Theme.dark = dark
                const tag = dark ? "dark" : "light"
                Screens.palette(root.Window.window, Theme)
                const f = createTemporaryObject(editorComponent, root)
                waitForRendering(f)
                const p = f.project
                finished.clear(); finished.target = p
                verify(p.importMedia(["file://" + samplesDir + "/people.mp4"]) === 1)
                verify(p.addClip(p.media[0].id))
                const id = p.tracks.find((t) => t.main).clips[0].id
                p.selectedClip = id
                p.seek(1.5)
                mouseClick(findChild(f.editor, "inspectorTab:Analyze"))
                // The background blurred behind the astronaut.
                verify(p.setPortraitCutout(id, { enabled: true, blur: 0.7 }))
                wait(500)
                scrollTo(findChild(f.editor, "cutoutSection"))
                wait(700)
                grabImage(window.contentItem).save(screensDir + "/background-blur-" + tag + ".png")
                verify(p.setPortraitCutout(id, { enabled: false }))
                // A crown on the head, following the face.
                verify(p.findFaces(id))
                tryVerify(() => finished.count >= 1, 30000)
                const crown = p.stickers.find((s) => s.title.toLowerCase().indexOf("crown") >= 0) || p.stickers[0]
                verify(p.stickToFaces(id, crown.id, "head"))
                findChild(f.editor, "faceSticker").currentIndex = p.stickers.findIndex((s) => s.id === crown.id)
                p.selectedClip = id
                p.seek(1.5)
                wait(500)
                scrollTo(findChild(f.editor, "faceChip:1"))
                wait(700)
                grabImage(window.contentItem).save(screensDir + "/face-stickers-" + tag + ".png")
                // The recorder with the camera's background blurred.
                mouseClick(findChild(f.editor, "screenRecordButton"))
                const panel = findChild(f.editor, "recordPanel")
                tryVerify(() => panel.opened)
                const camera = findChild(panel.contentItem, "cameraChoice")
                camera.currentIndex = 1
                camera.activated(1)
                panel.cameraBackground = "blur"
                wait(600)
                grabImage(window.contentItem).save(screensDir + "/recorder-background-" + tag + ".png")
                panel.close()
                f.destroy(); wait(100)
            }
            Theme.dark = true
        }
    }
}
