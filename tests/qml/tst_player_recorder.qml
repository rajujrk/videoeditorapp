import QtQuick
import QtTest
import TechieVideoEditor.Theme
import TechieVideoEditor.Player
import TechieVideoEditor.Shell

// The player and the recorder (31c): before and after a grade, the transparency grid, social
// app safe areas; pausing and resuming a recording, and the countdown before it.
Item {
    id: root
    width: 1400
    height: 860

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
        id: testCase
        name: "PlayerRecorder"
        when: windowShown

        function init() { EditingPreferences.reset() }
        function cleanup() { EditingPreferences.reset() }

        function still(name) { return "file://" + stillsDir + "/" + name }
        function createEditor() {
            const fixture = createTemporaryObject(editorComponent, root)
            verify(fixture)
            waitForRendering(fixture)
            return fixture
        }
        function mainTrack(p) { return p.tracks.find((t) => t.main) }
        // Red | blue halves on the main track, selected, the playhead on it.
        function halves() {
            const f = createEditor()
            const p = f.project
            compare(p.importMedia([still("halves.png")]), 1)
            verify(p.addClip(p.media[0].id, mainTrack(p).id, 0))
            p.selectedClip = mainTrack(p).clips[0].id
            p.seek(0.5)
            waitForRendering(f.editor)
            return f
        }
        function settled(parent, name) {
            let item = null
            let last = null
            tryVerify(() => {
                item = findChild(parent, name)
                if (!item || !item.visible)
                    return false
                const p = item.mapToItem(root, 0, 0)
                const stable = last !== null && p.x === last.x && p.y === last.y
                last = p
                return stable
            }, 3000, name + " never settled")
            for (let a = item; a && a !== root; a = a.parent)
                waitForPolish(a, 1000)
            return item
        }
        function click(parent, name) {
            const item = settled(parent, name)
            mouseMove(item)
            mouseClick(item)
        }
        // The player's colour at canvas fraction (u, v); the overlays named stay, the rest hidden.
        function canvasColor(f, u, v, keep) {
            const names = ["maskOverlay", "transformOverlay", "guidesOverlay", "compareOverlay"]
            const overlays = names.filter((n) => !(keep || []).includes(n)).map((n) => findChild(f.editor, n))
            overlays.forEach((o) => o.opacity = 0)
            const image = grabImage(root)
            overlays.forEach((o) => o.opacity = 1)
            const guides = findChild(f.editor, "guidesOverlay")
            const r = guides.canvasRect
            const scale = image.width / root.width
            const p = guides.mapToItem(root, r.x + u * r.width, r.y + v * r.height)
            return image.pixel(Math.round(p.x * scale), Math.round(p.y * scale))
        }
        function grey(c) { return Math.abs(c.r - c.g) < 0.08 && Math.abs(c.g - c.b) < 0.08 && c.r > 0.1 }
        function red(c) { return c.r > 0.6 && c.b < 0.3 }
        function blue(c) { return c.b > 0.6 && c.r < 0.3 }

        function test_before_and_after_a_grade() {
            const f = halves()
            const p = f.project
            const player = findChild(f.editor, "player")
            verify(p.setClipColor(p.selectedClip, { saturation: -1 }))
            tryVerify(() => grey(canvasColor(f, 0.25, 0.5)) && grey(canvasColor(f, 0.75, 0.5)), 5000, "the grade never showed")

            // Compared: the left half as it was (red), the right as graded (grey).
            const button = settled(f.editor, "compareButton")
            verify(button.enabled)
            verify(!button.checked)
            click(f.editor, "compareButton")
            verify(player.compareGrade)
            verify(button.checked)
            const overlay = findChild(f.editor, "compareOverlay")
            verify(overlay.visible)
            compare(player.compareSplit, 0.5)
            tryVerify(() => red(canvasColor(f, 0.25, 0.5)), 5000, "the left half was not shown before its grade")
            verify(grey(canvasColor(f, 0.75, 0.5)))
            verify(findChild(f.editor, "compareLabel:before").visible)
            verify(findChild(f.editor, "compareLabel:after").visible)
            // Undo history untouched: comparing is only a view.
            compare(p.undoText, "Change Color")

            // The divider dragged to 90 %: the blue half is now before its grade too.
            settled(f.editor, "compareHandle")
            const r = overlay.canvasRect
            const y = r.y + r.height / 2
            const toX = r.x + r.width * 0.9
            mousePress(overlay, overlay.lineX, y)
            mouseMove(overlay, (overlay.lineX + toX) / 2, y)
            mouseMove(overlay, toX, y)
            mouseRelease(overlay, toX, y)
            fuzzyCompare(player.compareSplit, 0.9, 0.02)
            tryVerify(() => blue(canvasColor(f, 0.75, 0.5)), 5000, "the blue half kept its grade")
            player.compareSplit = 0.98
            verify(!findChild(f.editor, "compareLabel:after").visible) // too narrow to name
            verify(findChild(f.editor, "compareLabel:before").visible)
            // A double click puts it back in the middle.
            mouseDoubleClickSequence(overlay, overlay.lineX, y)
            compare(player.compareSplit, 0.5)
            player.compareSplit = 7 // (kept within the picture)
            compare(player.compareSplit, 1)
            player.compareSplit = 0.5

            // Shift+B turns it off and on again.
            const shortcut = findChild(f.editor, "shortcut:compareGrade")
            verify(shortcut.enabled)
            compare(KeyMap.sequences.compareGrade[0], "Shift+B")
            shortcut.activated()
            verify(!player.compareGrade)
            verify(!overlay.visible)
            tryVerify(() => grey(canvasColor(f, 0.25, 0.5)), 5000, "still showing the picture before its grade")
            shortcut.activated()
            verify(player.compareGrade)
            tryVerify(() => red(canvasColor(f, 0.25, 0.5)), 5000)
            // Looking around a 360° picture: nothing to compare.
            player.lookAround = true
            verify(!player.compareGrade)
            verify(!button.enabled)
            player.lookAround = false
        }

        function test_transparency_grid() {
            const f = halves()
            const p = f.project
            const player = findChild(f.editor, "player")
            // Small in the middle: around it the canvas is empty.
            verify(p.setClipVisual(p.selectedClip, { scale: 0.5 }))
            tryVerify(() => red(canvasColor(f, 0.4, 0.5)), 5000)
            const corner = canvasColor(f, 0.05, 0.05)
            verify(corner.r < 0.05 && corner.g < 0.05 && corner.b < 0.05, "an empty canvas is black: " + corner)

            const button = settled(f.editor, "transparencyGridButton")
            verify(!button.checked)
            click(f.editor, "transparencyGridButton")
            verify(EditingPreferences.transparencyGrid) // remembered
            verify(button.checked)
            verify(player.transparencyGrid)
            // A light and a darker grey, a cell apart (8 points), where nothing covers the canvas.
            const greys = () => {
                const values = []
                for (let i = 0; i < 6; ++i) {
                    const c = canvasColor(f, 0.02 + i * 0.006, 0.05)
                    if (!grey(c) || c.r < 0.5)
                        return []
                    values.push(Math.round(c.r * 100))
                }
                return values
            }
            tryVerify(() => greys().length === 6, 5000, "no checkerboard where the canvas is empty")
            verify(Math.max(...greys()) - Math.min(...greys()) > 10, "the checkerboard has one grey only: " + greys())
            // The picture over it as it was.
            verify(red(canvasColor(f, 0.4, 0.5)))
            verify(blue(canvasColor(f, 0.6, 0.5)))

            // Off again: black.
            click(f.editor, "transparencyGridButton")
            verify(!player.transparencyGrid)
            tryVerify(() => { const c = canvasColor(f, 0.05, 0.05); return c.r < 0.05 && c.g < 0.05 }, 5000)
        }

        function test_social_app_safe_areas() {
            const f = halves()
            const p = f.project
            const guides = findChild(f.editor, "guidesOverlay")
            verify(!guides.visible)
            compare(guides.profiles.map((x) => x.id), ["feed", "stories", "live"])
            // A vertical canvas from the same menu.
            const vertical = findChild(f.editor, "guides:social:canvas")
            verify(vertical.enabled)
            vertical.triggered()
            compare(p.canvasWidth, 1080)
            compare(p.canvasHeight, 1920)
            compare(p.undoText, "Change Canvas")
            verify(!vertical.enabled)
            // The picture filling the canvas: red on the left, blue on the right.
            verify(p.setClipVisual(p.selectedClip, { scale: 3.3 }))
            tryVerify(() => red(canvasColor(f, 0.3, 0.05)) && blue(canvasColor(f, 0.92, 0.95)), 5000)

            const before = canvasColor(f, 0.3, 0.05, ["guidesOverlay"])
            findChild(f.editor, "guides:social:feed").triggered()
            compare(guides.social, "feed")
            verify(guides.visible)
            verify(findChild(f.editor, "guidesButton").checked)
            verify(findChild(f.editor, "guides:social:feed").checked)
            verify(!findChild(f.editor, "guides:social:none").checked)
            // The top bar shaded; the middle, clear, as it was.
            tryVerify(() => canvasColor(f, 0.3, 0.05, ["guidesOverlay"]).r < before.r - 0.2, 3000, "the top bar was not shaded")
            verify(red(canvasColor(f, 0.3, 0.5, ["guidesOverlay"])))
            // The buttons' column at the right, only beside the lower middle.
            const column = canvasColor(f, 0.92, 0.6, ["guidesOverlay"])
            const above = canvasColor(f, 0.92, 0.3, ["guidesOverlay"])
            verify(column.b < above.b - 0.2, "the buttons' column was not shaded")
            compare(guides.socialClear.y, 0.1)

            // Stories: no column; the reply bar at the bottom.
            findChild(f.editor, "guides:social:stories").triggered()
            compare(guides.social, "stories")
            tryVerify(() => blue(canvasColor(f, 0.92, 0.6, ["guidesOverlay"])), 3000)
            verify(canvasColor(f, 0.92, 0.95, ["guidesOverlay"]).b < canvasColor(f, 0.92, 0.6, ["guidesOverlay"]).b - 0.2)
            findChild(f.editor, "guides:social:live").triggered()
            compare(guides.socialClear.height, 0.5)
            findChild(f.editor, "guides:social:none").triggered()
            compare(guides.social, "")
            verify(!guides.visible)
        }

        function openRecorder(f) {
            click(f.editor, "screenRecordButton")
            const panel = findChild(f.editor, "recordPanel")
            tryVerify(() => panel.opened)
            compare(f.project.screenAccess, "granted")
            return panel
        }

        function test_pause_and_resume_a_recording() {
            const f = createEditor()
            const p = f.project
            const panel = openRecorder(f)
            verify(!findChild(panel.contentItem, "pauseScreenRecording").visible)
            verify(!p.pauseScreenRecording()) // nothing recording
            click(panel.contentItem, "startScreenRecording")
            verify(p.screenRecording)
            verify(!p.screenRecordingPaused)
            tryVerify(() => p.screenRecordingTime > 1.0, 4000)

            click(panel.contentItem, "pauseScreenRecording")
            verify(p.screenRecordingPaused)
            verify(!p.pauseScreenRecording()) // paused already
            const button = findChild(panel.contentItem, "pauseScreenRecording")
            compare(button.text, "Resume")
            const time = findChild(panel.contentItem, "screenRecordingTime")
            verify(time.text.indexOf("Paused") > 0, time.text)
            // The frame round the screen goes amber, and still.
            const frame = panel.recordingFrame
            verify(frame && frame.paused)
            verify(Qt.colorEqual(findChild(frame.contentItem, "recordingFrameBorder").border.color, Theme.color.warning))
            const atPause = p.screenRecordingTime
            wait(1500)
            fuzzyCompare(p.screenRecordingTime, atPause, 0.15) // the clock stands still

            click(panel.contentItem, "pauseScreenRecording")
            verify(!p.screenRecordingPaused)
            compare(button.text, "Pause")
            verify(!frame.paused)
            tryVerify(() => p.screenRecordingTime > atPause + 1.0, 4000)
            const recorded = p.screenRecordingTime
            click(panel.contentItem, "stopScreenRecording")
            verify(!p.screenRecording)
            verify(!p.screenRecordingPaused)
            compare(p.undoText, "Record Screen")
            // The take leaves the second and a half paused out.
            const screen = p.tracks.find((t) => t.name === "Screen").clips[0]
            const firstId = Number(screen.id)
            fuzzyCompare(screen.duration, recorded, 0.4)
            verify(screen.duration < recorded + 1.0)

            // Stopped while paused: the take ends where the pause began.
            const again = openRecorder(f)
            click(again.contentItem, "startScreenRecording")
            tryVerify(() => p.screenRecordingTime > 1.0, 4000)
            verify(p.pauseScreenRecording())
            const pausedAt = p.screenRecordingTime
            wait(800)
            verify(p.stopScreenRecording())
            let second = null
            for (const t of p.tracks)
                for (const c of t.clips)
                    if (t.name === "Screen" && Number(c.id) !== firstId)
                        second = c
            verify(second)
            fuzzyCompare(second.duration, pausedAt, 0.3)
        }

        function test_countdown_before_recording() {
            const f = createEditor()
            const p = f.project
            const panel = openRecorder(f)
            compare(EditingPreferences.recordCountdown, 0)
            click(panel.contentItem, "countdown:3")
            compare(EditingPreferences.recordCountdown, 3)
            verify(findChild(panel.contentItem, "countdown:3").variant === "primary")
            EditingPreferences.recordCountdown = 4 // (only 0, 3, 5 or 10)
            compare(EditingPreferences.recordCountdown, 3)

            click(panel.contentItem, "startScreenRecording")
            verify(panel.counting)
            verify(!p.screenRecording) // not yet
            compare(panel.countdownLeft, 3)
            const countdown = panel.countdownWindow
            verify(countdown && countdown.visible)
            compare(findChild(countdown.contentItem, "countdownNumber").text, "3")
            // Over the middle of what will be recorded (the 1280 × 720 test display).
            fuzzyCompare(countdown.x + countdown.width / 2, 640, 1)
            fuzzyCompare(countdown.y + countdown.height / 2, 360, 1)
            verify(settled(panel.contentItem, "countdownText").text.indexOf("3") >= 0)
            verify(!findChild(panel.contentItem, "startScreenRecording").visible)
            tryCompare(panel, "countdownLeft", 2, 1500)
            compare(findChild(countdown.contentItem, "countdownNumber").text, "2")

            // Cancelled: nothing recorded, the countdown gone.
            click(panel.contentItem, "cancelCountdown")
            verify(!panel.counting)
            verify(!panel.countdownWindow)
            wait(1300)
            verify(!p.screenRecording)

            // Counted to the end: recording begins, the countdown gone first.
            click(panel.contentItem, "startScreenRecording")
            verify(panel.counting)
            tryVerify(() => p.screenRecording, 4500, "the recording never began")
            verify(!panel.counting)
            verify(!panel.countdownWindow)
            verify(panel.recordingFrame)
            tryVerify(() => p.screenRecordingTime > 0.5, 3000)
            verify(p.stopScreenRecording())
        }
    }
}
