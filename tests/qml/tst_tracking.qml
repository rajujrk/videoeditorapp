import QtQuick
import QtTest
import TechieVideoEditor.Theme
import TechieVideoEditor.Player
import TechieVideoEditor.Shell

// Planar and point tracking through the real editor: the kind chosen in the Analyze tab, the
// plane's four corners placed and moved on the player, a point clicked; tracked in the
// background; a picture pinned onto the plane (corner pin keyframes), the shot held still on
// a point. (How exactly a moving plane is followed: the engine's tests.)
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
            ProjectController { id: controller; view: screen.player; autosaveEnabled: false }
            EditorScreen { id: screen; anchors.fill: parent; project: controller }
        }
    }
    SignalSpy { id: finished; signalName: "analysisFinished" }

    TestCase {
        id: testCase
        name: "Tracking"
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
        function waitFinished(count) {
            tryVerify(() => finished.count >= count, 60000, "the analysis never finished")
            compare(finished.signalArguments[count - 1][0], true, finished.signalArguments[count - 1][1])
        }
        // The colour bars sample on the main track, selected, the Analyze tab open.
        function bars(f) {
            const p = f.project
            if (p.importMedia(["file://" + samplesDir + "/test_av_1080p30.mp4"]) !== 1)
                skip("samples missing: run tools/make_test_media.sh")
            verify(p.addClip(p.media[0].id))
            const id = p.tracks.find((t) => t.main).clips[0].id
            p.selectedClip = id
            p.seek(0)
            click(f.editor, "inspectorTab:Analyze")
            return id
        }
        // Drags on the player from picture fraction a to b (or clicks at a).
        function dragOnPlayer(f, a, b) {
            const overlay = findChild(f.editor, "trackOverlay")
            const area = findChild(f.editor, "trackArea")
            const from = overlay.fromPicture(a[0], a[1])
            const to = b ? overlay.fromPicture(b[0], b[1]) : from
            mousePress(area, from.x, from.y)
            if (b) {
                mouseMove(area, (from.x + to.x) / 2, (from.y + to.y) / 2)
                mouseMove(area, to.x, to.y)
            }
            mouseRelease(area, to.x, to.y)
        }

        function test_a_plane_is_placed_tracked_and_a_picture_pinned_onto_it() {
            const f = createEditor()
            const p = f.project
            const clip = bars(f)
            // The plane: a box dragged, then a corner moved (perspective).
            click(f.editor, "trackKind:plane")
            compare(p.trackAreaKind, "plane")
            verify(settled(f.editor, "chooseTrackArea").text.indexOf("corners") > 0)
            click(f.editor, "chooseTrackArea")
            verify(p.pickingTrackArea)
            dragOnPlayer(f, [0.3, 0.3], [0.7, 0.7])
            verify(!p.pickingTrackArea)
            compare(p.trackArea.corners.length, 8)
            fuzzyCompare(p.trackArea.corners[0], 0.3, 0.01)
            fuzzyCompare(p.trackArea.corners[5], 0.7, 0.01)
            const corner = settled(f.editor, "trackCorner:1") // top right
            const overlay = findChild(f.editor, "trackOverlay")
            const to = overlay.fromPicture(0.66, 0.34)
            const grip = corner.mapToItem(overlay, corner.width / 2, corner.height / 2)
            mousePress(overlay, grip.x, grip.y)
            mouseMove(overlay, (grip.x + to.x) / 2, (grip.y + to.y) / 2)
            mouseMove(overlay, to.x, to.y)
            mouseRelease(overlay, to.x, to.y)
            fuzzyCompare(p.trackArea.corners[2], 0.66, 0.01)
            fuzzyCompare(p.trackArea.corners[3], 0.34, 0.01)
            verify(findChild(f.editor, "trackPlane").visible)
            // Corners that make no shape are refused.
            verify(!p.setTrackArea(clip, { corners: [0.1, 0.1, 0.5, 0.1, 0.9, 0.1, 0.1, 0.5] }))

            click(f.editor, "trackMotion")
            waitFinished(1)
            compare(p.undoText, "Track Motion")
            const tracks = p.selectedClipInfo.motionTracks
            compare(tracks.length, 1)
            compare(tracks[0].kind, "plane")
            verify(!tracks[0].surface)
            verify(settled(f.editor, "motionTrack:1").text.indexOf("Track 1 · plane") === 0)
            verify(tracks[0].end > 1.5, "ended at " + tracks[0].end)

            // A picture pinned onto it: its corners on the plane's (the bars stand still).
            compare(p.importMedia(["file://" + stillsDir + "/disc.png"]), 1)
            verify(p.addClip(p.media[1].id, p.tracks.find((t) => t.name === "Video 2").id, 0))
            const picture = p.selectedClip
            verify(picture !== clip)
            click(f.editor, "inspectorTab:Analyze")
            verify(settled(f.editor, "followSection"))
            const choice = findChild(f.editor, "followChoice")
            compare(choice.model[choice.currentIndex].kind, "plane")
            verify(settled(f.editor, "followPin").visible)
            click(f.editor, "followPin")
            compare(settled(f.editor, "followTrack").text, "Pin")
            click(f.editor, "followTrack")
            compare(p.undoText, "Pin to Plane")
            const keys = p.selectedClipInfo.keyframes
            verify(keys.pinTopLeftX.length >= 2)
            // The picture (square, fitted in the 16:9 canvas: 0.34..0.66 across) is pinned onto
            // the plane: its top left moves from (0.22, 0) to (0.3, 0.3), its top right to (0.66, 0.34).
            fuzzyCompare(keys.pinTopLeftX[0].value, 0.3 - 0.21875, 0.02)
            fuzzyCompare(keys.pinTopLeftY[0].value, 0.3, 0.02)
            fuzzyCompare(keys.pinTopRightX[0].value, 0.66 - 0.78125, 0.02)
            fuzzyCompare(keys.pinTopRightY[0].value, 0.34, 0.02)
        }

        // Tracked from the middle of the clip, followed by a clip that starts before the track:
        // its keyframes start where both are on screen, once each.
        function test_a_track_started_midway_is_followed_and_pinned() {
            const f = createEditor()
            const p = f.project
            const clip = bars(f)
            p.seek(0.5)
            verify(p.setTrackArea(clip, { corners: [0.3, 0.3, 0.7, 0.3, 0.7, 0.7, 0.3, 0.7] }))
            verify(p.trackMotion(clip, { kind: "plane" }))
            waitFinished(1)
            const track = p.selectedClipInfo.motionTracks[0]
            verify(track.start >= 0.45, "started at " + track.start)
            compare(p.importMedia(["file://" + stillsDir + "/disc.png"]), 1)
            verify(p.addClip(p.media[1].id, p.tracks.find((t) => t.name === "Video 2").id, 0))
            const picture = p.selectedClip
            verify(p.followTrack(picture, clip, track.id, { pin: true }), p.lastError)
            compare(p.undoText, "Pin to Plane")
            const pins = p.selectedClipInfo.keyframes.pinTopLeftX
            fuzzyCompare(pins[0].time, track.start, 0.04)
            for (let i = 1; i < pins.length; ++i)
                verify(pins[i].time > pins[i - 1].time, "keyframe " + i + " not after the one before")
            p.undo()
            verify(p.followTrack(picture, clip, track.id, { scale: true }), p.lastError)
            verify(p.selectedClipInfo.keyframes.x.length >= 2)
        }

        function test_a_point_is_tracked_and_the_shot_held_still_on_it() {
            const f = createEditor()
            const p = f.project
            const clip = bars(f)
            click(f.editor, "trackKind:point")
            compare(p.trackAreaKind, "point")
            click(f.editor, "chooseTrackArea")
            dragOnPlayer(f, [0.4, 0.5])
            verify(p.trackArea.point)
            fuzzyCompare(p.trackArea.x, 0.4, 0.01)
            verify(p.trackArea.width > 0.03) // a small spot round it
            verify(settled(f.editor, "trackPoint").visible)
            click(f.editor, "trackMotion")
            waitFinished(1)
            const tracks = p.selectedClipInfo.motionTracks
            compare(tracks.length, 1)
            compare(tracks[0].kind, "point")
            verify(settled(f.editor, "motionTrack:1").text.indexOf("Track 1 · point") === 0)

            // Held still on it: position keyframes on the clip itself (still bars: barely moving).
            click(f.editor, "holdStill:1")
            compare(p.undoText, "Hold Still")
            const keys = p.selectedClipInfo.keyframes
            verify(keys.x.length >= 1)
            verify(Math.abs(keys.x[keys.x.length - 1].value) < 0.02)
            // Another clip cannot hold this clip's track still; and a kind that does not exist.
            verify(!p.followTrack(clip, -1, 1, { hold: true }))
            verify(!p.trackMotion(clip, { kind: "wobble" }))
            // Choosing another kind forgets what was drawn for this one.
            p.trackAreaKind = "object"
            compare(p.trackArea.clipId, undefined)
        }
    }
}
