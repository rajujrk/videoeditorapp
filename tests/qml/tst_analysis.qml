import QtQuick
import QtTest
import TechieVideoEditor.Theme
import TechieVideoEditor.Player
import TechieVideoEditor.Shell

// The Analyze tab: pauses, scene changes, the beat and ducking, through the real editor,
// on media the test harness writes (see writeAnalysisMedia in main.cpp).
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

    SignalSpy { id: finished; signalName: "analysisFinished" }

    TestCase {
        id: testCase
        name: "Analysis"
        when: windowShown

        function media(name) { return "file://" + analysisMediaDir + "/" + name }
        function createEditor() {
            const fixture = createTemporaryObject(editorComponent, root)
            verify(fixture)
            waitForRendering(fixture)
            finished.clear()
            finished.target = fixture.project
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
        // A clip of `file` (on the track addClip picks), selected, with the Analyze tab open.
        function withClip(f, file) {
            const p = f.project
            compare(p.importMedia([media(file)]), 1)
            verify(p.addClip(p.media[p.media.length - 1].id))
            let id = 0
            for (const track of p.tracks)
                for (const c of track.clips)
                    if (c.mediaId === p.media[p.media.length - 1].id)
                        id = c.id
            p.selectedClip = id
            click(f.editor, "inspectorTab:Analyze")
            return id
        }
        function clipsOn(p, kind) {
            const track = p.tracks.find((t) => t.kind === kind && (kind !== "video" || t.main))
            return track.clips
        }
        function waitFinished(count) {
            tryVerify(() => finished.count >= count, 20000, "the analysis never finished")
            compare(finished.signalArguments[count - 1][0], true, finished.signalArguments[count - 1][1])
        }

        function test_pauses_are_found_shown_and_cut_out() {
            const f = createEditor()
            const p = f.project
            const id = withClip(f, "talk.wav")
            click(f.editor, "findPauses")
            waitFinished(1)
            compare(p.silences.clipId, id)
            compare(p.silences.ranges.length, 2)
            fuzzyCompare(p.silences.ranges[0].start, 1.1, 0.02)
            fuzzyCompare(p.silences.ranges[0].end, 1.9, 0.02)
            fuzzyCompare(p.silences.ranges[1].start, 4.4, 0.02)
            fuzzyCompare(p.silences.ranges[1].end, 5.7, 0.02)
            fuzzyCompare(p.silences.total, 2.1, 0.05)
            verify(settled(f.editor, "pausesFound").text.indexOf("2 pauses") === 0)
            verify(findChild(f.editor, "pauseBand:1"))

            // Cutting them out leaves three pieces, 2.1 s shorter, as one undo step.
            click(f.editor, "removePauses")
            const pieces = clipsOn(p, "audio")
            compare(pieces.length, 3)
            fuzzyCompare(pieces[2].start + pieces[2].duration, 6.5 - 2.1, 0.05)
            compare(p.undoText, "Remove Pauses")
            compare(p.silences.ranges, undefined)
            verify(!findChild(f.editor, "pauseBand:0"))
            p.undo()
            compare(clipsOn(p, "audio").length, 1)

            // The second time it is instant: the measurements are kept.
            verify(p.findSilences(id, { minDuration: 0.25 }))
            verify(!p.analyzing)
            compare(p.silences.ranges.length, 3)
            // And so are they for the next editor, on disk.
            const g = createEditor()
            const q = g.project
            compare(q.importMedia([media("talk.wav")]), 1)
            verify(q.addClip(q.media[0].id))
            verify(q.findSilences(clipsOn(q, "audio")[0].id))
            verify(!q.analyzing)
            compare(q.silences.ranges.length, 2)
        }

        function test_scene_changes_split_or_mark() {
            const f = createEditor()
            const p = f.project
            const id = withClip(f, "scenes.mov")
            click(f.editor, "findScenes")
            waitFinished(1)
            compare(p.scenes.cuts.length, 2)
            fuzzyCompare(p.scenes.cuts[0], 1.0, 0.04)
            fuzzyCompare(p.scenes.cuts[1], 2.0, 0.04)
            verify(findChild(f.editor, "sceneCut:1"))
            verify(settled(f.editor, "scenesFound").text.indexOf("2 scene changes") === 0)

            click(f.editor, "splitAtScenes")
            compare(clipsOn(p, "video").length, 3)
            compare(p.undoText, "Split at Scenes")
            p.undo()
            compare(clipsOn(p, "video").length, 1)

            verify(p.findScenes(id))
            verify(p.markScenes())
            compare(p.markers.length, 2)
            compare(p.markers[0].note, "Scene 2")
            compare(p.markers[0].color, "green")
            // Low sensitivity: one shot.
            verify(p.findScenes(id, { threshold: 1.0 }))
            compare(p.scenes.cuts.length, 0)
        }

        function test_the_beat_becomes_markers() {
            const f = createEditor()
            const p = f.project
            withClip(f, "beat.wav")
            click(f.editor, "findBeats")
            waitFinished(1)
            fuzzyCompare(p.beats.bpm, 120, 1.5)
            verify(p.beats.times.length >= 14 && p.beats.times.length <= 16, p.beats.times.length)
            fuzzyCompare(p.beats.times[0], 0.25, 0.03)
            verify(settled(f.editor, "beatsFound").text.indexOf("120 BPM") === 0)
            click(f.editor, "markEveryBar")
            compare(p.markers.length, Math.ceil(p.beats.times.length / 4))
            fuzzyCompare(p.markers[1].time - p.markers[0].time, 2.0, 0.03)
            compare(p.undoText, "Add Beat Markers")
        }

        function test_music_is_lowered_under_speech() {
            const f = createEditor()
            const p = f.project
            compare(p.importMedia([media("talk.mov"), media("hum.wav")]), 2)
            verify(p.addClip(p.media[0].id)) // the talk, on the main video track
            verify(p.addClip(p.media[1].id)) // the music, on the audio track
            const music = clipsOn(p, "audio")[0].id
            p.selectedClip = music
            click(f.editor, "inspectorTab:Analyze")
            click(f.editor, "duckUnderSpeech")
            waitFinished(1)
            compare(p.undoText, "Auto Ducking")
            const keys = p.selectedClipInfo.keyframes.volume
            verify(keys.length >= 8, JSON.stringify(keys))
            // Down while talking, back up in the long pause (4.3–5.8 s), down again after.
            const valueAt = (t) => {
                let v = keys[0].value
                for (const k of keys)
                    if (k.time <= t + 1e-6)
                        v = k.value
                return v
            }
            fuzzyCompare(valueAt(0.5), -12, 0.01)
            fuzzyCompare(valueAt(1.5), 0, 0.01)
            fuzzyCompare(valueAt(3.15), -12, 0.01) // the short 0.3 s gap is bridged
            fuzzyCompare(valueAt(5.0), 0, 0.01)
            fuzzyCompare(valueAt(6.0), -12, 0.01)
            p.undo()
            compare(p.selectedClipInfo.keyframes.volume, undefined)
        }

        function test_cancelling_an_analysis() {
            const f = createEditor()
            const p = f.project
            const longClip = "file://" + samplesDir + "/4k60_h264.mp4" // 20 s of 4K60
            if (p.importMedia([longClip]) !== 1)
                skip("4k60_h264.mp4 missing: run tools/make_test_media.sh")
            verify(p.addClip(p.media[0].id))
            p.selectedClip = clipsOn(p, "video")[0].id
            click(f.editor, "inspectorTab:Analyze")
            verify(p.findScenes(p.selectedClip))
            verify(p.analyzing)
            verify(!p.findSilences(p.selectedClip)) // one at a time
            verify(settled(f.editor, "analysisTask").text.indexOf("Looking for scene changes") === 0)
            click(f.editor, "cancelAnalysis")
            tryVerify(() => finished.count === 1, 10000)
            compare(finished.signalArguments[0][0], false)
            compare(finished.signalArguments[0][1], "") // cancelled, not failed
            verify(!p.analyzing)
            compare(p.scenes.cuts, undefined)
        }

        function test_what_cannot_be_analysed_says_why() {
            const f = createEditor()
            const p = f.project
            const id = withClip(f, "talk.wav")
            verify(p.setClipSpeed(id, 2, false))
            verify(!p.findSilences(id))
            verify(p.lastError.indexOf("normal speed") >= 0, p.lastError)
            verify(settled(f.editor, "analyzePage"))
            p.undo()
            // Nothing else plays under it: nothing to duck under.
            verify(!p.duckUnderSpeech(id))
            verify(p.lastError.indexOf("No other clip") === 0, p.lastError)
            // Nothing found yet: nothing to apply.
            verify(!p.removeSilences())
            verify(!p.splitAtScenes())
            verify(!p.addBeatMarkers(1))
            // A file with no moving pictures has no scenes.
            verify(!p.findScenes(id))
        }
    }
}
