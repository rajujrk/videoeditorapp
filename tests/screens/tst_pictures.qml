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
        name: "ShotPictures"
        when: windowShown
        function mainTrack(p) { return p.tracks.find((t) => t.main) }
        function test_shots() {
            const av = "file://" + samplesDir + "/test_av_1080p30.mp4"
            for (const dark of [true, false]) {
                Theme.dark = dark
                const tag = dark ? "dark" : "light"
                Screens.palette(root.Window.window, Theme)
                let f = createTemporaryObject(editorComponent, root)
                waitForRendering(f)
                let p = f.project
                // A gradient background, a video pinned in perspective over it.
                verify(p.addColorClip({ kind: "linear", from: [0.1, 0.12, 0.3, 1], to: [0.55, 0.2, 0.6, 1], angle: 30 }) > 0)
                const colour = p.selectedClip
                compare(p.importMedia([av]), 1)
                const above = p.tracks[p.tracks.findIndex((t) => t.main) - 1]
                verify(p.addClip(p.media.find((m) => !m.colourFill).id, above.id, 0))
                const video = p.selectedClip
                p.selectedClip = video
                verify(p.setClipVisual(video, { scale: 0.6, rotation: 0, anchorX: -0.5, anchorY: -0.5,
                                                pinTopLeftX: 0.05, pinTopLeftY: 0.02, pinTopRightX: -0.02, pinTopRightY: 0.1,
                                                pinBottomRightX: -0.04, pinBottomRightY: -0.06, pinBottomLeftX: 0.08, pinBottomLeftY: 0.0 }))
                verify(p.animateClip(video, "fade-in", 0.4))
                p.pinningCorners = true
                p.seek(1)
                findChild(f.editor, "inspectorTab:Video").clicked()
                wait(300)
                const pinSection = findChild(f.editor, "cornerPinSection")
                let pf = pinSection.parent
                while (pf && pf.contentY === undefined)
                    pf = pf.parent
                if (pf)
                    pf.contentY = Math.max(0, Math.min(pinSection.mapToItem(pf.contentItem, 0, 0).y - 20, pf.contentHeight - pf.height))
                mouseMove(root, 1, 1)
                wait(900)
                waitForRendering(f)
                grabImage(f).save(screensDir + "/corner-pin-" + tag + ".png")
                p.pinningCorners = false

                // The colour clip's own settings.
                p.selectedClip = colour
                p.seek(1)
                wait(600)
                const section = findChild(f.editor, "colourClipSection")
                tryVerify(() => section.visible)
                // Scroll the inspector to it.
                let flick = section.parent
                while (flick && flick.contentY === undefined)
                    flick = flick.parent
                if (flick)
                    flick.contentY = Math.max(0, Math.min(section.mapToItem(flick.contentItem, 0, 0).y - 20,
                                                          flick.contentHeight - flick.height))
                mouseMove(root, 1, 1)
                wait(700)
                waitForRendering(f)
                grabImage(f).save(screensDir + "/colour-clip-" + tag + ".png")
                f.destroy(); wait(50)
            }
            Theme.dark = true
        }
    }
}
