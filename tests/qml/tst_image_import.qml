import QtQuick
import QtTest
import TechieVideoEditor.Theme
import TechieVideoEditor.Player
import TechieVideoEditor.Shell

// Importing pictures the app could not take before: SVG drawings (drawn sharp, not blown up
// from their nominal size), camera RAW photos and OpenEXR through ImageIO, and numbered
// image sequences played as one video.
Item {
    id: root
    width: 1400
    height: 900

    SignalSpy { id: finished; signalName: "exportFinished" }

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
        name: "ImageImport"
        when: windowShown

        function still(name) { return "file://" + stillsDir + "/" + name }

        function createEditor() {
            const fixture = createTemporaryObject(editorComponent, root)
            verify(fixture)
            waitForRendering(fixture)
            return fixture
        }
        function mediaNamed(project, name) { return project.media.find((m) => m.name === name) }
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
        function colorAt(item, fx, fy) {
            const image = grabImage(root)
            const scale = image.width / root.width
            const p = item.mapToItem(root, item.width * fx, item.height * fy)
            return image.pixel(Math.round(p.x * scale), Math.round(p.y * scale))
        }
        function isRed(c) { return c.r > 0.6 && c.g < 0.3 && c.b < 0.3 }
        function isBlue(c) { return c.b > 0.6 && c.r < 0.3 }
        // Pixels across the middle row of the player, between fx0 and fx1, that are neither
        // the red nor the blue: the width of the edge between them.
        function mixedAcross(item, fx0, fx1) {
            const image = grabImage(root)
            const scale = image.width / root.width
            const a = item.mapToItem(root, item.width * fx0, item.height * 0.5)
            const b = item.mapToItem(root, item.width * fx1, item.height * 0.5)
            let mixed = 0
            for (let x = Math.round(a.x * scale); x <= Math.round(b.x * scale); ++x) {
                const c = image.pixel(x, Math.round(a.y * scale))
                if (!isRed(c) && !isBlue(c))
                    ++mixed
            }
            return mixed
        }

        function test_an_svg_is_drawn_sharp() {
            const f = createEditor()
            const p = f.project
            compare(p.importMedia([still("drawing.svg")]), 1)
            const m = mediaNamed(p, "drawing.svg")
            verify(m)
            verify(m.still)
            verify(m.hasVideo)
            // Its own measure: 40 × 20 (a 2:1 picture).
            compare(m.width, 40)
            compare(m.height, 20)
            verify(p.addClip(m.id))
            const player = settled(f.editor, "player")
            p.seek(1)
            tryVerify(() => isRed(colorAt(player, 0.3, 0.5)) && isBlue(colorAt(player, 0.7, 0.5)), 5000,
                      "the drawing never showed")
            // Drawn at the size it is shown, the edge between the halves is a pixel or two;
            // blown up from 40 pixels it would be a smear tens of pixels wide.
            verify(mixedAcross(player, 0.4, 0.6) <= 4, "a blurred edge: " + mixedAcross(player, 0.4, 0.6) + " pixels")
        }

        function test_a_camera_raw_photo() {
            const f = createEditor()
            const p = f.project
            compare(p.importMedia([still("photo.dng")]), 1)
            const m = mediaNamed(p, "photo.dng")
            verify(m)
            verify(m.still)
            // The photo's size, not its 64 × 48 preview's.
            compare(m.width, 256)
            compare(m.height, 192)
            verify(p.addClip(m.id))
            const player = settled(f.editor, "player")
            p.seek(1)
            // Developed by macOS from the mosaic: red-ish on the left, blue-ish on the right
            // (4:3 in a 16:9 canvas, so between an eighth and seven eighths across).
            tryVerify(() => {
                const left = colorAt(player, 0.3, 0.5)
                const right = colorAt(player, 0.7, 0.5)
                return left.r > left.b + 0.2 && right.b > right.r + 0.2
            }, 5000, "the RAW photo never showed")
            tryCompare(settled(f.editor, "mediaThumb:photo.dng"), "status", Image.Ready)
        }

        function test_an_openexr_picture() {
            const f = createEditor()
            const p = f.project
            compare(p.importMedia([still("halves.exr")]), 1)
            const m = mediaNamed(p, "halves.exr")
            verify(m)
            verify(m.still)
            compare(m.width, 400)
            compare(m.height, 200)
            verify(p.addClip(m.id))
            const player = settled(f.editor, "player")
            p.seek(1)
            tryVerify(() => isRed(colorAt(player, 0.3, 0.5)) && isBlue(colorAt(player, 0.7, 0.5)), 5000,
                      "the EXR never showed")
        }

        function openSequenceDialog(f, frame) {
            const dialog = findChild(f.editor, "imageSequenceDialog")
            verify(dialog)
            dialog.openFor(frame)
            tryVerify(() => dialog.opened)
            return dialog
        }

        function test_an_image_sequence_plays_as_one_video() {
            const f = createEditor()
            const p = f.project
            verify(findChild(f.editor, "importFrom:sequence"))
            // Any picture of it will do.
            const dialog = openSequenceDialog(f, still("sequence/shot_0003.png"))
            compare(findChild(dialog.contentItem, "imageSequenceName").text, "shot_####.png")
            compare(findChild(dialog.contentItem, "imageSequenceFrames").text, "12 frames, shot_0001.png to shot_0012.png")
            compare(findChild(dialog.contentItem, "imageSequenceSize").text, "320 × 180")
            const gaps = findChild(dialog.contentItem, "imageSequenceGaps")
            verify(gaps.visible)
            compare(gaps.text, "1 number has no picture; the frame before it is shown there.")
            // At 24 frames a second: half a second.
            findChild(dialog.contentItem, "imageSequenceRate").currentIndex = 1
            mouseClick(settled(dialog.contentItem, "imageSequenceImport"))
            tryVerify(() => !dialog.visible)

            const m = mediaNamed(p, "shot_####.png")
            verify(m, "named by its pattern")
            verify(!m.still)
            verify(m.hasVideo)
            verify(!m.hasAudio)
            compare(m.sequenceFrames, 12)
            compare(m.frameRate, 24)
            fuzzyCompare(m.duration, 0.5, 0.0001)
            compare(m.width, 320)
            compare(m.height, 180)
            verify(m.path.endsWith("/sequence/shot_0001.png"))
            tryCompare(settled(f.editor, "mediaThumb:shot_####.png"), "status", Image.Ready)
            // Asked again at the same rate: the same media.
            compare(p.importImageSequence(still("sequence/shot_0009.png"), 24, 1), m.id)

            verify(p.addClip(m.id))
            const clip = p.tracks.find((t) => t.main).clips[0]
            fuzzyCompare(clip.duration, 0.5, 0.0001)
            const player = settled(f.editor, "player")
            const frameAt = (n) => (n + 0.5) / 24
            p.seek(frameAt(2))
            tryVerify(() => isRed(colorAt(player, 0.5, 0.5)), 5000, "frame 3 is red")
            p.seek(frameAt(8))
            tryVerify(() => isBlue(colorAt(player, 0.5, 0.5)), 5000, "frame 9 is blue")
            // 0006 is missing: 0005 shows in its place, then 0007 is blue.
            p.seek(frameAt(5))
            tryVerify(() => isRed(colorAt(player, 0.5, 0.5)), 5000, "the gap shows the frame before")
            p.seek(frameAt(6))
            tryVerify(() => isBlue(colorAt(player, 0.5, 0.5)), 5000, "frame 7 is blue")

            // Exported like any video: half a second, red then blue.
            const file = "file://" + scratchDir + "/sequence-export.mp4"
            finished.clear()
            finished.target = p
            verify(p.exportVideo(file, { format: "mp4-h264" }))
            finished.wait(60000)
            compare(finished.signalArguments[0][0], true, finished.signalArguments[0][1])
            const g = createEditor()
            const q = g.project
            verify(q.importAndAppend(file))
            fuzzyCompare(q.media[0].duration, 0.5, 0.05)
            const exported = settled(g.editor, "player")
            q.seek(frameAt(1))
            tryVerify(() => isRed(colorAt(exported, 0.5, 0.5)), 5000, "the export starts red")
            q.seek(frameAt(10))
            tryVerify(() => isBlue(colorAt(exported, 0.5, 0.5)), 5000, "the export ends blue")
        }

        function test_a_moved_sequence_is_saved_and_relinked() {
            const dir = scratchDir + "/sequence-relink"
            for (let n = 1; n <= 12; ++n)
                if (n !== 6)
                    verify(testFiles.copy(stillsDir + "/sequence/shot_" + String(n).padStart(4, "0") + ".png",
                                          dir + "/takes/shot_" + String(n).padStart(4, "0") + ".png"))
            let f = createEditor()
            let p = f.project
            const id = p.importImageSequence("file://" + dir + "/takes/shot_0002.png", 25, 1)
            verify(id >= 0)
            verify(p.addClip(id))
            verify(p.save("file://" + dir + "/project.tve"))
            f.destroy()

            // Reopened: still a sequence at 25 frames a second.
            f = createEditor()
            p = f.project
            verify(p.open("file://" + dir + "/project.tve"))
            let m = p.media.find((x) => x.id === id)
            compare(m.name, "shot_####.png")
            compare(m.sequenceFrames, 12)
            compare(m.frameRate, 25)
            fuzzyCompare(m.duration, 12 / 25, 0.0001)
            f.destroy()

            // The folder moves: missing, then relinked to any picture of it in its new place.
            verify(testFiles.move(dir + "/takes", dir + "/moved/takes"))
            f = createEditor()
            p = f.project
            verify(p.open("file://" + dir + "/project.tve"))
            compare(p.missingMedia.length, 1)
            compare(p.missingMedia[0].name, "shot_####.png")
            verify(p.relinkMedia(id, "file://" + dir + "/moved/takes/shot_0010.png", false))
            compare(p.missingMedia.length, 0)
            m = p.media.find((x) => x.id === id)
            verify(m.path.endsWith("/moved/takes/shot_0001.png"))
            compare(m.sequenceFrames, 12)
            compare(m.frameRate, 25)
            const player = settled(f.editor, "player")
            p.seek(8.5 / 25)
            tryVerify(() => isBlue(colorAt(player, 0.5, 0.5)), 5000, "the relinked sequence plays")
            // A single picture is not a sequence to relink it to.
            verify(!p.relinkMedia(id, still("disc.png"), false))
            verify(p.lastError.indexOf("no other numbered pictures") >= 0)
        }

        function test_a_picture_with_no_sequence() {
            const f = createEditor()
            const p = f.project
            const dialog = openSequenceDialog(f, still("disc.png"))
            const error = findChild(dialog.contentItem, "imageSequenceError")
            verify(error.visible)
            compare(error.text, "disc.png has no other numbered pictures beside it in its folder")
            verify(!findChild(dialog.contentItem, "imageSequenceImport").enabled)
            verify(!findChild(dialog.contentItem, "imageSequenceGaps").visible)
            compare(p.importImageSequence(still("disc.png"), 30, 1), -1)
            compare(p.media.length, 0)
            // A rate out of range is refused.
            compare(p.importImageSequence(still("sequence/shot_0001.png"), 0, 1), -1)
            compare(p.importImageSequence(still("sequence/shot_0001.png"), 1000, 1), -1)
            compare(p.media.length, 0)
            dialog.close()
        }
    }
}
