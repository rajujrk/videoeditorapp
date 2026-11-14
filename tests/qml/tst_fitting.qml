import QtQuick
import QtTest
import TechieVideoEditor.Theme
import TechieVideoEditor.Player
import TechieVideoEditor.Shell

// Generation with keys and fitting (slice 30d), through the real editor: music fitted to a
// length on this Mac (song.wav, 32 s at 120 BPM), an AI transition made from the frames
// either side of a cut, and a video from an idea (Claude plans the shots, Google makes them)
// — the cloud parts talking HTTP to the mock services (TVE_CLOUD_MOCK=1; keys in a scratch
// store, never the Keychain).
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
        name: "Fitting"
        when: windowShown

        function media(name) { return "file://" + analysisMediaDir + "/" + name }
        function createEditor() {
            const fixture = createTemporaryObject(editorComponent, root)
            verify(fixture)
            waitForRendering(fixture)
            finished.clear()
            finished.target = fixture.project
            for (const provider of ["anthropic", "google"])
                fixture.project.cloud.removeKey(provider)
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
        // A control's own click (no mouse): steps whose mouse click can be lost under load.
        function press(item) {
            scrollIntoView(item)
            verify(item.enabled, item.objectName + " is disabled")
            item.clicked()
        }
        function waitFinished(count) {
            tryVerify(() => finished.count >= count, 30000, "the analysis never finished")
            compare(finished.signalArguments[count - 1][0], true, finished.signalArguments[count - 1][1])
        }
        function requests(cloud, path) {
            return cloud.mockRequests().filter((r) => r.path.indexOf(path) === 0)
        }
        function audioClips(p) {
            const track = p.tracks.find((t) => t.kind === "audio" && t.clips.length > 0)
            return track ? track.clips : []
        }
        function mainClips(p) { return p.tracks.find((t) => t.main).clips }
        function captionTexts(p) {
            const track = p.tracks.find((t) => t.captions)
            return track ? track.clips.map((c) => c.label) : []
        }
        function openGenerate(f) {
            settled(f.editor, "libraryTab:Generate").clicked()
            return settled(f.editor, "generatePage")
        }

        function test_music_fits_to_a_length() {
            const f = createEditor()
            const p = f.project
            compare(p.importMedia([media("song.wav")]), 1)
            verify(p.addClip(p.media[0].id))
            const song = audioClips(p)[0]
            fuzzyCompare(song.duration, 32, 0.05)
            p.selectedClip = song.id
            settled(f.editor, "inspectorTab:Audio").clicked()
            const length = settled(f.editor, "fitLength")
            compare(length.text, "32.0")

            // Shorter: whole bars out of the middle, one undo step.
            length.text = "20"
            press(settled(f.editor, "fitMusic"))
            waitFinished(1)
            compare(p.undoText, "Fit Music to Length")
            let clips = audioClips(p)
            compare(clips.length, 2)
            fuzzyCompare(clips[0].start, 0, 0.001)
            fuzzyCompare(clips[1].start, clips[0].start + clips[0].duration, 0.001)
            fuzzyCompare(clips[1].start + clips[1].duration, 20, 0.02)
            compare(p.selectedClip, clips[0].id)
            const said = settled(f.editor, "fitResult").text
            verify(said.indexOf("bars out") >= 0 && said.indexOf("20.0 s") >= 0, said)
            // The music from the second piece on is the end of the song.
            fuzzyCompare(clips[1].sourceIn + clips[1].duration * clips[1].speed, 32, 0.05)
            p.undo()
            compare(audioClips(p).length, 1)
            fuzzyCompare(audioClips(p)[0].duration, 32, 0.05)

            // Longer: a run of bars played again.
            verify(p.fitMusic(song.id, 45))
            waitFinished(2)
            clips = audioClips(p)
            verify(clips.length >= 2, clips.length)
            // The first piece runs past where the last one starts: bars heard twice.
            verify(clips[clips.length - 1].sourceIn < clips[0].sourceIn + clips[0].duration * clips[0].speed)
            const last = clips[clips.length - 1]
            fuzzyCompare(last.start + last.duration, 45, 0.05)
            verify(settled(f.editor, "fitResult").text.indexOf("again") >= 0, settled(f.editor, "fitResult").text)
            p.undo()

            // To the video's end: shorter than four bars, so the start, trimmed and faded.
            compare(p.importMedia([media("talk.mov")]), 1)
            verify(p.addClip(p.media[1].id))
            p.selectedClip = song.id
            const toVideo = settled(f.editor, "fitToVideo")
            press(toVideo)
            compare(length.text, "6.5")
            press(settled(f.editor, "fitMusic"))
            waitFinished(3)
            clips = audioClips(p).filter((c) => c.mediaId === p.media[0].id)
            compare(clips.length, 1)
            fuzzyCompare(clips[0].duration, 6.5, 0.02)
            verify(clips[0].fadeOut > 1, clips[0].fadeOut)

            // Not music on an audio track, or not a sensible length: refused.
            verify(!p.fitMusic(mainClips(p)[0].id, 10))
            verify(!p.fitMusic(song.id, 0.2))
        }

        function test_ai_transition_between_two_clips() {
            const f = createEditor()
            const p = f.project
            compare(p.importMedia([media("scenes.mov")]), 1)
            verify(p.addClip(p.media[0].id))
            verify(p.addClip(p.media[0].id))
            // (Ids copied: clips read from the project are live and follow its list.)
            const aId = Number(mainClips(p)[0].id)
            const bId = Number(mainClips(p)[1].id)
            fuzzyCompare(mainClips(p)[1].start, 3, 0.01)
            verify(p.addTitle({ text: "Later" }, -1, 4))
            const title = p.selectedClip
            p.seek(2.8)
            const page = openGenerate(f)
            settled(page, "generateKind:video").clicked()
            settled(page, "videoMode:transition").clicked()
            const where = settled(page, "transitionCut")
            verify(where.text.indexOf("scenes.mov") >= 0 && where.text.indexOf("00:00:03") >= 0, where.text)
            verify(!settled(page, "generateButton").enabled) // no key
            verify(p.cloud.setKey("google", "test-google-key"))
            const button = settled(page, "generateButton")
            compare(button.text, "Make the transition")
            settled(page, "generatePrompt").text = "a whip pan"
            press(button)
            compare(p.cloud.job, "transition")
            tryCompare(p.cloud, "job", "", 10000)
            compare(p.undoText, "AI Transition")

            // Made from the two frames, in order, and put between the clips.
            const request = requests(p.cloud, "/v1beta/interactions").pop()
            compare(request.body.generation_config.video_config.task, "image_to_video")
            compare(request.body.input.length, 3)
            compare(request.body.input[0].type, "image")
            compare(request.body.input[1].type, "image")
            verify(request.body.input[2].text.indexOf("a whip pan") >= 0)
            compare(request.body.response_format.aspect_ratio, "16:9")
            const clips = mainClips(p)
            compare(clips.length, 3)
            const bridge = p.media.find((m) => m.id === clips[1].mediaId)
            compare(bridge.generated.kind, "video")
            compare(bridge.generated.source, "between scenes.mov and scenes.mov")
            fuzzyCompare(clips[1].start, 3, 0.01)
            fuzzyCompare(clips[2].start, 3 + clips[1].duration, 0.01)
            compare(clips[2].id, bId)
            compare(p.selectedClip, clips[1].id)
            // What came after the cut moved along with it.
            let titleNow = null
            for (const t of p.tracks)
                for (const c of t.clips)
                    if (c.id === title)
                        titleNow = c
            fuzzyCompare(titleNow.start, 4 + clips[1].duration, 0.01)
            p.undo()
            compare(mainClips(p).length, 2)

            // With no cut to bridge: refused.
            verify(!p.aiTransition(aId))
        }

        function test_video_from_an_idea() {
            const f = createEditor()
            const p = f.project
            const page = openGenerate(f)
            settled(page, "generateKind:idea").clicked()
            verify(settled(page, "generateTerms").text.indexOf("Planned by Claude") === 0)
            verify(!settled(page, "generateButton").enabled)
            verify(p.cloud.setKey("anthropic", "test-claude-key"))
            verify(!settled(page, "generateButton").enabled) // Google too
            verify(p.cloud.setKey("google", "test-google-key"))

            // Planned by Claude: the number of shots asked for, each to review.
            settled(page, "generatePrompt").text = "A paper boat sails down a rainy street"
            settled(page, "ideaShots:3").clicked()
            const button = settled(page, "generateButton")
            compare(button.text, "Plan it")
            press(button)
            compare(p.cloud.job, "plan")
            tryCompare(p.cloud, "job", "", 5000)
            compare(p.ideaPlan.shots.length, 3)
            compare(settled(page, "ideaTitle").text, "A Mock Idea")
            const plan = lastClaude(p.cloud)
            compare(plan.body.output_config.format.type, "json_schema")
            verify(plan.body.messages[0].content.indexOf("A paper boat sails down a rainy street") >= 0)
            verify(plan.body.messages[0].content.indexOf("wide 16:9") >= 0)

            // The second shot changed before it is made.
            settled(page, "ideaShotPrompt:1").text = "The boat drops into a gutter, close up, rain"
            settled(page, "ideaShotCaption:1").text = "Down it goes"
            compare(settled(page, "generateButton").text, "Make the video (3 shots)")
            press(settled(page, "generateButton"))
            compare(p.cloud.job, "idea")
            tryCompare(p.cloud, "job", "", 20000)
            compare(p.undoText, "Video from an Idea")
            const shots = requests(p.cloud, "/v1beta/interactions").slice(-3)
            compare(shots.length, 3)
            compare(shots[1].body.input[shots[1].body.input.length - 1].text, "The boat drops into a gutter, close up, rain")
            const clips = mainClips(p)
            compare(clips.length, 3)
            for (let i = 0; i < 3; ++i) {
                const made = p.media.find((m) => m.id === clips[i].mediaId)
                compare(made.generated.kind, "video")
                compare(made.generated.source, "idea: A Mock Idea")
            }
            fuzzyCompare(clips[1].start, clips[0].start + clips[0].duration, 0.01)
            compare(captionTexts(p), ["Part 1", "Down it goes", "Part 3"])
            compare(p.cloud.usage.videos, 3)
            p.undo()
            compare(mainClips(p).length, 0)
            compare(captionTexts(p).length, 0)

            // A shot that fails: those made before stay in the media bin, nothing is placed.
            const before = p.media.length
            verify(p.makeIdeaVideo({ title: "Two", shots: [{ prompt: "A calm lake", caption: "" }, { prompt: "#blocked", caption: "" }] }))
            tryCompare(p.cloud, "job", "", 20000)
            compare(p.media.length, before + 1)
            compare(mainClips(p).length, 0)
            verify(p.cloud.status.indexOf("Made 1 of 2 shots") === 0, p.cloud.status)
            // A plan with no shots, or a shot without words: refused.
            verify(!p.makeIdeaVideo({ title: "None", shots: [] }))
            verify(!p.makeIdeaVideo({ title: "Empty", shots: [{ prompt: " ", caption: "" }] }))
            verify(!p.planIdea("   "))

            // Start over.
            settled(page, "ideaStartOver").clicked()
            compare(Object.keys(p.ideaPlan).length, 0)
            compare(settled(page, "generateButton").text, "Plan it")
        }

        function lastClaude(cloud) {
            const list = requests(cloud, "/v1/messages")
            return list[list.length - 1]
        }
    }
}
