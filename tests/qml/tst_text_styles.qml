import QtQuick
import QtTest
import TechieVideoEditor.Theme
import TechieVideoEditor.Player
import TechieVideoEditor.Shell

// Title styles seen in the player's pixels: underline, vertical and curved text, gradient and
// picture fills, 3D depth and tilt.
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
        name: "TextStyles"
        when: windowShown

        function createEditor() {
            const fixture = createTemporaryObject(editorComponent, root)
            verify(fixture)
            waitForRendering(fixture)
            return fixture
        }
        // A white block-letter title on black, selected, the playhead on it.
        function title(fields) {
            const f = createEditor()
            const p = f.project
            verify(p.addTitle(Object.assign({ text: "HHHH", size: 0.2, fontFamily: "Menlo", bold: true }, fields), -1, 0))
            p.seek(1)
            waitForRendering(f.editor)
            return f
        }
        // The player's pixels over the canvas matching `test`: their bounds and count (canvas
        // fractions), and the average colour of them.
        function scan(f, test) {
            const overlays = ["transformOverlay", "guidesOverlay"].map((n) => findChild(f.editor, n))
            overlays.forEach((o) => o.opacity = 0)
            const image = grabImage(root)
            overlays.forEach((o) => o.opacity = 1)
            const o = overlays[0], r = o.canvasRect
            const scale = image.width / root.width
            const result = { minX: 1, maxX: 0, minY: 1, maxY: 0, count: 0, r: 0, g: 0, b: 0 }
            for (let v = 0.01; v < 1; v += 0.01) {
                for (let u = 0.005; u < 1; u += 0.005) {
                    const p = o.mapToItem(root, r.x + u * r.width, r.y + v * r.height)
                    const c = image.pixel(Math.round(p.x * scale), Math.round(p.y * scale))
                    if (!test(c))
                        continue
                    result.minX = Math.min(result.minX, u); result.maxX = Math.max(result.maxX, u)
                    result.minY = Math.min(result.minY, v); result.maxY = Math.max(result.maxY, v)
                    result.count++; result.r += c.r; result.g += c.g; result.b += c.b
                }
            }
            if (result.count) { result.r /= result.count; result.g /= result.count; result.b /= result.count }
            return result
        }
        function lit(c) { return c.r + c.g + c.b > 1.2 }
        function settle(f, check, message) { tryVerify(() => check(), 5000, message) }

        function test_underline_runs_under_the_line() {
            const f = title({})
            const p = f.project
            let plain = null
            settle(f, () => { plain = scan(f, lit); return plain.count > 50 }, "the title never showed")
            verify(p.setClipText(p.selectedClip, { underline: true }))
            let lined = null
            settle(f, () => { lined = scan(f, lit); return lined.maxY > plain.maxY + 0.02 }, "no underline below the text")
            fuzzyCompare(lined.minX, plain.minX, 0.02) // as wide as the text
            fuzzyCompare(lined.maxX, plain.maxX, 0.02)
        }

        function test_vertical_and_curved_text() {
            const f = title({ text: "HHHHHH", size: 0.12 })
            const p = f.project
            let flat = null
            settle(f, () => { flat = scan(f, lit); return flat.count > 50 }, "the title never showed")
            verify(flat.maxX - flat.minX > (flat.maxY - flat.minY) * 2)
            // Stacked: taller than wide.
            verify(p.setClipText(p.selectedClip, { vertical: true }))
            settle(f, () => { const s = scan(f, lit); return s.maxY - s.minY > (s.maxX - s.minX) * 2 }, "not stacked")
            // Arched: the ends lower than the middle.
            verify(p.setClipText(p.selectedClip, { vertical: false, curve: 0.6 }))
            settle(f, () => {
                const ends = scan(f, (c) => lit(c)), middle = flat
                return ends.maxY > middle.maxY + 0.05
            }, "not arched")
            const ends = scan(f, lit)
            verify(ends.minY < flat.minY + 0.03) // the middle still at the top
            verify(!p.setClipText(p.selectedClip, { curve: 2 }))
        }

        function test_gradient_and_picture_fills() {
            const f = title({ color: [1, 0, 0, 1] })
            const p = f.project
            verify(p.setClipText(p.selectedClip, { fill: { kind: "linear", to: [0, 0, 1, 1], angle: 0 } }))
            compare(p.selectedClipInfo.text.fill.kind, "linear")
            const redish = (c) => c.r > 0.6 && c.b < 0.35
            const blueish = (c) => c.b > 0.6 && c.r < 0.35
            settle(f, () => { const red = scan(f, redish), blue = scan(f, blueish)
                              return red.count > 10 && blue.count > 10 && red.maxX < blue.minX + 0.05 }, "red not left of blue")
            // A picture: red and blue halves, across the letters.
            verify(p.setClipText(p.selectedClip, { fill: { kind: "image", imagePath: "file://" + stillsDir + "/halves.png" } }))
            settle(f, () => scan(f, redish).count > 10 && scan(f, blueish).count > 10, "the picture did not fill the letters")
            verify(!p.setClipText(p.selectedClip, { fill: { kind: "image", imagePath: "" } }))
        }

        function test_3d_depth_and_tilt() {
            const f = title({ color: [1, 1, 1, 1] })
            const p = f.project
            let flat = null
            settle(f, () => { flat = scan(f, lit); return flat.count > 50 }, "the title never showed")
            // Green sides down and to the right of the white faces.
            verify(p.setClipText(p.selectedClip, { depth: 0.3, depthColor: [0, 0.8, 0, 1] }))
            const green = (c) => c.g > 0.25 && c.r < 0.2 && c.b < 0.2
            settle(f, () => { const s = scan(f, green); return s.count > 20 && s.maxX > flat.maxX && s.maxY > flat.maxY }, "no 3D sides")
            // Turned away: narrower on screen.
            verify(p.setClipText(p.selectedClip, { depth: 0, tiltY: 55 }))
            settle(f, () => { const s = scan(f, lit); return s.count > 20 && s.maxX - s.minX < (flat.maxX - flat.minX) * 0.8 }, "not turned")
            verify(!p.setClipText(p.selectedClip, { tiltX: 80 }))
            compare(p.selectedClipInfo.text.tiltY, 55)
        }
    }
}
