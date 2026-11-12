import QtQuick
import QtTest
import TechieVideoEditor.Theme
import TechieVideoEditor.Player
import TechieVideoEditor.Shell

// Batch gain: several selected clips turned up or down, set to one volume, or each normalized
// to a loudness, together, from the Audio tab.
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

    SignalSpy { id: normalized; signalName: "clipsNormalized" }

    TestCase {
        id: testCase
        name: "BatchGain"
        when: windowShown

        readonly property string av: "file://" + samplesDir + "/test_av_1080p30.mp4"

        function createEditor() {
            const fixture = createTemporaryObject(editorComponent, root)
            verify(fixture)
            waitForRendering(fixture)
            normalized.clear()
            normalized.target = fixture.project
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
            }, 5000, name + " never settled")
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
        function clips(p) {
            let all = []
            for (const track of p.tracks)
                all = all.concat(track.clips)
            return all
        }
        function gainOf(p, id) { return clips(p).find((c) => c.id === id).gainDb }
        function select(p, ids) {
            p.selectedClip = ids[0]
            for (const id of ids.slice(1))
                p.toggleClipSelection(id)
            compare(p.selectedClips.length, ids.length)
        }

        function test_change_and_set_the_volume_of_several_clips() {
            const f = createEditor()
            const p = f.project
            for (let i = 0; i < 3; ++i)
                verify(p.importAndAppend(av))
            const main = p.tracks.find((t) => t.main).clips.map((c) => c.id)
            compare(main.length, 3)
            verify(p.addTitle({ text: "No sound" }, -1, 0))
            const title = p.selectedClip
            select(p, main.concat([title]))
            click(f.editor, "inspectorTab:Audio")
            const section = settled(f.editor, "batchGainSection")
            compare(section.title, "4 clips selected")
            compare(section.withSound, 3)
            // Down 6 dB, all at once: one undo step.
            settled(f.editor, "batchGainAmount").value = -6
            click(f.editor, "batchGainChange")
            for (const id of main)
                fuzzyCompare(gainOf(p, id), -6, 1e-4)
            compare(p.undoText, "Change Volume")
            compare(settled(f.editor, "batchGainAmount").value, 0) // ready for the next change
            p.undo()
            for (const id of main)
                compare(gainOf(p, id), 0)

            // Animated volume moves with it; setting one volume leaves it.
            p.selectedClip = main[1]
            p.seek(0.5)
            verify(p.addKeyframes(main[1], ["volume"]))
            const before = p.selectedClipInfo.keyframes.volume.map((k) => k.value)
            select(p, main)
            compare(p.changeClipsGain(p.selectedClips, 4), 3)
            p.selectedClip = main[1]
            const after = p.selectedClipInfo.keyframes.volume.map((k) => k.value)
            for (let i = 0; i < before.length; ++i)
                fuzzyCompare(after[i], before[i] + 4, 1e-4)
            select(p, main)
            settled(f.editor, "batchGainAmount").value = -10
            click(f.editor, "batchGainSet")
            fuzzyCompare(gainOf(p, main[0]), -10, 1e-4)
            fuzzyCompare(gainOf(p, main[2]), -10, 1e-4)
            fuzzyCompare(gainOf(p, main[1]), 4, 1e-4) // animated: as it was
            compare(p.lastError, "A clip with animated volume was left as it was")
            // Kept within −60 … +12 dB.
            compare(p.changeClipsGain(p.selectedClips, 40), 3)
            fuzzyCompare(gainOf(p, main[0]), 12, 1e-4)
            compare(p.changeClipsGain([title], 3), 0) // a title has no sound
        }

        function test_normalize_each_clip_to_a_loudness() {
            const f = createEditor()
            const p = f.project
            // A tone at −20 dBFS and the same file 10 dB quieter by its volume: both reach −16 LUFS.
            compare(p.importMedia(["file://" + analysisMediaDir + "/hum.wav", "file://" + analysisMediaDir + "/talk.wav"]), 2)
            const audio = p.tracks.find((t) => t.kind === "audio")
            verify(p.addClip(p.media[0].id, audio.id, 0))
            verify(p.addClip(p.media[1].id, audio.id, 8))
            const ids = clips(p).map((c) => c.id)
            compare(ids.length, 2)
            select(p, ids)
            click(f.editor, "inspectorTab:Audio")
            const target = settled(f.editor, "batchLoudnessTarget")
            target.currentIndex = 1 // −16 LUFS
            click(f.editor, "batchNormalize")
            tryVerify(() => normalized.count === 1, 20000, "never normalized")
            compare(normalized.signalArguments[0][0], 2)
            compare(normalized.signalArguments[0][1], 0)
            compare(p.undoText, "Normalize Loudness")
            verify(settled(f.editor, "batchNormalizeResult").text.indexOf("2 clips normalized") === 0)
            // Each measured on its own and set to make up the difference.
            for (const id of ids) {
                p.selectedClip = id
                const l = p.selectedClipInfo.loudness
                fuzzyCompare(l.target, -16, 1e-6)
                fuzzyCompare(gainOf(p, id), -16 - l.measured, 0.05)
            }
            verify(Math.abs(gainOf(p, ids[0]) - gainOf(p, ids[1])) > 1) // different clips, different gains
            p.undo()
            for (const id of ids)
                compare(gainOf(p, id), 0)
            verify(!p.normalizeClips([], -16))
        }
    }
}
