import QtQuick
import QtTest
import TechieVideoEditor.Theme
import TechieVideoEditor.Player
import TechieVideoEditor.Shell

// Transitions through the real editor UI: library, timeline markers and the inspector.
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
        name: "Transitions"
        when: windowShown

        readonly property string av: "file://" + samplesDir + "/test_av_1080p30.mp4"

        function initTestCase() {
            const probe = Qt.createQmlObject('import TechieVideoEditor.Player; ProjectController {}', testCase)
            if (probe.importMedia([av]) !== 1)
                skip("samples missing: run tools/make_test_media.sh")
            probe.destroy()
        }

        function createEditor() {
            const fixture = createTemporaryObject(editorComponent, root)
            verify(fixture)
            waitForRendering(fixture)
            return fixture
        }
        function mainTrack(project) { return project.tracks.find((t) => t.main) }
        function mainClips(project) { return mainTrack(project).clips }

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
            for (let a = item; a && a !== root; a = a.parent)
                verify(waitForPolish(a), "layout never ran for " + name)
            return item
        }

        function click(editor, name) {
            const item = settled(editor, name)
            mouseMove(item)
            mouseClick(item)
        }

        // Two 2-second clips touching at 2 s on the main track, playhead near the cut.
        function twoClips(f) {
            const p = f.project
            p.importAndAppend(av)
            p.importAndAppend(av)
            p.seek(1.8)
            return p
        }

        function test_library_click_adds_at_the_nearest_cut() {
            const f = createEditor()
            const p = twoClips(f)
            click(f.editor, "libraryTab:Transitions")
            click(f.editor, "libraryTransition:tve.transition.dissolve")
            const b = mainClips(p)[1]
            compare(p.undoText, "Add Transition")
            compare(p.selectedTransition, b.id)
            compare(b.transition.transitionId, "tve.transition.dissolve")
            fuzzyCompare(b.transition.start, 1.5, 1e-6) // 1 s, centred on the cut
            fuzzyCompare(b.transition.end, 2.5, 1e-6)

            // A marker over the cut; the inspector shows the transition instead of clip tabs.
            settled(f.editor, "transition:" + b.id)
            settled(f.editor, "transitionPage")
            verify(!findChild(f.editor, "inspectorTab:Video").visible)

            p.undo()
            compare(mainClips(p)[1].transition.clipId, undefined)
        }

        function test_inspector_changes_duration_position_and_settings() {
            const f = createEditor()
            const p = twoClips(f)
            verify(p.addTransition("tve.transition.wipe"))
            const id = mainClips(p)[1].id

            click(f.editor, "alignment:startsAtCut")
            compare(p.selectedTransitionInfo.alignment, "startsAtCut")
            fuzzyCompare(p.selectedTransitionInfo.start, 2.0, 1e-6)
            compare(p.undoText, "Change Transition")

            verify(p.setTransition(id, { duration: 3 }))
            // Each side is limited to half its clip: after the cut, at most 1 s.
            fuzzyCompare(p.selectedTransitionInfo.duration, 1.0, 1e-6)
            fuzzyCompare(p.selectedTransitionInfo.requestedDuration, 3.0, 1e-6)

            verify(p.setTransition(id, { values: { direction: 2, softness: 0.3 } }))
            compare(p.selectedTransitionInfo.values.direction, 2)
            fuzzyCompare(p.selectedTransitionInfo.values.softness, 0.3, 1e-6)
            verify(!p.setTransition(id, { alignment: "sideways" }))
        }

        function test_changing_the_kind_keeps_the_timing() {
            const f = createEditor()
            const p = twoClips(f)
            verify(p.addTransition("tve.transition.wipe"))
            const id = mainClips(p)[1].id
            verify(p.setTransition(id, { duration: 0.6 }))
            verify(p.addTransition("tve.transition.iris", id))
            compare(p.selectedTransitionInfo.transitionId, "tve.transition.iris")
            fuzzyCompare(p.selectedTransitionInfo.requestedDuration, 0.6, 1e-6)
            compare(p.undoText, "Change Transition")
        }

        function test_remove_button_and_delete_key() {
            const f = createEditor()
            const p = twoClips(f)
            verify(p.addTransition("tve.transition.dissolve"))
            click(f.editor, "removeTransitionButton")
            compare(p.undoText, "Remove Transition")
            compare(p.selectedTransition, 0)
            p.undo()

            // Select the marker on the timeline; the toolbar's delete (like ⌫) then removes the
            // transition, not a clip. (Clicked rather than typed: shortcuts need the test window
            // to be the active one, which the desktop may not allow.)
            const id = mainClips(p)[1].id
            p.selectedTransition = 0
            click(f.editor, "transition:" + id)
            compare(p.selectedTransition, id)
            compare(p.selectedClip, 0)
            click(f.editor, "deleteButton")
            compare(mainClips(p).length, 2)
            compare(mainClips(p)[1].transition.clipId, undefined)
        }

        function test_selecting_a_clip_leaves_the_transition_page() {
            const f = createEditor()
            const p = twoClips(f)
            verify(p.addTransition("tve.transition.dissolve"))
            p.selectedClip = mainClips(p)[0].id
            compare(p.selectedTransition, 0)
            tryVerify(() => !findChild(f.editor, "transitionPage").visible)
        }

        function test_dropping_needs_a_cut() {
            const f = createEditor()
            const p = f.project
            p.importAndAppend(av)
            verify(!p.addTransition("tve.transition.dissolve"))
            verify(p.lastError.indexOf("side by side") >= 0)
            p.importAndAppend(av)
            const track = mainTrack(p).id
            verify(!p.addTransitionAt("tve.transition.dissolve", track, 1.0, 0.25)) // 1 s from the cut
            verify(p.addTransitionAt("tve.transition.dissolve", track, 2.1, 0.25))
            verify(!p.addTransition("tve.transition.nope", mainClips(p)[1].id))
        }

        function test_library_leads_with_the_basics() {
            const f = createEditor()
            const list = f.project.availableTransitions
            compare(list.length, 60)
            compare(list[0].categories[0], "basic")
            verify(list.findIndex((t) => t.id === "tve.transition.dissolve") < 4)
        }

        function test_library_previews_render() {
            const f = createEditor()
            click(f.editor, "libraryTab:Transitions")
            let preview = null
            tryVerify(() => (preview = findChild(f.editor, "transitionPreview:tve.transition.dissolve")) !== null, 3000)
            tryVerify(() => preview.status === Image.Ready, 5000, "preview never rendered")
            compare(preview.sourceSize.width, 192)
        }
    }
}
