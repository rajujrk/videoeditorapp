import QtQuick
import QtTest
import TechieVideoEditor.Theme
import TechieVideoEditor.Player
import TechieVideoEditor.Shell

// Motion through the real editor: stabilizing a shaky clip, removing flicker, temporal
// denoise, and tracking a card's surface for a title to follow (samples from
// tools/make_test_media.sh: shaky.mp4, flicker.mp4, card.mp4).
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
        name: "Motion"
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
        // The player's colour at canvas fraction (u, v), as [r, g, b] 0..255.
        function colour(f, u, v) {
            const overlay = findChild(f.editor, "maskOverlay")
            const r = overlay.canvasRect
            const image = grabImage(root)
            const scale = image.width / root.width
            const p = overlay.mapToItem(root, r.x + u * r.width, r.y + v * r.height)
            const c = image.pixel(Math.round(p.x * scale), Math.round(p.y * scale))
            return [c.r * 255, c.g * 255, c.b * 255]
        }
        function difference(a, b) { return Math.abs(a[0] - b[0]) + Math.abs(a[1] - b[1]) + Math.abs(a[2] - b[2]) }
        // A sample on the main track, selected, the Analyze tab open.
        function withSample(f, name) {
            const p = f.project
            if (p.importMedia(["file://" + samplesDir + "/" + name]) !== 1)
                skip(name + " missing: run tools/make_test_media.sh")
            verify(p.addClip(p.media[p.media.length - 1].id))
            const id = p.tracks.find((t) => t.main).clips[0].id
            p.selectedClip = id
            click(f.editor, "inspectorTab:Analyze")
            return id
        }
        function waitFinished(count) {
            tryVerify(() => finished.count >= count, 60000, "the analysis never finished")
            compare(finished.signalArguments[count - 1][0], true, finished.signalArguments[count - 1][1])
        }
        // What the player shows at a few points, at a time.
        function look(f, seconds) {
            f.project.seek(seconds)
            wait(300)
            waitForRendering(root)
            return [colour(f, 0.3, 0.3), colour(f, 0.5, 0.5), colour(f, 0.7, 0.6)]
        }
        function lookDifference(a, b) {
            let d = 0
            for (let i = 0; i < a.length; ++i)
                d += difference(a[i], b[i])
            return d
        }

        function test_stabilizing_a_shaky_shot() {
            const f = createEditor()
            const p = f.project
            const id = withSample(f, "shaky.mp4")
            const shaky = lookDifference(look(f, 1.0), look(f, 1.4))
            click(f.editor, "stabilizeLock")
            click(f.editor, "stabilize")
            waitFinished(1)
            compare(p.undoText, "Stabilize")
            const info = p.selectedClipInfo.stabilization
            verify(info.enabled)
            verify(info.lock)
            verify(info.zoom > 1.04 && info.zoom < 1.15, info.zoom)
            verify(settled(f.editor, "stabilizeState").text.indexOf("Stabilized, zoomed in") === 0)
            // Locked, two moments of the shot look alike where they did not before.
            const steadied = lookDifference(look(f, 1.0), look(f, 1.4))
            verify(steadied < shaky / 2, steadied + " vs " + shaky)
            // Again, instant (the motion is kept); then off, one step.
            verify(p.stabilize(id, { lock: false, smoothness: 0.2 }))
            verify(!p.analyzing)
            verify(!p.selectedClipInfo.stabilization.lock)
            click(f.editor, "removeStabilization")
            compare(p.undoText, "Remove Stabilization")
            verify(!p.selectedClipInfo.stabilization.enabled)
            p.undo()
            verify(p.selectedClipInfo.stabilization.enabled)
        }

        function test_flicker_and_grain() {
            const f = createEditor()
            const p = f.project
            const id = withSample(f, "flicker.mp4")
            // Between two frames, over a few points (the pattern moves a little too).
            const flicker = lookDifference(look(f, 1.0), look(f, 1.0 + 1 / 30))
            click(f.editor, "removeFlicker")
            waitFinished(1)
            compare(p.undoText, "Remove Flicker")
            verify(p.selectedClipInfo.deflicker.enabled)
            const even = lookDifference(look(f, 1.0), look(f, 1.0 + 1 / 30))
            verify(even < flicker * 0.6, even + " vs " + flicker)
            click(f.editor, "keepFlicker")
            compare(p.undoText, "Keep Flicker")

            verify(p.setDenoise(id, 0.6))
            compare(p.undoText, "Denoise")
            compare(p.selectedClipInfo.denoise, 0.6)
            verify(p.setDenoise(id, 0.3))
            compare(p.undoText, "Change Denoise")
            verify(!p.setDenoise(id, 2))
            verify(p.setDenoise(id, 0))
            compare(p.undoText, "Remove Denoise")
            // The picture still draws with denoise on, frame after frame.
            verify(p.setDenoise(id, 1))
            look(f, 1.0)
            look(f, 1.0 + 1 / 30)
            verify(colour(f, 0.5, 0.5)[0] >= 0)
        }

        function test_a_title_follows_a_tracked_surface() {
            const f = createEditor()
            const p = f.project
            const card = withSample(f, "card.mp4")
            p.seek(0)
            // Draw a box around the card (centre 0.375, 0.528; about 0.23 × 0.28) on the player.
            click(f.editor, "chooseTrackArea")
            verify(p.pickingTrackArea)
            const overlay = findChild(f.editor, "trackOverlay")
            const area = findChild(f.editor, "trackArea")
            const a = overlay.fromPicture(0.24, 0.36)
            const b = overlay.fromPicture(0.51, 0.70)
            mousePress(area, a.x, a.y)
            mouseMove(area, (a.x + b.x) / 2, (a.y + b.y) / 2)
            mouseMove(area, b.x, b.y)
            mouseRelease(area, b.x, b.y)
            verify(!p.pickingTrackArea)
            compare(p.trackArea.clipId, card)
            fuzzyCompare(p.trackArea.x, 0.375, 0.01)
            verify(settled(f.editor, "trackBox"))

            click(f.editor, "trackSurface")
            waitFinished(1)
            compare(p.undoText, "Track Motion")
            const tracks = p.selectedClipInfo.motionTracks
            compare(tracks.length, 1)
            verify(tracks[0].surface)
            fuzzyCompare(tracks[0].start, 0, 0.05)
            verify(tracks[0].end > 2.5, tracks[0].end)
            verify(settled(f.editor, "motionTrack:1").text.indexOf("Track 1 · surface") === 0)

            // A title on the card follows it: moving right, turning clockwise.
            verify(p.addTitle({ text: "Hi" }, -1, 0))
            const title = p.selectedClip
            click(f.editor, "inspectorTab:Analyze")
            verify(settled(f.editor, "followSection"))
            click(f.editor, "followTurn")
            click(f.editor, "followTrack")
            compare(p.undoText, "Follow Track")
            const keys = p.selectedClipInfo.keyframes
            verify(keys.x.length >= 2)
            fuzzyCompare(keys.x[0].value, 0, 0.01)               // from where it was
            verify(keys.x[keys.x.length - 1].value > 0.15)        // 4 px a frame of 1280: ≈ 0.27 in 3 s
            verify(keys.rotation[keys.rotation.length - 1].value > 15) // ≈ 0.25° a frame
            compare(keys.scale, undefined)                        // not asked for

            // The track can go; following needs a track on screen at the same time.
            p.selectedClip = card
            click(f.editor, "deleteTrack:1")
            compare(p.undoText, "Delete Track")
            compare(p.selectedClipInfo.motionTracks.length, 0)
            compare(p.tracksToFollow(title).length, 0)
            verify(!p.followTrack(title, card, 1))
            verify(!p.trackMotion(card))
            verify(p.lastError.indexOf("Draw a box") === 0, p.lastError)
        }
    }
}
