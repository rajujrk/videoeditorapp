import QtQuick
import QtTest
import TechieVideoEditor.Theme
import TechieVideoEditor.Player
import TechieVideoEditor.Shell

// Titles through the real editor UI: library, timeline, inspector and the GPU picture.
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
        name: "Titles"
        when: windowShown

        readonly property string av: "file://" + samplesDir + "/test_av_1080p30.mp4"

        function createEditor() {
            const fixture = createTemporaryObject(editorComponent, root)
            verify(fixture)
            waitForRendering(fixture)
            return fixture
        }
        function titleClips(project) {
            let clips = []
            for (const track of project.tracks)
                clips = clips.concat(track.clips.filter((c) => c.isTitle))
            return clips
        }

        function settled(editor, name) {
            let item = null
            let last = null
            tryVerify(() => {
                item = findChild(editor, name)
                if (!item || !item.visible)
                    return false
                const p = item.mapToItem(root, 0, 0)
                const stable = last !== null && p.x === last.x && p.y === last.y
                last = p
                return stable
            }, 3000, name + " never settled")
            for (let a = item; a && a !== root; a = a.parent)
                verify(waitForPolish(a), "layout never ran for " + name)
            return item
        }

        // Colour at a point of `item` (fractions of its size), from a grab of the whole window:
        // grabImage(item) is not cropped to the item here, and grabs are in device pixels.
        function colorAt(item, fx, fy) {
            const image = grabImage(root)
            const scale = image.width / root.width
            const p = item.mapToItem(root, item.width * fx, item.height * fy)
            return image.pixel(Math.round(p.x * scale), Math.round(p.y * scale))
        }

        // Scrolls the nearest scrolling ancestor so `item` is fully in view, like a user would.
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

        function click(editor, name) {
            scrollIntoView(settled(editor, name))
            const item = settled(editor, name)
            mouseMove(item)
            mouseClick(item)
        }

        function test_library_click_adds_a_title_at_the_playhead() {
            const f = createEditor()
            const p = f.project
            p.importAndAppend(av)
            p.seek(0.5)
            click(f.editor, "libraryTab:Titles")
            click(f.editor, "libraryTitle:Big Bold")
            compare(p.undoText, "Add Title")
            const titles = titleClips(p)
            compare(titles.length, 1)
            fuzzyCompare(titles[0].start, 0.5, 1e-6)
            fuzzyCompare(titles[0].duration, 5, 1e-6)
            compare(titles[0].label, "BIG NEWS")
            compare(p.selectedClip, titles[0].id)
            compare(p.selectedClipInfo.text.bold, true)
            // On the overlay track above the main one, not a new track.
            compare(p.tracks.length, 3)

            // A second one at the same time needs a track of its own.
            click(f.editor, "libraryTitle:Subtitle")
            compare(titleClips(p).length, 2)
            compare(p.tracks.length, 4)
            p.undo()
            compare(p.tracks.length, 3)
        }

        function test_inspector_edits_the_selected_title() {
            const f = createEditor()
            const p = f.project
            verify(p.addTitle({ text: "Hello" }, -1, 0))
            const id = p.selectedClip
            click(f.editor, "inspectorTab:Video")
            const area = settled(f.editor, "titleText")
            compare(area.text, "Hello")

            click(f.editor, "titleStyle:italic")
            compare(p.selectedClipInfo.text.italic, true)
            compare(p.undoText, "Edit Title")
            click(f.editor, "titleAlign:right")
            compare(p.selectedClipInfo.text.alignment, "right")
            const swatch = settled(f.editor, "titleColor:2")
            click(f.editor, "titleColor:2") // yellow
            fuzzyCompare(p.selectedClipInfo.text.color[2], 0.2, 1e-6)
            click(f.editor, "titleShadow")
            compare(p.selectedClipInfo.text.shadow, true)

            // The text box follows edits made elsewhere (e.g. undo).
            verify(p.setClipText(id, { text: "Changed" }))
            tryCompare(area, "text", "Changed")
            p.undo()
            tryCompare(area, "text", "Hello")
            verify(!p.setClipText(id, { size: 3 })) // out of range: refused
        }

        function test_titles_have_no_speed_controls() {
            const f = createEditor()
            const p = f.project
            verify(p.addTitle({ text: "Hello" }, -1, 0))
            click(f.editor, "inspectorTab:Speed")
            tryVerify(() => !findChild(f.editor, "speedSlider").visible)
            verify(!p.freezeFrame(2))
        }

        function test_library_previews_draw_the_text() {
            const f = createEditor()
            click(f.editor, "libraryTab:Titles")
            const preview = settled(f.editor, "titlePreview:Big Bold")
            tryVerify(() => preview.status === Image.Ready, 5000)
            compare(preview.sourceSize.width, 320)
            // Somewhere across the middle the yellow text shows.
            let yellow = false
            for (let i = 0; i <= 40 && !yellow; ++i) {
                const c = colorAt(preview, i / 40, 0.5)
                yellow = c.r > 0.8 && c.g > 0.6 && c.b < 0.4
            }
            verify(yellow, "no yellow text across the preview's middle")
        }

        function test_the_player_draws_titles() {
            const f = createEditor()
            const p = f.project
            const player = settled(f.editor, "player")
            // A huge block of white over black (no video): the player's centre turns white.
            verify(p.addTitle({ text: "█████", size: 0.4, fontFamily: "Menlo" }, -1, 0))
            p.seek(1)
            const centre = () => colorAt(player, 0.5, 0.5)
            tryVerify(() => { const c = centre(); return c.r > 0.9 && c.g > 0.9 && c.b > 0.9 },
                      5000, "the title never appeared in the player")
            verify(p.setClipText(p.selectedClip, { color: [1, 0, 0, 1] }))
            tryVerify(() => { const c = centre(); return c.r > 0.9 && c.g < 0.1 && c.b < 0.1 },
                      5000, "the edit never reached the player")
        }
    }
}
