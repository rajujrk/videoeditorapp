import QtQuick
import QtTest
import TechieVideoEditor.Theme
import TechieVideoEditor.Player
import TechieVideoEditor.Shell

// Everyday timeline tools through the real editor: J K L shuttle, typing a timecode, In and
// Out, the blade, closing gaps, track height, pasting attributes and transitions, audio
// crossfades.
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
        name: "TimelineTools"
        when: windowShown

        readonly property string av: "file://" + samplesDir + "/test_av_1080p30.mp4"
        function still(name) { return "file://" + stillsDir + "/" + name }

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
        // Three 2-second clips on the main track (0–6 s).
        function threeClips() {
            const f = createEditor()
            const p = f.project
            for (let i = 0; i < 3; ++i)
                verify(p.importAndAppend(av))
            compare(mainTrack(p).clips.length, 3)
            return f
        }
        function shortcut(f, name) {
            const s = findChild(f.editor, "shortcut:" + name)
            verify(s, name)
            verify(s.enabled, name + " is off")
            s.activated()
        }

        function test_jkl_shuttle() {
            const f = threeClips()
            const p = f.project
            p.seek(3)
            shortcut(f, "shuttleForward") // L: play
            compare(p.shuttleRate, 1)
            verify(p.playing)
            shortcut(f, "shuttleForward") // L again: 2× (through frames)
            compare(p.shuttleRate, 2)
            verify(findChild(f.editor, "shuttleBadge").visible)
            shortcut(f, "shuttleStop") // K
            compare(p.shuttleRate, 0)
            verify(!p.playing)
            // J: backwards; again: twice as fast; the playhead goes back.
            const from = p.playhead
            shortcut(f, "shuttleBack")
            compare(p.shuttleRate, -1)
            shortcut(f, "shuttleBack")
            compare(p.shuttleRate, -2)
            tryVerify(() => p.playhead < from - 0.3, 3000, "the playhead did not go back")
            shortcut(f, "shuttleStop")
            compare(p.shuttleRate, 0)
            // Space after shuttling plays forwards at 1×.
            p.togglePlay()
            compare(p.shuttleRate, 1)
            p.pause()
        }

        function test_typing_a_timecode_goes_there() {
            const f = threeClips()
            const p = f.project
            fuzzyCompare(p.parseTimecode("00:00:04:15"), 4.5, 0.001)
            fuzzyCompare(p.parseTimecode("4:15"), 4.5, 0.001)
            fuzzyCompare(p.parseTimecode("45"), 1.5, 0.001) // frames
            fuzzyCompare(p.parseTimecode("2.25"), 2.25, 0.001)
            compare(p.parseTimecode("abc"), -1)
            // Click the timecode, type, Return.
            mouseClick(findChild(f.editor, "playheadTimecode"))
            const field = findChild(f.editor, "timecodeField")
            tryVerify(() => field.visible && field.activeFocus)
            field.text = "00:00:03:10"
            field.accepted()
            fuzzyCompare(p.playhead, 3 + 10 / 30, 0.02)
            verify(!field.visible)
        }

        function test_in_and_out() {
            const f = threeClips()
            const p = f.project
            p.seek(1)
            shortcut(f, "markIn")
            p.seek(3)
            shortcut(f, "markOut")
            fuzzyCompare(p.markIn, 1, 0.001)
            fuzzyCompare(p.markOut, 3, 0.001)
            verify(findChild(f.editor, "markedRange").visible)
            // An Out before In clears In.
            p.setMarkOut(0.5)
            compare(p.markIn, -1)
            p.setMarkIn(1)
            p.setMarkOut(3)
            // The export dialog offers the range.
            const options = { format: "mp4-h264", range: "marks", width: 320, height: 180 }
            fuzzyCompare(p.exportRange(options).duration, 2, 0.05)
            // Playing it stops at Out.
            verify(p.playMarkedRange())
            fuzzyCompare(p.playhead, 1, 0.05)
            tryVerify(() => !p.playing, 5000, "it did not stop at Out")
            fuzzyCompare(p.playhead, 3, 0.05)
            // Deleting it: two seconds out of the timeline, the marks cleared.
            verify(p.deleteMarkedRange())
            fuzzyCompare(p.duration, 4, 0.05)
            compare(p.markIn, -1)
            compare(p.undoText, "Delete the Range")
            p.undo()
            fuzzyCompare(p.duration, 6, 0.05)
            shortcut(f, "markIn")
            shortcut(f, "clearMarks")
            compare(p.markIn, -1)
        }

        function test_the_blade_cuts_where_clicked() {
            const f = threeClips()
            const p = f.project
            const timeline = findChild(f.editor, "bladeToolButton")
            shortcut(f, "blade")
            verify(timeline.checked)
            const clip = mainTrack(p).clips[0]
            const item = findChild(f.editor, "clip:" + clip.id)
            waitForPolish(item, 1000)
            // Hovering shows the cut line and moves nothing.
            const x = item.x
            mouseMove(item, 40, item.height / 2)
            tryVerify(() => findChild(item, "bladeLine").visible)
            compare(item.x, x)
            compare(item.y, 3)
            // Half a second in (60 px a second; the clip's body starts 6 px in).
            mouseClick(item, 30, item.height / 2)
            compare(mainTrack(p).clips.length, 4)
            fuzzyCompare(mainTrack(p).clips[0].duration, 0.5, 0.04)
            compare(p.undoText, "Split Clip")
            shortcut(f, "selectTool")
            verify(!timeline.checked)
            verify(findChild(f.editor, "selectToolButton").checked)
        }

        function test_gaps_close_and_tracks_change_height() {
            const f = createEditor()
            const p = f.project
            compare(p.importMedia([still("disc.png")]), 1)
            const above = p.tracks[p.tracks.findIndex((t) => t.main) - 1]
            verify(p.addClip(p.media[0].id, above.id, 1)) // 1–6 s
            verify(p.addClip(p.media[0].id, above.id, 8)) // 8–13 s
            verify(p.addClip(p.media[0].id, above.id, 15)) // 15–20 s
            // The one gap at 7 s; then every gap left; a lead-in stays.
            verify(p.closeGapAt(above.id, 7))
            const after = p.tracks.find((t) => t.id === above.id).clips
            fuzzyCompare(after[1].start, 6, 0.001)
            fuzzyCompare(after[2].start, 13, 0.001)
            compare(p.closeGaps(above.id), 1)
            const closed = p.tracks.find((t) => t.id === above.id).clips
            fuzzyCompare(closed[0].start, 1, 0.001)
            fuzzyCompare(closed[2].start, 11, 0.001)
            compare(p.closeGaps(above.id), 0) // none left

            // Track height from a track's menu.
            const header = findChild(f.editor, "trackHeader:Video 1")
            const normal = header.height
            const panel = findChild(f.editor, "timelineMenus").timeline
            panel.trackScale = 1.5
            tryVerify(() => header.height > normal * 1.4)
            panel.trackScale = 0.7
            tryVerify(() => header.height < normal * 0.75)
        }

        function test_attributes_and_transitions_paste_onto_other_clips() {
            const f = threeClips()
            const p = f.project
            const clips = mainTrack(p).clips
            verify(p.setClipVisual(clips[0].id, { scale: 0.5, x: 0.2, opacity: 0.7 }))
            verify(p.setClipSpeed(clips[0].id, 2, false))
            verify(p.copyAttributes(clips[0].id))
            verify(p.hasCopiedAttributes)
            // Position and size only, onto the third clip.
            p.selectedClip = clips[2].id
            verify(p.pasteAttributes(["visual"]))
            compare(p.undoText, "Paste Attributes")
            fuzzyCompare(p.selectedClipInfo.visual.scale, 0.5, 0.001)
            fuzzyCompare(p.selectedClipInfo.visual.opacity, 0.7, 0.001)
            compare(p.selectedClipInfo.speed, 1)
            // With speed too: half as long.
            verify(p.pasteAttributes(["speed"]))
            compare(p.selectedClipInfo.speed, 2)
            fuzzyCompare(p.selectedClipInfo.duration, 1, 0.01)

            // A transition copied from one cut onto another.
            const second = mainTrack(p).clips[1].id
            verify(p.addTransition("tve.transition.dissolve", second))
            verify(p.copyTransition(second))
            const third = mainTrack(p).clips[2].id
            verify(p.pasteTransition(third))
            verify(Object.keys(mainTrack(p).clips[2].transition).length > 0)
        }

        function test_sound_clips_crossfade() {
            const f = createEditor()
            const p = f.project
            verify(p.importAndAppend(av))
            verify(p.importAndAppend(av))
            const main = mainTrack(p).clips
            verify(p.detachAudio(main[0].id))
            verify(p.detachAudio(main[1].id))
            const audio = p.tracks.find((t) => t.kind === "audio" && t.clips.length === 2)
            verify(audio)
            verify(p.addTransition("tve.transition.dissolve", audio.clips[1].id))
            const track = p.tracks.find((t) => t.id === audio.id)
            verify(Object.keys(track.clips[1].transition).length > 0)
            tryVerify(() => findChild(f.editor, "transition:" + track.clips[1].id) !== null)
        }
    }
}
