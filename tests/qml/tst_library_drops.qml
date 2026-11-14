import QtQuick
import QtTest
import TechieVideoEditor.Theme
import TechieVideoEditor.Player
import TechieVideoEditor.Shell

// Effects, filters, the adjustment layer, stickers and sounds dragged from the library onto the
// timeline: onto the clip under the pointer, or at the track and time there; and a new or
// opened project leaving nothing of the last one in the player.
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
        name: "LibraryDrops"
        when: windowShown

        function createEditor() {
            const fixture = createTemporaryObject(editorComponent, root)
            verify(fixture)
            waitForRendering(fixture)
            return fixture
        }
        function mainTrack(p) { return p.tracks.find((t) => t.main) }
        function trackNamed(p, name) { return p.tracks.find((t) => t.name === name) }
        // A 5-second picture on the main track (0–5 s), and the timeline.
        function withClip() {
            const f = createEditor()
            const p = f.project
            compare(p.importMedia(["file://" + stillsDir + "/halves.png"]), 1)
            verify(p.addClip(p.media[0].id, mainTrack(p).id, 0))
            return f
        }
        // The y of a track's middle in the timeline's track space.
        function yOf(timeline, track) {
            let top = 0
            for (const t of timeline.tracks) {
                if (t.id === track.id)
                    return top + timeline.trackHeight(t.kind) / 2
                top += timeline.trackHeight(t.kind)
            }
            return -1
        }
        function xOf(timeline, seconds) { return seconds * timeline.pixelsPerSecond }
        // A library tab opened, and a tile's delegate (what the drag carries) by its button's name.
        function tile(f, tab, name) {
            findChild(f.editor, "libraryTab:" + tab).clicked()
            let button = null
            tryVerify(() => (button = findChild(f.editor, name)) !== null, 3000, name)
            return button.parent
        }

        function test_the_tiles_carry_what_they_are() {
            const f = createEditor()
            const p = f.project
            const effect = tile(f, "Effects", "libraryEffect:tve.color.black_white")
            compare(effect.Drag.keys, ["application/x-tve-effect"])
            compare(effect.Drag.mimeData["application/x-tve-effect"], "tve.color.black_white")
            compare(effect.libraryItem, "tve.color.black_white")
            const lookId = p.looks[0].id
            const look = tile(f, "Filters", "libraryLook:" + lookId)
            compare(look.Drag.keys, ["application/x-tve-look"])
            compare(look.libraryItem, lookId)
            const adjustment = tile(f, "Filters", "libraryAdjustmentLayer")
            compare(adjustment.libraryItem, "")
            const sticker = tile(f, "Stickers", "librarySticker:heart_bounce")
            compare(sticker.Drag.keys, ["application/x-tve-sticker"])
            compare(sticker.libraryItem, "heart_bounce")
            findChild(f.editor, "libraryTab:Audio").clicked()
            let sound = null
            tryVerify(() => (sound = findChild(f.editor, "sound:tve.music.pulse")) !== null, 3000)
            compare(sound.Drag.keys, ["application/x-tve-sound"])
            compare(sound.libraryItem, "tve.music.pulse")
            // A click on a tile still does what it did.
            p.selectedClip = 0
            compare(p.importMedia(["file://" + stillsDir + "/halves.png"]), 1)
            verify(p.addClip(p.media[0].id, mainTrack(p).id, 0))
            p.selectedClip = mainTrack(p).clips[0].id
            tile(f, "Effects", "libraryEffect:tve.color.black_white")
            // (scrolled to it first)
            const grid = findChild(f.editor, "effectsGrid")
            grid.positionViewAtIndex(grid.model.findIndex((e) => e.id === "tve.color.black_white"), GridView.Contain)
            waitForRendering(grid)
            mouseClick(findChild(f.editor, "libraryEffect:tve.color.black_white"))
            tryVerify(() => p.selectedClipInfo.effects.some((e) => e.effectId === "tve.color.black_white"))
        }

        function test_effects_and_filters_go_on_the_clip_they_are_dropped_on() {
            const f = withClip()
            const p = f.project
            const timeline = findChild(f.editor, "timelinePanel")
            const main = mainTrack(p)
            const clipId = main.clips[0].id
            p.selectedClip = 0
            verify(timeline.goesOnClip("application/x-tve-effect", "x"))
            verify(!timeline.goesOnClip("application/x-tve-look", "")) // the adjustment layer: at a time
            compare(timeline.clipAt(main, 2).id, clipId)
            compare(timeline.clipAt(main, 7), null)

            // An effect on the clip: there, and the clip selected.
            verify(timeline.dropItem("application/x-tve-effect", "tve.color.black_white", xOf(timeline, 2), yOf(timeline, main)))
            compare(p.selectedClip, clipId)
            verify(p.selectedClipInfo.effects.some((e) => e.effectId === "tve.color.black_white"))
            compare(p.undoText, "Add Effect")
            // Dropped where there is no clip: nothing, and why.
            verify(!timeline.dropItem("application/x-tve-effect", "tve.color.black_white", xOf(timeline, 9), yOf(timeline, main)))
            compare(p.lastError, "Drop the effect on a clip")
            verify(!timeline.dropItem("application/x-tve-effect", "tve.color.black_white", xOf(timeline, 2), -50))

            // A filter (look) on the clip.
            const lookId = p.looks[0].id
            p.selectedClip = 0
            verify(timeline.dropItem("application/x-tve-look", lookId, xOf(timeline, 1), yOf(timeline, main)))
            compare(p.selectedClip, clipId)
            compare(p.selectedClipInfo.look.id, lookId)
            verify(!timeline.dropItem("application/x-tve-look", lookId, xOf(timeline, 9), yOf(timeline, main)))
            compare(p.lastError, "Drop the filter on a clip")

            // The adjustment layer: placed at the time dropped, above the main track.
            timeline.pixelsPerSecond = 60
            const above = trackNamed(p, "Video 2")
            verify(timeline.dropItem("application/x-tve-look", "", xOf(timeline, 1.5), yOf(timeline, above)))
            const layer = p.tracks.map((t) => t.clips).reduce((a, b) => a.concat(b), []).find((c) => c.isAdjustment)
            verify(layer)
            fuzzyCompare(layer.start, 1.5, 0.05)
        }

        function test_stickers_and_sounds_land_where_they_are_dropped() {
            const f = withClip()
            const p = f.project
            const timeline = findChild(f.editor, "timelinePanel")
            timeline.pixelsPerSecond = 60
            snapOff(f)
            // A sticker on Video 2 at 3 s.
            const video2 = trackNamed(p, "Video 2")
            verify(timeline.dropItem("application/x-tve-sticker", "heart_bounce", xOf(timeline, 3), yOf(timeline, video2)))
            compare(p.undoText, "Add Sticker")
            let placed = trackNamed(p, "Video 2").clips
            compare(placed.length, 1)
            fuzzyCompare(placed[0].start, 3, 0.05)
            // Dropped on a sound track: at that time on a picture track instead.
            const audio = trackNamed(p, "Audio 1")
            verify(timeline.dropItem("application/x-tve-sticker", "heart_bounce", xOf(timeline, 3), yOf(timeline, audio)))
            const stickers = p.tracks.filter((t) => t.kind === "video").map((t) => t.clips).reduce((a, b) => a.concat(b), [])
                                     .filter((c) => Math.abs(c.start - 3) < 0.05)
            compare(stickers.length, 2)
            compare(trackNamed(p, "Audio 1").clips.length, 0)

            // A sound on Audio 1 at 2 s; then on the main track: on a sound track at that time.
            verify(timeline.dropItem("application/x-tve-sound", "tve.music.pulse", xOf(timeline, 2), yOf(timeline, audio)))
            compare(p.undoText, "Add Music")
            placed = trackNamed(p, "Audio 1").clips
            compare(placed.length, 1)
            fuzzyCompare(placed[0].start, 2, 0.05)
            verify(timeline.dropItem("application/x-tve-sound", "tve.music.still_water", xOf(timeline, 4), yOf(timeline, mainTrack(p))))
            const sounds = p.tracks.filter((t) => t.kind === "audio").map((t) => t.clips).reduce((a, b) => a.concat(b), [])
            compare(sounds.length, 2)
            verify(sounds.some((c) => Math.abs(c.start - 4) < 0.05))
            compare(mainTrack(p).clips.length, 1)
        }
        function snapOff(f) {
            const timeline = findChild(f.editor, "timelinePanel")
            // (the snapping toggle in the timeline's toolbar)
            for (const child of findChildrenWithIcon(timeline, "snap"))
                child.checked = false
        }
        function findChildrenWithIcon(item, icon) {
            let found = []
            for (const child of item.children) {
                if (child.iconName === icon)
                    found.push(child)
                found = found.concat(findChildrenWithIcon(child, icon))
            }
            return found
        }

        function test_a_new_project_leaves_nothing_of_the_last_one_in_the_player() {
            const f = withClip()
            const p = f.project
            const player = findChild(f.editor, "player")
            const centre = () => {
                const image = grabImage(root)
                const s = image.width / root.width
                const at = player.mapToItem(root, player.width / 2, player.height * 0.2)
                return image.pixel(Math.round(at.x * s), Math.round(at.y * s))
            }
            p.seek(1)
            tryVerify(() => { const c = centre(); return c.r > 0.5 || c.b > 0.5 }, 5000, "the picture never showed")
            // The media open in the source monitor too, and comparing.
            const monitor = findChild(f.editor, "sourceMonitor")
            let panel = monitor
            while (panel && panel.showingSource === undefined)
                panel = panel.parent
            panel.openSource(p.media[0])
            verify(panel.showingSource)
            verify(monitor.media !== null)
            panel.showingSource = false
            player.compareGrade = true

            p.newProject(1920, 1080)
            compare(p.duration, 0)
            tryVerify(() => { const c = centre(); return c.r < 0.05 && c.g < 0.05 && c.b < 0.05 }, 5000,
                      "the last project's picture stayed in the player")
            compare(monitor.media, null)
            verify(!panel.showingSource)
            verify(!findChild(f.editor, "playerTabs").visible)
            verify(!player.compareGrade)
        }
    }
}
