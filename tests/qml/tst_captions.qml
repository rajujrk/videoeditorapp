import QtQuick
import QtTest
import TechieVideoEditor.Theme
import TechieVideoEditor.Player
import TechieVideoEditor.Shell

// Captions: the speech model download (checked), captions from speech on the caption track,
// and SRT/VTT out and in, through the real editor. The model comes from a local folder the
// harness sets up (tools/fetch_models.sh fetches the real one; without it these tests skip).
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

    SignalSpy { id: downloaded; signalName: "modelDownloadFinished" }
    SignalSpy { id: finished; signalName: "analysisFinished" }

    TestCase {
        id: testCase
        name: "Captions"
        when: windowShown

        readonly property string speech: "file://" + samplesDir + "/speech_en.wav"

        function init() {
            if (!speechModelAvailable)
                skip("speech model missing: run tools/fetch_models.sh")
        }
        function createEditor() {
            const fixture = createTemporaryObject(editorComponent, root)
            verify(fixture)
            waitForRendering(fixture)
            downloaded.clear()
            downloaded.target = fixture.project
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
        function model(p, id) { return p.speechModels.find((m) => m.id === id) }
        // The base English model, downloaded (from the local folder) if it is not yet.
        function withModel(p) {
            if (model(p, "base.en-q5_1").downloaded)
                return
            const before = downloaded.count
            verify(p.downloadSpeechModel("base.en-q5_1"))
            tryVerify(() => downloaded.count > before, 30000)
            verify(model(p, "base.en-q5_1").downloaded)
        }
        function captionTrack(p) { return p.tracks.find((t) => t.captions) }

        function test_a_model_downloads_are_checked() {
            const f = createEditor()
            const p = f.project
            compare(p.speechModels.length, 5)
            compare(model(p, "base.en-q5_1").size, 59721011)
            verify(model(p, "base.en-q5_1").englishOnly)
            verify(!model(p, "small-q5_1").englishOnly)
            click(f.editor, "libraryTab:Captions")
            verify(!settled(f.editor, "generateCaptions").enabled || model(p, "base.en-q5_1").downloaded)

            // A file that is not the model is refused and removed.
            verify(p.downloadSpeechModel("small.en-q5_1"))
            tryVerify(() => downloaded.count === 1, 10000)
            compare(downloaded.signalArguments[0][1], false)
            verify(downloaded.signalArguments[0][2].indexOf("not the expected file") >= 0, downloaded.signalArguments[0][2])
            verify(!model(p, "small.en-q5_1").downloaded)
            verify(!p.downloadSpeechModel("nope"))

            // The real one is kept, with the progress shown while it comes.
            if (!model(p, "base.en-q5_1").downloaded) {
                click(f.editor, "downloadModel:base.en-q5_1")
                tryVerify(() => downloaded.count === 2, 30000)
                compare(downloaded.signalArguments[1][1], true, downloaded.signalArguments[1][2])
            }
            verify(model(p, "base.en-q5_1").downloaded)
            compare(p.modelDownload.id, undefined)
        }

        function test_b_captions_from_speech() {
            const f = createEditor()
            const p = f.project
            withModel(p)
            compare(p.importMedia([speech]), 1)
            verify(p.addClip(p.media[0].id))
            p.selectedClip = 0
            click(f.editor, "libraryTab:Captions")
            compare(settled(f.editor, "captionScope").text, "From every clip with sound")
            click(f.editor, "generateCaptions")
            tryVerify(() => finished.count === 1, 60000, "captions never came")
            compare(finished.signalArguments[0][0], true, finished.signalArguments[0][1])
            compare(p.undoText, "Generate Captions")

            // Two captions, one per sentence, either side of the pause; on a Captions track.
            const track = captionTrack(p)
            verify(track)
            compare(track.name, "Captions")
            compare(track.kind, "title")
            compare(p.captionCount, 2)
            const first = track.clips[0]
            const second = track.clips[1]
            verify(first.label.toLowerCase().indexOf("quick brown fox") >= 0, first.label)
            verify(first.start < 0.3, first.start)
            verify(first.start + first.duration < 3.0, first.start + first.duration)
            fuzzyCompare(second.start, 4.33, 0.15)
            verify(settled(f.editor, "captionSummary").text.indexOf("2 captions") === 0)

            // As SRT and VTT, and back.
            const srt = tempDir + "/tve_captions.srt"
            const vtt = tempDir + "/tve_captions.vtt"
            verify(p.exportSubtitles("file://" + srt))
            const text = testFiles.read(srt)
            verify(text.indexOf("1\n00:00:00,") === 0, text)
            verify(text.toLowerCase().indexOf("lazy dog") >= 0, text)
            verify(p.exportSubtitles("file://" + vtt))
            verify(testFiles.read(vtt).indexOf("WEBVTT") === 0)
            p.undo()
            compare(captionTrack(p), undefined)
            verify(p.importSubtitles("file://" + vtt))
            compare(p.undoText, "Import Captions")
            compare(p.captionCount, 2)
            compare(captionTrack(p).clips[1].label, second.label)

            // Again: the transcript is remembered, so it is instant.
            verify(p.generateCaptions({ model: "base.en-q5_1" }))
            verify(!p.analyzing)
            compare(p.captionCount, 2)

            // Only the selected clips: none with sound selected, nothing to do.
            p.selectedClip = captionTrack(p).clips[0].id
            verify(!p.generateCaptions())
            verify(p.lastError.indexOf("no sound") >= 0, p.lastError)
            verify(!p.importSubtitles("file://" + srt + ".missing"))
        }
    }
}
