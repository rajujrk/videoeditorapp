import QtQuick
import QtTest
import TechieVideoEditor.Theme
import TechieVideoEditor.Player
import TechieVideoEditor.Shell

// People, found with Vision: the portrait cutout, covering faces and auto reframe, through
// the real editor, on samples/people.mp4 (a portrait sliding left to right over grey; the
// face's centre at x ≈ 0.30 + 0.15·t, y ≈ 0.24).
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
        name: "People"
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
        // The player's red at canvas fraction (u, v).
        function red(f, u, v) {
            const overlay = findChild(f.editor, "maskOverlay")
            const r = overlay.canvasRect
            const image = grabImage(root)
            const scale = image.width / root.width
            const p = overlay.mapToItem(root, r.x + u * r.width, r.y + v * r.height)
            return image.pixel(Math.round(p.x * scale), Math.round(p.y * scale)).r * 255
        }
        function faceX(t) { return (100 + (1920 - 864 - 200) * t / 3 + 0.56 * 864) / 1920 }
        // people.mp4 on the timeline, selected, at 1.5 s, the Analyze tab open.
        function withPeople(f) {
            const p = f.project
            if (p.importMedia(["file://" + samplesDir + "/people.mp4"]) !== 1)
                skip("people.mp4 missing: run tools/make_test_media.sh")
            verify(p.addClip(p.media[0].id))
            const id = p.tracks.find((t) => t.main).clips[0].id
            p.selectedClip = id
            p.seek(1.5)
            click(f.editor, "inspectorTab:Analyze")
            return id
        }
        function waitFinished(count) {
            tryVerify(() => finished.count >= count, 30000, "the analysis never finished")
            compare(finished.signalArguments[count - 1][0], true, finished.signalArguments[count - 1][1])
        }

        function test_portrait_cutout_clears_the_background() {
            const f = createEditor()
            const p = f.project
            withPeople(f)
            tryVerify(() => Math.abs(red(f, 0.03, 0.5) - 128) < 20, 5000, "the grey background")
            click(f.editor, "cutoutSwitch")
            compare(p.undoText, "Portrait Cutout")
            compare(p.selectedClipInfo.cutout.enabled, true)
            // The grey around the person is gone (the black canvas shows); the face stays.
            tryVerify(() => red(f, 0.03, 0.5) < 25, 5000, "the background was not cut out")
            verify(red(f, faceX(1.5), 0.28) > 80)

            // Inverted: the person goes, the background comes back.
            click(f.editor, "cutoutInvert")
            compare(p.selectedClipInfo.cutout.invert, true)
            tryVerify(() => red(f, faceX(1.5), 0.28) < 25, 5000, "the person was not taken out")
            tryVerify(() => Math.abs(red(f, 0.03, 0.5) - 128) < 20, 5000)

            // Settings are one undo step each, in range.
            verify(p.setPortraitCutout(p.selectedClip, { softness: 0.8, shift: 0.5 }))
            compare(p.undoText, "Change Cutout")
            compare(p.selectedClipInfo.cutout.softness, 0.8)
            verify(!p.setPortraitCutout(p.selectedClip, { shift: 3 }))
            p.undo()
            p.undo()
            p.undo()
            compare(p.selectedClipInfo.cutout.enabled, false)
            tryVerify(() => Math.abs(red(f, 0.03, 0.5) - 128) < 20, 5000)
        }

        function test_faces_are_found_chosen_and_covered() {
            const f = createEditor()
            const p = f.project
            const id = withPeople(f)
            click(f.editor, "findFaces")
            waitFinished(1)
            compare(p.faces.clipId, id)
            compare(p.faces.tracks.length, 1)
            fuzzyCompare(p.faces.tracks[0].start, 0, 0.1)
            fuzzyCompare(p.faces.tracks[0].end, 3, 0.15)
            verify(settled(f.editor, "facesFound").text.indexOf("1 face") === 0)

            // Left out, there is nothing to cover; chosen again, covered with a fill.
            click(f.editor, "faceChip:1")
            verify(!findChild(f.editor, "coverFaces").enabled)
            click(f.editor, "faceChip:1")
            click(f.editor, "faceStyle:fill")
            click(f.editor, "coverFaces")
            compare(p.undoText, "Cover Faces")
            compare(p.selectedClipInfo.faceCover.faceIds, [1])
            compare(p.selectedClipInfo.faceCover.style, "fill")
            tryVerify(() => red(f, faceX(1.5), 0.25) < 40, 5000, "the face was not covered")
            verify(Math.abs(red(f, 0.03, 0.5) - 128) < 20) // the rest untouched
            verify(settled(f.editor, "facesCovered").text.indexOf("Covering face 1") === 0)
            // It follows the face: covered later on too.
            p.seek(2.5)
            tryVerify(() => red(f, faceX(2.5), 0.25) < 40, 5000, "the cover did not follow the face")

            // Another style is one step; the second time faces are found instantly (kept).
            click(f.editor, "faceStyle:mosaic")
            compare(p.undoText, "Change Face Cover")
            compare(p.selectedClipInfo.faceCover.style, "mosaic")
            verify(p.findFaces(id))
            verify(!p.analyzing)
            click(f.editor, "uncoverFaces")
            compare(p.undoText, "Uncover Faces")
            compare(p.selectedClipInfo.faceCover.faceIds.length, 0)
            p.undo()
            compare(p.selectedClipInfo.faceCover.faceIds, [1])
        }

        function test_auto_reframe_follows_the_face_into_portrait() {
            const f = createEditor()
            const p = f.project
            withPeople(f)
            compare(p.canvasWidth, 1920)
            click(f.editor, "reframeAspect:9:16")
            click(f.editor, "autoReframe")
            waitFinished(1)
            compare(p.undoText, "Auto Reframe")
            compare(p.canvasWidth, 1080)
            compare(p.canvasHeight, 1920)
            const info = p.selectedClipInfo
            fuzzyCompare(info.visual.scale, (1920 / 1080) / (1080 / 1920), 1e-6)
            const xs = info.keyframes.x
            verify(xs.length >= 2, JSON.stringify(xs))
            fuzzyCompare(xs[0].time, 0, 1e-6)
            verify(xs[0].value > 0.3, xs[0].value)                   // the face left of centre: picture moved right
            verify(xs[xs.length - 1].value < -0.3, xs[xs.length - 1].value)
            // The face is in the middle of the portrait frame.
            p.seek(1.5)
            tryVerify(() => red(f, 0.5, 0.25) > 80 && red(f, 0.5, 0.25) < 250, 5000, "the face is not in the frame")
            // One undo step puts the canvas and the clip back.
            p.undo()
            compare(p.canvasWidth, 1920)
            compare(p.selectedClipInfo.keyframes.x, undefined)

            // Square, kept on the canvas's shorter side; instant the second time.
            verify(p.autoReframe(p.selectedClip, { aspect: "1:1" }))
            verify(!p.analyzing)
            compare(p.canvasWidth, 1080)
            compare(p.canvasHeight, 1080)
            verify(!p.autoReframe(p.selectedClip, { aspect: "wide" }))
            verify(p.lastError.indexOf("Unknown shape") === 0, p.lastError)
            // Reversed clips cannot be reframed.
            verify(p.setClipSpeed(p.selectedClip, 1, true))
            verify(!p.autoReframe(p.selectedClip, { aspect: "9:16" }))
            verify(p.lastError.indexOf("steady speed") > 0, p.lastError)
        }

        function test_only_video_clips_find_people() {
            const f = createEditor()
            const p = f.project
            verify(p.addTitle({ text: "Hello" }, -1, 0))
            verify(!p.selectedClipInfo.canFindPeople)
            click(f.editor, "inspectorTab:Analyze")
            verify(!findChild(f.editor, "cutoutSection").visible)
            verify(!findChild(f.editor, "facesSection").visible)
            verify(!findChild(f.editor, "reframeSection").visible)
            verify(!p.findFaces(p.selectedClip))
            verify(p.lastError.indexOf("Select a clip of a video file") === 0, p.lastError)
            verify(!p.setPortraitCutout(p.selectedClip, { enabled: true }))
            // Covering needs faces found first.
            if (p.importMedia(["file://" + samplesDir + "/people.mp4"]) !== 1)
                skip("people.mp4 missing: run tools/make_test_media.sh")
            verify(p.addClip(p.media[0].id))
            const id = p.tracks.find((t) => t.main).clips[0].id
            verify(!p.coverFaces(id, []))
            verify(p.lastError.indexOf("Find the faces first") === 0, p.lastError)
            verify(!p.uncoverFaces(id))
        }
    }
}
