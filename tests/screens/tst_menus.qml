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
        name: "ShotMenus"
        when: windowShown
        function mainTrack(p) { return p.tracks.find((t) => t.main) }
        function build() {
            const f = createTemporaryObject(editorComponent, root)
            waitForRendering(f)
            const p = f.project
            compare(p.importMedia(["file://" + samplesDir + "/test_av_1080p30.mp4", "file://" + stillsDir + "/disc.png"]), 2)
            verify(p.addClip(p.media[0].id, -1, 0))
            verify(p.addClip(p.media[0].id, -1, 2))
            const above = p.tracks[p.tracks.findIndex((t) => t.main) - 1]
            verify(p.addClip(p.media[1].id, above.id, 0))
            const disc = p.tracks.find((t) => t.id === above.id).clips[0].id
            verify(p.setClipVisual(disc, { scale: 0.45, x: 0.18, y: -0.1, rotation: -12 }))
            let panel = findChild(f.editor, "splitButton")
            while (panel && panel.pixelsPerSecond === undefined) panel = panel.parent
            panel.pixelsPerSecond = 160
            p.seek(1.2)
            return { f: f, p: p, disc: disc }
        }
        function test_shots() {
            for (const dark of [true, false]) {
                Theme.dark = dark
                const tag = dark ? "dark" : "light"
                Screens.palette(root.Window.window, Theme)
                // The handles on the selected picture.
                let b = build()
                b.p.selectedClip = b.disc
                tryVerify(() => findChild(b.f.editor, "transformOverlay").frame !== null)
                wait(600)
                waitForRendering(b.f)
                grabImage(b.f).save(screensDir + "/transform-" + tag + ".png")
                b.f.destroy(); wait(50)
                // A clip's right-click menu.
                b = build()
                const clip = mainTrack(b.p).clips[0]
                const item = findChild(b.f.editor, "clip:" + clip.id)
                waitForPolish(item, 1000)
                mouseClick(item, 60, item.height / 2, Qt.RightButton)
                tryVerify(() => findChild(b.f.editor, "clipMenu").opened)
                wait(500)
                waitForRendering(b.f)
                grabImage(b.f).save(screensDir + "/clip-menu-" + tag + ".png")
                findChild(b.f.editor, "clipMenu").close()
                // A track header's menu.
                const header = findChild(b.f.editor, "trackHeader:Video 1")
                mouseClick(header, 40, header.height / 2, Qt.RightButton)
                tryVerify(() => findChild(b.f.editor, "trackMenu").opened)
                wait(400)
                waitForRendering(b.f)
                grabImage(b.f).save(screensDir + "/track-menu-" + tag + ".png")
                findChild(b.f.editor, "trackMenu").close()
                b.f.destroy(); wait(50)
            }
            Theme.dark = true
        }
    }
}
