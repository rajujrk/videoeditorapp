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
            ProjectController { id: controller; view: screen.player }
            EditorScreen { id: screen; anchors.fill: parent; project: controller }
        }
    }
    TestCase {
        name: "ShotTools"
        when: windowShown
        function mainTrack(p) { return p.tracks.find((t) => t.main) }
        function test_shots() {
            for (const dark of [true, false]) {
                Theme.dark = dark
                const tag = dark ? "dark" : "light"
                Screens.palette(root.Window.window, Theme)
                const f = createTemporaryObject(editorComponent, root)
                waitForRendering(f)
                const p = f.project
                for (let i = 0; i < 3; ++i)
                    verify(p.importAndAppend("file://" + samplesDir + "/test_av_1080p30.mp4"))
                const panel = findChild(f.editor, "timelineMenus").timeline
                panel.pixelsPerSecond = 160
                p.setMarkIn(1.5)
                p.setMarkOut(4)
                p.seek(2.4)
                findChild(f.editor, "shortcut:blade").activated()
                const clip = findChild(f.editor, "clip:" + mainTrack(p).clips[1].id)
                waitForPolish(clip, 1000)
                mouseMove(clip, 140, clip.height / 2)
                wait(500)
                waitForRendering(f)
                grabImage(f).save(screensDir + "/in-out-blade-" + tag + ".png")
                findChild(f.editor, "shortcut:selectTool").activated()
                // Paste attributes.
                verify(p.copyAttributes(mainTrack(p).clips[0].id))
                p.selectedClip = mainTrack(p).clips[2].id
                panel.pasteAttributes()
                tryVerify(() => findChild(f.editor, "pasteAttributes").opened)
                wait(400)
                waitForRendering(f)
                grabImage(f).save(screensDir + "/paste-attributes-" + tag + ".png")
                findChild(f.editor, "pasteAttributes").close()
                f.destroy(); wait(50)
            }
            Theme.dark = true
        }
    }
}
