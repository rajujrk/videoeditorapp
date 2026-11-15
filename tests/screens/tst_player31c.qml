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
    // A window's grab laid over the editor's, where the window is.
    Component {
        id: layered
        Item {
            property alias under: below.source
            property alias over: above.source
            property alias overX: above.x
            property alias overY: above.y
            property alias overWidth: above.width
            property alias overHeight: above.height
            width: 1470
            height: 790
            z: 100
            Image { id: below; anchors.fill: parent; cache: false }
            Image { id: above; cache: false }
        }
    }
    readonly property string scratch: "file://" + screensMediaDir + "/"
    TestCase {
        name: "ShotPlayer31c"
        when: windowShown
        function inspectorTo(f, name) {
            const section = findChild(f.editor, name)
            for (let a = section.parent; a; a = a.parent) {
                if (a.contentY === undefined || a.contentHeight === undefined)
                    continue
                a.contentY = Math.max(0, Math.min(section.mapToItem(a.contentItem, 0, 0).y - 12, a.contentHeight - a.height))
                break
            }
        }
        function test_shots() {
            const window = root.Window.window
            for (const dark of [true, false]) {
                Theme.dark = dark
                EditingPreferences.reset()
                const tag = dark ? "dark" : "light"
                Screens.palette(root.Window.window, Theme)

                // Before and after a warm, punchy grade.
                let f = createTemporaryObject(editorComponent, root)
                waitForRendering(f)
                let p = f.project
                verify(p.importAndAppend(root.scratch + "night-street.mp4"))
                const main = () => p.tracks.find((t) => t.main)
                p.selectedClip = Number(main().clips[0].id)
                verify(p.setClipColor(p.selectedClip, { temperature: 0.55, contrast: 0.3, saturation: 0.45 }))
                p.seek(2)
                findChild(f.editor, "inspectorTab:Color").clicked()
                p.selectedClip = 0
                const player = findChild(f.editor, "player")
                player.compareGrade = true
                player.compareSplit = 0.46
                wait(1500)
                grabImage(window.contentItem).save(screensDir + "/grade-compare-" + tag + ".png")
                player.compareGrade = false

                // The transparency grid: a sticker-like disc and a title, nothing behind them.
                f.destroy(); wait(150)
                f = createTemporaryObject(editorComponent, root)
                waitForRendering(f)
                p = f.project
                verify(p.importMedia(["file://" + stillsDir + "/disc.png"]) === 1)
                verify(p.addClip(p.media[0].id, main().id, 0))
                p.selectedClip = Number(main().clips[0].id)
                verify(p.setClipVisual(p.selectedClip, { scale: 0.75, x: -0.18 }))
                p.seek(0.5)
                p.selectedClip = 0
                EditingPreferences.transparencyGrid = true
                wait(1500)
                grabImage(window.contentItem).save(screensDir + "/transparency-grid-" + tag + ".png")
                EditingPreferences.transparencyGrid = false

                // Social app safe areas on a vertical canvas.
                f.destroy(); wait(150)
                f = createTemporaryObject(editorComponent, root)
                waitForRendering(f)
                p = f.project
                verify(p.setCanvas(1080, 1920, 30))
                verify(p.importAndAppend(root.scratch + "portrait-wide.mp4"))
                p.selectedClip = Number(main().clips[0].id)
                verify(p.setClipVisual(p.selectedClip, { scale: 3.2 }))
                p.seek(1)
                p.selectedClip = 0
                findChild(f.editor, "guidesOverlay").social = "feed"
                wait(1500)
                grabImage(window.contentItem).save(screensDir + "/social-safe-areas-" + tag + ".png")

                // The recorder: counting down, and paused.
                f.destroy(); wait(150)
                f = createTemporaryObject(editorComponent, root)
                waitForRendering(f)
                p = f.project
                verify(p.importAndAppend(root.scratch + "night-street.mp4"))
                p.seek(1)
                findChild(f.editor, "screenRecordButton").clicked()
                const panel = findChild(f.editor, "recordPanel")
                tryVerify(() => panel.opened)
                EditingPreferences.recordCountdown = 3
                panel.begin()
                countdownTimerStop(panel)
                wait(900)
                const under = tempDir + "/tve-shot-countdown-under-" + tag + ".png"
                grabImage(window.contentItem).save(under)
                const cd = panel.countdownWindow
                const over = tempDir + "/tve-shot-countdown-over-" + tag + ".png"
                grabImage(cd.contentItem).save(over)
                let stack = createTemporaryObject(layered, root, { under: "file://" + under, over: "file://" + over,
                                                                   overX: 1090 - cd.width / 2, overY: 300 - cd.height / 2,
                                                                   overWidth: cd.width, overHeight: cd.height })
                wait(500)
                grabImage(stack).save(screensDir + "/record-countdown-" + tag + ".png")
                stack.destroy()
                panel.cancelCountdown()
                EditingPreferences.recordCountdown = 0

                panel.begin()
                tryVerify(() => p.screenRecording)
                wait(2300)
                verify(p.pauseScreenRecording())
                wait(900)
                const pausedUnder = tempDir + "/tve-shot-paused-under-" + tag + ".png"
                grabImage(window.contentItem).save(pausedUnder)
                const frame = panel.recordingFrame
                const pausedOver = tempDir + "/tve-shot-paused-over-" + tag + ".png"
                grabImage(frame.contentItem).save(pausedOver)
                stack = createTemporaryObject(layered, root, { under: "file://" + pausedUnder, over: "file://" + pausedOver,
                                                               overX: frame.x, overY: frame.y,
                                                               overWidth: frame.width, overHeight: frame.height })
                wait(500)
                grabImage(stack).save(screensDir + "/record-paused-" + tag + ".png")
                stack.destroy()
                verify(p.stopScreenRecording())
                f.destroy(); wait(150)
            }
            EditingPreferences.reset()
            Theme.dark = true
        }
        function countdownTimerStop(panel) {
            // (held on its first second for the picture)
            for (const child of panel.children || [])
                if (child.objectName === "countdownTimer")
                    child.stop()
            const timer = findChild(panel, "countdownTimer")
            if (timer)
                timer.stop()
        }
    }
}
