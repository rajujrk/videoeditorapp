import QtQuick
import QtTest
import TechieVideoEditor.Theme
import TechieVideoEditor.Player
import TechieVideoEditor.Shell

// Everyday timeline handling (slice 31a) through the real editor: replacing media, turning
// clips off, placeholders, extending an edit, selecting from the playhead, the next edit,
// match frame, loop, auto ripple, moving and locking tracks, deleting every transition or
// marker, the default transition, rotating and flipping, skimming; the player's frame border
// with pictures snapping to it; and recording the entire screen, a window or an area.
Item {
    id: root
    width: 1400
    height: 860

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
        name: "TimelineHandling"
        when: windowShown

        readonly property string av: "file://" + samplesDir + "/test_av_1080p30.mp4"
        function still(name) { return "file://" + stillsDir + "/" + name }

        function initTestCase() {
            const probe = Qt.createQmlObject('import TechieVideoEditor.Player; ProjectController {}', testCase)
            if (probe.importMedia([av]) !== 1)
                skip("samples missing: run tools/make_test_media.sh")
            probe.destroy()
        }
        function init() { EditingPreferences.reset() }
        function cleanup() { EditingPreferences.reset() }

        function createEditor() {
            const fixture = createTemporaryObject(editorComponent, root)
            verify(fixture)
            waitForRendering(fixture)
            return fixture
        }
        function mainTrack(p) { return p.tracks.find((t) => t.main) }
        // Three 2-second clips on the main track (0–6 s).
        function threeClips() {
            const f = createEditor()
            const p = f.project
            for (let i = 0; i < 3; ++i)
                verify(p.importAndAppend(av))
            compare(mainTrack(p).clips.length, 3)
            return f
        }
        function shortcut(f, name) {
            const s = findChild(f.editor, "shortcut:" + name)
            verify(s, name)
            verify(s.enabled, name + " is off")
            s.activated()
        }
        function settled(parent, name) {
            let item = null
            tryVerify(() => {
                item = findChild(parent, name)
                return item !== null && item.visible && item.width > 0
            }, 3000, name + " never showed")
            waitForPolish(item, 1000)
            return item
        }
        // A menu entry, also inside a submenu (made by the menu, outside the object tree).
        function entry(f, name, menuName) {
            if (menuName === undefined)
                return findChild(f.editor, name)
            const menu = findChild(f.editor, menuName)
            for (let i = 0; menu && i < menu.count; ++i)
                if (menu.itemAt(i) && menu.itemAt(i).objectName === name)
                    return menu.itemAt(i)
            return null
        }
        function trigger(f, name, menuName) {
            const e = entry(f, name, menuName)
            verify(e, name)
            verify(e.enabled, name + " is disabled")
            e.triggered()
            if (e.menu)
                e.menu.dismiss()
        }
        function drag(item, fromX, fromY, toX, toY) {
            mousePress(item, fromX, fromY)
            for (let i = 1; i <= 8; ++i)
                mouseMove(item, fromX + (toX - fromX) * i / 8, fromY + (toY - fromY) * i / 8, -1, Qt.LeftButton)
            mouseRelease(item, toX, toY)
        }

        function test_replacing_media_keeps_the_clip_where_it_is() {
            const f = threeClips()
            const p = f.project
            compare(p.importMedia([still("halves.png")]), 1)
            const photo = p.media.find((m) => m.still).id
            const second = mainTrack(p).clips[1]
            verify(p.setClipVisual(second.id, { opacity: 0.5 }))
            // Offered on the clip's menu: the project's other pictures.
            p.selectedClip = second.id
            const menus = findChild(f.editor, "timelineMenus")
            menus.openClipMenu(second, mainTrack(p), f.editor, 10, 10)
            const replace = findChild(f.editor, "clipMenu:replace")
            verify(replace.enabled)
            verify(entry(f, "clipMenu:replace:" + photo, "clipMenu:replace"))
            trigger(f, "clipMenu:replace:" + photo, "clipMenu:replace")
            findChild(f.editor, "clipMenu").dismiss()
            compare(p.undoText, "Replace Media")
            const replaced = mainTrack(p).clips[1]
            compare(replaced.id, second.id)
            compare(replaced.mediaId, photo)
            fuzzyCompare(replaced.start, 2, 0.001)
            fuzzyCompare(replaced.duration, 2, 0.001)
            p.selectedClip = replaced.id
            fuzzyCompare(p.selectedClipInfo.visual.opacity, 0.5, 1e-6)
            p.undo()
            compare(mainTrack(p).clips[1].mediaId, second.mediaId)

            // A placeholder at the playhead, replaced by a shorter video: the clip shortens.
            p.seek(6)
            const placeholder = p.addPlaceholder()
            verify(placeholder > 0)
            compare(p.undoText, "Add Placeholder")
            const stand = mainTrack(p).clips.find((c) => c.id === placeholder)
            verify(stand.placeholder)
            fuzzyCompare(stand.duration, 5, 0.001)
            verify(p.media.find((m) => m.id === stand.mediaId).placeholder)
            verify(p.replaceClipMedia(placeholder, second.mediaId))
            const filled = mainTrack(p).clips.find((c) => c.id === placeholder)
            verify(!filled.placeholder)
            fuzzyCompare(filled.duration, 2, 0.05)
            verify(p.lastError.indexOf("shorter") >= 0)
            // Only media in the project.
            verify(!p.replaceClipMedia(placeholder, 12345))
        }

        function test_a_clip_turned_off_stays_but_is_not_seen() {
            const f = threeClips()
            const p = f.project
            const second = mainTrack(p).clips[1].id
            p.selectedClip = second
            p.seek(3)
            compare(p.clipAtCanvas(0.5, 0.5), second)
            shortcut(f, "toggleClip")
            compare(p.undoText, "Turn Clip Off")
            compare(p.selectedClipInfo.enabled, false)
            compare(mainTrack(p).clips[1].enabled, false)
            compare(p.clipAtCanvas(0.5, 0.5), 0) // nothing shows there
            compare(mainTrack(p).clips.length, 3)
            tryVerify(() => findChild(f.editor, "clipBody:" + second).opacity < 0.5)
            // On again from its menu.
            findChild(f.editor, "timelineMenus").openClipMenu(mainTrack(p).clips[1], mainTrack(p), f.editor, 10, 10)
            compare(findChild(f.editor, "clipMenu:enabled").text, "Turn on")
            trigger(f, "clipMenu:enabled")
            compare(p.undoText, "Turn Clip On")
            compare(p.clipAtCanvas(0.5, 0.5), second)
        }

        function test_edits_from_the_playhead() {
            const f = threeClips()
            const p = f.project
            const clips = mainTrack(p).clips
            // Next and previous edit.
            p.seek(0.5)
            shortcut(f, "nextEdit")
            fuzzyCompare(p.playhead, 2, 0.001)
            shortcut(f, "nextEdit")
            fuzzyCompare(p.playhead, 4, 0.001)
            shortcut(f, "previousEdit")
            fuzzyCompare(p.playhead, 2, 0.001)
            // Select from the playhead on, and up to it.
            p.seek(3)
            shortcut(f, "selectForward")
            compare(p.selectedClips.length, 2)
            verify(p.selectedClips.indexOf(clips[0].id) < 0)
            shortcut(f, "selectBackward")
            compare(p.selectedClips.length, 2)
            verify(p.selectedClips.indexOf(clips[2].id) < 0)
            // Extend: the first clip's end, trimmed back, pulled out to the playhead.
            verify(p.trimClip(clips[0].id, false, 1.2))
            p.selectedClip = clips[0].id
            p.seek(1.7)
            shortcut(f, "extendEdit")
            compare(p.undoText, "Extend Edit")
            fuzzyCompare(mainTrack(p).clips[0].duration, 1.7, 0.01)
            // Past the end of its media: refused, said why.
            p.seek(2.5)
            verify(!p.extendEdit())
            verify(p.lastError.length > 0)
        }

        function test_match_frame_opens_the_media_at_that_frame() {
            const f = threeClips()
            const p = f.project
            p.seek(3.25)
            const found = p.matchFrame()
            compare(found.mediaId, mainTrack(p).clips[1].mediaId)
            fuzzyCompare(found.seconds, 1.25, 0.04)
            shortcut(f, "matchFrame")
            const player = findChild(f.editor, "sourceMonitor")
            tryVerify(() => player.ready, 5000)
            tryVerify(() => Math.abs(player.controller.playhead - 1.25) < 0.05, 3000, "at " + player.controller.playhead)
            fuzzyCompare(p.playhead, 3.25, 0.001) // the timeline stays
        }

        function test_loop_goes_round_again() {
            const f = threeClips()
            const p = f.project
            const loop = findChild(f.editor, "loopButton")
            verify(!loop.checked)
            loop.clicked()
            verify(EditingPreferences.loopPlayback)
            p.seek(5.6)
            p.play()
            tryVerify(() => p.playing && p.playhead < 1.5, 4000, "did not go round, at " + p.playhead)
            p.pause()
            // A marked range goes round within itself.
            p.setMarkIn(1)
            p.setMarkOut(2)
            verify(p.playMarkedRange())
            wait(1600)
            verify(p.playing)
            verify(p.playhead >= 0.95 && p.playhead <= 2.05, p.playhead)
            p.pause()
            shortcut(f, "loop")
            verify(!EditingPreferences.loopPlayback)
        }

        function test_auto_ripple_closes_gaps_on_every_track() {
            const f = threeClips()
            const p = f.project
            const upper = p.addTrack("video")
            verify(p.addClip(p.media[0].id, upper, 0))
            verify(p.addClip(p.media[0].id, upper, 3))
            const track = () => p.tracks.find((t) => t.id === upper)
            // Without it, deleting leaves the gap above the main track.
            p.deleteClip(track().clips[0].id)
            fuzzyCompare(track().clips[0].start, 3, 0.001)
            p.undo()
            findChild(f.editor, "autoRippleButton").clicked()
            verify(EditingPreferences.autoRipple)
            p.deleteClip(track().clips[0].id)
            compare(p.undoText, "Ripple Delete")
            fuzzyCompare(track().clips[0].start, 1, 0.001) // moved back by the clip's 2 s
        }

        function test_tracks_move_and_lock_together() {
            const f = threeClips()
            const p = f.project
            const upper = p.addTrack("video")
            verify(p.renameTrack(upper, "B-roll"))
            const order = () => p.tracks.map((t) => t.name)
            const before = order()
            verify(before.indexOf("B-roll") < before.indexOf("Video 1")) // above the main track
            const menus = findChild(f.editor, "timelineMenus")
            menus.openTrackMenu(p.tracks.find((t) => t.id === upper), f.editor, 10, 10)
            trigger(f, "trackMenu:moveDown")
            compare(p.undoText, "Move Track Down")
            verify(order().indexOf("B-roll") > order().indexOf("Video 2")) // under Video 2 now
            verify(p.moveTrack(upper, -1))
            verify(order().indexOf("B-roll") > order().indexOf("Video 1"))
            verify(p.tracks.find((t) => t.name === "B-roll").main) // the lowest video track is the main one
            verify(!p.moveTrack(upper, -1)) // at the bottom of the pictures
            compare(p.lockAllTracks(true), p.tracks.length)
            verify(p.tracks.every((t) => t.locked))
            compare(p.undoText, "Lock Every Track")
            verify(!p.deleteClip(p.tracks.find((t) => t.name === "Video 1").clips[0].id)) // locked
            compare(p.lockAllTracks(false), p.tracks.length)
        }

        function test_every_transition_or_marker_goes_at_once() {
            const f = threeClips()
            const p = f.project
            const clips = mainTrack(p).clips
            verify(p.addTransition("tve.transition.dissolve", clips[1].id))
            verify(p.addTransition("tve.transition.wipe", clips[2].id))
            p.addMarker(1)
            p.addMarker(5)
            const menus = findChild(f.editor, "timelineMenus")
            const confirm = findChild(f.editor, "timelineConfirm")
            menus.openLaneMenu(mainTrack(p), 1, f.editor, 10, 10)
            trigger(f, "laneMenu:deleteTransitions")
            tryVerify(() => confirm.opened)
            findChild(confirm.contentItem, "confirmAccept").clicked()
            tryVerify(() => mainTrack(p).clips.every((c) => !c.transition || c.transition.clipId === undefined))
            compare(p.undoText, "Delete Every Transition")
            menus.openLaneMenu(mainTrack(p), 1, f.editor, 10, 10)
            trigger(f, "laneMenu:deleteMarkers")
            tryVerify(() => confirm.opened)
            findChild(confirm.contentItem, "confirmAccept").clicked()
            tryCompare(p.markers, "length", 0)
            compare(p.undoText, "Delete Every Marker")
            p.undo()
            compare(p.markers.length, 2)
            compare(p.removeAllTransitions(), 0) // none left
        }

        function test_the_default_transition_and_its_length() {
            const f = threeClips()
            const p = f.project
            EditingPreferences.defaultTransition = "tve.transition.wipe"
            EditingPreferences.defaultTransitionLength = 0.5
            p.seek(3.9)
            shortcut(f, "defaultTransition")
            const info = p.selectedTransitionInfo
            compare(info.transitionId, "tve.transition.wipe")
            fuzzyCompare(info.duration, 0.5, 0.001)
            // Any new transition gets the default length; one put over another keeps its own.
            verify(p.addTransition("tve.transition.dissolve", mainTrack(p).clips[1].id))
            fuzzyCompare(p.selectedTransitionInfo.duration, 0.5, 0.001)
            verify(p.setTransition(mainTrack(p).clips[1].id, { duration: 1.5 }))
            verify(p.addTransition("tve.transition.wipe", mainTrack(p).clips[1].id))
            fuzzyCompare(p.selectedTransitionInfo.duration, 1.5, 0.001)
            // ⌘T over it gives it the default length again.
            verify(p.addDefaultTransition(mainTrack(p).clips[1].id))
            fuzzyCompare(p.selectedTransitionInfo.duration, 0.5, 0.001)
            // Lengths are kept between 0.1 and 10 seconds.
            EditingPreferences.defaultTransitionLength = 40
            compare(EditingPreferences.defaultTransitionLength, 10)
        }

        function test_rotating_and_flipping_a_picture() {
            const f = threeClips()
            const p = f.project
            const clip = mainTrack(p).clips[0]
            p.selectedClip = clip.id
            findChild(f.editor, "timelineMenus").openClipMenu(clip, mainTrack(p), f.editor, 10, 10)
            trigger(f, "clipMenu:rotateRight", "clipMenu:rotate")
            findChild(f.editor, "clipMenu").dismiss()
            compare(p.undoText, "Rotate")
            fuzzyCompare(p.selectedClipInfo.visual.rotation, 90, 1e-6)
            verify(p.rotateClip(clip.id, -90))
            fuzzyCompare(p.selectedClipInfo.visual.rotation, 0, 1e-6)
            verify(p.flipClip(clip.id, true))
            compare(p.undoText, "Flip Horizontally")
            compare(p.selectedClipInfo.visual.flipH, true)
            compare(mainTrack(p).clips[0].flipH, true)
            verify(p.flipClip(clip.id, false))
            compare(p.selectedClipInfo.visual.flipV, true)
            // Kept by the visual's own keys too.
            verify(p.setClipVisual(clip.id, { flipH: false }))
            compare(p.selectedClipInfo.visual.flipH, false)
        }

        function test_skimming_shows_the_frame_under_the_pointer() {
            const f = threeClips()
            const p = f.project
            p.seek(1)
            const lanes = settled(f.editor, "laneSpace")
            mouseMove(lanes, 200, 20)
            wait(50)
            compare(p.skimTime, -1) // off
            findChild(f.editor, "skimmingButton").clicked()
            verify(EditingPreferences.skimming)
            mouseMove(lanes, 210, 20)
            mouseMove(lanes, 220, 20)
            tryVerify(() => p.skimTime > 0, 2000)
            verify(Math.abs(p.skimTime - 1) > 0.05)
            fuzzyCompare(p.playhead, 1, 0.001) // the playhead stays
            verify(findChild(f.editor, "skimLine").visible)
            // Off the timeline: back to the playhead's frame.
            mouseMove(root, 5, 5)
            tryCompare(p, "skimTime", -1, 2000)
            fuzzyCompare(p.playhead, 1, 0.001)
            // Seeking while skimming ends it.
            mouseMove(lanes, 240, 20)
            tryVerify(() => p.skimTime > 0, 2000)
            p.seek(2)
            compare(p.skimTime, -1)
            mouseMove(root, 5, 5)
        }

        // The halves picture (2:1) at half size in the middle, selected, its frame shown.
        function halfSize() {
            const f = createEditor()
            const p = f.project
            compare(p.importMedia([still("halves.png")]), 1)
            verify(p.addClip(p.media[0].id, mainTrack(p).id, 0))
            const id = mainTrack(p).clips[0].id
            verify(p.setClipVisual(id, { scale: 0.5 }))
            p.selectedClip = id
            p.seek(1)
            const overlay = findChild(f.editor, "transformOverlay")
            tryVerify(() => overlay.frame !== null, 3000, "the frame never showed")
            waitForRendering(f.editor)
            return f
        }

        function test_the_border_shows_and_pictures_snap_to_it() {
            const f = halfSize()
            const p = f.project
            const overlay = findChild(f.editor, "transformOverlay")
            const border = findChild(f.editor, "canvasBorder")
            verify(border.visible)
            const r = overlay.canvasRect
            fuzzyCompare(border.x, r.x, 0.5)
            fuzzyCompare(border.width, r.width, 0.5)
            // A thin line along each edge.
            fuzzyCompare(findChild(border, "canvasBorder:top").width, r.width, 0.5)
            compare(findChild(border, "canvasBorder:top").height, 1)
            fuzzyCompare(findChild(border, "canvasBorder:left").height, r.height, 0.5)
            fuzzyCompare(findChild(border, "canvasBorder:right").x, r.width - 0.5, 0.01)
            // Moved to 3 points short of the left edge: it snaps onto it, the edge felt once.
            const body = findChild(f.editor, "transformBody")
            const c = overlay.frame.centre
            const left = overlay.boundsOf(overlay.frame).left
            const bumps = overlay.bumps
            drag(body, c.x, c.y, c.x - (left - r.x) + 3, c.y)
            tryVerify(() => Math.abs(p.selectedClipInfo.visual.x + 0.25) < 1e-6, 2000, "x " + p.selectedClipInfo.visual.x)
            compare(overlay.bumps, bumps + 1)
            fuzzyCompare(overlay.boundsOf(overlay.frame).left, r.x, 0.01)
            p.undo()

            // Scaled up to 3 points short of the canvas's sides: it snaps to fill the width.
            tryVerify(() => overlay.frame !== null && Math.abs(overlay.frame.centre.x - (r.x + r.width / 2)) < 0.5)
            const from = overlay.frame.corners[2]
            const centre = overlay.frame.centre
            const k = (r.width / 2 - 3) / (from.x - centre.x)
            drag(overlay, from.x, from.y, centre.x + (from.x - centre.x) * k, centre.y + (from.y - centre.y) * k)
            tryVerify(() => Math.abs(p.selectedClipInfo.visual.scale - 1) < 1e-6, 2000, "scale " + p.selectedClipInfo.visual.scale)
            verify(overlay.bumps >= bumps + 3) // the left and right edges

            // Off, it goes where it is put.
            p.undo()
            EditingPreferences.snapToBorder = false
            tryVerify(() => overlay.frame !== null && Math.abs(overlay.frame.centre.x - (r.x + r.width / 2)) < 0.5)
            const c2 = overlay.frame.centre
            const left2 = overlay.boundsOf(overlay.frame).left
            drag(body, c2.x, c2.y, c2.x - (left2 - r.x) + 3, c2.y)
            tryVerify(() => Math.abs(p.selectedClipInfo.visual.x + 0.25) > 0.001 && p.selectedClipInfo.visual.x < -0.2, 2000)
            // And the border can be hidden.
            EditingPreferences.canvasBorder = false
            verify(!border.visible)
        }

        function test_recording_the_entire_screen_a_window_or_an_area() {
            const f = createEditor()
            const p = f.project
            findChild(f.editor, "screenRecordButton").clicked()
            const panel = findChild(f.editor, "recordPanel")
            tryVerify(() => panel.opened)
            const choice = findChild(panel.contentItem, "screenChoice")
            compare(panel.mode, "display")
            compare(choice.count, 2) // no screen, the display
            compare(choice.currentText, "Test display (1280 × 720)")
            findChild(panel.contentItem, "startScreenRecording").clicked()
            verify(p.screenRecording)
            // The display, marked just inside its edges, and left out of the recording.
            tryVerify(() => panel.recordingFrame !== null && panel.recordingFrame.visible)
            const frame = panel.recordingFrame
            compare(frame.x, 0)
            compare(frame.width, 1280)
            verify(p.recordingExclusions().length >= 1)
            findChild(panel.contentItem, "stopScreenRecording").clicked()
            verify(!p.screenRecording)
            tryCompare(panel, "recordingFrame", null)

            // A window: the list is of windows; the frame goes round it.
            findChild(f.editor, "screenRecordButton").clicked()
            tryVerify(() => panel.opened)
            findChild(panel.contentItem, "recordMode:window").clicked()
            compare(panel.mode, "window")
            compare(choice.currentText, "Test app — Test window")
            findChild(panel.contentItem, "startScreenRecording").clicked()
            verify(p.screenRecording)
            tryVerify(() => panel.recordingFrame !== null)
            compare(panel.recordingFrame.x, 116)
            compare(panel.recordingFrame.y, 86)
            compare(panel.recordingFrame.width, 808)
            compare(panel.recordingFrame.height, 508)
            findChild(panel.contentItem, "stopScreenRecording").clicked()
            tryCompare(panel, "recordingFrame", null)

            // An area: chosen on the display (16:9, then dragged to size), then recorded.
            findChild(f.editor, "screenRecordButton").clicked()
            tryVerify(() => panel.opened)
            findChild(panel.contentItem, "recordMode:area").clicked()
            tryVerify(() => panel.areaPicker !== null && panel.areaPicker.visible)
            verify(!findChild(panel.contentItem, "startScreenRecording").enabled) // no area yet
            const picker = panel.areaPicker
            findChild(picker.contentItem, "areaShape:16x9").clicked()
            compare(picker.areaWidth, 1024)
            compare(picker.areaHeight, 576)
            compare(picker.areaX, 128)
            picker.setArea(100, 50, 640, 360)
            compare(findChild(picker.contentItem, "areaSize").children[0].text, "640 × 360")
            findChild(picker.contentItem, "acceptArea").clicked()
            tryCompare(panel, "areaPicker", null)
            compare(panel.area.x, 100)
            compare(panel.area.width, 640)
            verify(findChild(panel.contentItem, "chosenArea").text.indexOf("640 × 360") >= 0)
            findChild(panel.contentItem, "startScreenRecording").clicked()
            verify(p.screenRecording)
            tryVerify(() => panel.recordingFrame !== null)
            compare(panel.recordingFrame.x, 96)
            compare(panel.recordingFrame.y, 46)
            compare(panel.recordingFrame.width, 648)
            compare(panel.recordingFrame.height, 368)
            compare(panel.recordedArea.width, 640)
            tryVerify(() => p.screenRecordingTime > 0.5, 4000)
            findChild(panel.contentItem, "stopScreenRecording").clicked()
            tryCompare(panel, "recordingFrame", null)
            verify(p.tracks.some((t) => t.name === "Screen"))

            // Cancelling a new choice keeps the area chosen before.
            findChild(f.editor, "screenRecordButton").clicked()
            tryVerify(() => panel.opened)
            findChild(panel.contentItem, "chooseArea").clicked()
            tryVerify(() => panel.areaPicker !== null)
            compare(panel.areaPicker.areaX, 100) // starts from it
            findChild(panel.areaPicker.contentItem, "cancelArea").clicked()
            tryCompare(panel, "areaPicker", null)
            compare(panel.area.width, 640)
            panel.close()
        }
    }
}
