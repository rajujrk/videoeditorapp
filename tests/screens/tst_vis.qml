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
    SignalSpy { id: finished; signalName: "analysisFinished" }
    TestCase {
        name: "ShotVis"
        when: windowShown
        function scrollTo(f, name) {
            const section = findChild(f.editor, name)
            let flick = section.parent
            while (flick && flick.contentY === undefined)
                flick = flick.parent
            if (flick)
                flick.contentY = Math.max(0, Math.min(section.mapToItem(flick.contentItem, 0, 0).y - 20, flick.contentHeight - flick.height))
        }
        function test_shots() {
            for (const dark of [true, false]) {
                Theme.dark = dark
                const tag = dark ? "dark" : "light"
                Screens.palette(root.Window.window, Theme)
                const f = createTemporaryObject(editorComponent, root)
                waitForRendering(f)
                const p = f.project
                finished.clear(); finished.target = p
                verify(p.addColorClip({ kind: "radial", from: [0.12, 0.08, 0.22, 1], to: [0.03, 0.03, 0.08, 1] }) > 0)
                compare(p.importMedia(["file://" + samplesDir + "/speech_en.wav"]), 1)
                const audio = p.tracks.find((t) => t.kind === "audio")
                verify(p.addClip(p.media[1].id, audio.id, 0))
                p.seek(1.5)
                verify(p.addVisualiser({ text: "Now playing", size: 0.06, bold: true,
                                         visualiser: { style: "bars", color: [0.3, 0.8, 1, 1], peak: [1, 0.35, 0.75, 1], size: 0.45 } }))
                tryVerify(() => finished.count === 1, 20000)
                compare(finished.signalArguments[0][0], true)
                p.seek(1.5)
                findChild(f.editor, "inspectorTab:Video").clicked()
                wait(300)
                scrollTo(f, "titleVisualiserSection")
                findChild(f.editor, "transformOverlay").opacity = 0
                mouseMove(root, 1, 1)
                wait(900)
                waitForRendering(f)
                grabImage(f).save(screensDir + "/visualiser-" + tag + ".png")
                if (dark) {
                    for (const style of ["mirror", "wave", "ring"]) {
                        verify(p.setClipText(p.selectedClip, { visualiser: { style: style } }))
                        wait(500)
                        grabImage(f).save(tempDir + "/tve-shot-vis-" + style + ".png")
                    }
                }
                f.destroy(); wait(50)
            }
            Theme.dark = true
        }
    }
}
