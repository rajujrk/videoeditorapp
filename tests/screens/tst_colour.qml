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
        name: "ShotColour"
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
                const f = createTemporaryObject(editorComponent, root)
                waitForRendering(f)
                const p = f.project
                verify(p.importAndAppend(av))
                verify(p.importAndAppend(av))
                p.selectedClip = mainTrack(p).clips[0].id
                p.seek(1)
                // A warm, dim look on the clip, then the HDR wheels.
                verify(p.setClipColor(p.selectedClip, { exposure: -0.6, temperature: 0.5,
                                                        shadow: { x: -0.4, y: -0.5, master: 0 },
                                                        highlight: { x: 0.5, y: 0.2, master: 0.1 },
                                                        specular: { x: 0, y: 0, master: -0.3 } }))
                findChild(f.editor, "inspectorTab:Color").clicked()
                wait(300)
                scrollTo(f, "hdrWheelsSection")
                findChild(f.editor, "transformOverlay").opacity = 0
                mouseMove(root, 1, 1)
                wait(900)
                waitForRendering(f)
                grabImage(f).save(screensDir + "/hdr-wheels-" + tag + ".png")
                // Auto colour and match, and lens correction on the Video tab.
                scrollTo(f, "autoColourSection")
                wait(400)
                waitForRendering(f)
                grabImage(f).save(screensDir + "/auto-colour-" + tag + ".png")
                f.destroy(); wait(50)
            }
            Theme.dark = true
        }
    }
}
