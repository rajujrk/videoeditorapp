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
        name: "ShotBeauty"
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
                verify(p.importMedia(["file://" + samplesDir + "/people.mp4"]) === 1)
                verify(p.addClip(p.media[0].id))
                const id = p.tracks.find((t) => t.main).clips[0].id
                p.selectedClip = id
                p.seek(1.5)
                // The beauty filter, in the Analyze tab.
                mouseClick(findChild(f.editor, "inspectorTab:Analyze"))
                verify(p.setFaceBeauty(id, { enabled: true, smooth: 0.8, brighten: 0.4, teeth: 0.6 }))
                wait(500)
                scrollTo(findChild(f.editor, "beautySection"))
                wait(700)
                grabImage(window.contentItem).save(screensDir + "/beauty-" + tag + ".png")
                verify(p.setFaceBeauty(id, { enabled: false }))
                // Skin protected from a cold, grey grade, in the Color tab.
                mouseClick(findChild(f.editor, "inspectorTab:Color"))
                verify(p.setClipColor(id, { saturation: -0.85, temperature: -0.4, protectSkin: 1 }))
                wait(500)
                scrollTo(findChild(f.editor, "grade:protectSkin"))
                wait(700)
                grabImage(window.contentItem).save(screensDir + "/skin-protect-" + tag + ".png")
                f.destroy(); wait(100)
            }
            Theme.dark = true
        }
    }
}
