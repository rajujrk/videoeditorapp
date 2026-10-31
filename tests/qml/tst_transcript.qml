import QtQuick
import QtTest
import TechieVideoEditor.Theme
import TechieVideoEditor.Player
import TechieVideoEditor.Shell

// Editing by the transcript: deleting words cuts them, captions follow; fillers and long
// pauses go at once; caption styles. On spoken samples (tools/make_test_media.sh) with the
// speech model (tools/fetch_models.sh); without them these tests skip.
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
        name: "Transcript"
        when: windowShown

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
        function click(parent, name, modifiers) {
            scrollIntoView(settled(parent, name))
            const item = settled(parent, name)
            mouseMove(item)
            mouseClick(item, item.width / 2, item.height / 2, Qt.LeftButton, modifiers === undefined ? Qt.NoModifier : modifiers)
        }
        // A spoken sample on the audio track, its transcript made (and captions, if asked).
        function withSpeech(f, file, captions) {
            const p = f.project
            if (!p.speechModels[0].downloaded) {
                verify(p.downloadSpeechModel("base.en-q5_1"))
                tryVerify(() => downloaded.count === 1, 30000)
            }
            compare(p.importMedia(["file://" + samplesDir + "/" + file]), 1)
            verify(p.addClip(p.media[0].id))
            p.selectedClip = 0
            const before = finished.count
            verify(captions ? p.generateCaptions({ model: "base.en-q5_1" }) : p.transcribe({ model: "base.en-q5_1" }))
            tryVerify(() => finished.count > before, 60000, "no transcript")
            compare(finished.signalArguments[finished.count - 1][0], true, finished.signalArguments[finished.count - 1][1])
        }
        function said(p) { return p.transcript.map((w) => w.text.toLowerCase().replace(/[^a-z]/g, "")).join(" ") }
        function audioClips(p) { return p.tracks.find((t) => t.kind === "audio").clips }

        function test_deleting_words_cuts_them_and_captions_follow() {
            const f = createEditor()
            const p = f.project
            withSpeech(f, "speech_en.wav", true)
            compare(said(p), "the quick brown fox jumps over the lazy dog we edit our videos on the timeline every day")
            const captionsBefore = p.captionCount

            // Click "quick", shift-click "fox": three words selected, the playhead at "quick".
            click(f.editor, "libraryTab:Transcript")
            click(f.editor, "word:1")
            fuzzyCompare(p.playhead, p.transcript[1].start, 0.001)
            click(f.editor, "word:3", Qt.ShiftModifier)
            const page = findChild(f.editor, "transcriptPage")
            compare(page.selected, [1, 2, 3])
            compare(settled(f.editor, "deleteWords").text, "Delete 3 words")
            const cutFrom = p.transcript[1].start
            const cutTo = p.transcript[4].start
            click(f.editor, "deleteWords")

            compare(p.undoText, "Delete Words")
            compare(said(p), "the jumps over the lazy dog we edit our videos on the timeline every day")
            compare(audioClips(p).length, 2)
            fuzzyCompare(audioClips(p)[1].start, cutFrom, 0.02)
            // The captions were made again for the cut: no "quick brown fox", same timing as the words.
            const first = p.tracks.find((t) => t.captions).clips[0]
            verify(first.label.toLowerCase().indexOf("quick") < 0, first.label)
            verify(first.label.toLowerCase().indexOf("jumps") >= 0, first.label)
            compare(p.captionCount, captionsBefore)
            // Total length shorter by the cut.
            fuzzyCompare(p.duration, 7.126 - (cutTo - cutFrom), 0.03)

            // One undo brings words, clip and captions back.
            p.undo()
            compare(said(p).indexOf("quick brown fox") > 0, true)
            compare(audioClips(p).length, 1)
            verify(p.tracks.find((t) => t.captions).clips[0].label.toLowerCase().indexOf("quick") >= 0)

            // The Delete key does the same in the transcript.
            click(f.editor, "word:9") // "we"
            keyClick(Qt.Key_Backspace)
            verify(said(p).indexOf(" we ") < 0, said(p))
        }

        function test_long_pauses_shorten() {
            const f = createEditor()
            const p = f.project
            withSpeech(f, "speech_en.wav", false)
            compare(p.captionCount, 0)
            // The 1.7 s pause becomes 0.3 s; nothing else changes.
            compare(p.removeTranscriptPauses(0.7, 0.15), 1)
            compare(p.undoText, "Remove Pauses")
            fuzzyCompare(p.duration, 7.126 - (1.71 - 0.3), 0.06)
            compare(said(p), "the quick brown fox jumps over the lazy dog we edit our videos on the timeline every day")
            compare(p.removeTranscriptPauses(0.7, 0.15), 0) // none left
            verify(p.lastError.indexOf("No pauses") === 0)
        }

        function test_fillers_go_at_once() {
            const f = createEditor()
            const p = f.project
            withSpeech(f, "speech_fillers.wav", false)
            const fillers = p.transcript.filter((w) => w.filler).length
            verify(fillers >= 1, said(p))
            click(f.editor, "libraryTab:Transcript")
            compare(settled(f.editor, "removeFillers").text, "Remove fillers (" + fillers + ")")
            click(f.editor, "removeFillers")
            compare(p.transcript.filter((w) => w.filler).length, 0)
            verify(said(p).indexOf("short clips") >= 0, said(p))
            compare(p.removeFillerWords(), 0)
        }

        function test_caption_styles() {
            const f = createEditor()
            const p = f.project
            withSpeech(f, "speech_en.wav", true)
            compare(p.captionMotion, "none")
            click(f.editor, "libraryTab:Captions")
            click(f.editor, "captionStyle:karaoke")
            compare(p.captionMotion, "karaoke")
            compare(p.undoText, "Caption Style")
            // Every caption, and captions made again keep it.
            for (const clip of p.tracks.find((t) => t.captions).clips) {
                p.selectedClip = clip.id
                compare(p.selectedClipInfo.text.captionMotion, "karaoke")
                verify(p.selectedClipInfo.text.wordCount > 0)
            }
            p.selectedClip = 0
            verify(p.generateCaptions({ model: "base.en-q5_1" }))
            compare(p.captionMotion, "karaoke")
            verify(p.setCaptionStyle("pop", [0.2, 0.9, 1.0, 1.0]))
            p.selectedClip = p.tracks.find((t) => t.captions).clips[0].id
            fuzzyCompare(p.selectedClipInfo.text.highlightColor[1], 0.9, 1e-6)
            p.undo()
            compare(p.captionMotion, "karaoke")
        }
    }
}
