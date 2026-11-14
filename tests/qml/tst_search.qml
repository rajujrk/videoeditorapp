import QtQuick
import QtTest
import TechieVideoEditor.Theme
import TechieVideoEditor.Player
import TechieVideoEditor.Shell

// Search and checking (slice 30c), through the real editor: smart search in the Media tab
// (labels and on-screen text found with Vision on this Mac, what is said from transcripts),
// Claude proofreading the titles and captions, and Claude writing a prompt from the frame at
// the playhead — the Claude parts talking HTTP to the mock service (TVE_CLOUD_MOCK=1; keys
// in a scratch store, never the Keychain).
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
        name: "Search"
        when: windowShown

        function createEditor() {
            const fixture = createTemporaryObject(editorComponent, root)
            verify(fixture)
            waitForRendering(fixture)
            finished.clear()
            finished.target = fixture.project
            fixture.project.cloud.removeKey("anthropic")
            fixture.project.cloud.claudeModel = "claude-opus-5-5"
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
                if (a.contentY === undefined || a.contentHeight === undefined || a.contentItem === undefined)
                    continue
                const top = item.mapToItem(a.contentItem, 0, 0).y
                if (top < a.contentY || top + item.height > a.contentY + a.height) {
                    a.contentY = Math.max(0, Math.min(top - 20, a.contentHeight - a.height))
                    waitForPolish(a, 1000)
                }
                return
            }
        }
        function click(item) {
            scrollIntoView(item)
            mouseMove(item)
            mouseClick(item)
        }
        // A control's own click (no mouse): for steps whose mouse click can be lost under the
        // parallel test load.
        function press(item) {
            verify(item.enabled, item.objectName + " is disabled")
            item.clicked()
        }
        function lastClaudeRequest(cloud) {
            const requests = cloud.mockRequests().filter((r) => r.path.indexOf("/v1/messages") === 0)
            return requests[requests.length - 1]
        }
        function captionTexts(p) {
            const track = p.tracks.find((t) => t.captions)
            return track ? track.clips.map((c) => c.label) : []
        }
        // A sign as a picture file (in the scratch stills folder): big words as a title,
        // snapshotted by the app's own renderer, then the title undone. Its path.
        function signPicture(p, name) {
            const path = stillsDir + "/" + name + ".png"
            verify(p.addTitle({ text: "HARBOUR CAFE\nOpen every day", size: 0.12, bold: true }, -1, 0))
            p.seek(1)
            compare(p.takeSnapshot("file://" + path), path)
            p.undo()
            p.seek(0)
            return path
        }
        function waitFinished(count) {
            tryVerify(() => finished.count >= count, 60000, "the analysis never finished")
            compare(finished.signalArguments[count - 1][0], true, finished.signalArguments[count - 1][1])
        }

        function test_smart_search_finds_what_is_seen_and_shown() {
            const f = createEditor()
            const p = f.project
            const signPath = signPicture(p, "search-sign")
            if (p.importMedia(["file://" + samplesDir + "/people.mp4"]) !== 1)
                skip("people.mp4 missing: run tools/make_test_media.sh")
            compare(p.importMedia(["file://" + signPath]), 1)
            const video = p.media.find((m) => m.name === "people.mp4")
            const picture = p.media.find((m) => m.name === "search-sign.png")
            verify(video && picture)
            compare(p.searchStatus.lookable, 2)
            compare(p.searchStatus.looked, 0)
            compare(p.mediaLabels(video.id).length, 0)

            // Before looking: the names only.
            press(settled(f.editor, "libraryTab:Media"))
            const field = settled(f.editor, "librarySearch")
            field.text = "harbour"
            const title = settled(f.editor, "momentsTitle")
            tryVerify(() => title.text.indexOf("Look at the media") >= 0, 3000, title.text)
            const grid = findChild(f.editor, "mediaGrid")
            tryCompare(grid, "count", 0)
            field.text = "sign"
            tryCompare(grid, "count", 1) // by its name

            // Looking at the media (offered while searching): labels and the text on screen,
            // kept per file.
            const look = settled(f.editor, "lookAtMedia")
            verify(settled(f.editor, "searchReachText").text.indexOf("0 of 2") >= 0)
            click(look)
            tryVerify(() => p.analyzing, 2000)
            waitFinished(1)
            compare(p.searchStatus.looked, 2)
            tryVerify(() => !findChild(f.editor, "lookAtMedia").visible)
            // Not searching: the bin has its room back.
            field.text = ""
            tryVerify(() => !findChild(f.editor, "searchReach").visible)
            field.text = "sign"
            verify(settled(f.editor, "searchReachText").text.indexOf("every picture and video") >= 0)

            // What the sign says, on screen.
            field.text = "Harbour"
            tryVerify(() => findChild(f.editor, "searchMoments").count === 1, 3000)
            const hit = p.searchMedia("harbour")[0]
            compare(hit.mediaId, picture.id)
            verify(hit.shown)
            verify(!hit.said)
            compare(hit.snippet, "HARBOUR CAFE")
            compare(settled(f.editor, "momentSnippet:0").text, "HARBOUR CAFE")
            compare(settled(f.editor, "momentsTitle").text, "1 moment")
            tryCompare(grid, "count", 1)
            verify(findChild(f.editor, "media:search-sign.png"))
            // Two words of the sign, together.
            compare(p.searchMedia("open day").length, 1)
            compare(p.searchMedia("harbour zebra").length, 0)

            // What the video shows: its labels, and its moments found by them.
            const labels = p.mediaLabels(video.id)
            verify(labels.length > 0)
            verify(labels[0].share > 0.5, JSON.stringify(labels))
            const word = labels[0].label.split(" ")[0]
            const seen = p.searchMedia(word).filter((h) => h.mediaId === video.id)
            compare(seen.length, 1, word)
            verify(seen[0].seen)
            compare(seen[0].start, 0)
            verify(seen[0].end > 2.5 && seen[0].end <= 3.01, String(seen[0].end))

            // A moment opens in the source monitor with In and Out around it.
            field.text = word
            tryVerify(() => findChild(f.editor, "searchMoments").count >= 1, 3000)
            const moments = findChild(f.editor, "searchMoments")
            let at = -1
            tryVerify(() => {
                for (let i = 0; i < moments.count; ++i) {
                    const row = findChild(moments, "moment:" + i)
                    if (row && row.modelData.mediaId === video.id)
                        at = i
                }
                return at >= 0
            }, 3000)
            const row = settled(moments, "moment:" + at)
            mouseClick(row)
            const monitor = findChild(f.editor, "sourceMonitor")
            tryVerify(() => monitor && monitor.ready, 5000, "the source monitor did not open")
            compare(monitor.media.id, video.id)
            compare(monitor.controller.markIn, 0)
            verify(Math.abs(monitor.controller.markOut - seen[0].end) < 0.05)
            // Double-clicked: on the timeline at the playhead, just that moment.
            mouseDoubleClickSequence(row)
            const clips = p.tracks.find((t) => t.main).clips
            compare(clips.length, 1)
            verify(Math.abs(clips[0].duration - seen[0].end) < 0.05, String(clips[0].duration))

            // Looking again finds nothing new to do.
            verify(p.analyzeForSearch())
            waitFinished(2)
        }

        function test_claude_proofreads_titles_and_captions() {
            const f = createEditor()
            const p = f.project
            // Without a key: the button waits for one.
            compare(p.importSubtitles("file://" + captionFixturesDir + "/talk.srt"), true)
            press(settled(f.editor, "libraryTab:Captions"))
            verify(!settled(f.editor, "checkText").enabled)
            verify(settled(f.editor, "checkStatus").text.indexOf("Claude API key") >= 0)
            verify(p.cloud.setKey("anthropic", "test-claude-key"))

            // Typos in a caption and a title.
            const captions = p.tracks.find((t) => t.captions).clips
            verify(p.setClipText(captions[1].id, { text: "Teh tide had left shells everywhere." }))
            verify(p.addTitle({ text: "Its a good day to recieve guests" }, -1, 0))
            const titleId = p.selectedClip
            const undoBefore = p.undoText

            press(settled(f.editor, "checkText"))
            compare(p.cloud.job, "proofread")
            tryCompare(p.cloud, "job", "", 5000)
            const fixes = p.textFixes
            compare(fixes.length, 2)
            compare(fixes[0].kind, "caption")
            compare(fixes[0].before, "Teh tide had left shells everywhere.")
            compare(fixes[0].after, "The tide had left shells everywhere.")
            compare(fixes[0].note, "Spelling")
            compare(fixes[1].kind, "title")
            compare(fixes[1].clipId, titleId)
            compare(fixes[1].after, "Its a good day to receive guests")
            // Nothing changed yet.
            compare(captionTexts(p)[1], "Teh tide had left shells everywhere.")
            compare(p.undoText, undoBefore)
            verify(settled(f.editor, "checkStatus").text.indexOf("2 things to fix") >= 0)
            // What was sent: every title and caption, as a structured request.
            const request = lastClaudeRequest(p.cloud)
            compare(request.body.output_config.format.type, "json_schema")
            const prompt = request.body.messages[0].content
            verify(prompt.indexOf("We walked along the beach at dawn.") >= 0)
            verify(prompt.indexOf("Its a good day to recieve guests") >= 0)

            // The title's fix left out: only the caption's applied, one undo step.
            compare(settled(f.editor, "textFixAfter:0").text, "The tide had left shells everywhere.")
            click(settled(f.editor, "textFixChosen:1"))
            const apply = settled(f.editor, "applyTextFixes")
            compare(apply.text, "Apply 1 fix")
            press(apply)
            compare(captionTexts(p)[1], "The tide had left shells everywhere.")
            compare(captionTexts(p)[0], "We walked along the beach at dawn.")
            compare(p.undoText, "Correct Text")
            compare(p.textFixes.length, 1) // the title's, still offered
            p.undo()
            compare(captionTexts(p)[1], "Teh tide had left shells everywhere.")
            p.redo()

            // A fix whose text has changed since is not applied.
            verify(p.setClipText(titleId, { text: "Something else" }))
            verify(!p.applyTextFixes([p.textFixes[0].index]))
            compare(p.textFixes.length, 0)
            compare(p.undoText, "Edit Title")

            // Checked again with nothing wrong: nothing to fix.
            verify(p.setClipText(titleId, { text: "A good day" }))
            verify(p.checkText())
            tryCompare(p.cloud, "job", "", 5000)
            compare(p.textFixes.length, 0)
            verify(settled(f.editor, "checkStatus").text.indexOf("Nothing to fix") >= 0)
            verify(!findChild(f.editor, "applyTextFixes").visible)
        }

        function test_claude_writes_a_prompt_from_the_frame() {
            const f = createEditor()
            const p = f.project
            if (p.importMedia(["file://" + samplesDir + "/people.mp4"]) !== 1)
                skip("people.mp4 missing: run tools/make_test_media.sh")
            press(settled(f.editor, "libraryTab:Generate"))
            const button = settled(f.editor, "promptFromFrame")
            verify(!button.enabled) // no key, nothing on the timeline
            verify(p.cloud.setKey("anthropic", "test-claude-key"))
            verify(!button.enabled)
            verify(!p.promptFromPicture("picture"))
            verify(p.addClip(p.media[0].id))
            p.seek(1.5)
            tryVerify(() => button.enabled)

            press(button)
            compare(p.cloud.job, "describe")
            tryCompare(p.cloud, "job", "", 10000)
            const prompt = settled(f.editor, "generatePrompt")
            verify(prompt.text.indexOf("A mock prompt written from the picture") === 0, prompt.text)
            // The frame went as a JPEG, before the request's words, at most 1568 pixels wide.
            const request = lastClaudeRequest(p.cloud)
            const content = request.body.messages[0].content
            compare(content.length, 2)
            compare(content[0].type, "image")
            compare(content[0].source.type, "base64")
            compare(content[0].source.media_type, "image/jpeg")
            verify(content[0].source.data.length > 1000)
            compare(content[1].type, "text")
            verify(content[1].text.indexOf("image generator") >= 0)
            compare(request.body.output_config.effort, "low")

            // For a video, from a picture in the media bin.
            compare(p.importMedia(["file://" + signPicture(p, "prompt-sign")]), 1)
            const still = p.media.find((m) => m.still)
            verify(p.promptFromPicture("video", still.id))
            tryCompare(p.cloud, "job", "", 10000)
            verify(lastClaudeRequest(p.cloud).body.messages[0].content[1].text.indexOf("video generator") >= 0)
            // Not a picture, or not a kind of prompt: refused.
            verify(!p.promptFromPicture("video", p.media.find((m) => !m.still).id))
            verify(!p.promptFromPicture("song"))
        }
    }
}
