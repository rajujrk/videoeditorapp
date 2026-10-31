import QtQuick
import QtTest
import TechieVideoEditor.Theme
import TechieVideoEditor.Player
import TechieVideoEditor.Shell

// The AI features through the real editor and the real Claude and ElevenLabs adapters,
// talking HTTP to the mock services (TVE_CLOUD_MOCK=1; keys in a scratch store, never the
// Keychain): keys and models, writing from captions (streaming, retries, refusals,
// fallbacks, stopping), translating captions in batches, and voices with captions.
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

    TestCase {
        id: testCase
        name: "Cloud"
        when: windowShown

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
            for (let a = item; a && a !== root; a = a.parent)
                waitForPolish(a, 1000)
            return item
        }
        function click(item) {
            // Scrolled into view first (inside a scroll view).
            for (let a = item.parent; a; a = a.parent) {
                if (a.contentY === undefined || a.contentHeight === undefined || a.contentItem === undefined)
                    continue
                const top = item.mapToItem(a.contentItem, 0, 0).y
                if (top < a.contentY || top + item.height > a.contentY + a.height) {
                    a.contentY = Math.max(0, Math.min(top - 20, a.contentHeight - a.height))
                    waitForPolish(a, 1000)
                }
                break
            }
            mouseMove(item)
            mouseClick(item)
        }
        function claudeRequests(cloud) {
            return cloud.mockRequests().filter((r) => r.path.indexOf("/v1/messages") === 0)
        }
        function captionTexts(p) {
            const track = p.tracks.find((t) => t.captions)
            return track ? track.clips.map((c) => c.label) : []
        }
        function captionTimes(p) {
            const track = p.tracks.find((t) => t.captions)
            return track ? track.clips.map((c) => c.start.toFixed(3) + "-" + (c.start + c.duration).toFixed(3)) : []
        }
        function lastClaudeRequest(cloud) {
            const requests = cloud.mockRequests().filter((r) => r.path.indexOf("/v1/messages") === 0)
            return requests[requests.length - 1]
        }
        function withKeys(p) {
            verify(p.cloud.setKey("anthropic", "test-claude-key"))
            verify(p.cloud.setKey("elevenlabs", "test-voice-key"))
        }
        function init() {
            // No keys left from another test.
            const f = createEditor()
            f.project.cloud.removeKey("anthropic")
            f.project.cloud.removeKey("elevenlabs")
            f.project.cloud.claudeModel = "claude-opus-5-5"
        }

        function test_keys_and_models_in_settings() {
            const f = createEditor()
            const cloud = f.project.cloud
            verify(cloud.mock)
            verify(!cloud.hasClaudeKey)
            verify(!cloud.hasElevenLabsKey)
            compare(cloud.claudeModel, "claude-opus-5-5")
            click(settled(f.editor, "cloudSettingsButton"))
            const dialog = findChild(f.editor, "cloudSettings")
            tryCompare(dialog, "opened", true)
            const field = settled(dialog.contentItem, "claudeKeyField")
            compare(field.echoMode, TextInput.Password)
            verify(!settled(dialog.contentItem, "saveClaudeKey").enabled)
            field.text = "test-claude-key"
            click(settled(dialog.contentItem, "saveClaudeKey"))
            verify(cloud.hasClaudeKey)
            compare(field.text, "")
            compare(settled(dialog.contentItem, "cloudSettingsMessage").text, "Saved in your Keychain.")
            verify(!cloud.setKey("anthropic", "   "))
            // The cheaper model, remembered by a new editor.
            click(settled(dialog.contentItem, "claudeModel:claude-sonnet-5-5"))
            compare(cloud.claudeModel, "claude-sonnet-5-5")
            const g = createEditor()
            compare(g.project.cloud.claudeModel, "claude-sonnet-5-5")
            verify(g.project.cloud.hasClaudeKey)
            g.visible = false
            click(settled(dialog.contentItem, "removeClaudeKey"))
            verify(!cloud.hasClaudeKey)
            // ElevenLabs: its voices join the Mac's.
            const systemVoices = cloud.voices.filter((v) => v.provider === "system").length
            verify(cloud.setKey("elevenlabs", "test-voice-key"))
            tryVerify(() => cloud.voices.some((v) => v.id === "elevenlabs:mockvoice1"), 5000)
            compare(cloud.voices.filter((v) => v.provider === "system").length, systemVoices)
            const avery = cloud.voices.find((v) => v.id === "elevenlabs:mockvoice1")
            compare(avery.name, "Avery")
            compare(avery.detail, "female, american")
            // A refused key says so.
            verify(cloud.setKey("elevenlabs", "bad-key"))
            tryVerify(() => cloud.voicesError !== "", 5000)
            verify(!cloud.voices.some((v) => v.provider === "elevenlabs"))
        }

        function test_write_titles_from_captions() {
            const f = createEditor()
            const p = f.project
            withKeys(p)
            compare(p.importSubtitles("file://" + captionFixturesDir + "/talk.srt"), true)
            compare(p.captionCount, 3)
            click(settled(f.editor, "libraryTab:Transcript"))
            click(settled(f.editor, "writeWithAi"))
            const panel = findChild(f.editor, "writePanel")
            tryCompare(panel, "opened", true)
            compare(panel.kind, "titles")
            p.cloud.resetUsage()
            click(settled(panel.contentItem, "writeButton"))
            compare(p.cloud.job, "write")
            tryCompare(p.cloud, "job", "", 5000)
            compare(panel.lines.length, 5)
            compare(panel.lines[0], "A Day by the Sea")
            compare(p.cloud.status, "Written by Claude Opus 5.5")
            // What was sent: the key, the API version, the fallback opt-in, streaming, the
            // model and effort, and what is said in the video.
            const request = lastClaudeRequest(p.cloud)
            compare(request.headers["x-api-key"], "test-claude-key")
            compare(request.headers["anthropic-version"], "2026-10-21")
            compare(request.headers["anthropic-beta"], "server-side-fallback-2026-07-01")
            compare(request.body.model, "claude-opus-5-5")
            compare(request.body.stream, true)
            compare(request.body.fallbacks, "default")
            compare(request.body.output_config.effort, "medium")
            verify(request.body.thinking === undefined)
            verify(request.body.messages[0].content.indexOf("The tide had left shells everywhere.") >= 0)
            verify(request.body.system.indexOf("transcript") >= 0)
            // What it used.
            verify(p.cloud.usage.inputTokens > 0)
            verify(p.cloud.usage.outputTokens > 0)
            verify(p.cloud.usage.cost > 0)
            // A title onto the timeline.
            p.seek(0)
            click(settled(panel.contentItem, "addWrittenTitle:1"))
            compare(p.undoText, "Add Title")
            verify(p.tracks.some((t) => t.clips.some((c) => c.isTitle && c.label === "Waves, Wind and Wonder")))

            // A description, streamed into the editable draft.
            click(settled(panel.contentItem, "writeKind:description"))
            click(settled(panel.contentItem, "writeButton"))
            tryCompare(p.cloud, "job", "", 5000)
            verify(settled(panel.contentItem, "writeDraft").text.indexOf("mock answer") >= 0)
        }

        function test_retries_refusals_fallbacks_and_stop() {
            const f = createEditor()
            const p = f.project
            withKeys(p)
            compare(p.importSubtitles("file://" + captionFixturesDir + "/talk.srt"), true)
            let errors = []
            p.errorOccurred.connect((message) => errors.push(message))
            let finished = []
            p.cloud.jobFinished.connect((job, ok, error) => finished.push({ job: job, ok: ok, error: error }))

            // Busy once: tried again after the wait the server asks for.
            const before = claudeRequests(p.cloud).length
            verify(p.writeCopy("description", "", "#overload"))
            tryCompare(p.cloud, "job", "", 5000)
            compare(finished[finished.length - 1].ok, true)
            compare(claudeRequests(p.cloud).length - before, 2)
            verify(p.cloud.draft.indexOf("mock answer") >= 0)

            // Declined.
            verify(p.writeCopy("description", "", "#refuse"))
            tryCompare(p.cloud, "job", "", 5000)
            compare(finished[finished.length - 1].ok, false)
            verify(errors[errors.length - 1].indexOf("declined") >= 0, errors[errors.length - 1])

            // Declined mid-answer, then answered by another model: only that answer stays.
            verify(p.writeCopy("description", "", "#fallback"))
            tryCompare(p.cloud, "job", "", 5000)
            compare(finished[finished.length - 1].ok, true)
            verify(p.cloud.draft.indexOf("Declined part") < 0, p.cloud.draft)
            verify(p.cloud.draft.indexOf("This is a mock answer") === 0, p.cloud.draft)

            // Stopped while streaming: no error.
            const errorCount = errors.length
            verify(p.writeCopy("description", "", "#slow"))
            tryVerify(() => p.cloud.draft.length > 0, 5000)
            p.cloud.cancel()
            tryCompare(p.cloud, "job", "", 5000)
            compare(finished[finished.length - 1].ok, false)
            compare(finished[finished.length - 1].error, "")
            compare(errors.length, errorCount)

            // One job at a time.
            verify(p.writeCopy("description", "", "#slow"))
            verify(!p.writeCopy("titles"))
            verify(!p.translateCaptions("French"))
            p.cloud.cancel()
            tryCompare(p.cloud, "job", "", 5000)

            // A refused key, and no key.
            verify(p.cloud.setKey("anthropic", "bad-key"))
            verify(p.writeCopy("titles"))
            tryCompare(p.cloud, "job", "", 5000)
            verify(errors[errors.length - 1].indexOf("not accepted") >= 0, errors[errors.length - 1])
            p.cloud.removeKey("anthropic")
            verify(p.writeCopy("titles"))
            tryCompare(p.cloud, "job", "", 5000)
            verify(errors[errors.length - 1].indexOf("Add your Claude API key") >= 0, errors[errors.length - 1])
            // Nothing said: nothing to write from.
            const g = createEditor()
            verify(!g.project.writeCopy("titles"))
        }

        function test_translate_captions_keeping_their_times() {
            const f = createEditor()
            const p = f.project
            withKeys(p)
            compare(p.importSubtitles("file://" + captionFixturesDir + "/talk.srt"), true)
            const times = captionTimes(p)
            click(settled(f.editor, "libraryTab:Captions"))
            const language = settled(f.editor, "translateLanguage")
            language.currentIndex = language.model.indexOf("French")
            click(settled(f.editor, "translateCaptions"))
            compare(p.cloud.job, "translate")
            tryCompare(p.cloud, "job", "", 5000)
            compare(captionTexts(p), ["[French] We walked along the beach at dawn.",
                                      "[French] The tide had left shells everywhere.",
                                      "[French] By noon we were home again."])
            compare(captionTimes(p), times)
            compare(p.undoText, "Translate Captions to French")
            const request = lastClaudeRequest(p.cloud)
            compare(request.body.output_config.format.type, "json_schema")
            verify(request.body.messages[0].content.indexOf("Target language: French") >= 0)
            p.undo()
            compare(captionTexts(p)[0], "We walked along the beach at dawn.")

            // 130 captions: two requests, one undo step.
            compare(p.importSubtitles("file://" + captionFixturesDir + "/long.srt"), true)
            compare(p.captionCount, 130)
            const before = claudeRequests(p.cloud).length
            verify(p.translateCaptions("German"))
            tryCompare(p.cloud, "job", "", 10000)
            compare(claudeRequests(p.cloud).length - before, 2)
            const texts = captionTexts(p)
            compare(texts.length, 130)
            compare(texts[0], "[German] Line number 1")
            compare(texts[129], "[German] Line number 130")
            compare(p.undoText, "Translate Captions to German")
        }

        function test_cloud_voice_with_captions() {
            const f = createEditor()
            const p = f.project
            withKeys(p)
            tryVerify(() => p.cloud.voices.some((v) => v.id === "elevenlabs:mockvoice2"), 5000)
            compare(p.importMedia(["file://" + analysisMediaDir + "/scenes.mov"]), 1)
            verify(p.addClip(p.media[0].id))
            p.seek(1.0)
            p.cloud.resetUsage()
            const text = "Hello there. This voice comes from the cloud."
            // Listen first: the same text and voice then goes on without speaking again.
            verify(p.speakText(text, "elevenlabs:mockvoice2", { preview: true }))
            tryCompare(p.cloud, "job", "", 5000)
            verify(p.soundLibrary.previewing.indexOf("file:" + voicesDir) === 0, p.soundLibrary.previewing)
            p.soundLibrary.stopPreview()
            compare(p.cloud.usage.characters, text.length)
            const requests = p.cloud.mockRequests().length
            verify(p.speakText(text, "elevenlabs:mockvoice2", {}))
            compare(p.cloud.mockRequests().length, requests)
            compare(p.undoText, "Add Voice")
            const voiceTrack = p.tracks.find((t) => t.clips.some((c) => c.id === p.selectedClip))
            compare(voiceTrack.kind, "audio")
            const clip = voiceTrack.clips.find((c) => c.id === p.selectedClip)
            compare(clip.start, 1.0)
            verify(clip.path.indexOf(voicesDir + "/Voice 1") === 0, clip.path)
            verify(Math.abs(clip.duration - (text.length * 0.06 + 0.2)) < 0.05, clip.duration)
            // Captions from the character times: starting with the first word, at the playhead.
            const captions = p.tracks.find((t) => t.captions).clips
            verify(captions.length >= 1)
            compare(captions[0].start, 1.0)
            verify(captions[0].label.indexOf("Hello there.") === 0, captions[0].label)
            const request = p.cloud.mockRequests().filter((r) => r.path.indexOf("/with-timestamps") > 0).pop()
            compare(request.headers["xi-api-key"], "test-voice-key")
            compare(request.body.model_id, "eleven_multilingual_v2")
            verify(request.path.indexOf("/v1/text-to-speech/mockvoice2/with-timestamps?output_format=mp3_44100_128") === 0, request.path)
            // One undo step takes both away.
            p.undo()
            verify(!p.tracks.some((t) => t.clips.some((c) => c.id === clip.id)))
            verify(!p.tracks.find((t) => t.captions) || p.tracks.find((t) => t.captions).clips.length === 0)
        }

        function test_voice_pane_with_a_mac_voice() {
            const f = createEditor()
            const p = f.project
            const english = p.cloud.voices.find((v) => v.provider === "system" && v.language.indexOf("en") === 0)
            if (!english)
                skip("No English system voice on this Mac")
            click(settled(f.editor, "libraryTab:Audio"))
            click(settled(f.editor, "audioSection:voice"))
            const pane = settled(f.editor, "voicePane")
            pane.voice = english.id
            settled(pane, "voiceText").text = "One small step for the edit."
            p.seek(0)
            click(settled(pane, "addVoice"))
            tryCompare(p.cloud, "job", "", 30000)
            compare(p.undoText, "Add Voice")
            let clip = null
            for (const t of p.tracks)
                for (const c of t.clips)
                    if (c.id === p.selectedClip)
                        clip = c
            verify(clip.path.endsWith(".wav"), clip.path)
            verify(clip.duration > 0.8 && clip.duration < 6, clip.duration)
            const captions = p.tracks.find((t) => t.captions).clips
            compare(captions.map((c) => c.label).join(" "), "One small step for the edit.")
            verify(captions[0].start < 0.5, captions[0].start)
            // The words' times come from the voice: the last word ends within the sound, not
            // squeezed into its start or pushed past its end.
            const last = captions[captions.length - 1]
            verify(last.start + last.duration <= clip.duration + 0.01, last.start + last.duration)
            verify(last.start + last.duration > clip.duration * 0.6, last.start + last.duration)
        }
    }
}
