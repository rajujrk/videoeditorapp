import QtQuick
import QtTest
import TechieVideoEditor.Theme
import TechieVideoEditor.Player
import TechieVideoEditor.Shell

// The editing copilot through the real editor and the real Claude adapter, talking HTTP to the
// mock Claude (TVE_CLOUD_MOCK=1), which follows a plan written into the request ("#plan […]"):
// its tools run on a copy, the person sees the changes and applies them as one undo step (or
// doesn't); the requests are a well-formed, append-only conversation; bad tool input, failed
// calls, refusals and a project edited meanwhile are handled.
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
        name: "Copilot"
        when: windowShown

        readonly property string av: "file://" + samplesDir + "/test_av_1080p30.mp4"

        function initTestCase() {
            const probe = Qt.createQmlObject('import TechieVideoEditor.Player; ProjectController {}', testCase)
            if (probe.importMedia([av]) !== 1)
                skip("samples missing: run tools/make_test_media.sh")
            probe.destroy()
        }
        function createEditor(withKey = true) {
            const fixture = createTemporaryObject(editorComponent, root)
            verify(fixture)
            waitForRendering(fixture)
            fixture.project.cloud.removeKey("anthropic")
            if (withKey)
                verify(fixture.project.cloud.setKey("anthropic", "test-claude-key"))
            fixture.project.cloud.claudeModel = "claude-opus-5-5"
            return fixture
        }
        function settled(parentItem, name) {
            let item = null
            let last = null
            tryVerify(() => {
                item = findChild(parentItem, name)
                if (!item || !item.visible)
                    return false
                const p = item.mapToItem(root, 0, 0)
                const stable = last !== null && p.x === last.x && p.y === last.y
                last = p
                return stable
            }, 3000, name + " never settled")
            return item
        }
        function claudeRequests(cloud) {
            return cloud.mockRequests().filter((r) => r.path.indexOf("/v1/messages") === 0)
        }
        function ask(f, request) {
            verify(f.project.copilot.ask(request))
            tryVerify(() => !f.project.copilot.busy, 10000, "the copilot never finished")
        }
        function last(list) { return list[list.length - 1] }
        function main(p) { return p.tracks.find((t) => t.main) }
        function plan(calls) { return "#plan " + JSON.stringify(calls) }
        // JSON with its keys in order (Qt keeps object keys sorted).
        function canon(value) {
            if (value !== null && typeof value === "object" && typeof value.length === "number")
                return "[" + Array.prototype.map.call(value, canon).join(",") + "]"
            if (value !== null && typeof value === "object")
                return "{" + Object.keys(value).sort().map((k) => JSON.stringify(k) + ":" + canon(value[k])).join(",") + "}"
            return JSON.stringify(value)
        }
        function texts(p) { return p.copilot.messages.map((m) => m.kind + ":" + (m.text || "")) }

        function test_it_proposes_and_nothing_changes_until_applied() {
            const f = createEditor()
            const p = f.project
            verify(p.importAndAppend(av))
            const clip = main(p).clips[0].id
            const effect = f.editor.player.availableEffects[0].id
            const before = claudeRequests(p.cloud).length
            const calls = [{ name: "split_clip", input: { clip_id: clip, at: 1.0 } },
                           { name: "add_title", input: { text: "Welcome", start: 0, duration: 1.5 } },
                           { name: "set_color", input: { clip_ids: [clip], temperature: 0.4 } },
                           { name: "add_effect", input: { clip_id: clip, effect_id: effect } }]
            const undoBefore = p.undoText
            ask(f, "Split it, warm it up, add a title and an effect. " + plan(calls))

            // Proposed, not done.
            compare(p.copilot.proposal.length, 4)
            verify(p.copilot.proposal[0].indexOf("Split “test_av_1080p30.mp4” at 1.00 s") === 0, p.copilot.proposal[0])
            verify(p.copilot.proposal[1].indexOf("Add title “Welcome” at 0.00 s") === 0, p.copilot.proposal[1])
            compare(main(p).clips.length, 1)
            compare(p.undoText, undoBefore)
            compare(texts(p).slice(0, 2), ["you:Split it, warm it up, add a title and an effect. " + plan(calls),
                                           "copilot:Done: the changes are ready for you to look over."])
            compare(last(p.copilot.messages).kind, "changes")
            compare(last(p.copilot.messages).state, "proposed")

            // The conversation as sent: tools streamed eagerly, adaptive thinking, the cache,
            // fallbacks; the same system prompt and tools in both turns; append-only.
            const requests = claudeRequests(p.cloud).slice(before)
            compare(requests.length, 2)
            const first = requests[0].body
            const second = requests[1].body
            compare(first.model, "claude-opus-5-5")
            compare(first.thinking.type, "adaptive")
            compare(first.output_config.effort, "high")
            compare(first.fallbacks, "default")
            compare(first.cache_control.type, "ephemeral")
            compare(requests[0].headers["anthropic-beta"], "server-side-fallback-2026-07-01")
            compare(first.tools.length, 12)
            verify(first.tools.every((t) => t.eager_input_streaming === true && t.input_schema.additionalProperties === false))
            verify(first.system[0].text.indexOf("<effects>") > 0)
            verify(first.system[0].text.indexOf("tve.transition.dissolve") > 0)
            compare(JSON.stringify(second.system), JSON.stringify(first.system))
            compare(JSON.stringify(second.tools), JSON.stringify(first.tools))
            compare(first.messages.length, 1)
            verify(first.messages[0].content.indexOf("<project>") === 0)
            verify(first.messages[0].content.indexOf("clip " + clip + " “test_av_1080p30.mp4” 0.00–2.00") > 0, first.messages[0].content)
            compare(second.messages.length, 3)
            compare(JSON.stringify(second.messages[0]), JSON.stringify(first.messages[0]))
            const answer = second.messages[1].content
            compare(answer[0].type, "thinking")
            compare(answer[0].signature, "mock-signature-0") // passed back as it came
            const uses = answer.filter((b) => b.type === "tool_use")
            compare(uses.length, 4)
            compare(canon(uses.map((u) => u.input)), canon(calls.map((c) => c.input)))
            const results = second.messages[2].content
            compare(results.length, 4) // every result in one message
            compare(results.map((r) => r.tool_use_id), uses.map((u) => u.id))
            verify(results.every((r) => r.is_error !== true), JSON.stringify(results))
            verify(JSON.parse(results[0].content).created_clip_ids.length === 1)

            // Applied: one undo step.
            findChild(f.editor, "libraryTab:Copilot").clicked()
            const apply = settled(f.editor, "copilotApply")
            mouseClick(apply)
            compare(main(p).clips.length, 2)
            compare(p.undoText, "Copilot: 4 changes")
            compare(last(p.copilot.messages).state, "applied")
            compare(p.copilot.proposal.length, 0)
            verify(p.tracks.some((t) => t.kind === "video" && !t.main && t.clips.some((c) => c.isTitle && c.label === "Welcome")))
            p.undo()
            compare(main(p).clips.length, 1)
            compare(p.undoText, undoBefore)
            p.redo()
            compare(main(p).clips.length, 2)

            // The next request goes on the same conversation and says what became of the changes.
            const second2 = claudeRequests(p.cloud).length
            ask(f, "That's all for now.")
            const third = claudeRequests(p.cloud).slice(second2)[0].body
            compare(third.messages.length, 5)
            compare(JSON.stringify(third.messages.slice(0, 3)), JSON.stringify(second.messages))
            compare(third.messages[3].role, "assistant")
            verify(third.messages[4].content.indexOf("(The person applied your changes.)") === 0, third.messages[4].content)
            verify(third.messages[4].content.indexOf("<project>") > 0)
        }

        function test_bad_input_failed_calls_and_refusals() {
            const f = createEditor()
            const p = f.project
            verify(p.importAndAppend(av))
            const clip = main(p).clips[0].id
            // A tool input that isn't JSON: answered as such, nothing proposed.
            let before = claudeRequests(p.cloud).length
            ask(f, "#invalid")
            let requests = claudeRequests(p.cloud).slice(before)
            compare(requests.length, 2)
            const bad = requests[1].body.messages[2].content[0]
            compare(bad.tool_use_id, "toolu_mock_bad")
            compare(bad.is_error, true)
            verify(JSON.parse(bad.content).INVALID_JSON.indexOf("\"clip_id\": 2") >= 0, bad.content)
            compare(p.copilot.proposal.length, 0)
            verify(last(p.copilot.messages).text.indexOf("didn't work") >= 0, last(p.copilot.messages).text)

            // One good call and one on a clip that isn't there: only the good one is proposed.
            p.copilot.startOver()
            before = claudeRequests(p.cloud).length
            ask(f, plan([{ name: "add_title", input: { text: "Hi", start: 0, duration: 1 } },
                         { name: "split_clip", input: { clip_id: 99999, at: 1 } }]))
            const results = claudeRequests(p.cloud).slice(before)[1].body.messages[2].content
            compare(results[1].is_error, true)
            compare(JSON.parse(results[1].content).error, "there is no clip 99999")
            compare(p.copilot.proposal.length, 1)
            // A schema the call doesn't fit: said why, not run.
            p.copilot.discardChanges()
            before = claudeRequests(p.cloud).length
            ask(f, plan([{ name: "trim_clip", input: { clip_id: clip, end: "soon" } }]))
            const unfit = last(claudeRequests(p.cloud).slice(before)[1].body.messages).content[0]
            compare(JSON.parse(unfit.content).error, "input.end must be number")

            // Declined: said so, and the conversation ends where it was before it.
            const kept = claudeRequests(p.cloud).length
            ask(f, "#refuse this")
            compare(last(p.copilot.messages).kind, "error")
            compare(last(p.copilot.messages).text, "Claude declined this request.")
            const refused = last(claudeRequests(p.cloud)).body.messages.length
            ask(f, "And now?")
            compare(last(claudeRequests(p.cloud)).body.messages.length, refused) // the declined one gone, this one in its place
            verify(claudeRequests(p.cloud).length > kept)
        }

        function test_a_proposal_for_an_older_project_is_refused() {
            const f = createEditor()
            const p = f.project
            verify(p.importAndAppend(av))
            verify(p.importAndAppend(av))
            const second = main(p).clips[1].id
            ask(f, plan([{ name: "set_transition", input: { clip_id: second, transition_id: "tve.transition.dissolve", duration: 0.5 } },
                         { name: "set_captions", input: { captions: [{ start: 0, end: 1, text: "Hello" }, { start: 1, end: 2, text: "there" }] } }]))
            compare(p.copilot.proposal.length, 2)
            verify(p.copilot.proposal[0].indexOf("Add a Dissolve transition into") === 0 || p.copilot.proposal[0].indexOf("transition into") > 0,
                   p.copilot.proposal[0])
            compare(p.copilot.proposal[1], "Set the captions (2)")
            // Edited meanwhile: not applied over it.
            verify(p.addMarker(0.5, "meanwhile") !== 0)
            verify(!p.copilot.applyChanges())
            compare(last(p.copilot.messages).kind, "error")
            verify(main(p).clips[1].transition === undefined || !main(p).clips[1].transition.transitionId)
            // Asked again, made for the project as it is now.
            ask(f, plan([{ name: "set_transition", input: { clip_id: second, transition_id: "tve.transition.dissolve", duration: 0.5 } }]))
            verify(p.copilot.applyChanges())
            compare(main(p).clips[1].transition.transitionId, "tve.transition.dissolve")
            compare(p.markers.length, 1)
            // Not applying leaves it all as it is.
            ask(f, plan([{ name: "delete_clips", input: { clip_ids: [second], close_gaps: true } }]))
            p.copilot.discardChanges()
            compare(main(p).clips.length, 2)
            compare(last(p.copilot.messages).state, "discarded")
            // (The project description names what it has.)
            const described = p.copilot.describeProject()
            verify(described.indexOf("Tracks, top first:") > 0)
            verify(described.indexOf("transition in: tve.transition.dissolve 0.50 s") > 0, described)
            verify(described.indexOf("Markers:") > 0)
        }

        function test_trims_moves_looks_effects_titles_and_deletes() {
            const f = createEditor()
            const p = f.project
            verify(p.importAndAppend(av))
            verify(p.importAndAppend(av))
            verify(p.addTitle({ text: "Old", size: 0.1 }, -1, 0))
            const first = main(p).clips[0].id
            const second = main(p).clips[1].id
            const title = p.tracks.find((t) => t.kind === "video" && !t.main && t.clips.length > 0).clips[0].id
            const look = p.looks[0].id
            const effect = f.editor.player.availableEffects[0].id
            verify(p.addClipEffect(first, effect))
            ask(f, plan([{ name: "trim_clip", input: { clip_id: first, end: 1.5 } },
                         { name: "set_look", input: { clip_id: first, look_id: look, intensity: 0.5 } },
                         { name: "remove_effect", input: { clip_id: first, index: 0 } },
                         { name: "set_title_text", input: { clip_id: title, text: "New" } },
                         { name: "move_clip", input: { clip_id: second, start: 3 } }]))
            compare(p.copilot.proposal.length, 5)
            verify(p.copilot.proposal[0].indexOf("Trim “test_av_1080p30.mp4” to 0.00–1.50 s") === 0, p.copilot.proposal[0])
            verify(p.copilot.proposal[1].indexOf("(50%)") > 0, p.copilot.proposal[1])
            compare(p.copilot.proposal[3], "Change title “Old” to “New”")
            verify(p.copilot.applyChanges())
            let clips = main(p).clips
            fuzzyCompare(clips[0].duration, 1.5, 0.01)
            fuzzyCompare(clips[1].start, 3, 0.01)
            p.selectedClip = first
            compare(p.selectedClipInfo.effects.length, 0)
            compare(p.selectedClipInfo.look.id, look)
            p.selectedClip = title
            compare(p.selectedClipInfo.text.text, "New")
            // Deleting with the gaps closed.
            ask(f, plan([{ name: "delete_clips", input: { clip_ids: [first], close_gaps: true } }]))
            verify(p.copilot.applyChanges())
            clips = main(p).clips
            compare(clips.length, 1)
            fuzzyCompare(clips[0].start, 1.5, 0.01) // moved back by the deleted clip's 1.5 s
        }

        function test_without_a_key_it_says_so() {
            const f = createEditor(false)
            const p = f.project
            verify(p.importAndAppend(av))
            findChild(f.editor, "libraryTab:Copilot").clicked()
            verify(settled(f.editor, "copilotSettings").visible)
            verify(!findChild(f.editor, "copilotSend").enabled)
            ask(f, "Add a title")
            compare(last(p.copilot.messages).text, "Add your Claude API key in AI settings first.")
            compare(p.copilot.proposal.length, 0)
        }
    }
}
