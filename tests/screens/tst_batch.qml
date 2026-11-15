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
        name: "ShotBatch"
        when: windowShown
        function test_shots() {
            const av = "file://" + samplesDir + "/test_av_1080p30.mp4"
            for (const dark of [true, false]) {
                Theme.dark = dark
                const tag = dark ? "dark" : "light"
                Screens.palette(root.Window.window, Theme)
                const f = createTemporaryObject(editorComponent, root)
                waitForRendering(f)
                const p = f.project
                for (let i = 0; i < 3; ++i)
                    verify(p.importAndAppend(av))
                verify(p.importMedia(["file://" + analysisMediaDir + "/hum.wav"]) === 1)
                const audio = p.tracks.find((t) => t.kind === "audio")
                verify(p.addClip(p.media[p.media.length - 1].id, audio.id, 0))
                const ids = []
                for (const t of p.tracks) for (const c of t.clips) ids.push(c.id)
                p.selectedClip = ids[0]
                for (const id of ids.slice(1)) p.toggleClipSelection(id)
                p.seek(1)
                findChild(f.editor, "inspectorTab:Audio").clicked()
                wait(300)
                findChild(f.editor, "batchGainAmount").value = -4.5
                findChild(f.editor, "transformOverlay").opacity = 0
                mouseMove(root, 1, 1)
                wait(900)
                waitForRendering(f)
                grabImage(f).save(screensDir + "/batch-gain-" + tag + ".png")
                f.destroy(); wait(50)
            }
            Theme.dark = true
        }
    }
}
