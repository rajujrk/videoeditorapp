import QtQuick
import QtTest
import TechieVideoEditor.Theme
import TechieVideoEditor.Player
import TechieVideoEditor.Shell

// The effects library (categories, search, previews) and canvas effects in the player.
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
        name: "EffectLibrary"
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

        function effectsGrid(f) { return findChild(f.editor, "effectsGrid") }

        function test_categories_search_and_previews() {
            const f = createEditor()
            click(f.editor, "libraryTab:Effects")
            const grid = settled(f.editor, "effectsGrid")
            compare(grid.count, 61)
            // By category, overlays last.
            compare(grid.model[0].categories[0], "blur")
            compare(grid.model[60].categories[0], "overlay")
            click(f.editor, "effectCategory:blur")
            tryCompare(grid, "count", 9)
            verify(grid.model.every((e) => e.categories.indexOf("blur") >= 0))
            click(f.editor, "effectCategory:frame")
            tryCompare(grid, "count", 5)
            compare(grid.model.filter((e) => e.canvas).length, 4)
            click(f.editor, "effectCategory:overlay")
            tryCompare(grid, "count", 6)
            click(f.editor, "effectCategory:all")
            tryCompare(grid, "count", 61)

            // Search by title, within the category.
            const search = settled(f.editor, "librarySearch")
            search.text = "glow"
            tryCompare(grid, "count", 2) // Glow, Outer Glow
            click(f.editor, "effectCategory:light")
            tryCompare(grid, "count", 1)
            compare(grid.model[0].id, "tve.light.glow")
            click(f.editor, "effectCategory:all")
            search.text = "nothing like this"
            tryCompare(grid, "count", 0)
            // The search is per tab, and filters transitions too.
            click(f.editor, "libraryTab:Transitions")
            compare(search.text, "")
            search.text = "wipe"
            const transitions = f.project.availableTransitions.filter((t) => t.title.toLowerCase().indexOf("wipe") >= 0)
            verify(transitions.length >= 2)
            tryVerify(() => findChild(f.editor, "libraryTransition:" + transitions[0].id) !== null)
            verify(findChild(f.editor, "libraryTransition:tve.transition.dissolve") === null)
            click(f.editor, "libraryTab:Effects")
            compare(search.text, "")

            // GPU previews of the effect on a sample scene; a canvas effect shows it inset.
            click(f.editor, "effectCategory:light")
            const glow = settled(f.editor, "effectPreview:tve.light.glow")
            tryCompare(glow, "status", Image.Ready, 5000)
            compare(glow.sourceSize.width, 192)
            compare(glow.sourceSize.height, 108)
            click(f.editor, "effectCategory:frame")
            const shadow = settled(f.editor, "effectPreview:tve.frame.drop_shadow")
            tryCompare(shadow, "status", Image.Ready, 5000)
            tryVerify(() => {
                // The grey canvas around the inset scene.
                const c = colorAtItem(shadow, 0.05, 0.05)
                return Math.abs(c.r - 0.42) < 0.05 && Math.abs(c.g - c.b) < 0.05
            }, 3000, "the canvas preview has no grey surround")
        }
        function colorAtItem(item, fx, fy) {
            const image = grabImage(root)
            const scale = image.width / root.width
            const p = item.mapToItem(root, item.width * fx, item.height * fy)
            return image.pixel(Math.round(p.x * scale), Math.round(p.y * scale))
        }

        // The green screen still under a half-size halves.png on the track above, selected.
        function withInset() {
            const f = createEditor()
            const p = f.project
            compare(p.importMedia([still("keyed.png"), still("halves.png")]), 2)
            verify(p.addClip(mediaNamed(p, "keyed.png").id))
            const above = trackAbove(p)
            verify(p.addClip(mediaNamed(p, "halves.png").id, above.id, 0))
            const top = p.tracks.find((t) => t.id === above.id).clips[0].id
            p.selectedClip = top
            verify(p.setClipVisual(top, { scale: 0.5 }))
            p.seek(1)
            // halves (2:1, letterboxed) at half size: x 0.25..0.75, y 0.28..0.72 of the canvas.
            showsAt(f, 0.3, 0.5, isRed, "the inset never showed")
            showsAt(f, 0.5, 0.15, isGreen, "the clip below never showed")
            return f
        }

        function test_drop_shadow_falls_outside_the_picture() {
            const f = withInset()
            const p = f.project
            click(f.editor, "libraryTab:Effects")
            click(f.editor, "effectCategory:frame")
            click(f.editor, "libraryEffect:tve.frame.drop_shadow")
            compare(p.selectedClipInfo.effects[0].effectId, "tve.frame.drop_shadow")
            verify(p.setClipEffectParam(p.selectedClip, 0, "offset", 5))
            verify(p.setClipEffectParam(p.selectedClip, 0, "blur", 0.2))
            verify(p.setClipEffectParam(p.selectedClip, 0, "opacity", 1))
            // Below and right of the picture: shadow (dark), not the green below.
            showsAt(f, 0.765, 0.735, isBlack, "no shadow below and right of the picture")
            showsAt(f, 0.235, 0.265, isGreen, "a shadow above and left of the picture")
            showsAt(f, 0.3, 0.5, isRed, "the shadow covered the picture")
            // The picture's own effects still apply before it is placed.
            verify(p.addClipEffect(p.selectedClip, "tve.color.black_white"))
            showsAt(f, 0.3, 0.5, (c) => Math.abs(c.r - c.g) < 0.05, "a picture effect next to the shadow did not apply")
            showsAt(f, 0.765, 0.735, isBlack, "the shadow went away")
        }

        function test_outline_and_background_fill() {
            const f = withInset()
            const p = f.project
            verify(p.addClipEffect(p.selectedClip, "tve.frame.outline"))
            verify(p.setClipEffectParam(p.selectedClip, 0, "width", 3))
            verify(p.setClipEffectParam(p.selectedClip, 0, "color", [1, 0, 1, 1]))
            // Just above the picture's top edge: the magenta stroke.
            showsAt(f, 0.5, 0.265, (c) => c.r > 0.8 && c.b > 0.8 && c.g < 0.2, "no outline above the picture")
            showsAt(f, 0.5, 0.15, isGreen, "the outline reached too far")
            verify(p.removeClipEffect(p.selectedClip, 0))

            // A colour fill covers the whole canvas behind the clip (hiding the clip below).
            verify(p.addClipEffect(p.selectedClip, "tve.frame.background_fill"))
            verify(p.setClipEffectParam(p.selectedClip, 0, "mode", 1))
            verify(p.setClipEffectParam(p.selectedClip, 0, "color", [0, 0, 1, 1]))
            showsAt(f, 0.5, 0.15, isBlue, "the fill did not cover the canvas")
            showsAt(f, 0.3, 0.5, isRed, "the fill covered the clip")
            // A blurred fill takes its colours from the clip: red on the left, blue on the right.
            verify(p.setClipEffectParam(p.selectedClip, 0, "mode", 0))
            verify(p.setClipEffectParam(p.selectedClip, 0, "dim", 0))
            showsAt(f, 0.05, 0.5, (c) => c.r > c.b + 0.3, "the blurred fill is not red on the left")
            showsAt(f, 0.95, 0.5, (c) => c.b > c.r + 0.3, "the blurred fill is not blue on the right")
        }
    }
}
