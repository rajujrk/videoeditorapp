import QtQuick
import QtTest
import TechieVideoEditor.Theme
import TechieVideoEditor.Player
import TechieVideoEditor.Shell

// Your own effects, filters and stickers (all under Mine): an effect written in the shader
// editor, compiled in the app, previewed, saved and drawn on a clip; a clip's effects saved as
// one effect and put on another clip; a grade and a LUT kept as filters; a picture and a
// video's frame made into stickers. (Scratch folders: TVE_MY_* in the test runner.)
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
            ProjectController { id: controller; view: screen.player; autosaveEnabled: false }
            EditorScreen { id: screen; anchors.fill: parent; project: controller }
        }
    }

    TestCase {
        id: testCase
        name: "MyItems"
        when: windowShown

        function createEditor() {
            const fixture = createTemporaryObject(editorComponent, root)
            verify(fixture)
            waitForRendering(fixture)
            return fixture
        }
        function mainTrack(p) { return p.tracks.find((t) => t.main) }
        // Red | blue halves on the main track, selected.
        function halves(f) {
            const p = f.project
            compare(p.importMedia(["file://" + stillsDir + "/halves.png"]), 1)
            verify(p.addClip(p.media[0].id, mainTrack(p).id, 0))
            p.selectedClip = mainTrack(p).clips[0].id
            p.seek(0.5)
            return p.selectedClip
        }
        function canvasColor(f, u, v) {
            const overlays = ["maskOverlay", "transformOverlay", "guidesOverlay", "compareOverlay"].map((n) => findChild(f.editor, n))
            overlays.forEach((o) => o.opacity = 0)
            const image = grabImage(root)
            overlays.forEach((o) => o.opacity = 1)
            const guides = overlays[2]
            const r = guides.canvasRect
            const scale = image.width / root.width
            const at = guides.mapToItem(root, r.x + u * r.width, r.y + v * r.height)
            return image.pixel(Math.round(at.x * scale), Math.round(at.y * scale))
        }
        function openTab(f, tab, category, chipPrefix) {
            findChild(f.editor, "libraryTab:" + tab).clicked()
            const chip = findChild(f.editor, chipPrefix + category)
            verify(chip, chipPrefix + category)
            chip.clicked()
        }

        // A green-only effect: red and blue taken out, as much as its setting says.
        readonly property var greenOnly: ({
            title: "Green only",
            code: "vec4 effect(vec2 uv)\n{\n    vec4 c = texture(tve_source, uv);\n    return vec4(c.r * (1.0 - amount), c.g, c.b * (1.0 - amount), c.a);\n}\n",
            params: [{ id: "amount", title: "Amount", type: "float", min: 0, max: 1, default: 1 }]
        })

        function test_an_effect_written_in_the_shader_editor() {
            const f = createEditor()
            const p = f.project
            const clip = halves(f)
            // Mistakes are said, by line.
            let result = p.compileShaderEffect({ title: "Broken", code: "vec4 effect(vec2 uv)\n{\n    return nope;\n}\n", params: [] })
            verify(!result.ok)
            verify(result.error.indexOf("Line 3") >= 0, result.error)
            result = p.compileShaderEffect({ title: "No function", code: "float x;", params: [] })
            verify(!result.ok)
            verify(result.error.indexOf("vec4 effect(vec2 uv)") >= 0, result.error)
            result = p.compileShaderEffect(Object.assign({}, greenOnly, { params: [{ id: "tve_x", type: "float" }] }))
            verify(!result.ok)
            // Two settings of one name.
            result = p.compileShaderEffect(Object.assign({}, greenOnly, { params: greenOnly.params.concat(greenOnly.params) }))
            verify(!result.ok)
            verify(result.error.indexOf("amount") >= 0, result.error)

            // Compiled for a preview: an effect of the library (a draft, not listed).
            result = p.compileShaderEffect(greenOnly)
            verify(result.ok, result.error)
            verify(result.previewId.indexOf("user.draft_") === 0)
            compare(p.myEffects().length, 0)

            // Saved: in the Effects tab under Mine, with a preview, and on the clip like any effect.
            const id = p.saveShaderEffect(greenOnly)
            compare(id, "user.green_only")
            const mine = p.myEffects()
            compare(mine.length, 1)
            compare(mine[0].kind, "shader")
            compare(mine[0].title, "Green only")
            openTab(f, "Effects", "mine", "effectCategory:")
            const tile = findChild(f.editor, "libraryEffect:user.green_only")
            tryVerify(() => tile !== null && tile.visible)
            verify(findChild(f.editor, "mineEffectsBar").visible)
            verify(p.addClipEffect(clip, id))
            // Red | blue halves: both go dark (only green is left, and they have none).
            tryVerify(() => { const c = canvasColor(f, 0.25, 0.5); return c.r < 0.1 && c.b < 0.1 }, 5000, "the effect did not draw")
            // Its setting is a slider like any effect's.
            verify(p.setClipEffectParam(clip, 0, "amount", 0))
            tryVerify(() => canvasColor(f, 0.25, 0.5).r > 0.6, 5000, "the setting did not reach the shader")

            // Edited and saved again under the same id: the clip draws the new code.
            const spec = p.shaderEffectSpec(id)
            compare(spec.title, "Green only")
            spec.code = spec.code.replace("c.g, c.b * (1.0 - amount)", "1.0, c.b * (1.0 - amount)")
            compare(p.saveShaderEffect(spec), id)
            tryVerify(() => canvasColor(f, 0.25, 0.5).g > 0.9, 5000, "the edit did not reach the clip")

            // Shared as a file and brought back: a second effect of yours.
            const file = "file://" + tempDir + "/green.tveeffect"
            verify(p.exportMyEffect(id, file))
            const again = p.importMyEffect(file)
            compare(again, "user.green_only_2")
            compare(p.myEffects().length, 2)
            verify(p.removeMyEffect(again))
            compare(p.myEffects().length, 1)

            // The editor: opens with a starting effect that compiles, and saves it.
            const dialog = findChild(f.editor, "shaderEditor")
            dialog.openFor("")
            tryVerify(() => dialog.opened)
            tryVerify(() => dialog.previewId !== "", 5000, "the editor's effect never compiled: " + dialog.error)
            // The code is coloured as GLSL.
            const highlighter = findChild(dialog.contentItem, "shaderHighlighter")
            verify(highlighter)
            verify(highlighter.document !== null)
            compare(dialog.error, "")
            findChild(dialog.contentItem, "shaderTitle").text = "Warm wave"
            findChild(dialog.contentItem, "shaderSave").clicked()
            tryVerify(() => !dialog.opened)
            verify(p.myEffects().some((e) => e.id === "user.warm_wave"))
            verify(p.removeMyEffect("user.warm_wave"))
            verify(p.removeMyEffect(id))
            compare(p.myEffects().length, 0)
        }

        function test_a_clips_effects_saved_as_one_and_filters_of_your_own() {
            const f = createEditor()
            const p = f.project
            const first = halves(f)
            verify(p.addClipEffect(first, "tve.color.black_white"))
            verify(p.addClipEffect(first, "tve.blur.gaussian"))
            verify(p.setClipEffectParam(first, 0, "contrast", 1.5))
            const stack = p.saveEffectStack(first, "Moody")
            verify(stack.indexOf("stack.") === 0, p.lastError)
            const mine = p.myEffects().find((e) => e.id === stack)
            compare(mine.kind, "stack")
            compare(mine.count, 2)
            // On another clip in one step, settings and all; dropped on it from the library too.
            compare(p.importMedia(["file://" + stillsDir + "/disc.png"]), 1)
            verify(p.addClip(p.media[1].id, mainTrack(p).id, 5))
            const second = mainTrack(p).clips[1].id
            verify(p.applyEffectStack(second, stack))
            compare(p.undoText, "Add My Effect")
            p.selectedClip = second
            compare(p.selectedClipInfo.effects.length, 2)
            compare(p.selectedClipInfo.effects[0].effectId, "tve.color.black_white")
            const timeline = findChild(f.editor, "timelinePanel")
            p.undo()
            verify(timeline.dropItem("application/x-tve-effect", stack, 6 * timeline.pixelsPerSecond, timeline.trackHeight("video") * 1.5))
            p.selectedClip = second
            compare(p.selectedClipInfo.effects.length, 2)
            openTab(f, "Effects", "mine", "effectCategory:")
            tryVerify(() => findChild(f.editor, "libraryEffect:" + stack) !== null)

            // A grade kept as a filter, and a LUT made one: under Filters › Mine, put on a clip.
            verify(p.setClipColor(first, { saturation: -1 }))
            verify(p.saveColorPreset(first, "Grey"))
            verify(p.importLutAsFilter("file://" + fixturesDir + "/swap.cube", "Swapped"))
            verify(!p.importLutAsFilter("file://" + stillsDir + "/disc.png", "Not a LUT"))
            openTab(f, "Filters", "mine", "lookCategory:")
            tryVerify(() => findChild(f.editor, "libraryLook:preset:Grey") !== null && findChild(f.editor, "libraryLook:preset:Swapped") !== null)
            verify(findChild(f.editor, "mineFiltersBar").visible)
            verify(timeline.dropItem("application/x-tve-look", "preset:Swapped", 6 * timeline.pixelsPerSecond, timeline.trackHeight("video") * 1.5))
            compare(p.undoText, "Apply Color Preset")
            p.selectedClip = second
            compare(p.selectedClipInfo.color.lutName, "swap.cube")
            verify(p.deleteColorPreset("Grey"))
            verify(p.deleteColorPreset("Swapped"))
            verify(p.removeMyEffect(stack))
            verify(!p.applyEffectStack(second, stack))
        }

        function test_stickers_of_your_own() {
            const f = createEditor()
            const p = f.project
            const clip = halves(f)
            // A picture brought in: a sticker under Mine, with a preview, put on the timeline.
            const before = p.stickers.length
            const disc = p.addMySticker("file://" + stillsDir + "/disc.png")
            compare(disc, "mine/disc.png")
            compare(p.stickers.length, before + 1)
            const entry = p.stickers.find((s) => s.id === disc)
            compare(entry.category, "mine")
            verify(entry.picture)
            openTab(f, "Stickers", "mine", "stickerCategory:")
            tryVerify(() => findChild(f.editor, "librarySticker:" + disc) !== null)
            // Yours are named under the picture; the built-in ones are not.
            tryVerify(() => findChild(f.editor, "stickerName:" + disc) !== null)
            const name = findChild(f.editor, "stickerName:" + disc)
            verify(name.visible)
            compare(name.text, "disc")
            verify(findChild(f.editor, "mineStickersBar").visible)
            verify(p.addSticker(disc), p.lastError)
            compare(p.undoText, "Add Sticker")
            verify(p.media.some((m) => m.path.indexOf("disc.png") >= 0 && m.still))
            // Not a picture: said so.
            verify(p.addMySticker("file://" + fixturesDir + "/swap.cube") === "")
            // The selected clip (a picture) made into one; a second copy gets its own name.
            p.selectedClip = clip
            compare(p.saveClipAsSticker(clip), "mine/halves.png")
            compare(p.saveClipAsSticker(clip), "mine/halves 2.png")
            // A video's frame at the playhead.
            verify(p.importAndAppend("file://" + samplesDir + "/test_av_1080p30.mp4"))
            const video = mainTrack(p).clips[1]
            p.seek(video.start + 0.5)
            const frame = p.saveClipAsSticker(video.id)
            verify(frame.indexOf("mine/test_av_1080p30") === 0, p.lastError)
            verify(frame.endsWith(".png"))
            // Taken away.
            for (const id of [disc, "mine/halves.png", "mine/halves 2.png", frame])
                verify(p.removeMySticker(id))
            compare(p.stickers.length, before)
            verify(!p.removeMySticker("mine/../etc"))
        }
    }
}
