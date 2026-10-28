import QtQuick
import QtQuick.Window
import QtTest
import TechieVideoEditor.Theme
import TechieVideoEditor.Player
import TechieVideoEditor.Shell

// Markers and chapters, groups and compound clips, through the real editor.
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
        name: "Structure"
        when: windowShown

        function still(name) { return "file://" + stillsDir + "/" + name }

        function createEditor() {
            const fixture = createTemporaryObject(editorComponent, root)
            verify(fixture)
            waitForRendering(fixture)
            return fixture
        }
        // Presses a shortcut. Qt delivers shortcuts only to the focused window, and macOS may
        // refuse to focus a test window while another app is in front; then the shortcut's
        // action runs directly (with a warning) so the rest of the test still counts.
        function shortcut(key, modifiers, action) {
            root.Window.window.requestActivate()
            for (let waited = 0; !root.Window.active && waited < 3000; waited += 50)
                wait(50)
            if (!root.Window.active) {
                warn("the test window could not become active: running the shortcut's action directly")
                action()
                return
            }
            root.forceActiveFocus()
            keyClick(key, modifiers)
        }
        function mainTrack(project) { return project.tracks.find((t) => t.main) }
        function trackAbove(project) {
            const tracks = project.tracks // top to bottom
            return tracks[tracks.findIndex((t) => t.main) - 1]
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
        function click(parent, name, modifiers) {
            const item = settled(parent, name)
            mouseMove(item)
            mouseClick(item, item.width / 2, item.height / 2, Qt.LeftButton, modifiers === undefined ? Qt.NoModifier : modifiers)
        }
        // The player's colour at canvas fraction (u, v).
        function canvasColor(f, u, v) {
            const overlay = findChild(f.editor, "maskOverlay")
            const r = overlay.canvasRect
            overlay.opacity = 0
            const image = grabImage(root)
            overlay.opacity = 1
            const scale = image.width / root.width
            const p = overlay.mapToItem(root, r.x + u * r.width, r.y + v * r.height)
            return image.pixel(Math.round(p.x * scale), Math.round(p.y * scale))
        }
        function isRed(c) { return c.r > 0.6 && c.g < 0.3 && c.b < 0.3 }
        function isGreen(c) { return c.g > 0.5 && c.r < 0.3 && c.b < 0.5 }
        function isBlue(c) { return c.b > 0.6 && c.r < 0.3 }
        function showsAt(f, u, v, test, message) {
            tryVerify(() => test(canvasColor(f, u, v)), 5000, message)
        }

        // Red | blue halves on the main track and a green disc above them, both 0–5 s.
        function layered() {
            const f = createEditor()
            const p = f.project
            compare(p.importMedia([still("halves.png"), still("disc.png")]), 2)
            verify(p.addClip(p.media[0].id, mainTrack(p).id, 0))
            verify(p.addClip(p.media[1].id, trackAbove(p).id, 0))
            p.seek(1)
            showsAt(f, 0.5, 0.5, isGreen, "the disc never showed")
            showsAt(f, 0.05, 0.5, isRed, "the red half never showed")
            return f
        }

        function test_markers_are_added_edited_and_removed() {
            const f = layered()
            const p = f.project
            p.seek(0.5)
            shortcut(Qt.Key_M, Qt.NoModifier, () => p.addMarker())
            compare(p.markers.length, 1)
            fuzzyCompare(p.markers[0].time, 0.5, 0.001)
            compare(p.markers[0].color, "blue")
            compare(p.undoText, "Add Marker")
            p.seek(2)
            click(f.editor, "markerButton")
            compare(p.markers.length, 2)

            // Snapping finds markers; the playhead steps between them.
            fuzzyCompare(p.snap(0.53, 0.05), 0.5, 0.001)
            p.seek(1)
            shortcut(Qt.Key_Right, Qt.AltModifier, () => p.seekMarker(1))
            fuzzyCompare(p.playhead, 2, 0.001)
            shortcut(Qt.Key_Left, Qt.AltModifier, () => p.seekMarker(-1))
            shortcut(Qt.Key_Left, Qt.AltModifier, () => p.seekMarker(-1))
            fuzzyCompare(p.playhead, 0.5, 0.001)
            verify(!p.seekMarker(-1)) // none before the first

            // Click the first flag to edit it.
            const id = p.markers[0].id
            click(f.editor, "marker:" + id)
            const editor = findChild(f.editor, "markerEditor")
            tryVerify(() => editor.opened)
            const content = editor.contentItem
            click(content, "markerColor:green")
            compare(p.markers[0].color, "green")
            click(content, "markerChapter")
            compare(p.markers[0].chapter, true)
            const note = settled(content, "markerNote")
            mouseClick(note)
            keySequence("ctrl+a")
            for (const ch of "Intro")
                keyClick(ch)
            keyClick(Qt.Key_Return)
            compare(p.markers[0].note, "Intro")
            const length = settled(content, "markerDuration")
            mouseClick(length)
            keySequence("ctrl+a")
            keyClick(Qt.Key_1)
            keyClick(Qt.Key_Return)
            fuzzyCompare(p.markers[0].duration, 1, 0.001)
            tryVerify(() => findChild(f.editor, "marker:" + id) && findChild(findChild(f.editor, "marker:" + id), "markerRange").visible)
            p.undo()
            fuzzyCompare(p.markers[0].duration, 0, 0.001)
            p.redo()

            click(content, "removeMarker")
            tryVerify(() => !editor.visible)
            compare(p.markers.length, 1)
            fuzzyCompare(p.markers[0].time, 2, 0.001)
            p.undo()
            compare(p.markers.length, 2)
            compare(p.markers[0].note, "Intro")

            // Dragging a flag moves the marker (snapping to clip edges is off here).
            const flag = settled(f.editor, "marker:" + p.markers[1].id)
            const ruler = findChild(f.editor, "ruler")
            const from = flag.mapToItem(ruler, 4, ruler.height - 10)
            mousePress(ruler, from.x, from.y)
            for (let step = 1; step <= 6; ++step)
                mouseMove(ruler, from.x + 10 * step, from.y)
            mouseRelease(ruler, from.x + 60, from.y)
            tryVerify(() => Math.abs(p.markers[1].time - 3) < 0.15, 2000, "the marker did not move: " + p.markers[1].time)
            compare(p.undoText, "Change Marker")

            // Wrong values are refused.
            verify(!p.setMarker(id, { color: "teal" }))
            verify(!p.setMarker(id, { time: -1 }))
        }

        function test_grouped_clips_move_and_delete_together() {
            const f = layered()
            const p = f.project
            const bottom = mainTrack(p).clips[0].id
            const top = trackAbove(p).clips[0].id

            // ⇧-click adds a clip to the selection; grouping needs two.
            click(f.editor, "clip:" + bottom)
            compare(p.selectedClips.length, 1)
            verify(!findChild(f.editor, "groupButton").enabled)
            click(f.editor, "clip:" + top, Qt.ShiftModifier)
            compare(p.selectedClips.length, 2)
            click(f.editor, "groupButton")
            compare(p.undoText, "Group")
            verify(mainTrack(p).clips[0].groupId > 0)
            compare(mainTrack(p).clips[0].groupId, trackAbove(p).clips[0].groupId)
            tryVerify(() => findChild(findChild(f.editor, "clip:" + top), "groupBadge").visible)

            // Clicking one selects the group; ⇧-clicking it takes the group out again.
            click(f.editor, "clip:" + bottom)
            compare(p.selectedClips.length, 2)
            click(f.editor, "clip:" + top, Qt.ShiftModifier)
            compare(p.selectedClips.length, 0)

            // Dragging one moves both, as one undo step.
            const clip = settled(f.editor, "clip:" + bottom)
            const ruler = findChild(f.editor, "ruler")
            const start = clip.mapToItem(ruler, clip.width / 2, clip.height / 2)
            mousePress(ruler, start.x, start.y)
            for (let step = 1; step <= 6; ++step)
                mouseMove(ruler, start.x + 10 * step, start.y)
            mouseRelease(ruler, start.x + 60, start.y)
            tryVerify(() => mainTrack(p).clips.length === 1 && Math.abs(mainTrack(p).clips[0].start - 1) < 0.1)
            fuzzyCompare(trackAbove(p).clips[0].start, mainTrack(p).clips[0].start, 0.001)
            compare(p.undoText, "Move Group")

            // Deleting one deletes both; undo brings both back.
            p.selectedClip = bottom
            shortcut(Qt.Key_Delete, Qt.NoModifier, () => p.deleteClip(p.selectedClip))
            compare(mainTrack(p).clips.length, 0)
            compare(trackAbove(p).clips.length, 0)
            p.undo()
            compare(mainTrack(p).clips.length, 1)
            compare(trackAbove(p).clips.length, 1)

            // ⇧⌘G ungroups.
            p.selectedClip = bottom
            shortcut(Qt.Key_G, Qt.ControlModifier | Qt.ShiftModifier, () => p.ungroupSelected())
            compare(p.undoText, "Ungroup")
            compare(mainTrack(p).clips[0].groupId, 0)
            compare(trackAbove(p).clips[0].groupId, 0)
            p.selectedClip = bottom
            compare(p.selectedClips.length, 1)
        }

        function test_compound_clips_play_as_one_and_break_apart() {
            const f = layered()
            const p = f.project
            const bottom = mainTrack(p).clips[0].id
            const top = trackAbove(p).clips[0].id
            p.selectedClip = bottom
            p.toggleClipSelection(top)
            click(f.editor, "compoundButton")
            compare(p.undoText, "Create Compound Clip")
            compare(mainTrack(p).clips.length, 1)
            compare(trackAbove(p).clips.length, 0)
            const compound = mainTrack(p).clips[0]
            verify(compound.isCompound)
            compare(compound.label, "Compound 1")
            fuzzyCompare(compound.duration, 5, 0.01)
            compare(p.selectedClip, compound.id)
            tryVerify(() => findChild(f.editor, "clip:" + compound.id) !== null)
            verify(p.selectedClipInfo.isCompound)

            // It looks as the clips did: the disc over the red | blue halves.
            p.seek(1.5)
            showsAt(f, 0.5, 0.5, isGreen, "the compound's top layer did not show")
            showsAt(f, 0.05, 0.5, isRed, "the compound's bottom layer did not show")
            showsAt(f, 0.95, 0.5, isBlue, "the compound's bottom layer did not show")

            // It has no speed; trimmed and moved it plays the part it shows.
            verify(!p.setClipSpeed(compound.id, 2, false))
            verify(p.lastError.indexOf("compound") >= 0)
            verify(p.trimClip(compound.id, false, 3))
            verify(p.moveClip(compound.id, mainTrack(p).id, 1))
            fuzzyCompare(mainTrack(p).clips[0].start, 1, 0.01)
            fuzzyCompare(mainTrack(p).clips[0].duration, 3, 0.01)
            p.seek(0.5)
            showsAt(f, 0.5, 0.5, (c) => c.r < 0.1 && c.g < 0.1 && c.b < 0.1, "something showed before the compound clip")
            p.seek(2)
            showsAt(f, 0.5, 0.5, isGreen, "the moved compound clip did not show")

            // ⌥⌘G breaks it apart: the clips come back where they were.
            p.undo()
            p.undo()
            p.selectedClip = compound.id
            shortcut(Qt.Key_G, Qt.ControlModifier | Qt.AltModifier, () => p.breakCompound(p.selectedClip))
            compare(p.undoText, "Break Apart")
            compare(mainTrack(p).clips.length, 1)
            compare(trackAbove(p).clips.length, 1)
            compare(mainTrack(p).clips[0].id, bottom)
            compare(trackAbove(p).clips[0].id, top)
            showsAt(f, 0.5, 0.5, isGreen, "the disc did not come back")
            p.undo()
            compare(mainTrack(p).clips.length, 1)
            compare(mainTrack(p).clips[0].label, "Compound 1")

            // Saved and reopened, it is still a compound clip.
            const file = "file://" + tempDir + "/tve_compound.tve"
            verify(p.save(file))
            const g = createEditor()
            verify(g.project.open(file))
            const reopened = g.project.tracks.find((t) => t.main).clips[0]
            verify(reopened.isCompound)
            compare(reopened.label, "Compound 1")
        }
    }
}
