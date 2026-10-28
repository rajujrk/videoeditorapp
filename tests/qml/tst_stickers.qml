import QtQuick
import QtTest
import TechieVideoEditor.Theme
import TechieVideoEditor.Player
import TechieVideoEditor.Shell

// Stickers (Lottie) and video with transparency, through the real editor.
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
        name: "Stickers"
        when: windowShown

        function still(name) { return "file://" + stillsDir + "/" + name }

        function createEditor() {
            const fixture = createTemporaryObject(editorComponent, root)
            verify(fixture)
            waitForRendering(fixture)
            return fixture
        }
        function mediaNamed(project, name) { return project.media.find((m) => m.name === name) }
        function mainTrack(project) { return project.tracks.find((t) => t.main) }
        // The video track just above the main one.
        function trackAbove(project) {
            const tracks = project.tracks // top to bottom
            return tracks[tracks.findIndex((t) => t.main) - 1]
        }
        // One still on the main track, selected, at the playhead.
        function withStill(name) {
            const f = createEditor()
            const p = f.project
            compare(p.importMedia([still(name)]), 1)
            verify(p.addClip(p.media[0].id))
            p.selectedClip = mainTrack(p).clips[0].id
            p.seek(1)
            return f
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
        function click(editor, name) {
            scrollIntoView(settled(editor, name))
            const item = settled(editor, name)
            mouseMove(item)
            mouseClick(item)
        }
        // The player's colour at canvas fraction (u, v), without the mask handles over it.
        function canvasColor(f, u, v) {
            const overlay = findChild(f.editor, "maskOverlay")
            const r = overlay.canvasRect
            overlay.opacity = 0
            const image = grabImage(root)
            overlay.opacity = 1
            const scale = image.width / root.width
            const p = overlay.mapToItem(root, r.x + u * r.width, r.y + v * r.height)
            return image.pixel(Math.round(p.x * scale), Math.round(p.y * scale))
        }
        // Where canvas fraction (u, v) is in the overlay.
        function canvasPoint(f, u, v) {
            const r = findChild(f.editor, "maskOverlay").canvasRect
            return Qt.point(r.x + u * r.width, r.y + v * r.height)
        }
        // Drags on the overlay from one point to another (the handle under the press follows;
        // mouseDrag would map each step from the moving handle).
        function dragOn(f, from, to) {
            const overlay = findChild(f.editor, "maskOverlay")
            mousePress(overlay, from.x, from.y)
            for (let i = 1; i <= 10; ++i)
                mouseMove(overlay, from.x + (to.x - from.x) * i / 10, from.y + (to.y - from.y) * i / 10)
            mouseRelease(overlay, to.x, to.y)
        }
        function isRed(c) { return c.r > 0.6 && c.g < 0.3 && c.b < 0.3 }
        function isGreen(c) { return c.g > 0.5 && c.r < 0.2 }
        function isBlue(c) { return c.b > 0.6 && c.r < 0.3 }
        function isBlack(c) { return c.r < 0.08 && c.g < 0.08 && c.b < 0.08 }
        function showsAt(f, u, v, test, message) {
            tryVerify(() => test(canvasColor(f, u, v)), 5000, message)
        }

        // Green all over (the keyed still) on the main track, at 1 s.
        function overGreen() {
            const f = createEditor()
            const p = f.project
            compare(p.importMedia([still("keyed.png")]), 1)
            verify(p.addClip(p.media[0].id))
            p.seek(1)
            showsAt(f, 0.1, 0.1, isGreen, "the background never showed")
            return f
        }
        function isYellow(c) { return c.r > 0.8 && c.g > 0.6 && c.b < 0.4 }

        function test_the_sticker_library() {
            const f = createEditor()
            const p = f.project
            compare(p.stickers.length, 100)
            click(f.editor, "libraryTab:Stickers")
            const grid = settled(f.editor, "stickersGrid")
            compare(grid.count, 100)
            const counts = { shapes: 24, marks: 32, nature: 20, emoji: 16, party: 8 }
            for (const category in counts) {
                click(f.editor, "stickerCategory:" + category)
                tryCompare(grid, "count", counts[category])
            }
            click(f.editor, "stickerCategory:all")
            settled(f.editor, "librarySearch").text = "heart"
            tryCompare(grid, "count", 4)
            const preview = settled(f.editor, "stickerPreview:heart_pulse")
            tryCompare(preview, "status", Image.Ready, 5000)
            compare(preview.sourceSize.height, 160)
        }

        function test_a_sticker_plays_over_the_picture() {
            const f = overGreen()
            const p = f.project
            click(f.editor, "libraryTab:Stickers")
            click(f.editor, "stickerCategory:shapes")
            click(f.editor, "librarySticker:star_spin")
            compare(p.undoText, "Add Sticker")
            const info = p.selectedClipInfo
            verify(info.isSticker)
            compare(info.label, "Star · Spin") // its title, not its file's name
            verify(info.isStill) // no speed, any length
            compare(info.duration, 5)
            fuzzyCompare(info.visual.scale, 0.4, 1e-6)
            const media = p.media.find((m) => m.sticker)
            verify(media)
            verify(media.alpha)
            // On a track above the main one; the star in the middle, the picture around it.
            const main = p.tracks.findIndex((t) => t.main)
            verify(p.tracks.findIndex((t) => t.clips.some((c) => c.id === info.id)) < main)
            showsAt(f, 0.5, 0.5, isYellow, "the star never showed")
            showsAt(f, 0.1, 0.1, isGreen, "the sticker covered the picture")
            // It moves: the frame differs a moment later.
            const overlay = findChild(f.editor, "maskOverlay")
            p.seek(1.1)
            let before = null
            tryVerify(() => { before = canvasColor(f, 0.5, 0.39); return true })
            p.seek(1.4)
            tryVerify(() => {
                const c = canvasColor(f, 0.5, 0.39)
                return Math.abs(c.r - before.r) + Math.abs(c.g - before.g) + Math.abs(c.b - before.b) > 0.2
            }, 5000, "the star did not spin")
            // No speed for stickers.
            click(f.editor, "inspectorTab:Speed")
            verify(!p.setClipSpeed(info.id, 2, false))
        }

        function test_importing_a_lottie_file() {
            const f = createEditor()
            const p = f.project
            const file = "file://" + fixturesDir + "/../../../effects/stickers/heart_pulse.json"
            compare(p.importMedia([file]), 1)
            const media = p.media[0]
            verify(media.sticker)
            compare(media.width, 512)
            verify(p.addClip(media.id))
            verify(p.selectedClipInfo.isSticker)
            // A file that is not one is refused with a reason.
            compare(p.importMedia(["file://" + fixturesDir + "/../../../effects/stickers/stickers.json"]), 0)
            verify(p.lastError.indexOf("not a Lottie animation") >= 0)
        }

        function test_video_with_transparency_shows_what_is_below() {
            const f = overGreen()
            const p = f.project
            const alpha = "file://" + samplesDir + "/test_alpha_prores.mov"
            if (p.importMedia([alpha]) !== 1)
                skip("sample missing: run tools/make_test_media.sh")
            const media = p.media.find((m) => m.name === "test_alpha_prores.mov")
            verify(media.alpha)
            verify(!media.sticker)
            verify(p.addClip(media.id, trackAbove(p).id, 0))
            p.seek(0.02)
            // The red disc starts a quarter of the way across; around it, the green below.
            showsAt(f, 0.25, 0.5, isRed, "the disc never showed")
            showsAt(f, 0.75, 0.5, isGreen, "the transparent part hid the picture below")
            showsAt(f, 0.25, 0.1, isGreen, "the transparent part hid the picture below")
        }

        function test_stickers_survive_save_and_open() {
            const f = createEditor()
            const p = f.project
            verify(p.importMedia([still("keyed.png")]) === 1)
            verify(p.addClip(p.media[0].id))
            verify(p.addSticker("heart_bounce"))
            const file = "file://" + tempDir + "/tve_stickers_test.tve"
            verify(p.save(file))
            verify(p.open(file))
            const media = p.media.find((m) => m.sticker)
            verify(media)
            verify(media.alpha)
            p.seek(1)
            showsAt(f, 0.5, 0.5, (c) => c.r > 0.8 && c.g < 0.5, "the heart never showed after opening")
        }
    }
}
