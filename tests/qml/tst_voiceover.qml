import QtQuick
import QtTest
import TechieVideoEditor.Theme
import TechieVideoEditor.Player
import TechieVideoEditor.Shell

// Voice-over through the real editor. The input is the null device (real-time silence),
// so takes have real lengths without a microphone or a permission prompt.
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
        name: "VoiceOver"
        when: windowShown

        readonly property string av: "file://" + samplesDir + "/test_av_1080p30.mp4"
        readonly property string tone: "file://" + samplesDir + "/test_tone_440.wav"

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
            return item
        }
        function click(item) {
            mouseMove(item)
            mouseClick(item)
        }

        // Opens the panel from the timeline toolbar; returns it.
        function openPanel(f) {
            click(settled(f.editor, "voiceOverButton"))
            const panel = findChild(f.editor, "voiceOverPanel")
            verify(panel)
            tryCompare(panel, "opened", true)
            tryCompare(f.project, "voiceOverActive", true)
            return panel
        }
        function inPanel(panel, name) { return settled(panel.contentItem, name) }

        function trackNamed(project, name) {
            return project.tracks.find((t) => t.name === name)
        }
        function clipById(project, id) {
            for (const track of project.tracks) {
                const clip = track.clips.find((c) => c.id === id)
                if (clip)
                    return { track: track, clip: clip }
            }
            return null
        }

        function test_record_a_take_from_the_panel() {
            const f = createEditor()
            const p = f.project
            compare(p.importMedia([av]), 1)
            verify(p.addClip(p.media[0].id))
            p.seek(1.0)

            const panel = openPanel(f)
            verify(findChild(f.editor, "voiceOverButton").checked)
            verify(p.inputDevices.length > 0)
            compare(p.microphoneAccess, "granted")
            compare(inPanel(panel, "recordingStatus").text, "Ready")
            click(inPanel(panel, "countIn")) // straight in
            verify(!panel.countIn)

            click(inPanel(panel, "recordButton"))
            tryCompare(p, "recording", true)
            verify(p.playing)
            compare(p.recordingStart, 1.0)
            compare(inPanel(panel, "recordingStatus").text, "Recording")
            verify(settled(f.editor, "recordingRange"))
            compare(panel.closePolicy, 0) // stays open while recording
            tryVerify(() => p.recordingTime > 0.5, 3000)
            // The take is tied to where it started: seeking is ignored.
            const before = p.playhead
            p.seek(0)
            verify(p.playhead >= before)

            click(inPanel(panel, "recordButton"))
            tryCompare(p, "recording", false)
            verify(!p.playing)
            compare(p.undoText, "Record Voice-over")

            // On the free audio track (the video clip's own sound stays on it), selected.
            const found = clipById(p, p.selectedClip)
            verify(found)
            compare(found.track.name, "Audio 1")
            compare(found.clip.start, 1.0)
            verify(found.clip.duration > 0.5)
            verify(found.clip.path.indexOf(recordingsDir) === 0)
            verify(found.clip.path.endsWith(".wav"))
            const media = p.media.find((m) => m.id === found.clip.mediaId)
            verify(media)
            verify(media.hasAudio)
            verify(!media.hasVideo)

            p.undo() // one step takes it all away
            verify(!clipById(p, found.clip.id))
            verify(!p.media.find((m) => m.id === found.clip.mediaId))
            p.redo()
            verify(clipById(p, found.clip.id))

            panel.close()
            tryCompare(p, "voiceOverActive", false)
            verify(!findChild(f.editor, "voiceOverButton").checked)
        }

        function test_count_in_then_stop_with_space() {
            const f = createEditor()
            const p = f.project
            const panel = openPanel(f)
            verify(panel.countIn)
            click(inPanel(panel, "recordButton"))
            compare(panel.stage, "countdown")
            compare(inPanel(panel, "recordingStatus").text, "Starting in 3…")
            verify(!p.recording)
            tryCompare(p, "recording", true, 5000)
            wait(300)
            // Space toggles play; pausing ends the take.
            p.togglePlay()
            tryCompare(p, "recording", false)
            compare(p.undoText, "Record Voice-over")
            const found = clipById(p, p.selectedClip)
            verify(found)
            compare(found.clip.start, 0)
            // An empty timeline grows to hold the take.
            verify(p.duration > 0.2)
        }

        function test_cancel_the_count_in() {
            const f = createEditor()
            const p = f.project
            const panel = openPanel(f)
            const button = inPanel(panel, "recordButton")
            click(button)
            compare(panel.stage, "countdown")
            click(button)
            compare(panel.stage, "idle")
            wait(1200)
            verify(!p.recording)
            verify(!p.canUndo)
        }

        function test_a_busy_audio_track_gets_a_voice_over_track() {
            const f = createEditor()
            const p = f.project
            compare(p.importMedia([tone]), 1)
            verify(p.addClip(p.media[0].id)) // Audio 1, 0–3 s
            compare(clipById(p, p.selectedClip).track.name, "Audio 1")
            p.seek(1.0)
            verify(p.openVoiceOver())
            verify(p.startRecording())
            wait(400)
            verify(p.stopRecording())
            const found = clipById(p, p.selectedClip)
            compare(found.track.name, "Voice-over")
            compare(found.track.kind, "audio")
            compare(found.clip.start, 1.0)
            // Below every other track.
            compare(p.tracks[p.tracks.length - 1].name, "Voice-over")

            // The next take at the same place goes on that track if it has room after.
            p.seek(found.clip.start + found.clip.duration + 0.5)
            verify(p.startRecording())
            wait(300)
            verify(p.stopRecording())
            compare(clipById(p, p.selectedClip).track.name, "Voice-over")
            p.closeVoiceOver()
        }

        function test_a_take_past_the_end_keeps_going() {
            const f = createEditor()
            const p = f.project
            compare(p.importMedia([tone]), 1)
            verify(p.addClip(p.media[0].id, -1, 0)) // 3 s long
            p.seek(2.8)
            verify(p.openVoiceOver())
            verify(p.startRecording())
            tryVerify(() => p.playhead > 3.6, 3000) // playback did not stop at the end
            verify(p.recording)
            verify(p.stopRecording())
            // The take runs past the end too. (A take is a little shorter than the time it
            // ran: its first device period is trimmed and the last may still be in flight.)
            verify(p.duration > 3.3, "duration " + p.duration)
            p.closeVoiceOver()
        }

        function test_closing_the_panel_keeps_the_take() {
            const f = createEditor()
            const p = f.project
            const panel = openPanel(f)
            verify(p.startRecording())
            wait(300)
            p.closeVoiceOver()
            verify(!p.recording)
            verify(!p.voiceOverActive)
            compare(p.undoText, "Record Voice-over")
            panel.close()
        }

        function test_recording_needs_an_open_input_and_one_take_at_a_time() {
            const f = createEditor()
            const p = f.project
            verify(!p.recording)
            verify(!p.stopRecording())
            verify(p.startRecording()) // opens the input itself
            verify(p.voiceOverActive)
            verify(!p.startRecording())
            wait(200)
            verify(p.stopRecording())
            compare(p.inputDevice, p.inputDevices[0])
            p.inputDevice = p.inputDevice // no change, no reopen
            verify(p.voiceOverActive)
            p.closeVoiceOver()
            compare(p.inputLevel, 0)
        }

        function test_a_controller_destroyed_mid_take_keeps_it() {
            const p = Qt.createQmlObject('import TechieVideoEditor.Player; ProjectController {}', testCase)
            verify(p.startRecording())
            wait(300)
            p.destroy()
            wait(0)
            // No crash, and nothing left recording: a new take starts cleanly.
            const q = Qt.createQmlObject('import TechieVideoEditor.Player; ProjectController {}', testCase)
            verify(q.startRecording())
            wait(200)
            verify(q.stopRecording())
            q.destroy()
            wait(0)
        }
    }
}
