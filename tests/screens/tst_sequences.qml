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
        name: "ShotSequences"
        when: windowShown
        function mainTrack(p) { return p.tracks.find((t) => t.main) }
        function trackAbove(p) { return p.tracks[p.tracks.findIndex((t) => t.main) - 1] }
        function test_shots() {
            const av = "file://" + samplesDir + "/test_av_1080p30.mp4"
            for (const dark of [true, false]) {
                Theme.dark = dark
                const tag = dark ? "dark" : "light"
                Screens.palette(root.Window.window, Theme)
                let f = createTemporaryObject(editorComponent, root)
                waitForRendering(f)
                let p = f.project
                // Source monitor, In and Out marked.
                for (let i = 0; i < 3; ++i)
                    verify(p.importAndAppend(av))
                findChild(f.editor, "timelineMenus").timeline.pixelsPerSecond = 120
                p.seek(2.5)
                findChild(f.editor, "mediaMenu").media = p.media[0]
                findChild(f.editor, "mediaMenu:source").triggered()
                const source = findChild(f.editor, "sourceMonitor")
                tryVerify(() => source.ready)
                source.controller.setMarkIn(0.4)
                source.controller.setMarkOut(1.6)
                source.controller.seek(1.0)
                mouseMove(root, 1, 1); wait(700)
                waitForRendering(f)
                console.log("SHOTDIR " + tempDir); grabImage(f).save(screensDir + "/source-monitor-" + tag + ".png")
                findChild(f.editor, "playerTab:timeline").clicked()

                // A compound clip open in its tab.
                const clips = mainTrack(p).clips
                p.selectedClip = clips[0].id
                p.toggleClipSelection(clips[1].id)
                const compound = p.createCompound()
                verify(p.openCompoundClip(compound))
                p.selectedClip = mainTrack(p).clips[1].id
                mouseMove(root, 1, 1); wait(700)
                waitForRendering(f)
                grabImage(f).save(screensDir + "/compound-tab-" + tag + ".png")
                p.closeCompoundClip(0)
                f.destroy(); wait(50)

                // Multicam: two cameras, cut between.
                f = createTemporaryObject(editorComponent, root)
                waitForRendering(f)
                p = f.project
                compare(p.importMedia([av, "file://" + stillsDir + "/halves.png"]), 2)
                verify(p.addClip(p.media[0].id, mainTrack(p).id, 0))
                verify(p.addClip(p.media[1].id, trackAbove(p).id, 0))
                p.selectedClip = mainTrack(p).clips[0].id
                p.toggleClipSelection(trackAbove(p).clips[0].id)
                verify(p.makeMulticam() > 0)
                findChild(f.editor, "timelineMenus").timeline.pixelsPerSecond = 160
                p.seek(1.2)
                verify(p.cutToAngle(1))
                p.seek(1.7)
                verify(p.cutToAngle(0))
                p.selectedClip = mainTrack(p).clips[1].id
                p.seek(1.45)
                findChild(f.editor, "inspectorTab:Video").clicked()
                mouseMove(root, 1, 1); wait(900)
                waitForRendering(f)
                grabImage(f).save(screensDir + "/multicam-" + tag + ".png")
                f.destroy(); wait(50)
            }
            Theme.dark = true
        }
    }
}
