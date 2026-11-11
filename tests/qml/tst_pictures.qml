import QtQuick
import QtTest
import TechieVideoEditor.Theme
import TechieVideoEditor.Player
import TechieVideoEditor.Shell

// Placing and moving pictures through the real editor: the anchor point, corner pin,
// animation presets, pan and zoom, colour clips and snapshots.
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
        name: "Pictures"
        when: windowShown

        readonly property string av: "file://" + samplesDir + "/test_av_1080p30.mp4"
        function still(name) { return "file://" + stillsDir + "/" + name }

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
            return fixture
        }
        function mainTrack(p) { return p.tracks.find((t) => t.main) }
        function drag(item, fromX, fromY, toX, toY, modifiers) {
            const mods = modifiers === undefined ? Qt.NoModifier : modifiers
            mousePress(item, fromX, fromY, Qt.LeftButton, mods)
            for (let i = 1; i <= 6; ++i)
                mouseMove(item, fromX + (toX - fromX) * i / 6, fromY + (toY - fromY) * i / 6, -1, Qt.LeftButton, mods)
            mouseRelease(item, toX, toY, Qt.LeftButton, mods)
        }
        // A 2:1 picture on the main track, selected, the playhead on it, its frame showing.
        function picture() {
            const f = createEditor()
            const p = f.project
            compare(p.importMedia([still("halves.png")]), 1)
            verify(p.addClip(p.media[0].id, mainTrack(p).id, 0))
            p.selectedClip = mainTrack(p).clips[0].id
            p.seek(1)
            const overlay = findChild(f.editor, "transformOverlay")
            tryVerify(() => overlay.frame !== null, 3000, "the frame never showed")
            waitForRendering(f.editor)
            return f
        }
        function near(a, b, tolerance) { return Math.abs(a.x - b.x) < tolerance && Math.abs(a.y - b.y) < tolerance }

        function test_the_anchor_point_is_what_it_turns_about() {
            const f = picture()
            const p = f.project
            const overlay = findChild(f.editor, "transformOverlay")
            const corners = overlay.frame.corners.map((c) => Qt.point(c.x, c.y))
            // ⌥-drag the cross to the top-left corner: nothing moves but the anchor.
            const cross = findChild(f.editor, "transformAnchor")
            const c = overlay.frame.pivot
            drag(overlay, c.x, c.y, corners[0].x, corners[0].y, Qt.AltModifier)
            tryVerify(() => Math.abs(p.selectedClipInfo.visual.anchorX + 0.5) < 0.02, 2000, "anchorX " + p.selectedClipInfo.visual.anchorX)
            fuzzyCompare(p.selectedClipInfo.visual.anchorY, -0.5, 0.02)
            compare(p.undoText, "Change Picture")
            for (let i = 0; i < 4; ++i)
                verify(near(overlay.frame.corners[i], corners[i], 2), "corner " + i + " moved")
            verify(near(cross.at, corners[0], 2))
            // Half size now shrinks towards that corner.
            verify(p.setClipVisual(p.selectedClip, { scale: 0.5 }))
            tryVerify(() => near(overlay.frame.corners[0], corners[0], 2))
            verify(!near(overlay.frame.corners[2], corners[2], 20))
            // A plain drag from the middle still moves the picture.
            const m = overlay.frame.centre
            drag(findChild(f.editor, "transformBody"), m.x, m.y, m.x + 60, m.y)
            tryVerify(() => p.selectedClipInfo.visual.x > 0.02)
        }

        function test_corners_pin_one_by_one() {
            const f = picture()
            const p = f.project
            const overlay = findChild(f.editor, "transformOverlay")
            findChild(f.editor, "inspectorTab:Video").clicked()
            const toggle = findChild(f.editor, "pinCornersSwitch")
            toggle.toggle()
            toggle.toggled()
            verify(p.pinningCorners)
            waitForRendering(f.editor) // (the inspector's page changed: the player may have moved)
            wait(100)
            const r = overlay.canvasRect
            const from = overlay.frame.corners[1] // top right
            const others = [0, 2, 3].map((i) => Qt.point(overlay.frame.corners[i].x, overlay.frame.corners[i].y))
            drag(overlay, from.x, from.y, from.x - r.width * 0.2, from.y + r.height * 0.1)
            tryVerify(() => Math.abs(p.selectedClipInfo.visual.pinTopRightX + 0.2) < 0.01, 2000,
                      "pin " + p.selectedClipInfo.visual.pinTopRightX)
            fuzzyCompare(p.selectedClipInfo.visual.pinTopRightY, 0.1, 0.01)
            verify(p.selectedClipInfo.visual.pinned)
            for (const [n, i] of [0, 2, 3].entries())
                verify(near(overlay.frame.corners[i], others[n], 1.5), "corner " + i + " moved")
            // The picture at the canvas point where the corner went is the picture's top right.
            const placement = p.clipPlacement(p.selectedClip)
            const m = placement.m, u = 0.8 - 0.002, v = (r.height * 0.1 + (r.height - r.width / 2) / 2) / r.height + 0.002
            const w = m[6] * u + m[7] * v + m[8]
            fuzzyCompare((m[0] * u + m[1] * v + m[2]) / w, 1, 0.02)
            // Back.
            findChild(f.editor, "resetCornerPin").clicked()
            verify(!p.selectedClipInfo.visual.pinned)
            p.pinningCorners = false
        }

        function test_animation_presets_and_pan_and_zoom() {
            const f = picture()
            const p = f.project
            findChild(f.editor, "inspectorTab:Video").clicked()
            verify(p.clipAnimations().length > 20)
            const combo = findChild(f.editor, "animation:in")
            const index = p.clipAnimations().filter((a) => a.kind === "in").findIndex((a) => a.id === "slide-in-left") + 1
            combo.activated(index)
            compare(p.undoText, "Animate Clip")
            compare(combo.currentIndex, 0)
            const keys = p.selectedClipInfo.keyframes
            compare(keys.x.length, 2)
            fuzzyCompare(keys.x[0].value, -1.1, 0.001)
            fuzzyCompare(keys.x[1].time, 0.6, 0.02)
            verify(p.animateClip(p.selectedClip, "fade-out", 1))
            compare(p.selectedClipInfo.keyframes.opacity.length, 2)
            findChild(f.editor, "clearAnimation").clicked()
            compare(p.undoText, "Remove Animation")
            compare(Object.keys(p.selectedClipInfo.keyframes).length, 0)
            // Pan and zoom over the whole clip.
            findChild(f.editor, "panZoom:zoom-in").clicked()
            compare(p.undoText, "Pan and Zoom")
            const scale = p.selectedClipInfo.keyframes.scale
            fuzzyCompare(scale[0].value, 1, 0.001)
            fuzzyCompare(scale[1].value, 1.25, 0.001)
            fuzzyCompare(scale[1].time, p.selectedClipInfo.duration, 0.02)
            verify(!p.panZoom(p.selectedClip, "nowhere"))
        }

        function test_colour_clips() {
            const f = createEditor()
            const p = f.project
            verify(p.importAndAppend(av))
            p.seek(0.5)
            findChild(f.editor, "colourClipButton").clicked()
            compare(p.undoText, "Add Colour Clip")
            const info = p.selectedClipInfo
            compare(info.colourFill.kind, "solid")
            compare(info.trackKind, "video")
            fuzzyCompare(info.start, 0.5, 0.02)
            verify(info.label.indexOf("Colour") === 0)
            const media = p.media.find((m) => m.colourFill)
            compare(media.width, 1920)
            // A gradient instead: drawn again, the same clip.
            const id = p.selectedClip
            verify(p.setColorClip(id, { kind: "linear", from: [1, 0, 0, 1], to: [0, 0, 1, 1], angle: 0 }))
            compare(p.undoText, "Change Colour")
            compare(p.selectedClip, id)
            compare(p.selectedClipInfo.colourFill.kind, "linear")
            verify(p.selectedClipInfo.label.indexOf("Gradient FF0000-0000FF") === 0)
            findChild(f.editor, "inspectorTab:Video").clicked()
            tryVerify(() => findChild(f.editor, "colourClipSection").visible)
            verify(!p.setColorClip(mainTrack(p).clips[0].id, { kind: "solid" })) // not a colour clip
        }

        function test_a_snapshot_of_the_frame() {
            const f = createEditor()
            const p = f.project
            verify(p.importAndAppend(av))
            p.seek(1)
            findChild(f.editor, "snapshotButton").clicked()
            tryVerify(() => p.media.length === 2, 5000)
            const shot = p.media[1]
            compare(shot.still, true)
            compare(shot.width, 1920)
            compare(shot.height, 1080)
            verify(shot.name.indexOf("Untitled project 00.00.01.00") === 0, shot.name)
            // To a file of its own (JPEG by its name), not into the library.
            const file = tempDir + "/tve-snapshot.jpg"
            compare(p.takeSnapshot("file://" + file), file)
            compare(p.media.length, 2)
            const probe = Qt.createQmlObject('import TechieVideoEditor.Player; ProjectController { autosaveEnabled: false }', testCase)
            compare(probe.importMedia(["file://" + file]), 1)
            compare(probe.media[0].width, 1920)
            probe.destroy()
        }
    }
}
