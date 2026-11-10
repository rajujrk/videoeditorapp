import QtQuick
import QtTest
import TechieVideoEditor.Theme
import TechieVideoEditor.Player
import TechieVideoEditor.Shell

// 360° video through the real editor. A 360° project (the test pattern laid on its sphere's
// canvas) exported as an MP4 marked spherical; imported back, that file makes a 360° project
// of its own, which the player looks around in; made flat, its clip is reframed — the view in
// its direction, set from the inspector, dragged in the player, keyframed.
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

    SignalSpy { id: finished; signalName: "exportFinished" }

    TestCase {
        id: testCase
        name: "Sphere360"
        when: windowShown

        readonly property string av: "file://" + samplesDir + "/test_av_1080p30.mp4"
        readonly property string sphereFile: "file://" + tempDir + "/tve_360_export.mp4"
        // The test pattern on a 3840 × 1920 sphere: fitted, so it spans its height and this
        // much of its width, in the middle.
        readonly property real patternWidth: 1920 * 16 / 9 / 3840
        // Two spots inside the pattern's bars (yellow, magenta), 27° up (v = 0.35), and the
        // direction each is in on the sphere.
        readonly property real frontU: 0.42
        readonly property real rightU: 0.75
        function yawOf(patternU) { return ((1 - patternWidth) / 2 + patternU * patternWidth - 0.5) * 360 }

        function initTestCase() {
            const probe = Qt.createQmlObject('import TechieVideoEditor.Player; ProjectController {}', testCase)
            if (probe.importMedia([av]) !== 1)
                skip("samples missing: run tools/make_test_media.sh")
            probe.destroy()
        }
        function createEditor() {
            const fixture = createTemporaryObject(editorComponent, root)
            verify(fixture)
            waitForRendering(fixture)
            finished.clear()
            finished.target = fixture.project
            return fixture
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
        // The player's colour at (u, v) of what it shows, [r, g, b] 0..255.
        function colourAt(f, u, v) {
            wait(400)
            waitForRendering(root)
            const player = f.editor.player
            const image = grabImage(root)
            const scale = image.width / root.width
            // What it shows is fitted into the player like the canvas (or, looking around, fills it).
            const overlay = findChild(f.editor, "maskOverlay")
            const r = player.lookAround ? Qt.rect(0, 0, player.width, player.height) : overlay.canvasRect
            const p = overlay.mapToItem(root, r.x + u * r.width, r.y + v * r.height)
            const c = image.pixel(Math.round(p.x * scale), Math.round(p.y * scale))
            return [c.r * 255, c.g * 255, c.b * 255]
        }
        function near(a, b, tolerance) {
            return [0, 1, 2].every((c) => Math.abs(a[c] - b[c]) <= tolerance)
        }
        // Waits for the player's colour at (u, v) to come near `want`.
        function expectColour(f, u, v, want, what) {
            let now = colourAt(f, u, v)
            for (let i = 0; i < 10 && !near(now, want, 24); ++i)
                now = colourAt(f, u, v)
            verify(near(now, want, 24), what + ": " + JSON.stringify(now) + " vs " + JSON.stringify(want))
        }

        function test_a_360_project_exports_marked_and_comes_back_360() {
            // The pattern alone, flat, for reference: two spots on it.
            const f = createEditor()
            const p = f.project
            verify(p.importAndAppend(av))
            p.seek(0.5)
            const front = colourAt(f, frontU, 0.35)
            const right = colourAt(f, rightU, 0.35)
            compare(p.spherical, false)

            // A 360° project of it, exported.
            verify(p.setSphericalProject(true))
            compare(p.undoText, "Make 360° Project")
            compare([p.canvasWidth, p.canvasHeight], [3840, 1920])
            verify(p.exportVideo(sphereFile, { format: "mp4-h264", range: "custom", from: 0, to: 1 }))
            finished.wait(60000)
            compare(finished.signalArguments[0][0], true, finished.signalArguments[0][1])

            // The file is 360°: a project of it is 360°, looked around in the player.
            const g = createEditor()
            const q = g.project
            verify(q.importAndAppend(sphereFile))
            compare(q.media[0].spherical, true)
            verify(settled(g.editor, "sphereBadge:" + q.media[0].name).visible)
            compare(q.spherical, true)
            compare([q.canvasWidth, q.canvasHeight], [3840, 1920])
            q.seek(0.5)
            click(g.editor, "lookAroundButton")
            const player = g.editor.player
            verify(player.lookAround)
            // Looking 27° up (to the pattern's v = 0.35), ahead and then to the right.
            player.lookPitch = 27
            player.lookYaw = yawOf(frontU)
            expectColour(g, 0.5, 0.5, front, "looking ahead")
            player.lookYaw = yawOf(rightU)
            expectColour(g, 0.5, 0.5, right, "looking right")
            // Dragging the picture left turns the view right.
            player.lookYaw = 0
            const drag = settled(g.editor, "sphereDrag")
            mouseDrag(drag, drag.width / 2, drag.height / 2, -drag.width / 4, 0)
            verify(player.lookYaw > 10, "yaw " + player.lookYaw)
            verify(settled(g.editor, "sphereHint").text.indexOf("360° view") === 0)
            click(g.editor, "lookAroundButton")
            verify(!player.lookAround)

            // Made flat: the clip is reframed, looking where its direction says.
            const clip = q.tracks.find((t) => t.main).clips[0].id
            q.selectedClip = clip
            click(g.editor, "inspectorTab:Video")
            verify(settled(g.editor, "sphereNote").text.indexOf("A 360° project") === 0)
            click(g.editor, "sphereProjectButton")
            compare(q.spherical, false)
            compare(q.undoText, "Make Flat Project")
            compare([q.canvasWidth, q.canvasHeight], [3840, 2160])
            verify(settled(g.editor, "sphereNote").text.indexOf("Reframed") === 0)
            verify(q.setClipVisual(clip, { pitch: 27, yaw: yawOf(frontU) }))
            expectColour(g, 0.5, 0.5, front, "reframed ahead")
            verify(q.setClipVisual(clip, { yaw: yawOf(rightU) }))
            expectColour(g, 0.5, 0.5, right, "reframed to the right")

            // Dragging in the player sets the clip's direction, one undo step.
            const yaw = q.selectedClipInfo.visual.yaw
            const reframe = settled(g.editor, "sphereDrag")
            mouseDrag(reframe, reframe.width / 2, reframe.height / 2, reframe.width / 4, 0)
            tryVerify(() => q.selectedClipInfo.visual.yaw < yaw - 10, 3000, "the drag never turned the clip")
            verify(settled(g.editor, "sphereHint").text.indexOf("Reframing") === 0)
            q.undo()
            fuzzyCompare(q.selectedClipInfo.visual.yaw, yawOf(rightU), 0.01)

            // The direction keyframed: half-way, half-turned.
            q.seek(0)
            verify(q.setClipVisual(clip, { yaw: 0 }))
            verify(q.addKeyframes(clip, ["yaw"]))
            q.seek(1.0)
            verify(q.setClipVisual(clip, { yaw: 60 }))
            q.seek(0.5)
            fuzzyCompare(q.selectedClipInfo.visual.yaw, 30, 1.0)
        }
    }
}
