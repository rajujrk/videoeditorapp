import QtQuick
import QtTest
import TechieVideoEditor.Theme
import TechieVideoEditor.Player
import TechieVideoEditor.Shell

// Audio effects, keep pitch, loudness and the mixer through the real editor (no sound card needed).
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
        name: "AudioMix"
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
        // The A/V sample on the main track, selected.
        function withClip() {
            const f = createEditor()
            const p = f.project
            verify(p.importAndAppend(av))
            p.selectedClip = p.tracks.find((t) => t.main).clips[0].id
            return f
        }

        function test_audio_effects_on_a_clip() {
            const f = withClip()
            const p = f.project
            compare(p.audioEffects.length, 40)
            compare(p.audioEffects[0].categories[0], "eq")
            compare(p.audioEffects[p.audioEffects.length - 1].categories[0], "voice")
            click(f.editor, "inspectorTab:Audio")
            const picker = settled(f.editor, "audioEffectPicker")
            const robot = p.audioEffects.findIndex((e) => e.id === "tve.audio.robot")
            picker.currentIndex = robot
            compare(picker.currentText, "Voice · Robot")
            click(f.editor, "addAudioEffectButton")
            compare(p.undoText, "Add Audio Effect")
            compare(p.selectedClipInfo.audioEffects.length, 1)
            compare(p.selectedClipInfo.audioEffects[0].effectId, "tve.audio.robot")
            // Its settings are on its card; a change is one undo step.
            verify(settled(f.editor, "audioEffectParam:0:frequency"))
            verify(p.setAudioEffectParam(p.selectedClip, 0, "frequency", 120))
            compare(p.selectedClipInfo.audioEffects[0].values.frequency, 120)
            compare(p.undoText, "Change Audio Effect")
            verify(p.setAudioEffectParam(p.selectedClip, 0, "frequency", 5000)) // clamped to the range
            compare(p.selectedClipInfo.audioEffects[0].values.frequency, 200)
            verify(!p.setAudioEffectParam(p.selectedClip, 0, "colour", 1))
            // Off, then removed.
            click(f.editor, "audioEffect:0")
            compare(p.selectedClipInfo.audioEffects[0].enabled, false)
            compare(p.undoText, "Disable Audio Effect")
            verify(p.addAudioEffect(p.selectedClip, "tve.audio.hall"))
            compare(p.selectedClipInfo.audioEffects.length, 2)
            click(f.editor, "removeAudioEffect:0")
            compare(p.selectedClipInfo.audioEffects.length, 1)
            compare(p.selectedClipInfo.audioEffects[0].effectId, "tve.audio.hall")
            verify(!p.addAudioEffect(p.selectedClip, "tve.audio.unknown"))
            // Detached sound takes its effects to the audio clip.
            verify(p.detachAudio(p.selectedClip))
            const audioClip = p.tracks.find((t) => t.kind === "audio" && t.clips.length > 0).clips[0]
            p.selectedClip = audioClip.id
            compare(p.selectedClipInfo.audioEffects[0].effectId, "tve.audio.hall")
        }

        function test_keep_pitch() {
            const f = withClip()
            const p = f.project
            verify(p.selectedClipInfo.keepPitch)
            click(f.editor, "inspectorTab:Speed")
            const keep = settled(f.editor, "keepPitchSwitch")
            verify(keep.checked)
            click(f.editor, "keepPitchSwitch")
            verify(!p.selectedClipInfo.keepPitch)
            compare(p.undoText, "Pitch Follows Speed")
            p.undo()
            verify(p.selectedClipInfo.keepPitch)
            // Reversed sound always follows the speed.
            verify(p.setClipSpeed(p.selectedClip, 1, true))
            tryVerify(() => !findChild(f.editor, "keepPitchSwitch").enabled)
        }

        function test_normalize_loudness() {
            const f = withClip()
            const p = f.project
            click(f.editor, "inspectorTab:Audio")
            const target = settled(f.editor, "loudnessTarget")
            target.currentIndex = 2 // EBU R128, -23 LUFS
            click(f.editor, "normalizeLoudnessButton")
            verify(p.measuringLoudness)
            tryVerify(() => !p.measuringLoudness, 10000)
            compare(p.undoText, "Normalize Loudness")
            const loudness = p.selectedClipInfo.loudness
            verify(isFinite(loudness.measured))
            compare(loudness.target, -23)
            fuzzyCompare(p.selectedClipInfo.gainDb, Math.max(-60, Math.min(12, -23 - loudness.measured)), 0.01)
            verify(settled(f.editor, "loudnessResult").text.indexOf("-23 LUFS") >= 0
                   || findChild(f.editor, "loudnessResult").text.indexOf("−23") >= 0
                   || findChild(f.editor, "loudnessResult").text.indexOf("23 LUFS") >= 0)
            p.undo()
            compare(p.selectedClipInfo.gainDb, 0)
            // Animated volume is left alone.
            verify(p.addKeyframes(p.selectedClip, ["volume"]))
            verify(!p.normalizeLoudness(p.selectedClip, -14))
            verify(p.lastError.indexOf("keyframes") >= 0)
        }

        function test_mixer_strips_and_meters() {
            const f = withClip()
            const p = f.project
            verify(p.importMedia([toneWav]) >= 1)
            const audioTrack = p.tracks.find((t) => t.kind === "audio")
            verify(p.addClip(p.media.find((m) => m.name === "test_tone_440.wav").id, audioTrack.id, 0))
            click(f.editor, "mixerButton")
            const panel = findChild(f.editor, "mixerPanel")
            tryVerify(() => panel.opened)
            const strips = panel.contentItem // a popup's items live under its content item
            // A strip per track with sound (titles have none), and the master.
            for (const t of p.tracks)
                compare(findChild(strips, "mixerStrip:" + t.id) !== null, t.kind !== "title")
            verify(settled(strips, "mixerMaster"))
            compare(findChild(strips, "masterLufs:momentary").text, "— LUFS")

            // Volume, balance and solo; mute shares the timeline's flag.
            verify(p.setTrackMix(audioTrack.id, { gainDb: -6, pan: -0.5 }))
            compare(p.undoText, "Change Track Mix")
            const updated = p.tracks.find((t) => t.id === audioTrack.id)
            compare(updated.gainDb, -6)
            compare(updated.pan, -0.5)
            tryCompare(findChild(strips, "trackGain:" + audioTrack.id), "value", -6)
            clickWhenSettled(strips, "trackSolo:" + audioTrack.id)
            verify(p.tracks.find((t) => t.id === audioTrack.id).solo)
            clickWhenSettled(strips, "trackMute:" + audioTrack.id)
            verify(p.tracks.find((t) => t.id === audioTrack.id).muted)
            clickWhenSettled(strips, "trackMute:" + audioTrack.id)
            clickWhenSettled(strips, "trackSolo:" + audioTrack.id)
            verify(!p.tracks.find((t) => t.id === audioTrack.id).solo)

            // Playing, the meters move and the loudness is read.
            p.play()
            tryVerify(() => p.mixLevels.master !== undefined && p.mixLevels.master[0] > 0.05, 5000, "the master meter never moved")
            tryVerify(() => p.mixLevels.momentary !== undefined && p.mixLevels.momentary !== null, 5000)
            const level = p.mixLevels.tracks[String(audioTrack.id)]
            verify(level[0] > level[1]) // panned left
            tryVerify(() => findChild(strips, "masterLufs:momentary").text !== "— LUFS", 3000)
            p.pause()
            tryVerify(() => p.mixLevels.master === undefined, 3000, "the meters did not stop with play")
        }

        function test_export_normalizes_loudness() {
            const f = withClip()
            const p = f.project
            const file = "file://" + tempDir + "/tve_loudness_export.mp4"
            exportDone.target = p
            exportDone.clear()
            verify(p.exportVideo(file, { width: 640, height: 360, normalizeLoudness: true, loudnessTarget: -16 }))
            exportDone.wait(60000)
            compare(exportDone.signalArguments[0][0], true)
            // The export measures where it is told: about -16 LUFS.
            verify(p.importMedia([file]) >= 1)
            const exported = p.media.find((m) => m.name === "tve_loudness_export.mp4")
            verify(p.addClip(exported.id, p.tracks.find((t) => t.main).id))
            p.selectedClip = p.tracks.find((t) => t.main).clips[1].id
            verify(p.normalizeLoudness(p.selectedClip, -16))
            tryVerify(() => !p.measuringLoudness, 10000)
            fuzzyCompare(p.selectedClipInfo.loudness.measured, -16, 1.0)
        }
    }
    SignalSpy { id: exportDone; signalName: "exportFinished" }
}
