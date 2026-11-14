import QtQuick
import QtTest
import TechieVideoEditor.Theme
import TechieVideoEditor.Player
import TechieVideoEditor.Shell

// More tracks than the timeline shows: headers and lanes scroll up and down together (wheel,
// trackpad, the scroll bar), the ruler stays; a new track is scrolled into view; clicks and
// drops land on the track under the pointer, scrolled or not.
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
            ProjectController { id: controller; view: screen.player; autosaveEnabled: false }
            EditorScreen { id: screen; anchors.fill: parent; project: controller }
        }
    }

    TestCase {
        id: testCase
        name: "TimelineScroll"
        when: windowShown

        function init() { EditingPreferences.reset() }

        function createEditor() {
            const fixture = createTemporaryObject(editorComponent, root)
            verify(fixture)
            waitForRendering(fixture)
            return fixture
        }
        // Eight more video tracks and four more audio tracks than a new project has, and a
        // still on the bottom audio-less video track and the top one.
        function crowded() {
            const f = createEditor()
            const p = f.project
            for (let i = 0; i < 8; ++i)
                verify(p.addTrack("video", -1, true) > 0)
            for (let i = 0; i < 4; ++i) // (at the bottom, as the menus put them)
                verify(p.addTrack("audio", p.tracks[p.tracks.length - 1].id, false) > 0)
            compare(p.tracks.length, 15)
            compare(p.importMedia(["file://" + stillsDir + "/disc.png"]), 1)
            verify(p.addClip(p.media[0].id, p.tracks[0].id, 0), p.lastError)
            const bottomVideo = p.tracks.filter((t) => t.kind === "video").pop()
            verify(p.addClip(p.media[0].id, bottomVideo.id, 0))
            waitForRendering(f.editor)
            return f
        }
        function header(f, track) { return findChild(f.editor, "trackHeader:" + track.name) }
        function yIn(item, other) { return item.mapToItem(root, 0, 0).y - other.mapToItem(root, 0, 0).y }
        // Whether an item shows whole inside the lanes' viewport (below the ruler).
        function inView(timeline, item) {
            const area = findChild(timeline, "trackSpace").parent
            const top = item.mapToItem(area, 0, 0).y
            return top >= -0.5 && top + item.height <= area.height + 0.5
        }

        function test_more_tracks_than_fit_scroll() {
            const f = crowded()
            const p = f.project
            const timeline = findChild(f.editor, "timelinePanel")
            verify(timeline.tracksHeight > 0)
            verify(timeline.trackScrollMax > 0, "the tracks fit: " + timeline.tracksHeight)
            compare(timeline.trackScroll, 0)
            const bar = findChild(f.editor, "trackScrollBar")
            verify(bar.visible)
            verify(bar.size < 1)

            // The bottom track starts below the fold; its header and lane line up.
            const last = p.tracks[p.tracks.length - 1]
            verify(!inView(timeline, header(f, last)))

            // The wheel over the lanes scrolls the tracks; the ruler stays.
            const lanes = findChild(f.editor, "timelineLanes")
            const ruler = findChild(f.editor, "ruler")
            const rulerY = ruler.mapToItem(root, 0, 0).y
            const contentX = lanes.contentX
            for (let i = 0; i < 30; ++i)
                mouseWheel(lanes, lanes.width / 2, lanes.height / 2, 0, -120)
            tryCompare(timeline, "trackScroll", timeline.trackScrollMax)
            compare(lanes.contentX, contentX) // not along the time
            compare(ruler.mapToItem(root, 0, 0).y, rulerY)
            verify(inView(timeline, header(f, last)), "the bottom track never came into view")
            // Each header beside its own lane: the bottom video track's clip level with its header.
            const bottomVideo = p.tracks.filter((t) => t.kind === "video").pop()
            const clip = findChild(f.editor, "clip:" + bottomVideo.clips[0].id)
            verify(clip)
            fuzzyCompare(yIn(clip.parent, root) , yIn(header(f, bottomVideo), root), 1)
            compare(bar.position, timeline.trackScroll / timeline.tracksHeight)

            // Shift + wheel goes along the time instead.
            verify(p.importAndAppend("file://" + samplesDir + "/test_av_1080p30.mp4"))
            timeline.pixelsPerSecond = 400
            waitForRendering(f.editor)
            const scrolled = timeline.trackScroll
            mouseWheel(lanes, lanes.width / 2, lanes.height / 2, 0, -120, Qt.NoButton, Qt.ShiftModifier)
            tryVerify(() => lanes.contentX > contentX)
            compare(timeline.trackScroll, scrolled)

            // The wheel over the headers scrolls too, back up to the top.
            const headers = findChild(f.editor, "trackHeaders")
            for (let i = 0; i < 30; ++i)
                mouseWheel(headers, headers.width / 2, headers.height / 2, 0, 120)
            tryCompare(timeline, "trackScroll", 0)
            verify(inView(timeline, header(f, p.tracks[0])))
            timeline.trackScroll = 1e6 // (kept within the tracks)
            timeline.scrollTracks(0)
            verify(timeline.trackScroll <= timeline.trackScrollMax)
        }

        function test_a_new_track_is_scrolled_into_view() {
            const f = crowded()
            const p = f.project
            const timeline = findChild(f.editor, "timelinePanel")
            compare(timeline.trackScroll, 0)
            // An audio track at the very bottom, from the lane menu.
            const menus = findChild(f.editor, "timelineMenus")
            const id = menus.addTrack("audio", null)
            verify(id > 0, p.lastError + " / " + id)
            const added = p.tracks.find((t) => t.id === id)
            tryVerify(() => timeline.trackScroll > 0, 3000, "nothing scrolled")
            waitForRendering(f.editor)
            verify(inView(timeline, header(f, added)), "the new track is out of view")
            compare(timeline.trackScroll, timeline.trackScrollMax) // (the bottom one)
        }

        function test_clicks_and_drags_land_on_the_track_under_the_pointer_when_scrolled() {
            const f = crowded()
            const p = f.project
            const timeline = findChild(f.editor, "timelinePanel")
            // Drag the top track's clip down to the bottom video track, scrolled to the bottom.
            timeline.trackScroll = 0
            waitForRendering(f.editor)
            const top = p.tracks[0]
            const clipItem = findChild(f.editor, "clip:" + top.clips[0].id)
            const videos = p.tracks.filter((t) => t.kind === "video")
            const bottomVideo = videos[videos.length - 2] // (empty, below the fold)
            compare(bottomVideo.clips.length, 0)
            // Scrolled so both show: the target lane's middle in the viewport.
            const area = findChild(f.editor, "trackSpace").parent
            const target = header(f, bottomVideo)
            const from = clipItem.mapToItem(root, clipItem.width / 2, clipItem.height / 2)
            mousePress(root, from.x, from.y)
            // Move a little first, then scroll the tracks under the drag and drop on the target.
            mouseMove(root, from.x, from.y + 10)
            timeline.showTrack(bottomVideo.id)
            waitForRendering(f.editor)
            verify(inView(timeline, target))
            const to = target.mapToItem(root, 0, target.height / 2)
            mouseMove(root, from.x, (from.y + to.y) / 2)
            mouseMove(root, from.x, to.y)
            mouseRelease(root, from.x, to.y)
            tryVerify(() => p.tracks.find((t) => t.id === bottomVideo.id).clips.length === 1, 3000,
                      "the clip did not land on the track under the pointer")

            // Right-click on a scrolled lane: the menu is that lane's track's.
            timeline.trackScroll = timeline.trackScrollMax
            waitForRendering(f.editor)
            const last = p.tracks[p.tracks.length - 1]
            const lastHeader = header(f, last)
            verify(inView(timeline, lastHeader))
            const space = findChild(f.editor, "laneSpace")
            const at = lastHeader.mapToItem(space, 0, lastHeader.height / 2)
            mouseClick(space, 300, at.y, Qt.RightButton)
            const laneMenu = findChild(f.editor, "laneMenu")
            tryVerify(() => laneMenu.opened)
            compare(laneMenu.track.id, last.id)
            laneMenu.close()
        }
    }
}
