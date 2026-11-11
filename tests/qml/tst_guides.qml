import QtQuick
import QtTest
import TechieVideoEditor.Theme
import TechieVideoEditor.Player
import TechieVideoEditor.Shell

// Effect and colour masks and motion blur, seen in the player's pixels; the player's guides,
// safe areas, rulers and zoom.
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
            ProjectController { id: controller; view: screen.player; autosaveEnabled: false }
            EditorScreen { id: screen; anchors.fill: parent; project: controller }
        }
    }

    TestCase {
        id: testCase
        name: "Guides"
        when: windowShown

        function still(name) { return "file://" + stillsDir + "/" + name }
        function createEditor() {
            const fixture = createTemporaryObject(editorComponent, root)
            verify(fixture)
            waitForRendering(fixture)
            return fixture
        }
        function mainTrack(p) { return p.tracks.find((t) => t.main) }
        // Red | blue halves on the main track, selected, the playhead on it.
        function halves() {
            const f = createEditor()
            const p = f.project
            compare(p.importMedia([still("halves.png")]), 1)
            verify(p.addClip(p.media[0].id, mainTrack(p).id, 0))
            p.selectedClip = mainTrack(p).clips[0].id
            p.seek(0.5)
            waitForRendering(f.editor)
            return f
        }
        // The player's colour at canvas fraction (u, v), the overlays hidden.
        function canvasColor(f, u, v) {
            const overlays = ["maskOverlay", "transformOverlay", "guidesOverlay"].map((n) => findChild(f.editor, n))
            overlays.forEach((o) => o.opacity = 0)
            const image = grabImage(root)
            overlays.forEach((o) => o.opacity = 1)
            const r = overlays[0].canvasRect
            const scale = image.width / root.width
            const p = overlays[0].mapToItem(root, r.x + u * r.width, r.y + v * r.height)
            return image.pixel(Math.round(p.x * scale), Math.round(p.y * scale))
        }
        function grey(c) { return Math.abs(c.r - c.g) < 0.08 && Math.abs(c.g - c.b) < 0.08 && c.r > 0.1 }

        function test_a_colour_mask_keeps_the_grade_inside_it() {
            const f = halves()
            const p = f.project
            verify(p.setClipColor(p.selectedClip, { saturation: -1 }))
            tryVerify(() => grey(canvasColor(f, 0.25, 0.5)) && grey(canvasColor(f, 0.75, 0.5)), 5000, "the grade never showed")
            // A mask over the left half, for the colour only: the right half keeps its blue.
            verify(p.addMask(p.selectedClip, "rectangle"))
            verify(p.setMask(p.selectedClip, 0, { x: 0.25, y: 0.5, width: 0.5, height: 1, feather: 0, target: "colour" }))
            compare(p.selectedClipInfo.masks[0].target, "colour")
            tryVerify(() => { const c = canvasColor(f, 0.8, 0.5); return c.b > 0.6 && c.r < 0.3 }, 5000, "the blue half was graded")
            verify(grey(canvasColor(f, 0.2, 0.5)))
            // A colour mask cuts nothing away: both halves still show.
            verify(canvasColor(f, 0.95, 0.5).b > 0.5)
        }

        function test_an_effect_mask_keeps_the_effect_inside_it() {
            const f = halves()
            const p = f.project
            verify(p.addClipEffect(p.selectedClip, "tve.color.black_white"))
            tryVerify(() => grey(canvasColor(f, 0.75, 0.5)), 5000, "the effect never showed")
            verify(p.addMask(p.selectedClip, "rectangle"))
            verify(p.setMask(p.selectedClip, 0, { x: 0.25, y: 0.5, width: 0.5, height: 1, feather: 0, target: "effects", invert: true }))
            // Inverted: the effect everywhere but the left half.
            tryVerify(() => { const c = canvasColor(f, 0.2, 0.5); return c.r > 0.6 && c.b < 0.3 }, 5000, "the red half lost its colour")
            verify(grey(canvasColor(f, 0.8, 0.5)))
            // The inspector says what the mask is for.
            findChild(f.editor, "inspectorTab:Video").clicked()
            compare(findChild(f.editor, "maskTarget").currentIndex, 1)
        }

        function test_motion_blur_smears_a_moving_picture() {
            const f = halves()
            const p = f.project
            // Half size, from the left to the right over a second: its left edge at the middle
            // a quarter of the way in (x moves two canvas widths a second).
            verify(p.trimClip(p.selectedClip, false, 1))
            verify(p.panZoom(p.selectedClip, { fromScale: 0.5, fromX: -1, fromY: 0, toScale: 0.5, toX: 1, toY: 0 }))
            p.seek(0.5)
            const smeared = () => {
                let between = 0
                for (let i = 0; i <= 40; ++i) {
                    const c = canvasColor(f, 0.17 + i * 0.004, 0.5)
                    if (c.r > 0.15 && c.r < 0.8)
                        ++between
                }
                return between
            }
            tryVerify(() => smeared() <= 3, 5000, "a sharp edge was smeared")
            verify(p.setClipMotionBlur(p.selectedClip, 1))
            compare(p.undoText, "Motion Blur")
            compare(p.selectedClipInfo.motionBlur, 1)
            tryVerify(() => smeared() >= 8, 5000, "the moving edge was not blurred")
        }

        function test_guides_and_zoom() {
            const f = halves()
            const p = f.project
            const guides = findChild(f.editor, "guidesOverlay")
            verify(!guides.visible)
            const button = findChild(f.editor, "guidesButton")
            verify(!button.checked)
            findChild(f.editor, "guides:thirds").triggered()
            findChild(f.editor, "guides:safe").triggered()
            findChild(f.editor, "guides:rulers").triggered()
            verify(guides.visible && guides.thirds && guides.safeAreas && guides.rulers)
            verify(button.checked)
            verify(guides.rulerStep >= 100)
            // Drawn on the player: a light thirds line where the picture is red.
            const r = guides.canvasRect
            const lineAt = () => {
                findChild(f.editor, "transformOverlay").opacity = 0
                const image = grabImage(root)
                findChild(f.editor, "transformOverlay").opacity = 1
                const scale = image.width / root.width
                const p = guides.mapToItem(root, r.x + r.width / 3, r.y + r.height * 0.6)
                return image.pixel(Math.round(p.x * scale), Math.round(p.y * scale))
            }
            tryVerify(() => lineAt().g > 0.6, 3000, "no thirds line over the red half")
            // Zoom: 100 % makes the stage the canvas's size in device pixels.
            const zoom = findChild(f.editor, "playerZoom")
            zoom.activated(3)
            const stage = findChild(f.editor, "playerStage")
            const viewport = findChild(f.editor, "playerViewport")
            tryVerify(() => Math.abs(stage.width - (1920 / Screen.devicePixelRatio + 2 * stage.margin)) < 1)
            verify(viewport.contentWidth > viewport.width)
            // Its middle in the middle of the view.
            fuzzyCompare(viewport.contentX + viewport.width / 2, viewport.contentWidth / 2, 2)
            zoom.activated(0)
            tryCompare(stage, "width", viewport.width)
        }
    }
}
