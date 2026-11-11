import QtQuick
import QtTest
import TechieVideoEditor.Theme
import TechieVideoEditor.Player
import TechieVideoEditor.Shell

// The source monitor, compound clips opened in a timeline tab, a project brought in as a
// compound clip, and multicam angles, through the real editor.
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
        name: "Sequences"
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
        function mainTrack(p) { return p.tracks.find((t) => t.main) }
        function trackAbove(p) { return p.tracks[p.tracks.findIndex((t) => t.main) - 1] }
        function shortcut(f, name) {
            const s = findChild(f.editor, "shortcut:" + name)
            verify(s, name)
            verify(s.enabled, name + " is off")
            s.activated()
        }

        function test_the_source_monitor_places_a_marked_part() {
            const f = createEditor()
            const p = f.project
            verify(p.importAndAppend(av)) // 0–2 s
            verify(p.importAndAppend(av)) // 2–4 s
            const source = findChild(f.editor, "sourceMonitor")
            // From the library's menu: the media in a player of its own.
            findChild(f.editor, "mediaMenu").media = p.media[0]
            findChild(f.editor, "mediaMenu:source").triggered()
            tryVerify(() => source.visible && source.ready)
            verify(findChild(f.editor, "playerTabs").visible)
            compare(f.editor.transport, source.controller)
            fuzzyCompare(source.controller.duration, 2, 0.05)
            // The transport keys mark it: In at 0.5 s, Out at 1.5 s.
            source.controller.seek(0.5)
            shortcut(f, "markIn")
            source.controller.seek(1.5)
            shortcut(f, "markOut")
            fuzzyCompare(source.controller.markIn, 0.5, 0.02)
            compare(p.markIn, -1) // (the timeline's marks are its own)
            verify(findChild(f.editor, "sourceRange").visible)

            // Insert at 1 s: the first clip split, the rest a second later.
            p.seek(1)
            findChild(f.editor, "sourceInsert").clicked()
            compare(p.undoText, "Insert Clip")
            fuzzyCompare(p.duration, 5, 0.05)
            const main = mainTrack(p).clips
            compare(main.length, 4)
            fuzzyCompare(main[1].start, 1, 0.02)
            fuzzyCompare(main[1].sourceIn, 0.5, 0.02)
            fuzzyCompare(main[1].duration, 1, 0.02)
            // Overwrite: nothing moves.
            p.seek(0)
            findChild(f.editor, "sourceOverwrite").clicked()
            fuzzyCompare(p.duration, 5, 0.05)
            compare(p.undoText, "Overwrite Clip")
            // Append: after the last clip.
            findChild(f.editor, "sourceAppend").clicked()
            fuzzyCompare(p.duration, 6, 0.05)

            // Back to the timeline: its transport again.
            findChild(f.editor, "playerTab:timeline").clicked()
            verify(!source.visible)
            compare(f.editor.transport, p)
        }

        function test_a_compound_clip_opens_in_a_tab_of_its_own() {
            const f = createEditor()
            const p = f.project
            for (let i = 0; i < 3; ++i)
                verify(p.importAndAppend(av)) // 0–6 s
            const clips = mainTrack(p).clips
            p.selectedClip = clips[0].id
            p.toggleClipSelection(clips[1].id)
            const compound = p.createCompound() // 0–4 s
            verify(compound > 0)
            verify(!findChild(f.editor, "timelineTabs").visible)
            // Double-click it: its insides on the timeline, in a second tab.
            const item = findChild(f.editor, "clip:" + compound)
            waitForPolish(item, 1000)
            p.seek(3)
            mouseDoubleClickSequence(item, 40, item.height / 2)
            tryCompare(p.timelineTabs, "length", 2)
            compare(p.timelineTabs[1].name, "Compound 1")
            verify(findChild(f.editor, "timelineTabs").visible)
            fuzzyCompare(p.duration, 4, 0.05)
            fuzzyCompare(p.playhead, 3, 0.05) // the same picture
            compare(p.undoText, "") // a history of its own
            // An edit inside, undone inside, done again.
            const inside = mainTrack(p).clips
            compare(inside.length, 2)
            verify(p.deleteClip(inside[1].id))
            fuzzyCompare(p.duration, 2, 0.05)
            p.undo()
            fuzzyCompare(p.duration, 4, 0.05)
            p.redo()
            // Markers belong to the main timeline.
            compare(p.addMarker(), 0)

            // Back by its tab: the compound clip is 2 s now, the rest of the timeline moved up.
            findChild(f.editor, "timelineTab:0").clicked()
            compare(p.timelineTabs.length, 1)
            compare(p.undoText, "Edit Compound Clip")
            compare(p.selectedClip, compound)
            const after = mainTrack(p).clips
            fuzzyCompare(after.find((c) => c.id === compound).duration, 2, 0.05)
            p.undo()
            fuzzyCompare(mainTrack(p).clips.find((c) => c.id === compound).duration, 4, 0.05)
            p.redo()

            // Saved while open: the whole project, with the edits inside.
            verify(p.openCompoundClip(compound))
            verify(p.deleteClip(mainTrack(p).clips[0].id))
            const file = tempDir + "/tve-sequences.tve"
            verify(p.save("file://" + file))
            verify(!p.modified)
            findChild(f.editor, "closeTimelineTab").clicked()
            compare(p.timelineTabs.length, 1)
            verify(!p.modified)
            verify(p.open("file://" + file))
            compare(p.timelineTabs.length, 1)
            verify(mainTrack(p).clips.some((c) => c.isCompound))
        }

        function test_another_project_comes_in_as_a_compound_clip() {
            const f = createEditor()
            const p = f.project
            // A project of two clips, saved.
            const other = Qt.createQmlObject('import TechieVideoEditor.Player; ProjectController { autosaveEnabled: false }', testCase)
            verify(other.importAndAppend(av))
            verify(other.importAndAppend(av))
            const file = tempDir + "/tve-imported.tve"
            verify(other.save("file://" + file))
            other.destroy()

            verify(p.importAndAppend(av))
            p.seek(2)
            const made = p.importProjectAsCompound("file://" + file)
            verify(made > 0)
            compare(p.undoText, "Import Project")
            compare(p.media.length, 1) // the same file, not imported twice
            let clip = null
            for (const t of p.tracks)
                clip = clip || t.clips.find((c) => c.id === made)
            verify(clip.isCompound)
            compare(clip.label, "tve-imported")
            fuzzyCompare(clip.start, 2, 0.02)
            fuzzyCompare(clip.duration, 4, 0.05)
            verify(p.importProjectAsCompound("file://" + tempDir + "/missing.tve") === 0)
        }

        function test_multicam_angles_switch_and_cut() {
            const f = createEditor()
            const p = f.project
            compare(p.importMedia([av, "file://" + stillsDir + "/disc.png"]), 2)
            verify(p.addClip(p.media[0].id, mainTrack(p).id, 0))
            verify(p.addClip(p.media[1].id, trackAbove(p).id, 0))
            p.selectedClip = mainTrack(p).clips[0].id
            p.toggleClipSelection(trackAbove(p).clips[0].id)
            findChild(f.editor, "multicamButton").clicked()
            compare(p.undoText, "Make Multicam Clip")
            const info = p.selectedClipInfo
            verify(info.isMulticam)
            compare(info.angles.length, 2)
            compare(info.angle, 0)
            compare(info.angles[1].name, "disc")
            findChild(f.editor, "inspectorTab:Video").clicked()
            tryVerify(() => findChild(f.editor, "multicamSection").visible)
            // 2 at 1 s: camera 2 from there.
            p.seek(1)
            shortcut(f, "angle2")
            compare(p.undoText, "Cut to Angle")
            const parts = mainTrack(p).clips
            compare(parts.length, 2)
            compare(parts[0].angle, 0)
            compare(parts[1].angle, 1)
            fuzzyCompare(parts[1].start, 1, 0.02)
            // A tile in the inspector cuts too; the clip's menu switches a whole part.
            p.selectedClip = parts[0].id
            p.seek(0.5)
            findChild(f.editor, "angle:1").clicked()
            compare(mainTrack(p).clips.length, 3)
            verify(p.setClipAngle(mainTrack(p).clips[0].id, 1))
            compare(p.undoText, "Switch Angle")
            compare(mainTrack(p).clips[0].angle, 1)
            // Nothing multicam at the playhead: said so.
            p.seek(30)
            verify(!p.cutToAngle(0))
        }
    }
}
