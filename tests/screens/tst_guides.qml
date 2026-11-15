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
        name: "ShotGuides"
        when: windowShown
        function mainTrack(p) { return p.tracks.find((t) => t.main) }
        function scrollTo(f, name) {
            const section = findChild(f.editor, name)
            let flick = section.parent
            while (flick && flick.contentY === undefined)
                flick = flick.parent
            if (flick)
                flick.contentY = Math.max(0, Math.min(section.mapToItem(flick.contentItem, 0, 0).y - 20, flick.contentHeight - flick.height))
        }
        function test_shots() {
            const av = "file://" + samplesDir + "/test_av_1080p30.mp4"
            for (const dark of [true, false]) {
                Theme.dark = dark
                const tag = dark ? "dark" : "light"
                Screens.palette(root.Window.window, Theme)
                let f = createTemporaryObject(editorComponent, root)
                waitForRendering(f)
                let p = f.project
                verify(p.importAndAppend(av))
                p.seek(1)
                // An effect only inside a mask: black and white outside an ellipse.
                verify(p.addClipEffect(p.selectedClip, "tve.color.black_white"))
                verify(p.addMask(p.selectedClip, "ellipse"))
                verify(p.setMask(p.selectedClip, 0, { x: 0.5, y: 0.5, width: 0.45, height: 0.6, feather: 0.06, invert: true, target: "effects" }))
                const guides = findChild(f.editor, "guidesOverlay")
                guides.thirds = true
                guides.safeAreas = true
                guides.rulers = true
                findChild(f.editor, "inspectorTab:Video").clicked()
                wait(300)
                scrollTo(f, "masksSection")
                mouseMove(root, 1, 1)
                wait(900)
                waitForRendering(f)
                grabImage(f).save(screensDir + "/effect-mask-" + tag + ".png")
                f.destroy(); wait(50)

                // Motion blur: a picture flying across.
                f = createTemporaryObject(editorComponent, root)
                waitForRendering(f)
                p = f.project
                compare(p.importMedia([av, "file://" + stillsDir + "/disc.png"]), 2)
                verify(p.addClip(p.media[0].id, mainTrack(p).id, 0))
                const above = p.tracks[p.tracks.findIndex((t) => t.main) - 1]
                verify(p.addClip(p.media[1].id, above.id, 0))
                const disc = p.selectedClip
                verify(p.trimClip(disc, false, 1))
                verify(p.panZoom(disc, { fromScale: 0.4, fromX: -0.9, fromY: -0.2, toScale: 0.4, toX: 0.9, toY: 0.2 }))
                verify(p.setClipMotionBlur(disc, 1))
                p.seek(0.45)
                findChild(f.editor, "inspectorTab:Video").clicked()
                wait(300)
                scrollTo(f, "clipAnimationSection")
                findChild(f.editor, "transformOverlay").opacity = 0
                mouseMove(root, 1, 1)
                wait(900)
                waitForRendering(f)
                grabImage(f).save(screensDir + "/motion-blur-" + tag + ".png")
                f.destroy(); wait(50)
            }
            Theme.dark = true
        }
    }
}
