import QtQuick
import QtQuick.Window
import QtTest
import TechieVideoEditor.Theme
import TechieVideoEditor.Player
import TechieVideoEditor.Shell

// Relinking media that moved, and the keyboard shortcut editor, through the real editor.
Item {
    id: root
    width: 1400
    height: 900

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
        id: testCase
        name: "Hardening"
        when: windowShown

        readonly property string video: samplesDir + "/test_av_1080p30.mp4"
        readonly property string otherVideo: samplesDir + "/test_1080p30_h264.mp4"
        readonly property string tone: samplesDir + "/test_tone_440.wav"
        readonly property string still: stillsDir + "/halves.png"

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
                const p = item.mapToItem(root, 0, 0)
                const stable = last !== null && p.x === last.x && p.y === last.y
                last = p
                return stable
            }, 3000, name + " never settled")
            // Layouts place what just appeared on their next polish.
            for (let a = item; a && a !== root; a = a.parent)
                waitForPolish(a, 1000)
            return item
        }
        function scrollIntoView(item) {
            for (let a = item.parent; a; a = a.parent) {
                if (a.contentY === undefined || a.contentHeight === undefined || a.height === undefined)
                    continue
                const top = item.mapToItem(a.contentItem, 0, 0).y
                if (top < a.contentY || top + item.height > a.contentY + a.height)
                    a.contentY = Math.max(0, Math.min(top - 20, a.contentHeight - a.height))
                return
            }
        }
        function click(parent, name) {
            scrollIntoView(settled(parent, name))
            const item = settled(parent, name)
            mouseMove(item)
            mouseClick(item)
        }
        function cleanup() {
            KeyMap.resetAll()
        }

        // A project of a video and a photo in <scratch>/<folder>/, saved there.
        function projectIn(folder) {
            const dir = scratchDir + "/" + folder
            verify(testFiles.copy(video, dir + "/media/interview.mp4"))
            verify(testFiles.copy(still, dir + "/media/photo.png"))
            const f = createEditor()
            const p = f.project
            compare(p.importMedia(["file://" + dir + "/media/interview.mp4", "file://" + dir + "/media/photo.png"]), 2)
            verify(p.addClip(p.media[0].id))
            verify(p.addClip(p.media[1].id))
            compare(p.missingMedia.length, 0)
            verify(!findChild(f.editor, "missingMediaBar").visible)
            verify(p.save("file://" + dir + "/project.tve"))
            // Let the timeline's thumbnails finish before the files move away from under them.
            for (const clip of p.tracks.find((t) => t.main).clips) {
                tryVerify(() => {
                    for (let i = 0; ; ++i) {
                        const thumb = findChild(f.editor, "thumb:" + clip.id + ":" + i)
                        if (!thumb)
                            return i > 0
                        if (thumb.status === Image.Loading || thumb.status === Image.Null)
                            return false
                    }
                }, 10000, "thumbnails never finished")
            }
            f.destroy()
            return dir
        }

        function test_media_that_moved_is_found_again() {
            const dir = projectIn("moved")
            // The media folder moves, and the photo is renamed on the way.
            verify(testFiles.move(dir + "/media/interview.mp4", dir + "/elsewhere/deep/interview.mp4"))
            verify(testFiles.move(dir + "/media/photo.png", dir + "/elsewhere/holiday 3.png"))

            const f = createEditor()
            const p = f.project
            verify(p.open("file://" + dir + "/project.tve"))
            compare(p.missingMedia.length, 2)
            compare(p.missingMedia.map((m) => m.name).sort().join(","), "interview.mp4,photo.png")
            verify(p.media.every((m) => m.missing))
            const clips = p.tracks.find((t) => t.main).clips
            verify(clips.every((c) => c.missing))
            const bar = settled(f.editor, "missingMediaBar")
            verify(bar.visible)

            // The dialog lists them; searching the folder finds both (the photo by content).
            click(f.editor, "relinkButton")
            const dialog = findChild(f.editor, "relinkDialog")
            tryVerify(() => dialog.opened)
            tryVerify(() => findChild(dialog.contentItem, "missing:photo.png") !== null)
            compare(p.relinkFromFolder("file://" + dir + "/elsewhere"), 2)
            compare(p.missingMedia.length, 0)
            verify(p.media.find((m) => m.name === "holiday 3.png"))
            tryVerify(() => !bar.visible)
            verify(p.tracks.find((t) => t.main).clips.every((c) => !c.missing))
            compare(p.undoText, "Relink Media Files")
            dialog.close()

            // One undo step brings the old paths back; redo relinks again.
            p.undo()
            compare(p.missingMedia.length, 2)
            p.redo()
            compare(p.missingMedia.length, 0)

            // Nothing there: says so.
            p.undo()
            compare(p.relinkFromFolder("file://" + dir + "/media"), 0)
            verify(p.lastError.indexOf("None of the missing files") === 0, p.lastError)
        }

        function test_a_different_file_needs_a_second_yes() {
            const dir = projectIn("different")
            verify(testFiles.move(dir + "/media/interview.mp4", dir + "/gone/interview.mp4"))
            verify(testFiles.copy(otherVideo, dir + "/other/take2.mp4"))
            verify(testFiles.copy(tone, dir + "/other/tone.wav"))
            const f = createEditor()
            const p = f.project
            verify(p.open("file://" + dir + "/project.tve"))
            compare(p.missingMedia.length, 1)
            const id = p.missingMedia[0].id

            // A sound file cannot stand in for a video at all.
            verify(!p.relinkMedia(id, "file://" + dir + "/other/tone.wav"))
            verify(p.lastError.indexOf("different kind") >= 0, p.lastError)
            // Another video: refused until confirmed, in the dialog.
            click(f.editor, "relinkButton")
            const dialog = findChild(f.editor, "relinkDialog")
            tryVerify(() => dialog.opened)
            dialog.relink(id, "file://" + dir + "/other/take2.mp4", false)
            compare(p.missingMedia.length, 1)
            verify(findChild(dialog.contentItem, "relinkMessage").text.indexOf("not the same file") >= 0)
            click(dialog.contentItem, "relinkUseAnyway")
            compare(p.missingMedia.length, 0)
            compare(p.media.find((m) => m.id === id).name, "take2.mp4")
            tryVerify(() => findChild(dialog.contentItem, "relinkDone").text === "Done")
            click(dialog.contentItem, "relinkDone")
            tryVerify(() => !dialog.visible)
        }

        function test_the_keymap_changes_keys_and_keeps_them() {
            compare(KeyMap.actions.length, 19)
            compare(KeyMap.sequences.split, ["S"])
            compare(KeyMap.sequences["delete"], ["Del", "Backspace"])
            compare(KeyMap.text("group"), "⌘G")
            compare(KeyMap.hint("split"), " (S)")
            compare(KeyMap.sequenceFor(Qt.Key_G, Qt.ControlModifier | Qt.ShiftModifier), "Ctrl+Shift+G")
            compare(KeyMap.sequenceFor(Qt.Key_Shift, Qt.ShiftModifier), "") // a modifier alone

            verify(KeyMap.assign("split", "K"))
            compare(KeyMap.sequences.split, ["K"])
            compare(KeyMap.conflict("k"), "split")
            verify(KeyMap.actions.find((a) => a.id === "split").custom)
            // The file holds only what changed.
            verify(testFiles.read(KeyMap.filePath).indexOf("\"split\"") >= 0)
            verify(testFiles.read(KeyMap.filePath).indexOf("\"group\"") < 0)
            // Giving a key to another action takes it from the first.
            verify(KeyMap.assign("addMarker", "K"))
            compare(KeyMap.sequences.split, [])
            compare(KeyMap.text("split"), "")
            compare(KeyMap.sequences.addMarker, ["K"])
            // Resetting brings a default back, taking it from whoever had it.
            verify(KeyMap.assign("group", "S"))
            KeyMap.reset("split")
            compare(KeyMap.sequences.split, ["S"])
            compare(KeyMap.sequences.group, [])
            // The system's keys and nonsense are refused.
            verify(!KeyMap.assign("split", "Ctrl+Q"))
            verify(KeyMap.lastError.indexOf("system") >= 0, KeyMap.lastError)
            verify(!KeyMap.assign("split", ""))
            verify(!KeyMap.assign("nope", "J"))
            KeyMap.resetAll()
            verify(!testFiles.exists(KeyMap.filePath)) // nothing changed: no file
            compare(KeyMap.sequences.group, ["Ctrl+G"])
        }

        function test_the_editor_follows_the_keymap() {
            const f = createEditor()
            const p = f.project
            const shortcut = findChild(f.editor, "shortcut:split")
            verify(shortcut)
            compare(shortcut.sequences, ["S"])
            verify(KeyMap.assign("split", "Ctrl+K"))
            compare(shortcut.sequences, ["Ctrl+K"])
            p.importAndAppend("file://" + video)
            p.seek(1)
            // Pressing it splits, when macOS lets the test window have the keyboard.
            root.Window.window.requestActivate()
            for (let waited = 0; !root.Window.active && waited < 2000; waited += 50)
                wait(50)
            if (root.Window.active) {
                root.forceActiveFocus()
                keyClick(Qt.Key_K, Qt.ControlModifier)
                compare(p.tracks.find((t) => t.main).clips.length, 2)
            } else {
                warn("the test window could not become active: not pressing the new key")
            }
            // Tooltips show the key in use.
            compare(findChild(f.editor, "splitButton").tip, "Split at playhead (⌘K)")
        }

        function test_recording_a_key_in_the_dialog() {
            const f = createEditor()
            click(f.editor, "shortcutsButton")
            const dialog = findChild(f.editor, "shortcutsDialog")
            tryVerify(() => dialog.opened)
            const content = dialog.contentItem

            // Click a key, press a new one: it is set.
            click(content, "shortcutKey:split")
            compare(dialog.recording, "split")
            keyClick(Qt.Key_J)
            compare(dialog.recording, "")
            compare(KeyMap.sequences.split, ["J"])
            verify(findChild(content, "shortcutReset:split").visible)

            // A key in use asks first; Replace moves it.
            click(content, "shortcutKey:split")
            keyClick(Qt.Key_M)
            compare(KeyMap.sequences.split, ["J"]) // not yet
            const conflict = settled(content, "shortcutConflict")
            verify(conflict.text.indexOf("Add a marker") >= 0, conflict.text)
            click(content, "shortcutReplace")
            compare(KeyMap.sequences.split, ["M"])
            compare(KeyMap.sequences.addMarker, [])

            // Esc cancels recording; modifiers alone wait for the key.
            click(content, "shortcutKey:group")
            compare(dialog.recording, "group")
            keyClick(Qt.Key_Shift, Qt.ShiftModifier)
            compare(dialog.recording, "group")
            keyClick(Qt.Key_Escape)
            compare(dialog.recording, "")
            compare(KeyMap.sequences.group, ["Ctrl+G"])

            // Search narrows the list; Reset all puts everything back.
            const search = findChild(content, "shortcutSearch")
            search.text = "marker"
            tryVerify(() => {
                const key = findChild(content, "shortcutKey:split")
                return key === null || !key.visible
            })
            verify(findChild(content, "shortcutKey:addMarker") !== null)
            search.text = ""
            click(content, "shortcutsResetAll")
            compare(KeyMap.sequences.split, ["S"])
            compare(KeyMap.sequences.addMarker, ["M"])
            click(content, "shortcutsDone")
            tryVerify(() => !dialog.visible)
        }
    }
}
