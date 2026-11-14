import QtQuick
import QtTest
import TechieVideoEditor.Theme
import TechieVideoEditor.Player
import TechieVideoEditor.Shell

// Drawing on the screen while recording it: a clear layer over the recorded area (in the
// recording) and its controls (left out), pens that fade away, kept or cleared, closing with
// the recording.
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
        name: "LiveDrawing"
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
                const p = item.mapToItem(null, 0, 0)
                const stable = last !== null && p.x === last.x && p.y === last.y
                last = p
                return stable
            }, 5000, name + " never settled")
            for (let a = item; a && a.parent; a = a.parent)
                waitForPolish(a, 1000)
            return item
        }
        function click(parent, name) {
            const item = settled(parent, name)
            mouseMove(item)
            mouseClick(item)
        }
        function recorder(f) {
            click(f.editor, "screenRecordButton")
            const panel = findChild(f.editor, "recordPanel")
            tryVerify(() => panel.opened)
            return panel
        }
        // A stroke from (x0, y0) to (x1, y1) in a few steps.
        function stroke(layer, x0, y0, x1, y1) {
            const area = findChild(layer.contentItem, "liveDrawingArea")
            mousePress(area, x0, y0)
            for (let i = 1; i <= 8; ++i)
                mouseMove(area, x0 + (x1 - x0) * i / 8, y0 + (y1 - y0) * i / 8)
            mouseRelease(area, x1, y1)
        }
        function pixelAt(layer, x, y) {
            const image = grabImage(layer.contentItem)
            const scale = image.width / layer.width
            return image.pixel(Math.round(x * scale), Math.round(y * scale))
        }

        function test_drawn_over_the_recording_fading_away() {
            const f = createEditor()
            const p = f.project
            const panel = recorder(f)
            click(panel.contentItem, "drawOnScreen")
            click(panel.contentItem, "startScreenRecording")
            verify(p.screenRecording)
            tryVerify(() => f.editor.liveDrawing && f.editor.liveDrawing.visible, 3000, "no drawing layer")
            const layer = f.editor.liveDrawing
            // Over the recorded area (the test display), taking the pointer.
            compare(layer.width, 1280)
            compare(layer.height, 720)
            verify(layer.drawing)
            compare(layer.flags & Qt.WindowTransparentForInput, 0)
            // Its controls are left out of the recording, as is the frame marking what is
            // recorded; the drawing is not.
            const bar = layer.controls
            tryVerify(() => bar.visible)
            const out = p.recordingExclusions()
            compare(out.length, 2)
            verify(out.every((n) => n > 0))

            // A red stroke, there where it was drawn; gone a few seconds later.
            stroke(layer, 100, 100, 300, 200)
            compare(layer.strokes.length, 1)
            verify(layer.strokes[0].points.length > 4)
            tryVerify(() => { const c = pixelAt(layer, 200, 150); return c.r > 0.8 && c.g < 0.4 && c.a > 0.8 }, 3000, "no ink")
            compare(pixelAt(layer, 600, 500).a, 0) // clear elsewhere
            tryVerify(() => layer.strokes.length === 0, 6000, "the stroke never faded")

            // Kept: an arrow in blue stays until cleared.
            click(bar.contentItem, "liveDrawingFades")
            verify(!layer.fades)
            click(bar.contentItem, "liveDrawingTool:arrow")
            click(bar.contentItem, "liveDrawingInk:3")
            click(bar.contentItem, "liveDrawingWidth:12")
            stroke(layer, 400, 300, 700, 300)
            compare(layer.strokes[0].tool, "arrow")
            compare(layer.strokes[0].width, 12)
            wait(3800)
            compare(layer.strokes.length, 1)
            tryVerify(() => { const c = pixelAt(layer, 550, 300); return c.b > 0.8 && c.r < 0.3 }, 3000, "no blue arrow")
            click(bar.contentItem, "liveDrawingClear")
            compare(layer.strokes.length, 0)

            // Drawing off: the pointer goes through to the screen.
            click(bar.contentItem, "liveDrawingToggle")
            verify(!layer.drawing)
            verify((layer.flags & Qt.WindowTransparentForInput) !== 0)
            findChild(layer, "liveDrawingKey:draw").activated() // D
            verify(layer.drawing)

            // Stopping the recording takes it away.
            verify(p.stopScreenRecording())
            tryVerify(() => !f.editor.liveDrawing, 3000, "the drawing layer stayed")
            compare(p.recordingExclusions().length, 0)
        }

        function test_drawing_asked_for_during_a_recording() {
            const f = createEditor()
            const p = f.project
            const panel = recorder(f)
            click(panel.contentItem, "startScreenRecording")
            wait(200)
            verify(!f.editor.liveDrawing) // not asked for
            click(panel.contentItem, "drawDuringRecording")
            tryVerify(() => f.editor.liveDrawing && f.editor.liveDrawing.visible, 3000, "no drawing layer")
            verify(p.stopScreenRecording())
            tryVerify(() => !f.editor.liveDrawing, 3000)
        }
    }
}
