import QtQuick
import QtTest
import TechieVideoEditor.Theme
import TechieVideoEditor.Player
import TechieVideoEditor.Shell

// Masks, the chroma key, track mattes and split-screen layouts through the real editor.
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
        name: "Masks"
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

        function test_rectangle_mask_from_the_inspector() {
            const f = withStill("keyed.png")
            const p = f.project
            showsAt(f, 0.1, 0.5, isGreen, "the still never showed")
            click(f.editor, "inspectorTab:Video")
            click(f.editor, "addMask:rectangle")
            compare(p.undoText, "Add Mask")
            compare(p.activeMask, 0)
            compare(p.selectedClipInfo.masks.length, 1)
            compare(p.selectedClipInfo.masks[0].shape, "rectangle")
            verify(settled(f.editor, "maskChip:0").checked)
            verify(settled(f.editor, "maskEditor").visible)
            // Only the middle half shows; the canvas is black around it.
            showsAt(f, 0.5, 0.5, isRed, "the mask hid the middle")
            showsAt(f, 0.1, 0.5, isBlack, "the mask never cut the edge")
            showsAt(f, 0.3, 0.3, isGreen, "the mask cut too much")

            // On the player: the outline and the handles, the centre at the picture's centre.
            const overlay = settled(f.editor, "maskOverlay")
            verify(overlay.editing)
            const centre = settled(f.editor, "maskCentre")
            const expected = canvasPoint(f, 0.5, 0.5)
            fuzzyCompare(centre.x + centre.width / 2, expected.x, 1)
            fuzzyCompare(centre.y + centre.height / 2, expected.y, 1)
            verify(settled(f.editor, "maskSize").visible)
            verify(settled(f.editor, "maskRotate").visible)
            // Dragging the centre moves the mask (one undo step on release).
            dragOn(f, expected, canvasPoint(f, 0.7, 0.5))
            tryVerify(() => Math.abs(p.selectedClipInfo.masks[0].x - 0.7) < 0.01, 3000, "the drag did not move the mask")
            fuzzyCompare(p.selectedClipInfo.masks[0].y, 0.5, 0.01)
            compare(p.undoText, "Change Mask")
            showsAt(f, 0.9, 0.5, isGreen, "the moved mask did not show the right")
            showsAt(f, 0.4, 0.5, isBlack, "the moved mask still showed the left")
            p.undo()
            compare(p.selectedClipInfo.masks[0].x, 0.5)

            // Inverted: a hole in the middle.
            click(f.editor, "maskInvert")
            verify(p.selectedClipInfo.masks[0].invert)
            showsAt(f, 0.5, 0.5, isBlack, "the inverted mask still showed the middle")
            showsAt(f, 0.1, 0.5, isGreen, "the inverted mask hid the edge")

            // Feather from the slider API, then remove.
            verify(p.setMask(p.selectedClip, 0, { feather: 0.2 }))
            compare(p.selectedClipInfo.masks[0].feather, 0.2)
            verify(!p.setMask(p.selectedClip, 0, { feather: 2 }))
            click(f.editor, "removeMaskButton")
            compare(p.undoText, "Remove Mask")
            compare(p.selectedClipInfo.masks.length, 0)
            compare(p.activeMask, -1)
            verify(!overlay.visible)
            showsAt(f, 0.1, 0.5, isGreen, "the picture never came back")
        }

        function test_shapes_and_selection() {
            const f = withStill("keyed.png")
            const p = f.project
            click(f.editor, "inspectorTab:Video")
            for (const shape of ["ellipse", "line", "star", "heart"])
                click(f.editor, "addMask:" + shape)
            compare(p.selectedClipInfo.masks.length, 4)
            compare(p.activeMask, 3) // the newest
            // The line keeps the top half; the others add their middles to it.
            showsAt(f, 0.1, 0.2, isGreen, "the line mask hid the top")
            showsAt(f, 0.1, 0.8, isBlack, "the line mask kept the bottom")
            // A chip picks the mask to edit; the line has no size handle.
            click(f.editor, "maskChip:1")
            compare(p.activeMask, 1)
            verify(settled(f.editor, "maskRotate").visible)
            verify(!findChild(f.editor, "maskSize").visible)
            click(f.editor, "maskChip:1")
            compare(p.activeMask, -1)
            // Another clip ends the editing.
            click(f.editor, "maskChip:0")
            p.selectedClip = -1
            compare(p.activeMask, -1)
            // Masks are for pictures.
            verify(!p.addMask(-1, "rectangle"))
        }

        function test_pen_mask_points() {
            const f = withStill("keyed.png")
            const p = f.project
            click(f.editor, "inspectorTab:Video")
            click(f.editor, "addMask:pen")
            compare(p.selectedClipInfo.masks[0].shape, "pen")
            compare(p.selectedClipInfo.masks[0].points.length, 4)
            verify(settled(f.editor, "penPoint:3").visible)
            showsAt(f, 0.5, 0.5, isRed, "the pen mask hid the middle")
            showsAt(f, 0.1, 0.1, isBlack, "the pen mask kept the corner")
            // A click on the player adds a point on the nearest edge.
            const area = settled(f.editor, "penArea")
            const top = canvasPoint(f, 0.5, 0.1)
            mouseClick(area, top.x, top.y)
            tryCompare(p.selectedClipInfo.masks[0].points, "length", 5)
            compare(p.undoText, "Change Mask")
            const added = p.selectedClipInfo.masks[0].points[1]
            fuzzyCompare(added[0], 0.5, 0.01)
            fuzzyCompare(added[1], 0.1, 0.01)
            showsAt(f, 0.5, 0.2, isGreen, "the new point did not widen the mask")
            // Dragging a point moves it.
            verify(settled(f.editor, "penPoint:0"))
            dragOn(f, canvasPoint(f, 0.3, 0.3), canvasPoint(f, 0.05, 0.05))
            tryVerify(() => Math.abs(p.selectedClipInfo.masks[0].points[0][0] - 0.05) < 0.01, 3000, "the point did not move")
            fuzzyCompare(p.selectedClipInfo.masks[0].points[0][1], 0.05, 0.01)
            // Double-clicking a point removes it.
            mouseDoubleClickSequence(settled(f.editor, "penPoint:2"))
            tryCompare(p.selectedClipInfo.masks[0].points, "length", 4)
        }

        function test_chroma_key_with_the_picker() {
            const f = createEditor()
            const p = f.project
            compare(p.importMedia([still("halves.png"), still("keyed.png")]), 2)
            verify(p.addClip(mediaNamed(p, "halves.png").id))
            const above = trackAbove(p)
            verify(p.addClip(mediaNamed(p, "keyed.png").id, above.id, 0))
            p.selectedClip = p.tracks.find((t) => t.id === above.id).clips[0].id
            p.seek(1)
            showsAt(f, 0.8, 0.5, isGreen, "the green screen never showed")

            click(f.editor, "inspectorTab:Video")
            const section = settled(f.editor, "chromaKeySection")
            section.expanded = true
            click(f.editor, "pickKeyColor")
            verify(p.pickingKeyColor)
            const overlay = settled(f.editor, "maskOverlay")
            verify(overlay.picking)
            // Clicking the green picks it, keys it out and ends picking.
            const at = canvasPoint(f, 0.8, 0.5)
            mouseClick(settled(f.editor, "keyPicker"), at.x, at.y)
            verify(!p.pickingKeyColor)
            compare(p.undoText, "Change Key")
            const key = p.selectedClipInfo.chromaKey
            verify(key.enabled)
            verify(key.color[1] > 0.6 && key.color[0] < 0.1)
            // Halves shows through; the red square stays.
            showsAt(f, 0.8, 0.5, isBlue, "the key never removed the green")
            showsAt(f, 0.2, 0.5, isRed, "the left of the clip below never showed")
            showsAt(f, 0.5, 0.5, isRed, "the key removed the red square")
            verify(settled(f.editor, "chromaKeyEnabled").checked)

            // Settings, and the preset colours.
            verify(p.setChromaKey(p.selectedClip, { tolerance: 0.3, softness: 0.1, spill: 0.8 }))
            compare(p.selectedClipInfo.chromaKey.tolerance, 0.3)
            verify(!p.setChromaKey(p.selectedClip, { spill: 1.5 }))
            click(f.editor, "keyBlue")
            verify(p.selectedClipInfo.chromaKey.color[2] > 0.8)
            showsAt(f, 0.8, 0.5, isGreen, "a blue key removed the green")
            click(f.editor, "chromaKeyEnabled")
            verify(!p.selectedClipInfo.chromaKey.enabled)

            // Picking is cancelled by the button and by a new selection.
            click(f.editor, "pickKeyColor")
            verify(p.pickingKeyColor)
            click(f.editor, "pickKeyColor")
            verify(!p.pickingKeyColor)
            p.pickingKeyColor = true
            p.selectedClip = mainTrack(p).clips[0].id
            verify(!p.pickingKeyColor)
        }

        function test_track_matte() {
            const f = createEditor()
            const p = f.project
            compare(p.importMedia([still("disc.png"), still("halves.png")]), 2)
            // The matte (the disc) goes on the track above the clip it shapes.
            verify(p.addClip(mediaNamed(p, "halves.png").id))
            verify(p.addClip(mediaNamed(p, "disc.png").id, trackAbove(p).id, 0))
            const shaped = mainTrack(p).clips[0].id
            p.selectedClip = shaped
            p.seek(1)
            showsAt(f, 0.5, 0.5, isGreen, "the disc never showed")
            showsAt(f, 0.2, 0.5, isRed, "the clip never showed")

            click(f.editor, "inspectorTab:Video")
            verify(p.setMatte(shaped, "alpha"))
            compare(p.undoText, "Change Matte")
            compare(p.selectedClipInfo.matte, "alpha")
            compare(settled(f.editor, "matteMode").currentIndex, 1)
            // The halves only inside the disc; the disc itself is not drawn.
            showsAt(f, 0.42, 0.5, isRed, "the matte hid the inside")
            showsAt(f, 0.58, 0.5, isBlue, "the matte hid the inside")
            showsAt(f, 0.2, 0.5, isBlack, "the matte never cut the outside")
            // Inverted: everything but the disc.
            verify(p.setMatte(shaped, "invertedAlpha"))
            showsAt(f, 0.2, 0.5, isRed, "the inverted matte hid the outside")
            showsAt(f, 0.42, 0.5, isBlack, "the inverted matte kept the inside")
            verify(!p.setMatte(shaped, "sideways"))
            p.undo()
            p.undo()
            compare(p.selectedClipInfo.matte, "none")
            showsAt(f, 0.5, 0.5, isGreen, "the disc did not show again")
        }

        function test_split_screen_layouts() {
            const f = createEditor()
            const p = f.project
            compare(p.layouts.length, 6)
            compare(p.layouts[1].id, "side-by-side")
            compare(p.layouts[1].regions.length, 2)
            compare(p.importMedia([still("halves.png"), still("keyed.png")]), 2)
            verify(p.addClip(mediaNamed(p, "halves.png").id))
            p.seek(1)
            // Two clips are needed for a split screen.
            verify(!p.applyLayout("side-by-side"))
            verify(p.lastError.indexOf("needs 2 clips") >= 0)
            const above = trackAbove(p)
            verify(p.addClip(mediaNamed(p, "keyed.png").id, above.id, 0))
            p.selectedClip = mainTrack(p).clips[0].id

            click(f.editor, "inspectorTab:Video")
            const section = settled(f.editor, "layoutSection")
            section.expanded = true
            click(f.editor, "layout:side-by-side")
            compare(p.undoText, "Apply Layout")
            // The top clip (the green screen) on the left, the halves cropped to the right.
            showsAt(f, 0.25, 0.5, isRed, "the top clip's middle is not on the left")
            showsAt(f, 0.05, 0.5, isGreen, "the top clip does not fill the left")
            showsAt(f, 0.6, 0.5, isRed, "the halves' left is not on the right")
            showsAt(f, 0.95, 0.5, isBlue, "the halves' right is not on the right")
            // One undo restores both.
            p.undo()
            showsAt(f, 0.05, 0.5, isGreen, "undo did not restore the top clip")
            showsAt(f, 0.6, 0.5, isRed, "undo did not restore the top clip")

            // Picture in picture: the top clip small in the corner, the other full.
            click(f.editor, "layout:pip")
            showsAt(f, 0.2, 0.5, isRed, "the clip below is not full")
            showsAt(f, 0.8, 0.5, isBlue, "the clip below is not full")
            showsAt(f, 0.785, 0.22, isRed, "the inset's middle is not in the corner")
            showsAt(f, 0.65, 0.1, isGreen, "the inset is not in the corner")
        }
    }
}
