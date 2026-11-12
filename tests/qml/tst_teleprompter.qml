import QtQuick
import QtTest
import TechieVideoEditor.Theme
import TechieVideoEditor.Player
import TechieVideoEditor.Shell

// The teleprompter: opened from the recording panels in a window of its own (left out of
// screen recordings), scrolling at the speed set, mirrored, edited, kept, and following a
// recording.
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
        id: testCase
        name: "Teleprompter"
        when: windowShown

        function createEditor() {
            const fixture = createTemporaryObject(editorComponent, root)
            verify(fixture)
            waitForRendering(fixture)
            return fixture
        }
        function settled(parent, name) {
            let item = null
            let last = null
            tryVerify(() => {
                item = findChild(parent, name)
                if (!item || !item.visible)
                    return false
                const p = item.mapToItem(null, 0, 0)
                const stable = last !== null && p.x === last.x && p.y === last.y
                last = p
                return stable
            }, 5000, name + " never settled")
            for (let a = item; a && a.parent; a = a.parent)
                waitForPolish(a, 1000)
            return item
        }
        function click(parent, name) {
            const item = settled(parent, name)
            mouseMove(item)
            mouseClick(item)
        }
        // Opened from the screen recording panel.
        function open(f) {
            click(f.editor, "screenRecordButton")
            const panel = findChild(f.editor, "recordPanel")
            tryVerify(() => panel.opened)
            click(panel.contentItem, "openTeleprompter")
            tryVerify(() => f.editor.teleprompter && f.editor.teleprompter.visible, 3000, "no teleprompter window")
            panel.close()
            const w = f.editor.teleprompter
            tryVerify(() => w.contentItem.width > 0)
            return w
        }
        // How far it scrolls in `ms`.
        function scrolled(w, ms) {
            const scroller = findChild(w.contentItem, "teleprompterScroll")
            const before = scroller.contentY
            wait(ms)
            return scroller.contentY - before
        }
        function cleanup() {
            const s = Qt.createQmlObject('import TechieVideoEditor.Player; ProjectController {}', testCase)
            s.teleprompter.speed = 3
            s.teleprompter.fontSize = 56
            s.teleprompter.mirrored = false
            s.teleprompter.followRecording = true
            s.destroy()
        }

        function test_scrolls_at_its_speed_and_is_kept_out_of_recordings() {
            const f = createEditor()
            const p = f.project
            const w = open(f)
            compare(w.objectName, "teleprompter")
            // Left out of screen recordings while it is open.
            const out = p.recordingExclusions()
            compare(out.length, 1)
            verify(out[0] > 0)
            // A long script, scrolling once played; faster at a higher speed.
            const s = p.teleprompter
            s.text = Array.from({ length: 80 }, (_, i) => "Line " + (i + 1) + " of the script, read aloud.").join("\n")
            s.fontSize = 40
            s.speed = 2
            const scroller = findChild(w.contentItem, "teleprompterScroll")
            compare(scroller.contentY, 0)
            click(w.contentItem, "teleprompterPlay")
            verify(w.playing)
            const slow = scrolled(w, 600)
            verify(slow > 5, "slow " + slow)
            s.speed = 8
            const fast = scrolled(w, 600)
            verify(fast > slow * 2.5, "fast " + fast + " slow " + slow)
            // ↓ slows it by half a step; Space pauses, and it stays put.
            findChild(w, "teleprompterKey:down").activated()
            compare(s.speed, 7.5)
            findChild(w, "teleprompterKey:space").activated()
            verify(!w.playing)
            compare(scrolled(w, 300), 0)
            click(w.contentItem, "teleprompterRestart")
            compare(scroller.contentY, 0)
            // Mirrored for a beam-splitter.
            click(w.contentItem, "teleprompterMirror")
            verify(s.mirrored)
            compare(scroller.transform[0].xScale, -1)
            // Closed: no longer left out.
            w.close()
            tryVerify(() => !f.editor.teleprompter, 2000)
            compare(p.recordingExclusions().length, 0)
        }

        function test_edited_kept_and_following_a_recording() {
            const f = createEditor()
            const p = f.project
            const w = open(f)
            const s = p.teleprompter
            // Edited in the window: kept in its settings file.
            click(w.contentItem, "teleprompterEdit")
            const editor = settled(w.contentItem, "teleprompterText")
            editor.text = "Hello and welcome.\nToday we cut a trailer."
            click(w.contentItem, "teleprompterEdit") // Done
            compare(s.text, "Hello and welcome.\nToday we cut a trailer.")
            s.save()
            const saved = JSON.parse(testFiles.read(teleprompterFile))
            compare(saved.text, "Hello and welcome.\nToday we cut a trailer.")
            // Out-of-range settings are kept in range.
            s.fontSize = 500
            compare(s.fontSize, 120)
            s.speed = 0
            compare(s.speed, 1)
            s.fontSize = 40
            s.text = Array.from({ length: 60 }, (_, i) => "Line " + i).join("\n")
            // A recording starts it from the top, and stopping stops it.
            const scroller = findChild(w.contentItem, "teleprompterScroll")
            scroller.contentY = 200
            verify(p.startScreenRecording({ screen: p.listScreenTargets()[0], microphone: false }))
            tryVerify(() => w.playing, 2000, "the recording did not start it")
            verify(scroller.contentY < 200)
            tryVerify(() => scroller.contentY > 0, 3000, "it did not scroll")
            verify(p.stopScreenRecording())
            tryVerify(() => !w.playing, 2000, "stopping the recording did not stop it")
            // Not following: a recording leaves it alone.
            s.followRecording = false
            verify(p.startScreenRecording({ screen: p.listScreenTargets()[0], microphone: false }))
            wait(300)
            verify(!w.playing)
            verify(p.stopScreenRecording())
        }
    }
}
