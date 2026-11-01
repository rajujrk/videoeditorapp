import QtQuick
import QtTest
import TechieVideoEditor.Theme
import TechieVideoEditor.Player
import TechieVideoEditor.Shell

// Generated media through the real editor and the real OpenAI, Google and ElevenLabs
// adapters, talking HTTP to the mock services (TVE_CLOUD_MOCK=1): pictures (from words and
// from pictures), stickers (transparent, placed at the playhead), video (from words, a
// picture, first and last pictures, and extending one made here) and music and sound
// effects — each into the media bin with its prompt and maker kept.
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
        name: "Generate"
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
        function requests(cloud, path) {
            return cloud.mockRequests().filter((r) => r.path.indexOf(path) === 0)
        }
        function openTab(f) {
            click(settled(f.editor, "libraryTab:Generate"))
            return settled(f.editor, "generatePage")
        }
        function generate(f, page) {
            click(settled(page, "generateButton"))
            verify(f.project.cloud.job !== "")
            tryCompare(f.project.cloud, "job", "", 10000)
        }
        function init() {
            const f = createEditor()
            for (const provider of ["openai", "google", "elevenlabs"])
                f.project.cloud.removeKey(provider)
            f.project.cloud.imageModel = "gpt-image-2.5-flare"
        }

        function test_no_key_points_to_settings() {
            const f = createEditor()
            const page = openTab(f)
            verify(!settled(page, "generateButton").enabled)
            click(settled(page, "openAiSettings"))
            tryCompare(findChild(f.editor, "cloudSettings"), "opened", true)
        }

        function test_pictures_from_words_and_from_pictures() {
            const f = createEditor()
            const p = f.project
            verify(p.cloud.setKey("openai", "test-openai-key"))
            const page = openTab(f)
            settled(page, "generatePrompt").text = "A lighthouse at dusk"
            click(settled(page, "imageShape:landscape"))
            generate(f, page)
            compare(p.media.length, 1)
            const picture = p.media[0]
            verify(picture.still)
            verify(picture.path.indexOf(p.generatedDirectory + "/Picture ") === 0, picture.path)
            verify(picture.path.endsWith(".png"))
            compare(picture.generated.provider, "openai")
            compare(picture.generated.kind, "image")
            compare(picture.generated.model, "gpt-image-2.5-flare")
            compare(picture.generated.prompt, "A lighthouse at dusk")
            verify(picture.generated.created > 0)
            let request = requests(p.cloud, "/v1/images/generations").pop()
            compare(request.headers["authorization"], "Bearer test-openai-key")
            compare(request.body.model, "gpt-image-2.5-flare")
            compare(request.body.size, "1536x1024")
            compare(request.body.quality, "medium")
            compare(request.body.output_format, "png")
            verify(request.body.background === undefined)
            compare(p.cloud.usage.images, 1)
            verify(settled(page, "made:" + picture.name))

            // From a picture in the bin: an edit with it as a data URL, kept as the source.
            compare(p.importMedia(["file://" + stillsDir + "/halves.png"]), 1)
            p.cloud.imageModel = "gpt-image-2.5-sunburst"
            settled(page, "generatePrompt").text = "The same, in watercolour"
            click(settled(page, "pickMedia:halves.png"))
            click(settled(page, "imageQuality:high"))
            generate(f, page)
            request = requests(p.cloud, "/v1/images/edits").pop()
            compare(request.body.model, "gpt-image-2.5-sunburst")
            compare(request.body.quality, "high")
            compare(request.body.images.length, 1)
            verify(request.body.images[0].image_url.indexOf("data:image/png;base64,") === 0)
            const edited = p.media[p.media.length - 1]
            compare(edited.generated.source, "halves.png")
            compare(edited.generated.model, "gpt-image-2.5-sunburst")

            // Refused: a bad key, and a blocked prompt.
            let errors = []
            p.errorOccurred.connect((message) => errors.push(message))
            verify(p.generateImage("A #blocked thing"))
            tryCompare(p.cloud, "job", "", 5000)
            verify(errors[errors.length - 1].indexOf("safety system") >= 0, errors[errors.length - 1])
            verify(p.cloud.setKey("openai", "bad-key"))
            verify(p.generateImage("Anything"))
            tryCompare(p.cloud, "job", "", 5000)
            verify(errors[errors.length - 1].indexOf("not accepted") >= 0, errors[errors.length - 1])
            // Only pictures can be worked from.
            verify(!p.generateImage("x", { inputs: [12345] }))
        }

        function test_sticker_is_transparent_and_placed() {
            const f = createEditor()
            const p = f.project
            verify(p.cloud.setKey("openai", "test-openai-key"))
            compare(p.importMedia(["file://" + analysisMediaDir + "/scenes.mov"]), 1)
            verify(p.addClip(p.media[0].id))
            p.seek(1.0)
            const page = openTab(f)
            click(settled(page, "generateKind:sticker"))
            settled(page, "generatePrompt").text = "A red balloon"
            generate(f, page)
            compare(p.undoText, "Add AI Sticker")
            const request = requests(p.cloud, "/v1/images/generations").pop()
            compare(request.body.background, "transparent")
            compare(request.body.size, "1024x1024")
            const sticker = p.media[p.media.length - 1]
            compare(sticker.generated.kind, "sticker")
            verify(sticker.alpha)
            // Over the picture, on a free track above the main one.
            const track = p.tracks.find((t) => t.clips.some((c) => c.id === p.selectedClip))
            compare(track.kind, "video")
            verify(!track.main)
            const clip = track.clips.find((c) => c.id === p.selectedClip)
            compare(clip.start, 1.0)
            compare(clip.path, sticker.path)
            // A picture that comes back without transparency has its subject cut out here.
            let errors = []
            p.errorOccurred.connect((message) => errors.push(message))
            verify(p.generateImage("A green apple #opaque", { sticker: true }))
            tryCompare(p.cloud, "job", "", 10000)
            compare(errors, [])
            const cut = p.media[p.media.length - 1]
            verify(cut.alpha)
            compare(p.undoText, "Add AI Sticker")
        }

        function test_video_modes_and_extending() {
            const f = createEditor()
            const p = f.project
            verify(p.cloud.setKey("google", "test-google-key"))
            const page = openTab(f)
            click(settled(page, "generateKind:video"))
            settled(page, "generatePrompt").text = "Waves rolling in at dawn, slow pan"
            click(settled(page, "videoAspect:9:16"))
            generate(f, page)
            let request = requests(p.cloud, "/v1beta/interactions").pop()
            compare(request.headers["x-goog-api-key"], "test-google-key")
            compare(request.body.model, "gemini-omni-1.1-flash")
            compare(request.body.response_format.type, "video")
            compare(request.body.response_format.aspect_ratio, "9:16")
            compare(request.body.response_format.resolution, "720p")
            compare(request.body.generation_config.video_config.task, "text_to_video")
            compare(request.body.input[request.body.input.length - 1].text, "Waves rolling in at dawn, slow pan")
            const video = p.media[p.media.length - 1]
            verify(video.hasVideo && !video.still)
            verify(video.path.endsWith(".mp4"))
            verify(Math.abs(video.duration - 3) < 0.2, video.duration)
            compare(video.generated.provider, "google")
            compare(video.generated.kind, "video")
            verify(video.generated.reference.indexOf("v1_mock_") === 0, video.generated.reference)
            compare(p.cloud.usage.videos, 1)

            // From a picture, and from a first and a last picture (in the order chosen).
            compare(p.importMedia(["file://" + stillsDir + "/halves.png", "file://" + stillsDir + "/disc.png"]), 2)
            click(settled(page, "videoMode:image"))
            click(settled(page, "pickMedia:disc.png"))
            generate(f, page)
            request = requests(p.cloud, "/v1beta/interactions").pop()
            compare(request.body.generation_config.video_config.task, "image_to_video")
            compare(request.body.input[0].type, "image")
            compare(request.body.input[0].mime_type, "image/png")
            compare(p.media[p.media.length - 1].generated.source, "disc.png")
            click(settled(page, "videoMode:frames"))
            click(settled(page, "pickMedia:disc.png"))
            click(settled(page, "pickMedia:halves.png"))
            settled(page, "generatePrompt").text = "Morph between them"
            generate(f, page)
            request = requests(p.cloud, "/v1beta/interactions").pop()
            compare(request.body.input.length, 3)
            compare(p.media[p.media.length - 1].generated.source, "disc.png, halves.png")

            // Extend the first video from its row in Made here.
            click(settled(page, "extendMade:" + video.name))
            compare(page.videoMode, "extend")
            compare(page.picked, [video.id])
            settled(page, "generatePrompt").text = ""
            generate(f, page)
            request = requests(p.cloud, "/v1beta/interactions").pop()
            compare(request.body.previous_interaction_id, video.generated.reference)
            compare(request.body.generation_config.video_config.task, "extend")
            compare(p.media[p.media.length - 1].generated.source, "extends " + video.name)
            // Only videos made here can be extended.
            verify(!p.generateVideo("", { mode: "extend", extend: p.media.find((m) => m.name === "halves.png").id }))
            // Onto the timeline.
            click(settled(page, "addMade:" + video.name))
            verify(p.tracks.some((t) => t.clips.some((c) => c.path === video.path)))
        }

        function test_music_and_sound_effects_at_the_playhead() {
            const f = createEditor()
            const p = f.project
            verify(p.cloud.setKey("elevenlabs", "test-voice-key"))
            const page = openTab(f)
            click(settled(page, "generateKind:music"))
            settled(page, "generatePrompt").text = "Warm acoustic guitar, slow and hopeful"
            settled(page, "soundLength").value = 10
            generate(f, page)
            compare(p.undoText, "Add AI Music")
            let request = requests(p.cloud, "/v1/music").pop()
            verify(request.path.indexOf("output_format=mp3_44100_128") > 0, request.path)
            compare(request.body.music_length_ms, 10000)
            compare(request.body.force_instrumental, true)
            const music = p.media[p.media.length - 1]
            compare(music.generated.kind, "music")
            compare(music.generated.provider, "elevenlabs")
            verify(Math.abs(music.duration - 10) < 0.1, music.duration)
            verify(p.cloud.usage.soundSeconds >= 9.9)

            click(settled(page, "generateKind:sfx"))
            settled(page, "generatePrompt").text = "A wooden door creaks open"
            settled(page, "soundLength").value = 2.5
            click(settled(page, "soundLoop"))
            generate(f, page)
            compare(p.undoText, "Add AI Sound Effect")
            request = requests(p.cloud, "/v1/sound-generation").pop()
            compare(request.body.text, "A wooden door creaks open")
            compare(request.body.duration_seconds, 2.5)
            compare(request.body.loop, true)
            compare(p.media[p.media.length - 1].generated.kind, "sfx")
            // Undone: the clip and its media go.
            const count = p.media.length
            p.undo()
            compare(p.media.length, count - 1)

            // A refused prompt says why.
            let errors = []
            p.errorOccurred.connect((message) => errors.push(message))
            verify(p.generateSound("In the style of #blocked", { kind: "music", seconds: 5 }))
            tryCompare(p.cloud, "job", "", 5000)
            verify(errors[errors.length - 1].indexOf("names an artist") >= 0, errors[errors.length - 1])
        }

        function test_stopping_and_keeping_provenance_in_the_project() {
            const f = createEditor()
            const p = f.project
            verify(p.cloud.setKey("openai", "test-openai-key"))
            verify(p.generateImage("A cat in a hat"))
            p.cloud.cancel()
            tryCompare(p.cloud, "job", "", 5000)
            compare(p.media.length, 0)
            verify(p.generateImage("A cat in a hat"))
            tryCompare(p.cloud, "job", "", 5000)
            compare(p.media.length, 1)
            // Saved and opened again: still knows where it came from.
            const file = "file://" + tempDir + "/tve_generated_" + Date.now() + ".tve"
            verify(p.save(file))
            const g = createEditor()
            verify(g.project.open(file))
            compare(g.project.media.length, 1)
            compare(g.project.media[0].generated.prompt, "A cat in a hat")
            compare(g.project.media[0].generated.provider, "openai")
        }
    }
}
