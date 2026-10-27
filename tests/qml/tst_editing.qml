import QtQuick
import QtTest
import TechieVideoEditor.Theme
import TechieVideoEditor.Player
import TechieVideoEditor.Shell

// End-to-end editing through the real editor UI and controller, using the sample videos
// from tools/make_test_media.sh (each 60 frames at 30 fps, i.e. 2 s).
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
        name: "Editing"
        when: windowShown

        readonly property string clipA: "file://" + samplesDir + "/test_1080p30_h264.mp4"
        readonly property string clipB: "file://" + samplesDir + "/test_720p30_mjpeg.mov"

        function initTestCase() {
            const probe = Qt.createQmlObject('import TechieVideoEditor.Player; ProjectController {}', testCase)
            if (probe.importMedia([clipA, clipB]) !== 2)
                skip("samples missing: run tools/make_test_media.sh")
            probe.destroy()
        }

        function createEditor() {
            const fixture = createTemporaryObject(editorComponent, root)
            verify(fixture)
            waitForRendering(fixture)
            return fixture
        }

        function clipsOnMainTrack(project) {
            const main = project.tracks.find((t) => t.main)
            return main.clips
        }

        function test_import_and_append() {
            const f = createEditor()
            const p = f.project
            verify(p.importAndAppend(clipA))
            verify(p.importAndAppend(clipB))
            compare(p.media.length, 2)
            compare(p.media[0].width, 1920)
            compare(p.media[1].height, 720)
            const clips = clipsOnMainTrack(p)
            compare(clips.length, 2)
            compare(clips[1].start, 2)
            compare(p.duration, 4)
            compare(p.timecode(p.duration), "00:00:04:00")
            verify(p.modified)
        }

        function test_split_button_and_undo() {
            const f = createEditor()
            const p = f.project
            p.importAndAppend(clipA)
            p.seek(0.5)
            mouseClick(findChild(f.editor, "splitButton"))
            let clips = clipsOnMainTrack(p)
            compare(clips.length, 2)
            compare(clips[0].duration, 0.5)
            compare(clips[1].start, 0.5)
            compare(clips[1].sourceIn, 0.5)
            compare(p.undoText, "Split Clip")

            mouseClick(findChild(f.editor, "undoButton"))
            compare(clipsOnMainTrack(p).length, 1)
            mouseClick(findChild(f.editor, "redoButton"))
            compare(clipsOnMainTrack(p).length, 2)
        }

        function test_delete_on_main_track_closes_gap() {
            const f = createEditor()
            const p = f.project
            p.importAndAppend(clipA)
            p.importAndAppend(clipB)
            const first = clipsOnMainTrack(p)[0].id
            p.selectedClip = first
            mouseClick(findChild(f.editor, "deleteButton"))
            const clips = clipsOnMainTrack(p)
            compare(clips.length, 1)
            compare(clips[0].start, 0) // the second clip moved left
            compare(p.duration, 2)
        }

        function test_drag_clip_to_overlay_track() {
            const f = createEditor()
            const p = f.project
            p.importAndAppend(clipA)
            const id = clipsOnMainTrack(p)[0].id
            const clip = findChild(f.editor, "clip:" + id)
            verify(clip)
            // The clip follows the cursor while dragging, so drive the mouse in the
            // coordinates of the ruler, which does not move (as a real cursor would).
            const ruler = findChild(f.editor, "ruler")
            // Wait until the rebuilt lanes are laid out: the clip sits in the second lane.
            const secondLaneTop = Theme.size.ruler + Theme.size.videoTrack
            tryVerify(() => clip.mapToItem(ruler, 0, 0).y >= secondLaneTop)
            const start = clip.mapToItem(ruler, clip.width / 2, clip.height / 2)
            // Drag up one track (Video 1 → Video 2) and right by 1 s at 60 px/s.
            mousePress(ruler, start.x, start.y)
            for (let step = 1; step <= 6; ++step)
                mouseMove(ruler, start.x + 10 * step, start.y - Theme.size.videoTrack * step / 6)
            mouseRelease(ruler, start.x + 60, start.y - Theme.size.videoTrack)
            const overlay = p.tracks.find((t) => t.name === "Video 2")
            compare(overlay.clips.length, 1)
            fuzzyCompare(overlay.clips[0].start, 1, 0.05)
            compare(clipsOnMainTrack(p).length, 0)
            compare(p.undoText, "Move Clip") // the whole drag is one undo step
        }

        function test_trim_end_edge() {
            const f = createEditor()
            const p = f.project
            p.importAndAppend(clipA)
            const id = clipsOnMainTrack(p)[0].id
            waitForRendering(f.editor)
            const clip = findChild(f.editor, "clip:" + id)
            const ruler = findChild(f.editor, "ruler")
            const edge = clip.mapToItem(ruler, clip.width - 3, clip.height / 2)
            // Drag the right edge 30 px (0.5 s) to the left.
            mousePress(ruler, edge.x, edge.y)
            for (let step = 1; step <= 3; ++step)
                mouseMove(ruler, edge.x - 10 * step, edge.y)
            mouseRelease(ruler, edge.x - 30, edge.y)
            fuzzyCompare(clipsOnMainTrack(p)[0].duration, 1.5, 0.05)
            compare(p.undoText, "Trim Clip")
        }

        function test_refused_edit_explains_why() {
            const f = createEditor()
            const p = f.project
            p.importAndAppend(clipA)
            p.seek(0) // at a clip edge there is nothing to split
            verify(!p.splitAtPlayhead())
            verify(p.lastError.length > 0)
            const toast = findChild(f.editor, "errorToast")
            tryVerify(() => toast.visible)
        }

        function test_play_advances_and_stops_at_end() {
            const f = createEditor()
            const p = f.project
            p.importAndAppend(clipA)
            mouseClick(findChild(f.editor, "playButton"))
            verify(p.playing)
            tryVerify(() => p.playhead > 0.3, 3000)
            tryVerify(() => !p.playing, 5000) // stops at the end
            compare(p.playhead, 2)
        }

        function test_step_frames_and_timecode() {
            const f = createEditor()
            const p = f.project
            p.importAndAppend(clipA)
            p.stepFrames(7)
            compare(p.timecode(p.playhead), "00:00:00:07")
            p.stepFrames(-2)
            compare(p.timecode(p.playhead), "00:00:00:05")
        }

        function test_save_and_reopen() {
            const f = createEditor()
            const p = f.project
            p.importAndAppend(clipA)
            p.importAndAppend(clipB)
            const file = "file://" + tempDir + "/tve_qml_roundtrip.tve"
            verify(p.save(file))
            verify(!p.modified)
            compare(p.name, "tve_qml_roundtrip")

            const g = createEditor()
            verify(g.project.open(file))
            compare(g.project.media.length, 2)
            compare(clipsOnMainTrack(g.project).length, 2)
            compare(g.project.duration, 4)
            verify(!g.project.canUndo) // history starts fresh
        }
    }
}
