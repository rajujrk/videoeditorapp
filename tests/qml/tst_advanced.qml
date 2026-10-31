import QtQuick
import QtTest
import TechieVideoEditor.Theme
import TechieVideoEditor.Player
import TechieVideoEditor.Shell

// Advanced vision through the real editor: syncing cameras by sound, smart cutout of a
// subject, smooth slow motion, removing an object, and upscaling (samples from
// tools/make_test_media.sh).
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

    SignalSpy { id: finished; signalName: "analysisFinished" }

    TestCase {
        id: testCase
        name: "Advanced"
        when: windowShown

        function createEditor() {
            const fixture = createTemporaryObject(editorComponent, root)
            verify(fixture)
            waitForRendering(fixture)
            finished.clear()
            finished.target = fixture.project
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
        function red(f, u, v) {
            const overlay = findChild(f.editor, "maskOverlay")
            const r = overlay.canvasRect
            const image = grabImage(root)
            const scale = image.width / root.width
            const p = overlay.mapToItem(root, r.x + u * r.width, r.y + v * r.height)
            const c = image.pixel(Math.round(p.x * scale), Math.round(p.y * scale))
            return [c.r * 255, c.g * 255, c.b * 255]
        }
        function media(f, name) {
            const p = f.project
            if (p.importMedia(["file://" + samplesDir + "/" + name]) !== 1)
                skip(name + " missing: run tools/make_test_media.sh")
            return p.media[p.media.length - 1].id
        }
        function clipOf(p, mediaId) {
            for (const t of p.tracks)
                for (const c of t.clips)
                    if (c.mediaId === mediaId)
                        return c
            return null
        }
        function waitFinished(count, timeout) {
            tryVerify(() => finished.count >= count, timeout || 60000, "the work never finished")
            compare(finished.signalArguments[count - 1][0], true, finished.signalArguments[count - 1][1])
        }
        // Draws a box on the player (picture fractions) for the selected clip.
        function drawBox(f, x0, y0, x1, y1) {
            const overlay = findChild(f.editor, "trackOverlay")
            const area = findChild(f.editor, "trackArea")
            verify(f.project.pickingTrackArea)
            const a = overlay.fromPicture(x0, y0)
            const b = overlay.fromPicture(x1, y1)
            mousePress(area, a.x, a.y)
            mouseMove(area, (a.x + b.x) / 2, (a.y + b.y) / 2)
            mouseMove(area, b.x, b.y)
            mouseRelease(area, b.x, b.y)
            verify(!f.project.pickingTrackArea)
        }

        function test_cameras_line_up_by_sound() {
            const f = createEditor()
            const p = f.project
            const a = media(f, "sync_a.wav")
            const b = media(f, "sync_b.mov")
            verify(p.addClip(a, -1, 0))
            verify(p.addClip(b, -1, 0))
            const clipA = clipOf(p, a).id
            const clipB = clipOf(p, b).id
            // The recorder first, then the camera.
            p.selectedClip = clipA
            p.toggleClipSelection(clipB)
            click(f.editor, "inspectorTab:Analyze")
            verify(settled(f.editor, "syncSection"))
            click(f.editor, "syncBySound")
            waitFinished(1)
            compare(p.undoText, "Sync by Sound")
            // The camera heard it 1.3 s later into its file: the recorder moves 1.3 s later.
            fuzzyCompare(clipOf(p, a).start - clipOf(p, b).start, 1.3, 0.02)
            fuzzyCompare(clipOf(p, b).start, 0, 0.001)
            p.undo()
            compare(clipOf(p, a).start, 0)
            // One clip alone: nothing to line up with.
            verify(!p.syncBySound([clipA]))
            verify(p.lastError.indexOf("two or more") > 0, p.lastError)
        }

        function test_a_subject_is_cut_out() {
            const f = createEditor()
            const p = f.project
            const m = media(f, "people.mp4")
            verify(p.addClip(m))
            p.selectedClip = clipOf(p, m).id
            p.seek(0)
            click(f.editor, "inspectorTab:Analyze")
            click(f.editor, "drawSubject")
            // The astronaut, at the start (the face's centre at x ≈ 0.30).
            drawBox(f, 0.08, 0.1, 0.52, 0.98)
            click(f.editor, "cutOutSubject")
            waitFinished(1)
            compare(p.undoText, "Cut Out Subject")
            verify(p.selectedClipInfo.cutout.enabled)
            verify(p.selectedClipInfo.cutout.subject)
            verify(settled(f.editor, "subjectState"))
            // The grey around goes; the subject stays (and is followed as it moves).
            p.seek(1.5)
            tryVerify(() => red(f, 0.97, 0.5)[0] < 25, 5000, "the background was not cut out")
            const face = (100 + (1920 - 864 - 200) * 0.5 + 0.56 * 864) / 1920
            verify(red(f, face, 0.3)[0] > 80)
            click(f.editor, "backToPeople")
            verify(!p.selectedClipInfo.cutout.subject)
        }

        function test_smooth_slow_motion_and_object_removal() {
            const f = createEditor()
            const p = f.project
            const m = media(f, "card.mp4")
            verify(p.addClip(m))
            const id = clipOf(p, m).id
            p.selectedClip = id
            click(f.editor, "inspectorTab:Analyze")
            verify(settled(f.editor, "pictureSection"))

            // Smooth slow motion: 4 frames for every one, then the clip slowed to a quarter.
            click(f.editor, "smoothFactor:4")
            click(f.editor, "smooth")
            waitFinished(1)
            compare(p.undoText, "Smooth Slow Motion")
            compare(p.selectedClipInfo.processed.steps, ["smooth4"])
            verify(settled(f.editor, "processedState").text.indexOf("smooth slow motion 4×") > 0)
            // Again: instant (the copy is kept).
            p.undo()
            verify(p.processVideo(id, { kind: "smooth", factor: 4 }))
            verify(!p.analyzing)
            click(f.editor, "restoreVideo")
            compare(p.undoText, "Original Picture")
            compare(p.selectedClipInfo.processed, undefined)

            // Removing the card: drawn around at the start, followed, filled in.
            p.seek(0)
            const before = red(f, 0.375, 0.53)
            verify(before[0] > 230 && before[1] > 230 && before[2] > 230) // the white card
            click(f.editor, "drawObject")
            drawBox(f, 0.25, 0.36, 0.5, 0.7)
            click(f.editor, "removeObject")
            waitFinished(3, 120000)
            compare(p.undoText, "Remove Object")
            compare(p.selectedClipInfo.processed.steps, ["remove"])
            p.seek(1.0)
            tryVerify(() => {
                const c = red(f, (480 + 120) / 1280, 0.53)
                return !(c[0] > 230 && c[1] > 230 && c[2] > 230)
            }, 5000, "the card is still there")
            p.undo()
            compare(p.selectedClipInfo.processed, undefined)

            // Upscaling waits for Apple's model; with it, 2× or 4×.
            const model = p.upscaleModel.status
            verify(["ready", "download", "downloading", "unsupported"].indexOf(model) >= 0, model)
            if (model === "ready")
                verify(settled(f.editor, "upscaleScale:2"))
            else if (model === "download")
                verify(settled(f.editor, "downloadUpscaleModel"))
            verify(!p.processVideo(id, { kind: "fold" }))
            verify(!p.processVideo(id, { kind: "remove" }))
            verify(p.lastError.indexOf("Draw a box") === 0, p.lastError)
        }
    }
}
