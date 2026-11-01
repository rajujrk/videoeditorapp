import QtQuick
import QtTest
import TechieVideoEditor.Theme
import TechieVideoEditor.Player
import TechieVideoEditor.Shell

// Templates through the real editor: the built-in templates (browsed by category, starred,
// previewed), made in a chosen shape with their cuts on the music's beat; the storyboard
// (slots filled — cropped to fill, slowed to fit — starts moved, texts and music changed);
// and a project saved as a template and used again. Folders: TVE_TEMPLATE_DIR,
// TVE_TEMPLATE_CACHE_DIR, TVE_TEMPLATE_FAVORITES_FILE (scratch, from main.cpp).
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
        name: "Templates"
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
        function openTab(f) {
            click(settled(f.editor, "libraryTab:Templates"))
            return settled(f.editor, "templatesPage")
        }
        function trackNamed(p, name) { return p.tracks.find((t) => t.name === name) }
        function mediaNamed(p, name) { return p.media.find((m) => m.name === name) }

        function test_browse_star_and_preview() {
            const f = createEditor()
            const page = openTab(f)
            const library = f.project.templates
            const builtIn = library.templates.filter((t) => t.builtIn)
            compare(builtIn.length, 8)
            for (const t of builtIn) {
                verify(t.slots >= 4, t.id)
                verify(t.seconds > 8, t.id)
            }
            compare(library.shapes.map((s) => s.id), ["16:9", "9:16", "1:1", "4:3", "3:4", "21:9"])
            click(settled(page, "templateCategory:travel"))
            compare(page.shown().map((t) => t.id), ["tve.template.postcard"])
            const story = library.templates.find((t) => t.id === "tve.template.story")
            compare(story.shape, "9:16")
            // An animated preview, rendered from the template itself.
            const preview = settled(page, "templatePreview:tve.template.postcard")
            tryCompare(preview, "status", Image.Ready, 10000)
            verify(preview.implicitWidth > 100, preview.implicitWidth)
            // Starred: under Favourites, and remembered.
            click(settled(page, "favorite:tve.template.postcard"))
            click(settled(page, "templateCategory:favorites"))
            compare(page.shown().map((t) => t.id), ["tve.template.postcard"])
            const g = createEditor()
            verify(g.project.templates.templates.find((t) => t.id === "tve.template.postcard").favorite)
            g.project.templates.toggleFavorite("tve.template.postcard")
        }

        function test_use_a_template_in_a_shape() {
            const f = createEditor()
            const p = f.project
            const page = openTab(f)
            click(settled(page, "template:tve.template.postcard"))
            const picker = findChild(f.editor, "shapePicker")
            tryCompare(picker, "opened", true)
            compare(picker.shape, "16:9")
            click(settled(picker.contentItem, "shape:9:16"))
            click(settled(picker.contentItem, "useTemplate"))
            tryCompare(picker, "opened", false)
            compare(p.canvasWidth, 1080)
            compare(p.canvasHeight, 1920)
            const info = p.templateInfo
            compare(info.id, "tve.template.postcard")
            compare(info.slots.length, 5)
            compare(info.texts.length, 2)
            verify(info.music.name.length > 0)
            // Cut on the beat: four beats of a 76 BPM piece each.
            const bar = 4 * 60 / 76
            for (let i = 0; i < info.slots.length; ++i) {
                verify(Math.abs(info.slots[i].start - i * bar) < 0.01, info.slots[i].start)
                verify(Math.abs(info.slots[i].duration - bar) < 0.01, info.slots[i].duration)
                verify(!info.slots[i].filled)
            }
            compare(info.slots[0].label, "Arrival")
            verify(Math.abs(info.music.duration - 5 * bar) < 0.02, info.music.duration)
            verify(trackNamed(p, "Titles"))
            verify(trackNamed(p, "Music"))
            verify(p.media.filter((m) => m.placeholder).length === 5)
            // A transition between each slot.
            const main = trackNamed(p, "Video 1")
            compare(main.clips.filter((c) => c.transition && c.transition.transitionId === "tve.transition.dissolve").length, 4)
            // The storyboard shows.
            verify(settled(page, "storyboard"))
            verify(settled(page, "templateSlot:4"))
            // Every shape works.
            for (const shape of ["1:1", "4:3", "3:4", "21:9"]) {
                verify(p.newFromTemplate("tve.template.birthday", shape))
                compare(p.canvasWidth / p.canvasHeight, p.templates.shapes.find((s) => s.id === shape).width
                                                          / p.templates.shapes.find((s) => s.id === shape).height)
            }
            verify(!p.newFromTemplate("tve.template.birthday", "5:4"))
            verify(!p.newFromTemplate("tve.template.nothing"))
        }

        function test_fill_slots_texts_and_music() {
            const f = createEditor()
            const p = f.project
            verify(p.newFromTemplate("tve.template.postcard", "9:16"))
            compare(p.importMedia(["file://" + analysisMediaDir + "/scenes.mov", "file://" + analysisMediaDir + "/talk.mov",
                                   "file://" + stillsDir + "/halves.png"]), 3)
            const page = openTab(f)
            // From the slot's menu: a 3 s video into a 3.16 s slot, slowed to fit, cropped to fill.
            click(settled(page, "chooseSlot:0"))
            const menu = findChild(page, "templateSlot:0").menu
            tryCompare(menu, "opened", true)
            const item = findChild(menu.contentItem, "slotMedia:scenes.mov")
            verify(item)
            waitForRendering(menu.contentItem)
            click(item)
            compare(p.undoText, "Fill Slot")
            let slot = p.templateInfo.slots[0]
            verify(slot.filled)
            compare(slot.name, "scenes.mov")
            verify(slot.speed < 1 && slot.speed > 0.9, slot.speed)
            const filled = trackNamed(p, "Video 1").clips[0]
            compare(filled.path, mediaNamed(p, "scenes.mov").path)
            // A wide picture fills a tall frame by losing its sides.
            p.selectedClip = filled.id
            verify(p.selectedClipInfo.visual.cropLeft > 0.3, p.selectedClipInfo.visual.cropLeft)
            compare(p.selectedClipInfo.visual.cropTop, 0)
            p.undo()
            verify(!p.templateInfo.slots[0].filled)
            p.redo()

            // A longer video starts where asked (no further than leaves the slot full).
            verify(p.fillTemplateSlot(1, mediaNamed(p, "talk.mov").id, 2))
            slot = p.templateInfo.slots[1]
            compare(slot.speed, 1)
            verify(Math.abs(slot.sourceIn - 2) < 0.01)
            verify(settled(page, "slotStart:1"))
            verify(p.setTemplateSlotStart(1, 100))
            verify(Math.abs(p.templateInfo.slots[1].sourceIn - (6.5 - slot.duration)) < 0.05, p.templateInfo.slots[1].sourceIn)
            // A photo.
            verify(p.fillTemplateSlot(2, mediaNamed(p, "halves.png").id))
            compare(p.templateInfo.slots.filter((s) => s.filled).length, 3)
            // Not media.
            verify(!p.fillTemplateSlot(3, 9999))
            verify(!p.fillTemplateSlot(9, mediaNamed(p, "halves.png").id))

            // The texts.
            const field = settled(page, "templateText:0")
            compare(field.text, "SUMMER IN LISBON")
            field.text = "AUTUMN IN KYOTO"
            field.editingFinished()
            compare(p.templateInfo.texts[0].text, "AUTUMN IN KYOTO")
            verify(trackNamed(p, "Titles").clips.some((c) => c.label === "AUTUMN IN KYOTO"))

            // The music: later in the piece, then another piece (long enough).
            verify(p.setTemplateMusicStart(5))
            verify(Math.abs(p.templateInfo.music.sourceIn - 5) < 0.01)
            compare(p.undoText, "Move Music")
            let errors = []
            p.errorOccurred.connect((message) => errors.push(message))
            compare(p.importMedia(["file://" + analysisMediaDir + "/hum.wav"]), 1)
            verify(!p.setTemplateMusic(mediaNamed(p, "hum.wav").id))
            verify(errors[errors.length - 1].indexOf("shorter") >= 0, errors[errors.length - 1])
            const before = p.media.length
            verify(p.addSound("tve.music.pulse"))
            const pulse = p.media[before]
            verify(p.setTemplateMusic(pulse.id))
            compare(p.templateInfo.music.name, pulse.name)
            compare(p.undoText, "Replace Music")

            // Saved and opened again, still a template project.
            const file = "file://" + tempDir + "/tve_template_" + Date.now() + ".tve"
            verify(p.save(file))
            const g = createEditor()
            verify(g.project.open(file))
            compare(g.project.templateInfo.slots.filter((s) => s.filled).length, 3)
            compare(g.project.templateInfo.texts[0].text, "AUTUMN IN KYOTO")
        }

        function test_save_a_project_as_a_template() {
            const f = createEditor()
            const p = f.project
            compare(p.importMedia(["file://" + analysisMediaDir + "/scenes.mov", "file://" + stillsDir + "/halves.png"]), 2)
            verify(p.addClip(mediaNamed(p, "scenes.mov").id))
            verify(p.addClip(mediaNamed(p, "halves.png").id))
            p.seek(0.5)
            verify(p.addTitle({ text: "Hello there" }))
            p.seek(0)
            verify(p.addSound("tve.music.still_water"))
            const page = openTab(f)
            click(settled(page, "saveAsTemplate"))
            const dialog = findChild(f.editor, "saveTemplateDialog")
            tryCompare(dialog, "opened", true)
            // Suggested: both pictures, the title, the music.
            compare(dialog.slotIds.length, 2)
            compare(dialog.textIds.length, 1)
            verify(dialog.musicId > 0)
            settled(dialog.contentItem, "templateName").text = "Two shots"
            click(settled(dialog.contentItem, "saveTemplate"))
            compare(dialog.saved, "user.Two shots")
            dialog.close()
            const made = p.templates.templates.find((t) => t.id === "user.Two shots")
            verify(made)
            verify(!made.builtIn)
            compare(made.slots, 2)
            compare(made.category, "mine")
            click(settled(page, "templateCategory:mine"))
            compare(page.shown().map((t) => t.id), ["user.Two shots"])

            // Used again: the slots are placeholders, the rest came along (copied in).
            verify(p.newFromTemplate("user.Two shots"))
            const info = p.templateInfo
            compare(info.slots.length, 2)
            verify(!info.slots[0].filled)
            verify(info.slots[0].path.indexOf(templatesDir + "/Two shots.tvet/media/") === 0, info.slots[0].path)
            compare(info.texts.length, 1)
            compare(info.texts[0].text, "Hello there")
            verify(info.music.path.indexOf(templatesDir + "/Two shots.tvet/media/") === 0, info.music.path)
            verify(!p.media.some((m) => m.name === "scenes.mov"))
            // Deleted from the picker.
            click(settled(page, "templateCategory:mine"))
            click(settled(page, "template:user.Two shots"))
            const picker = findChild(f.editor, "shapePicker")
            tryCompare(picker, "opened", true)
            click(settled(picker.contentItem, "deleteTemplate"))
            verify(!p.templates.templates.some((t) => t.id === "user.Two shots"))
            // Nothing replaceable: refused.
            const g = createEditor()
            compare(g.project.saveAsTemplate("Empty", "mine", [], [], -1), "")
        }
    }
}
