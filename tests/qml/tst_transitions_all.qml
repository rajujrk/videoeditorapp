import QtQuick
import QtTest
import TechieVideoEditor.Theme
import TechieVideoEditor.Player
import TechieVideoEditor.Shell

// The 60 transitions in the library, and transitions on every cut (one, or random ones).
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
            ProjectController { id: controller; view: screen.player }
            EditorScreen { id: screen; anchors.fill: parent; project: controller }
        }
    }

    TestCase {
        id: testCase
        name: "TransitionsAll"
        when: windowShown

        readonly property string av: "file://" + samplesDir + "/test_av_1080p30.mp4"
        readonly property string toneWav: "file://" + samplesDir + "/test_tone_440.wav"

        function initTestCase() {
            const probe = Qt.createQmlObject('import TechieVideoEditor.Player; ProjectController {}', testCase)
            if (probe.importMedia([av, toneWav]) !== 2)
                skip("samples missing: run tools/make_test_media.sh")
            probe.destroy()
        }

        function createEditor() {
            const fixture = createTemporaryObject(editorComponent, root)
            verify(fixture)
            waitForRendering(fixture)
            return fixture
        }
        function track(project, name) { return project.tracks.find((t) => t.name === name) }

        // Edits rebuild the timeline rows; wait until a control has stopped moving (laid
        // out) before clicking it, as a person would.
        function settled(editor, name) {
            let item = null
            let last = null
            tryVerify(() => {
                item = findChild(editor, name)
                if (!item || !item.visible)
                    return false
                const p = item.mapToItem(root, 0, 0)
                const stable = last !== null && p.x === last.x && p.y === last.y
                last = p
                return stable
            }, 3000, name + " never settled")
            // A layout that has not run yet leaves positions stale but "stable"; wait for
            // the layout (polish) pass of the item and everything above it.
            for (let a = item; a && a !== root; a = a.parent)
                verify(waitForPolish(a), "layout never ran for " + name)
            return item
        }
        // Move there first, like a real cursor: leaving the previous control closes its
        // tooltip, which would otherwise take this click.
        function clickWhenSettled(editor, name) {
            const item = settled(editor, name)
            mouseMove(item)
            mouseClick(item)
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
        function click(editor, name) {
            scrollIntoView(settled(editor, name))
            clickWhenSettled(editor, name)
        }
        // Four clips end to end on the main track, two on the track above: four cuts.
        function withCuts() {
            const f = createEditor()
            const p = f.project
            for (let i = 0; i < 4; ++i)
                verify(p.importAndAppend(av))
            const main = p.tracks.find((t) => t.main)
            const above = p.tracks[p.tracks.findIndex((t) => t.main) - 1]
            verify(p.addClip(p.media[0].id, above.id, 0))
            verify(p.addClip(p.media[0].id, above.id, 2))
            return f
        }
        function cutClips(p) {
            const list = []
            for (const t of p.tracks) {
                if (t.kind !== "video")
                    continue
                for (let i = 1; i < t.clips.length; ++i)
                    if (Math.abs(t.clips[i - 1].start + t.clips[i - 1].duration - t.clips[i].start) < 1e-6)
                        list.push(t.clips[i])
            }
            return list
        }

        function test_sixty_transitions_by_category() {
            const f = createEditor()
            const p = f.project
            compare(p.availableTransitions.length, 60)
            click(f.editor, "libraryTab:Transitions")
            const grid = settled(f.editor, "transitionsGrid")
            compare(grid.count, 60)
            click(f.editor, "transitionCategory:luma")
            tryCompare(grid, "count", 10)
            click(f.editor, "transitionCategory:3d")
            tryCompare(grid, "count", p.availableTransitions.filter((t) => t.categories.indexOf("3d") >= 0).length)
            verify(grid.count >= 12)
            // Their previews are drawn like the others'.
            const preview = settled(f.editor, "transitionPreview:tve.transition.cube")
            tryCompare(preview, "status", Image.Ready, 5000)
            click(f.editor, "transitionCategory:all")
            tryCompare(grid, "count", 60)
        }

        function test_apply_one_transition_to_every_cut() {
            const f = withCuts()
            const p = f.project
            const cuts = cutClips(p)
            compare(cuts.length, 4)
            verify(p.addTransition("tve.transition.heart", cuts[0].id))
            verify(p.setTransition(cuts[0].id, { duration: 0.6 }))
            p.selectedTransition = cuts[0].id
            click(f.editor, "applyTransitionToAll")
            compare(p.undoText, "Apply Transition to All")
            for (const clip of cutClips(p)) {
                compare(clip.transition.transitionId, "tve.transition.heart")
                fuzzyCompare(clip.transition.duration, 0.6, 0.01)
            }
            // One undo step puts them all back.
            p.undo()
            const after = cutClips(p)
            compare(after.filter((c) => c.transition && c.transition.transitionId).length, 1)
            verify(!p.applyTransitionToAll("tve.transition.nonsense"))
        }

        function test_random_transitions_on_every_cut() {
            const f = withCuts()
            const p = f.project
            click(f.editor, "libraryTab:Transitions")
            click(f.editor, "randomTransitions")
            compare(p.undoText, "Random Transitions")
            const ids = p.availableTransitions.map((t) => t.id)
            const cuts = cutClips(p)
            compare(cuts.length, 4)
            for (const clip of cuts)
                verify(ids.indexOf(clip.transition.transitionId) >= 0)
            // Neighbouring cuts differ.
            for (let i = 1; i < 3; ++i)
                verify(cuts[i].transition.transitionId !== cuts[i - 1].transition.transitionId)
            // Locked tracks are left alone.
            p.undo()
            const above = p.tracks[p.tracks.findIndex((t) => t.main) - 1]
            verify(p.setTrackFlag(above.id, "locked", true))
            compare(p.applyRandomTransitions(1.5), 3)
        }

        function test_no_cuts() {
            const f = createEditor()
            const p = f.project
            verify(p.importAndAppend(av))
            compare(p.applyRandomTransitions(), 0)
            verify(p.lastError.indexOf("no cuts") >= 0)
        }
    }
}
