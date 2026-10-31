import QtQuick
import QtTest
import TechieVideoEditor.Theme
import TechieVideoEditor.Player
import TechieVideoEditor.Shell

// Screen and camera recording through the real editor, from the test sources (moving bars,
// a tone, a cursor circling and clicking every second; TVE_SCREEN_SOURCE / TVE_CAMERA_SOURCE).
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
        name: "Recording"
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
        function track(p, name) {
            return p.tracks.find((t) => t.name === name)
        }

        function test_screen_camera_and_microphone_are_recorded_together() {
            const f = createEditor()
            const p = f.project
            const before = p.tracks.length
            click(f.editor, "screenRecordButton")
            const panel = findChild(f.editor, "recordPanel")
            tryVerify(() => panel.opened)
            compare(p.screenAccess, "granted")
            compare(panel.screens.length, 1)
            compare(panel.screens[0].title, "Test display (1280 × 720)")
            compare(findChild(panel.contentItem, "screenChoice").currentText, "Test display (1280 × 720)")
            // The test camera too (picture in picture).
            const camera = findChild(panel.contentItem, "cameraChoice")
            compare(camera.count, 2)
            compare(camera.currentText, "No camera")
            camera.currentIndex = 1
            camera.activated(1)
            compare(panel.cameraIndex, 0)
            click(panel.contentItem, "startScreenRecording")
            verify(p.screenRecording)
            tryVerify(() => p.screenRecordingTime > 2.3, 6000)
            verify(settled(panel.contentItem, "screenRecordingTime").text.indexOf("0:02") > 0)
            click(panel.contentItem, "stopScreenRecording")
            verify(!p.screenRecording)
            compare(p.undoText, "Record Screen")

            // Three new tracks: the screen, the camera over it, the microphone below.
            compare(p.tracks.length, before + 3)
            const screen = track(p, "Screen").clips[0]
            const cam = track(p, "Camera").clips[0]
            const mic = track(p, "Microphone").clips[0]
            fuzzyCompare(screen.duration, 2.4, 0.4)
            fuzzyCompare(cam.duration, screen.duration, 0.15)
            compare(screen.start, 0)
            compare(cam.start, 0)
            compare(mic.start, 0)
            verify(p.tracks.findIndex((t) => t.name === "Camera") < p.tracks.findIndex((t) => t.name === "Screen")) // listed top first
            p.selectedClip = cam.id
            fuzzyCompare(p.selectedClipInfo.visual.scale, 0.3, 1e-6)
            fuzzyCompare(p.selectedClipInfo.visual.x, 0.33, 1e-6)

            // The screen's cursor: clicks every second, rings on by default.
            p.selectedClip = screen.id
            const cursor = p.selectedClipInfo.cursor
            verify(cursor !== undefined)
            verify(cursor.clicks >= 2, cursor.clicks)
            verify(cursor.rings)
            click(f.editor, "inspectorTab:Analyze")
            verify(settled(f.editor, "cursorSection"))
            click(f.editor, "cursorSpotlight")
            compare(p.undoText, "Cursor Effects")
            verify(p.selectedClipInfo.cursor.spotlight)
            verify(!p.setCursorEffects(screen.id, { size: 2 }))

            // Zooming on the clicks: scale keyframes up to 1.8× and back.
            click(f.editor, "zoomOnClicks")
            compare(p.undoText, "Zoom on Clicks")
            const scales = p.selectedClipInfo.keyframes.scale
            verify(scales.length >= 3)
            fuzzyCompare(Math.max(...scales.map((k) => k.value)), 1.8, 1e-6)
            fuzzyCompare(scales[0].value, 1, 1e-6)

            // Click sounds on a track of their own.
            click(f.editor, "addClickSounds")
            compare(p.undoText, "Click Sounds")
            compare(track(p, "Clicks").clips.length, cursor.clicks)

            // The whole recording is one step back.
            p.undo()
            p.undo()
            p.undo()
            p.undo()
            compare(p.tracks.length, before)
        }

        function test_a_camera_alone_and_what_cannot_start() {
            const f = createEditor()
            const p = f.project
            verify(!p.startScreenRecording({}))
            verify(p.lastError.indexOf("Choose a screen or a camera") === 0, p.lastError)
            verify(p.startScreenRecording({ camera: "test" }))
            verify(!p.startScreenRecording({ camera: "test" }))
            wait(1200)
            verify(p.stopScreenRecording())
            compare(p.undoText, "Record Camera")
            const cam = track(p, "Camera").clips[0]
            p.selectedClip = cam.id
            fuzzyCompare(p.selectedClipInfo.visual.scale, 1, 1e-6) // alone: full size
            compare(p.selectedClipInfo.cursor, undefined)
            verify(!p.stopScreenRecording())
            verify(!p.zoomOnClicks(cam.id))
        }
    }
}
