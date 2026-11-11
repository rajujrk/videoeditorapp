import QtQuick
import QtQuick.Window
import QtTest
import TechieVideoEditor.Theme
import TechieVideoEditor.Player
import TechieVideoEditor.Shell

// Placing clips by hand in the player (move, scale, rotate, click to select) and dropping
// media and files where they should land, through the real editor.
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
        name: "Transform"
        when: windowShown

        function still(name) { return "file://" + stillsDir + "/" + name }
        readonly property string av: "file://" + samplesDir + "/test_av_1080p30.mp4"

        function createEditor() {
            const fixture = createTemporaryObject(editorComponent, root)
            verify(fixture)
            waitForRendering(fixture)
            return fixture
        }
        function mainTrack(p) { return p.tracks.find((t) => t.main) }
        function trackAbove(p) { return p.tracks[p.tracks.findIndex((t) => t.main) - 1] }
        function drag(item, fromX, fromY, toX, toY, modifiers) {
            const mods = modifiers === undefined ? Qt.NoModifier : modifiers
            mousePress(item, fromX, fromY, Qt.LeftButton, mods)
            for (let i = 1; i <= 6; ++i)
                mouseMove(item, fromX + (toX - fromX) * i / 6, fromY + (toY - fromY) * i / 6, -1, Qt.LeftButton, mods)
            mouseRelease(item, toX, toY, Qt.LeftButton, mods)
        }
        // Red | blue halves on the main track, 0–5 s, selected, the playhead on it.
        function halves() {
            const f = createEditor()
            const p = f.project
            compare(p.importMedia([still("halves.png"), still("disc.png")]), 2)
            verify(p.addClip(p.media[0].id, mainTrack(p).id, 0))
            p.selectedClip = mainTrack(p).clips[0].id
            p.seek(1)
            const overlay = findChild(f.editor, "transformOverlay")
            tryVerify(() => overlay.frame !== null, 3000, "the frame never showed")
            waitForRendering(f.editor) // (the frame drawn where the drags will press)
            return f
        }

        function test_the_frame_moves_scales_and_rotates_the_clip() {
            const f = halves()
            const p = f.project
            const overlay = findChild(f.editor, "transformOverlay")
            const r = overlay.canvasRect
            // Fitted to the canvas: the 2:1 picture spans its width, letterboxed.
            const corners = overlay.frame.corners
            fuzzyCompare(corners[0].x, r.x, 1.5)
            fuzzyCompare(corners[2].x, r.x + r.width, 1.5)
            fuzzyCompare(corners[0].y, r.y + (r.height - r.width / 2) / 2, 1.5)
            fuzzyCompare(corners[2].y, r.y + (r.height + r.width / 2) / 2, 1.5)

            // Drag inside: moved by a quarter of the canvas's width, one undo step.
            const body = findChild(f.editor, "transformBody")
            const c = overlay.frame.centre
            drag(body, c.x - 40, c.y, c.x - 40 + r.width / 4, c.y)
            tryVerify(() => Math.abs(p.selectedClipInfo.visual.x - 0.25) < 0.01, 2000, "x " + p.selectedClipInfo.visual.x)
            compare(p.undoText, "Change Picture")
            // Back near the middle: it snaps to the centre line.
            const c2 = overlay.frame.centre
            drag(body, c2.x, c2.y, c2.x - r.width / 4 + 3, c2.y)
            tryCompare(p.selectedClipInfo.visual, "x", 0, 2000)

            // A corner dragged out to half again as far from the centre: half as large again.
            const corner = findChild(f.editor, "transformCorner:2")
            const cc = overlay.frame.centre
            const from = overlay.frame.corners[2]
            const to = Qt.point(cc.x + (from.x - cc.x) * 0.5, cc.y + (from.y - cc.y) * 0.5)
            drag(overlay, from.x, from.y, to.x, to.y)
            tryVerify(() => Math.abs(p.selectedClipInfo.visual.scale - 0.5) < 0.02, 2000, "scale " + p.selectedClipInfo.visual.scale)

            // The knob, turned a quarter round about the centre, with ⇧: 90°.
            const knob = findChild(f.editor, "transformRotate")
            const k = Qt.point(knob.x + knob.width / 2, knob.y + knob.height / 2)
            const centre = overlay.frame.centre
            const radius = Math.hypot(k.x - centre.x, k.y - centre.y)
            drag(overlay, k.x, k.y, centre.x + radius, centre.y + 2, Qt.ShiftModifier)
            tryCompare(p.selectedClipInfo.visual, "rotation", 90, 2000)
            p.undo()
            compare(p.selectedClipInfo.visual.rotation || 0, 0)

            // Hidden while playing, and for a clip the playhead is not on.
            p.seek(6)
            tryVerify(() => overlay.frame === null)
        }

        function test_clicking_a_picture_selects_its_clip() {
            const f = halves()
            const p = f.project
            // The disc above, small, in the top-left quarter.
            verify(p.addClip(p.media[1].id, trackAbove(p).id, 0))
            const disc = trackAbove(p).clips[0].id
            verify(p.setClipVisual(disc, { scale: 0.3, x: -0.25, y: -0.25 }))
            const halvesClip = mainTrack(p).clips[0].id
            p.selectedClip = halvesClip
            const overlay = findChild(f.editor, "transformOverlay")
            const r = overlay.canvasRect
            const body = findChild(f.editor, "transformBody")
            // Where only the halves are, outside the selected frame? Inside: it stays selected.
            mouseClick(body, r.x + r.width * 0.25, r.y + r.height * 0.25)
            compare(p.selectedClip, disc) // the disc is on top there
            mouseClick(body, r.x + r.width * 0.8, r.y + r.height * 0.8)
            compare(p.selectedClip, halvesClip)
            compare(p.clipAtCanvas(0.25, 0.25), disc)
            compare(p.clipAtCanvas(0.8, 0.8), halvesClip)
        }

        function test_drops_land_where_they_fit() {
            const f = createEditor()
            const p = f.project
            compare(p.importMedia([av]), 1)
            const media = p.media[0].id
            // Onto the main track at 0: there.
            verify(p.dropMediaAt(media, mainTrack(p).id, 0))
            compare(mainTrack(p).clips.length, 1)
            // Onto the main track where it is taken: on the track above instead.
            verify(p.dropMediaAt(media, mainTrack(p).id, 1))
            compare(trackAbove(p).clips.length, 1)
            fuzzyCompare(trackAbove(p).clips[0].start, 1, 0.01)
            // A video dropped on an audio track goes to a video track with room.
            const audio = p.tracks.find((t) => t.kind === "audio")
            verify(p.dropMediaAt(media, audio.id, 10))
            compare(audio.clips.length, 0)
            // Files dropped from Finder: imported, then placed one after another.
            const placed = p.dropFilesAt([still("halves.png"), still("disc.png")], mainTrack(p).id, 2)
            compare(placed, 2)
            compare(p.media.length, 3)
            const main = mainTrack(p).clips
            fuzzyCompare(main[main.length - 2].start, 2, 0.01)
            verify(main[main.length - 1].start > 2)
        }
    }
}
