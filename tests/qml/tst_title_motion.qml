import QtQuick
import QtTest
import TechieVideoEditor.Theme
import TechieVideoEditor.Player
import TechieVideoEditor.Shell

// Title templates, text animations, shapes and scrolling, through the real editor and the title renderer.
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
        name: "TitleMotion"
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

        // How much of an item's area (a fraction, sampled on a grid) is bright.
        function brightShare(item, x0, y0, x1, y1, rows) {
            const image = grabImage(root)
            const scale = image.width / root.width
            let bright = 0, total = 0
            for (let fy = y0; fy <= y1; fy += (y1 - y0) / (rows || 20)) {
                for (let fx = x0; fx <= x1; fx += (x1 - x0) / 40) {
                    const p = item.mapToItem(root, item.width * fx, item.height * fy)
                    const c = image.pixel(Math.round(p.x * scale), Math.round(p.y * scale))
                    if (c.r + c.g + c.b > 2.0)
                        ++bright
                    ++total
                }
            }
            return bright / total
        }
        // A title preview on a black background, at `t` seconds into a clip of `duration`.
        Component {
            id: previewComponent
            Rectangle {
                property var style: ({})
                property real t: 0
                width: 320
                height: 180
                color: "black"
                Image {
                    anchors.fill: parent
                    source: "image://tvetitle/" + encodeURIComponent(JSON.stringify(Object.assign({ t: parent.t }, parent.style)))
                }
            }
        }
        function preview(style, t) {
            // grabImage() renders the scene itself; waitForRendering() would sit out its timeout
            // when nothing else changes.
            const item = createTemporaryObject(previewComponent, root, { style: style, t: t })
            tryCompare(item.children[0], "status", Image.Ready)
            return item
        }

        function test_templates_by_category_and_search() {
            const f = createEditor()
            const p = f.project
            click(f.editor, "libraryTab:Titles")
            const grid = settled(f.editor, "titlesGrid")
            compare(grid.count, 40)
            const counts = { titles: 11, lower: 8, callouts: 8, credits: 5, animated: 8 }
            for (const category in counts) {
                click(f.editor, "titleCategory:" + category)
                tryCompare(grid, "count", counts[category])
            }
            click(f.editor, "titleCategory:all")
            tryCompare(grid, "count", 40)
            settled(f.editor, "librarySearch").text = "bubble"
            tryCompare(grid, "count", 3) // Speech, Reply and Thought Bubble
            settled(f.editor, "librarySearch").text = ""
            // Every template makes a valid title (presets, shapes and all).
            const templates = grid.model
            for (let i = 0; i < templates.length; ++i)
                verify(p.addTitle(templates[i].style, -1, i * 13), templates[i].name + ": " + p.lastError)
            // Credits last longer.
            let longest = 0
            for (const t of p.tracks)
                for (const c of t.clips)
                    longest = Math.max(longest, c.duration)
            compare(longest, 12) // the rolling credits
            compare(p.textAnimations.length, 19)
        }

        function test_in_animations_and_shapes_render() {
            // Fading in: nothing at the start, all of it once it has arrived.
            const fade = { text: "HELLO", size: 0.25, bold: true, animationIn: { preset: "fade", unit: "whole", duration: 1 } }
            verify(brightShare(preview(fade, 0), 0.1, 0.3, 0.9, 0.7) === 0)
            verify(brightShare(preview(fade, 2), 0.1, 0.3, 0.9, 0.7) > 0.05)
            // Typed out: at a quarter of the way, only the left of the text is there.
            const typed = { text: "ABCDEFGH", size: 0.2, bold: true, animationIn: { preset: "typewriter", unit: "character", duration: 2 } }
            const early = preview(typed, 0.55)
            verify(brightShare(early, 0.05, 0.35, 0.4, 0.65) > 0.02)
            verify(brightShare(early, 0.6, 0.35, 0.95, 0.65) === 0)
            verify(brightShare(preview(typed, 3), 0.6, 0.35, 0.95, 0.65) > 0.02)
            // A white box around dark text: the box shows outside the letters.
            const boxed = { text: "Hi", size: 0.15, color: [0, 0, 0, 1], shape: { kind: "box", color: [1, 1, 1, 1], padding: 0.6 } }
            verify(brightShare(preview(boxed, 1), 0.4, 0.3, 0.6, 0.7) > 0.3)
            verify(brightShare(preview(boxed, 1), 0.0, 0.0, 0.2, 0.2) === 0)
            // Every shape draws something.
            for (const kind of ["box", "rounded", "pill", "speech", "thought", "banner", "underline", "circle", "arrow", "tag"]) {
                const shaped = { text: "x", size: 0.12, color: [0, 0, 0, 0], shape: { kind: kind, color: [1, 1, 1, 1], padding: 0.5 } }
                verify(brightShare(preview(shaped, 1), 0.2, 0.2, 0.8, 0.8, 120) > 0, kind + " drew nothing") // thin lines too
            }
        }

        function test_credits_roll_and_tickers_crawl() {
            const credits = { text: "ONE\nTWO\nTHREE", size: 0.12, bold: true, scroll: "rollUp", duration: 10 }
            // Below the frame at the start, in the middle half way, above it at the end.
            verify(brightShare(preview(credits, 0.01), 0, 0, 1, 1) === 0)
            verify(brightShare(preview(credits, 5), 0.3, 0.3, 0.7, 0.7) > 0.05)
            verify(brightShare(preview(credits, 9.99), 0, 0, 1, 1) === 0)
            const ticker = { text: "NEWS NEWS NEWS", size: 0.12, bold: true, scroll: "crawlLeft", duration: 10 }
            verify(brightShare(preview(ticker, 0.01), 0, 0, 1, 1) === 0)
            verify(brightShare(preview(ticker, 5), 0, 0.4, 1, 0.6) > 0.05)
            // Part way in, the text has come in from the right.
            const entering = preview(ticker, 2)
            verify(brightShare(entering, 0.6, 0.4, 1.0, 0.6) > brightShare(entering, 0.0, 0.4, 0.3, 0.6))
        }

        function test_an_animated_title_moves_in_the_player() {
            const f = createEditor()
            const p = f.project
            p.seek(0)
            click(f.editor, "libraryTab:Titles")
            click(f.editor, "titleCategory:animated")
            click(f.editor, "libraryTitle:Zoom")
            const info = p.selectedClipInfo
            verify(info.isTitle)
            compare(info.text.animationIn.preset, "zoom")
            const player = settled(f.editor, "player")
            const overlay = findChild(f.editor, "maskOverlay")
            // At the very start the letters have not arrived; a second in, they have.
            p.seek(0.02)
            tryVerify(() => brightShare(player, 0.2, 0.35, 0.8, 0.65) < 0.01, 5000, "the title showed before it arrived")
            p.seek(2)
            tryVerify(() => brightShare(player, 0.2, 0.35, 0.8, 0.65) > 0.05, 5000, "the title never arrived")
            // Leaving at the end of the clip.
            p.seek(info.duration - 0.02)
            tryVerify(() => brightShare(player, 0.2, 0.35, 0.8, 0.65) < 0.01, 5000, "the title did not leave")
        }

        function test_inspector_animation_and_shape() {
            const f = createEditor()
            const p = f.project
            verify(p.addTitle({ text: "Hello" }))
            click(f.editor, "inspectorTab:Video")
            settled(f.editor, "titleShapeSection").expanded = true // collapsed while there is no shape
            const kind = settled(f.editor, "titleShapeKind")
            kind.activated(4) // speech bubble
            compare(p.selectedClipInfo.text.shape.kind, "speech")
            verify(settled(f.editor, "titleShapeTail").visible)
            const inPreset = settled(f.editor, "titleAnimation:animationIn")
            const bounce = inPreset.model.findIndex((m) => m.id === "bounce")
            inPreset.activated(bounce)
            compare(p.selectedClipInfo.text.animationIn.preset, "bounce")
            const unit = settled(f.editor, "titleAnimationUnit:animationIn")
            unit.activated(2) // by word
            compare(p.selectedClipInfo.text.animationIn.unit, "word")
            // Loops offer only loops; ins and outs never do.
            const loop = settled(f.editor, "titleAnimation:animationLoop")
            verify(loop.model.slice(1).every((m) => m.loop))
            verify(inPreset.model.slice(1).every((m) => !m.loop))
            settled(f.editor, "titleScroll").activated(1)
            compare(p.selectedClipInfo.text.scroll, "rollUp")
            p.undo()
            compare(p.selectedClipInfo.text.scroll, "none")
            verify(!p.setClipText(p.selectedClip, { animationLoop: { preset: "fade" } }))
        }
    }
}
