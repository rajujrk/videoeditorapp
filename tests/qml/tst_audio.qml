import QtQuick
import QtTest
import TechieVideoEditor.Theme
import TechieVideoEditor.Player
import TechieVideoEditor.Shell

// Audio editing through the real editor UI (TVE_AUDIO=null: no sound card needed).
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
        name: "Audio"
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
            return item
        }
        function clickWhenSettled(editor, name) {
            mouseClick(settled(editor, name))
        }

        function test_import_probes_sound() {
            const f = createEditor()
            const p = f.project
            p.importMedia([av, toneWav])
            compare(p.media[0].hasVideo, true)
            compare(p.media[0].hasAudio, true)
            compare(p.media[1].hasVideo, false)
            compare(p.media[1].hasAudio, true)
            fuzzyCompare(p.media[1].duration, 3, 0.01)
        }

        function test_sound_only_media_goes_to_an_audio_track() {
            const f = createEditor()
            const p = f.project
            verify(p.importAndAppend(toneWav))
            compare(track(p, "Audio 1").clips.length, 1)
            compare(track(p, "Video 1").clips.length, 0)
            // and it gets a waveform on the timeline
            const id = track(p, "Audio 1").clips[0].id
            const waveform = findChild(f.editor, "waveform:" + id)
            verify(waveform)
            tryVerify(() => waveform.ready, 5000)
        }

        function test_inspector_volume_and_fades() {
            const f = createEditor()
            const p = f.project
            p.importAndAppend(toneWav)
            p.selectedClip = track(p, "Audio 1").clips[0].id
            clickWhenSettled(f.editor, "inspectorTab:Audio")
            waitForRendering(f.editor)

            verify(p.setClipAudio(p.selectedClip, -6, 0.5, 1))
            compare(p.selectedClipInfo.gainDb, -6)
            compare(p.selectedClipInfo.fadeIn, 0.5)
            compare(p.undoText, "Change Audio")

            // Releasing the volume slider commits exactly one edit.
            const slider = settled(f.editor, "volumeSlider")
            const before = p.undoText
            mouseClick(slider, slider.width * 0.9, slider.height - 8)
            verify(p.selectedClipInfo.gainDb > 0)
            compare(p.undoText, "Change Audio")

            verify(!p.setClipAudio(p.selectedClip, 0, 2, 2)) // 4 s of fades on a 3 s clip
            verify(p.lastError.indexOf("fit") >= 0)
        }

        function test_detach_audio_button() {
            const f = createEditor()
            const p = f.project
            p.importAndAppend(av)
            const videoClip = track(p, "Video 1").clips[0].id
            p.selectedClip = videoClip
            clickWhenSettled(f.editor, "inspectorTab:Audio")
            waitForRendering(f.editor)
            clickWhenSettled(f.editor, "detachAudioButton")

            compare(track(p, "Audio 1").clips.length, 1)
            compare(track(p, "Video 1").clips[0].audioEnabled, false)
            compare(p.selectedClip, track(p, "Audio 1").clips[0].id) // selection follows the sound
            p.undo()
            compare(track(p, "Audio 1").clips.length, 0)
            compare(track(p, "Video 1").clips[0].audioEnabled, true)
        }

        function test_track_header_buttons() {
            const f = createEditor()
            const p = f.project
            p.importAndAppend(av)
            waitForRendering(f.editor)

            clickWhenSettled(f.editor, "mute:Video 1")
            compare(track(p, "Video 1").muted, true)
            compare(p.undoText, "Mute Track")

            clickWhenSettled(f.editor, "hide:Video 1")
            compare(track(p, "Video 1").hidden, true)

            clickWhenSettled(f.editor, "lock:Video 1")
            compare(track(p, "Video 1").locked, true)
            // Edits on a locked track are refused with a reason.
            verify(!p.deleteClip(track(p, "Video 1").clips[0].id))
            compare(p.lastError, "track is locked")
            clickWhenSettled(f.editor, "lock:Video 1")
            compare(track(p, "Video 1").locked, false)
        }

        function test_meter_moves_while_playing() {
            const f = createEditor()
            const p = f.project
            p.importAndAppend(toneWav) // 0.5 amplitude sine
            p.play()
            tryVerify(() => p.levelLeft > 0.4 && p.levelRight > 0.4, 3000)
            verify(p.levelLeft < 0.55)
            p.pause()
            tryVerify(() => p.levelLeft === 0, 2000)
        }

        function test_muted_track_is_silent() {
            const f = createEditor()
            const p = f.project
            p.importAndAppend(toneWav)
            p.setTrackFlag(track(p, "Audio 1").id, "muted", true)
            p.play()
            wait(400)
            compare(p.levelLeft, 0)
            p.pause()
        }
    }
}
