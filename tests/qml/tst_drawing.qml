import QtQuick
import QtTest
import TechieVideoEditor.Theme
import TechieVideoEditor.Player
import TechieVideoEditor.Shell

// Paths and overlays through the real editor: drawing on the player (a brush stroke, then a
// filled rectangle into the same drawing), the drawing's inspector, a title following a drawn
// path and running along one (or a shape of ours), and overlay transitions laid over a cut —
// a light leak (screened) and a paint stroke covering the cut, each its own movie with alpha
// made once into the overlay cache (TVE_OVERLAY_CACHE_DIR, scratch, from main.cpp).
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
        name: "Drawing"
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
        function click(parent, name) {
            scrollIntoView(settled(parent, name))
            const item = settled(parent, name)
            mouseMove(item)
            mouseClick(item)
        }
        // The player's colour at canvas fraction (u, v), as [r, g, b] 0..255.
        function colour(f, u, v) {
            const overlay = findChild(f.editor, "maskOverlay")
            const r = overlay.canvasRect
            const image = grabImage(root)
            const scale = image.width / root.width
            const p = overlay.mapToItem(root, r.x + u * r.width, r.y + v * r.height)
            const c = image.pixel(Math.round(p.x * scale), Math.round(p.y * scale))
            return [c.r * 255, c.g * 255, c.b * 255]
        }
        function look(f, u, v) {
            wait(300)
            waitForRendering(root)
            return colour(f, u, v)
        }
        function difference(a, b) { return Math.abs(a[0] - b[0]) + Math.abs(a[1] - b[1]) + Math.abs(a[2] - b[2]) }
        // (The list is top first.)
        function trackOf(p, clipId) { return p.tracks.findIndex((t) => t.clips.some((c) => c.id === clipId)) }
        function mainIndex(p) { return p.tracks.findIndex((t) => t.main) }
        // Drags across the drawing area through canvas points.
        function drag(f, points) {
            const overlay = findChild(f.editor, "drawingOverlay")
            const area = findChild(f.editor, "drawingArea")
            const at = (i) => overlay.fromCanvas(points[i])
            mousePress(area, at(0).x, at(0).y)
            for (let i = 1; i < points.length; ++i)
                mouseMove(area, at(i).x, at(i).y)
            mouseRelease(area, at(points.length - 1).x, at(points.length - 1).y)
        }

        function test_drawing_on_the_player() {
            const f = createEditor()
            const p = f.project
            compare(p.importMedia(["file://" + stillsDir + "/halves.png"]), 1)
            verify(p.addClip(p.media[0].id))
            p.seek(1)

            click(f.editor, "drawButton")
            verify(p.drawingMode)
            verify(settled(f.editor, "drawingTools"))
            // A brush stroke: a drawing of its own at the playhead, on a picture track above.
            drag(f, [[0.15, 0.5], [0.2, 0.45], [0.25, 0.42], [0.3, 0.45], [0.35, 0.5], [0.4, 0.55]])
            compare(p.undoText, "Add Drawing")
            const id = p.selectedClip
            verify(trackOf(p, id) >= 0 && trackOf(p, id) < mainIndex(p))
            verify(p.tracks[trackOf(p, id)].clips.find((c) => c.id === id).isDrawing)
            let info = p.selectedClipInfo
            verify(info.isDrawing)
            compare(info.label, "Drawing")
            fuzzyCompare(info.start, 1, 1e-6)
            fuzzyCompare(info.duration, 5, 1e-6)
            compare(info.drawing.paths.length, 1)
            compare(info.drawing.paths[0].kind, "brush")
            verify(info.drawing.paths[0].points.length >= 5)
            fuzzyCompare(info.drawing.paths[0].points[0][0], 0.15, 0.01)

            // A red filled rectangle into the same drawing.
            click(f.editor, "drawTool:rectangle")
            click(f.editor, "drawColour:4")
            click(f.editor, "drawFill")
            click(f.editor, "drawWidth:Thick")
            drag(f, [[0.55, 0.3], [0.65, 0.5], [0.8, 0.7]])
            compare(p.undoText, "Draw")
            compare(p.selectedClip, id)
            info = p.selectedClipInfo
            compare(info.drawing.paths.length, 2)
            const box = info.drawing.paths[1]
            compare(box.kind, "rectangle")
            compare(box.points.length, 2)
            fuzzyCompare(box.points[1][0], 0.8, 0.01)
            fuzzyCompare(box.stroke[0], 0.92, 0.01)
            fuzzyCompare(box.fill[3], 0.35, 0.01)
            fuzzyCompare(box.width, 0.022, 1e-6)
            // Shown on the player: the rectangle's middle reddened.
            click(f.editor, "drawDone")
            verify(!p.drawingMode)
            verify(!findChild(f.editor, "drawingOverlay").visible)
            const filled = look(f, 0.675, 0.5)
            p.undo()
            const bare = look(f, 0.675, 0.5)
            p.redo()
            verify(difference(filled, bare) > 40, filled + " / " + bare)
            verify(filled[0] - filled[2] > bare[0] - bare[2] + 20, filled + " / " + bare)

            // The inspector: how it draws on and off, its paths.
            p.selectedClip = id
            click(f.editor, "inspectorTab:Video")
            verify(settled(f.editor, "drawingSection"))
            verify(settled(f.editor, "removePath:0").enabled)
            click(f.editor, "drawOneByOne")
            compare(p.undoText, "Animate Drawing")
            compare(p.selectedClipInfo.drawing.oneByOne, false)
            verify(p.setDrawing(id, { drawIn: 2, drawOut: 1 }))
            fuzzyCompare(p.selectedClipInfo.drawing.drawIn, 2, 1e-6)
            // Longer than the clip: refused.
            verify(!p.setDrawing(id, { drawIn: 4, drawOut: 2 }))
            click(f.editor, "removePath:1")
            compare(p.undoText, "Remove Path")
            compare(p.selectedClipInfo.drawing.paths.length, 1)
            verify(!findChild(f.editor, "removePath:0").enabled)

            // Esc stops drawing.
            p.drawingMode = true
            const overlay = settled(f.editor, "drawingOverlay")
            tryVerify(() => overlay.activeFocus)
            keyClick(Qt.Key_Escape)
            verify(!p.drawingMode)
        }

        function test_a_title_follows_and_runs_along_a_drawn_path() {
            const f = createEditor()
            const p = f.project
            compare(p.importMedia(["file://" + stillsDir + "/halves.png"]), 1)
            verify(p.addClip(p.media[0].id))
            p.seek(0)
            const drawing = p.addDrawing({ kind: "line", points: [[0.1, 0.8], [0.9, 0.2]], stroke: [1, 1, 1, 1], width: 0.01 })
            verify(drawing > 0)
            // A drawing can't follow itself.
            compare(p.pathsToFollow(drawing).length, 0)
            verify(p.addTitle({ text: "Along the way" }, -1, 0))
            const title = p.selectedClip
            const paths = p.pathsToFollow(title)
            compare(paths.length, 1)
            compare(paths[0].label, "Path 1 (line)")

            // Running along one of our shapes, then the drawn line, then straight again.
            click(f.editor, "inspectorTab:Video")
            settled(f.editor, "titlePathSection").expanded = true
            click(f.editor, "titlePath:arc")
            compare(p.undoText, "Edit Title")
            verify(p.selectedClipInfo.text.path.length > 10)
            click(f.editor, "titlePath:drawn:0")
            compare(p.selectedClipInfo.text.path.length, 2)
            fuzzyCompare(p.selectedClipInfo.text.path[1][0], 0.9, 1e-6)
            click(f.editor, "titlePath:none")
            compare(p.selectedClipInfo.text.path.length, 0)

            // Following it: from one end to the other over the title's length, turning with it.
            click(f.editor, "inspectorTab:Analyze")
            verify(settled(f.editor, "followPathSection"))
            click(f.editor, "pathTurn")
            click(f.editor, "followPath")
            compare(p.undoText, "Follow Path")
            const keys = p.selectedClipInfo.keyframes
            verify(keys.x.length >= 40)
            fuzzyCompare(keys.x[0].value, -0.4, 1e-3)
            fuzzyCompare(keys.y[0].value, 0.3, 1e-3)
            fuzzyCompare(keys.x[keys.x.length - 1].value, 0.4, 1e-3)
            fuzzyCompare(keys.y[keys.y.length - 1].value, -0.3, 1e-3)
            // Up and to the right as seen on a 16:9 canvas.
            const angle = Math.atan2(-0.6, 0.8 * p.canvasWidth / p.canvasHeight) * 180 / Math.PI
            fuzzyCompare(keys.rotation[0].value, angle, 0.5)
            // Not offered for the drawing itself.
            p.selectedClip = drawing
            wait(100)
            verify(!findChild(f.editor, "followPathSection").visible)
        }

        function test_text_bends_round_a_circle() {
            const f = createEditor()
            const p = f.project
            // White blocks over black (no video): straight across the middle…
            verify(p.addTitle({ text: "██████", size: 0.1, fontFamily: "Menlo" }, -1, 0))
            p.seek(1)
            const white = (c) => c[0] > 200 && c[1] > 200 && c[2] > 200
            tryVerify(() => white(colour(f, 0.5, 0.5)), 5000, "the title never appeared")
            // …then round the circle, centred at its top: the middle goes dark, the top lights up.
            verify(p.setClipText(p.selectedClip, { pathPreset: "circle" }))
            tryVerify(() => !white(colour(f, 0.5, 0.5)), 5000, "the text never left the middle")
            let top = false
            for (let v = 0.1; v <= 0.3 && !top; v += 0.01)
                top = white(colour(f, 0.5, v))
            verify(top, "nothing at the circle's top")
        }

        function test_overlay_transitions_over_a_cut() {
            const f = createEditor()
            const p = f.project
            compare(p.importMedia(["file://" + stillsDir + "/halves.png", "file://" + stillsDir + "/disc.png"]), 2)
            verify(p.addClip(p.media[0].id))
            verify(p.addClip(p.media[1].id))
            p.seek(4.6)
            p.selectedClip = 0

            settled(f.editor, "libraryTab:Transitions").clicked()
            click(f.editor, "transitionCategory:overlays")
            const preview = settled(f.editor, "transitionPreview:leak.warm")
            tryCompare(preview, "status", Image.Ready)
            compare(findChild(f.editor, "transitionsGrid").count, 5)

            // A warm light leak centred on the cut, screened over it, on a picture track above.
            const tracks = p.tracks.length
            const before = look(f, 0.5, 0.5)
            click(f.editor, "libraryOverlay:leak.warm")
            compare(p.undoText, "Add Overlay")
            const first = p.selectedClip
            verify(trackOf(p, first) >= 0 && trackOf(p, first) < mainIndex(p))
            fuzzyCompare(p.selectedClipInfo.start, 5 - 0.6, 0.02)
            fuzzyCompare(p.selectedClipInfo.duration, 1.2, 0.02)
            compare(p.selectedClipInfo.visual.blend, "screen")
            compare(p.selectedClipInfo.label, "Warm light leak.mov")
            const leak = p.media.find((m) => m.path.endsWith("/Warm light leak.mov"))
            verify(leak)
            verify(leak.alpha)
            p.seek(5)
            const lit = look(f, 0.5, 0.5)
            p.seek(4.6)
            verify(lit[0] + lit[1] + lit[2] > before[0] + before[1] + before[2] + 60, lit + " / " + before)

            // A black paint stroke over the same cut: on another track (a new one on top),
            // covering the cut.
            click(f.editor, "libraryOverlay:paint.black")
            compare(p.tracks.length, tracks + 1)
            compare(trackOf(p, p.selectedClip), 0)
            compare(p.tracks[0].name, "Overlays")
            compare(p.selectedClipInfo.visual.blend, "normal")
            p.seek(5)
            const covered = look(f, 0.5, 0.5)
            verify(covered[0] + covered[1] + covered[2] < 60, covered)

            // Each one undo step; the movie is made once and used again.
            p.undo()
            p.undo()
            compare(p.tracks.length, tracks)
            compare(trackOf(p, first), -1)
            verify(p.addOverlayTransition("leak.warm"))
            compare(p.media.filter((m) => m.path.endsWith("/Warm light leak.mov")).length, 1)
            verify(!p.addOverlayTransition("nope"))
        }
    }
}
