import QtQuick
import QtTest
import TechieVideoEditor.Theme
import TechieVideoEditor.Player
import TechieVideoEditor.Shell

// Still images through the real editor: import, the media bin, the timeline, the player.
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
        name: "Stills"
        when: windowShown

        function still(name) { return "file://" + stillsDir + "/" + name }

        function createEditor() {
            const fixture = createTemporaryObject(editorComponent, root)
            verify(fixture)
            waitForRendering(fixture)
            return fixture
        }
        function mainClips(project) { return project.tracks.find((t) => t.main).clips }
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

        function test_import_every_still_format() {
            const f = createEditor()
            const p = f.project
            const names = ["halves.png", "halves.jpg", "halves.webp", "halves.tiff", "halves.heic"]
            compare(p.importMedia(names.map(still)), names.length)
            for (const name of names) {
                const m = mediaNamed(p, name)
                verify(m, name)
                verify(m.still, name)
                verify(m.hasVideo)
                verify(!m.hasAudio)
                compare(m.duration, 5)
                compare(m.width, 400)
                compare(m.height, 200)
            }
            // Turned the right way up by its EXIF tag.
            compare(p.importMedia([still("rotated.jpg")]), 1)
            compare(mediaNamed(p, "rotated.jpg").width, 200)
            compare(mediaNamed(p, "rotated.jpg").height, 400)
            // The bin says "Image" instead of a length, and shows its thumbnail.
            compare(settled(f.editor, "mediaDuration:halves.png").text, "Image")
            tryCompare(settled(f.editor, "mediaThumb:halves.png"), "status", Image.Ready)
            verify(!p.createProxy(mediaNamed(p, "halves.png").id))
        }

        function test_animated_gif_is_video() {
            const f = createEditor()
            const p = f.project
            if (p.importMedia(["file://" + samplesDir + "/test_anim.gif"]) !== 1)
                skip("samples missing: run tools/make_test_media.sh")
            const m = mediaNamed(p, "test_anim.gif")
            verify(!m.still)
            fuzzyCompare(m.duration, 2, 0.15)
        }

        function test_a_still_on_the_timeline() {
            const f = createEditor()
            const p = f.project
            compare(p.importMedia([still("halves.png")]), 1)
            verify(p.addClip(p.media[0].id))
            const clip = mainClips(p)[0]
            verify(clip.isStill)
            compare(clip.duration, 5)
            // A photo does not set the canvas.
            compare(p.canvasWidth, 1920)
            compare(p.canvasHeight, 1080)
            // Any length; no speed.
            verify(p.trimClip(clip.id, false, 30))
            compare(mainClips(p)[0].duration, 30)
            verify(!p.setClipSpeed(clip.id, 2, false))
            verify(p.lastError.indexOf("still image") >= 0)

            // In the player: fitted to the canvas, red on the left and blue on the right.
            const player = settled(f.editor, "player")
            p.seek(1)
            tryVerify(() => isRed(colorAt(player, 0.3, 0.5)) && isBlue(colorAt(player, 0.7, 0.5)), 5000,
                      "the still never showed")
            // Grades and looks apply to it like any picture.
            verify(p.setClipColor(clip.id, { saturation: -1 }))
            tryVerify(() => {
                const c = colorAt(player, 0.3, 0.5)
                return Math.abs(c.r - c.g) < 0.03 && Math.abs(c.g - c.b) < 0.03
            }, 5000, "the grade never reached the still")
        }

        function test_rotated_and_transparent_stills() {
            const f = createEditor()
            const p = f.project
            compare(p.importMedia([still("rotated.jpg"), still("disc.png"), still("halves.png")]), 3)
            verify(p.addClip(mediaNamed(p, "rotated.jpg").id))
            const player = settled(f.editor, "player")
            p.seek(1)
            // Upright: red on top, blue below.
            tryVerify(() => isRed(colorAt(player, 0.5, 0.3)) && isBlue(colorAt(player, 0.5, 0.7)), 5000,
                      "the rotated photo was not upright")

            // The disc over the halves on the track above: green in the middle, the halves around it.
            const tracks = p.tracks // top to bottom
            const above = tracks[tracks.findIndex((t) => t.main) - 1]
            verify(p.addClip(mediaNamed(p, "halves.png").id, p.tracks.find((t) => t.main).id, 5))
            verify(p.addClip(mediaNamed(p, "disc.png").id, above.id, 5))
            p.seek(6)
            tryVerify(() => {
                const centre = colorAt(player, 0.5, 0.5)
                return centre.g > 0.6 && centre.r < 0.3 && isRed(colorAt(player, 0.3, 0.5))
            }, 5000, "the transparent still did not show what is under it")
        }

        function test_stills_survive_save_and_open() {
            const f = createEditor()
            const p = f.project
            compare(p.importMedia([still("halves.webp")]), 1)
            verify(p.addClip(p.media[0].id))
            const file = "file://" + tempDir + "/tve_stills_test.tve"
            verify(p.save(file))
            verify(p.open(file))
            verify(p.media[0].still)
            verify(mainClips(p)[0].isStill)
            const player = settled(f.editor, "player")
            p.seek(1)
            tryVerify(() => isRed(colorAt(player, 0.3, 0.5)), 5000)
        }
    }
}
