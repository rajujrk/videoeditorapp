import QtQuick
import QtTest
import TechieVideoEditor.Theme
import TechieVideoEditor.Player
import TechieVideoEditor.Shell

// Right-click menus (clips, track headers, empty lanes, media), adding and deleting tracks,
// the clipboard and its shortcuts, and the home screen's quick actions, through the real
// editor.
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
            ProjectController { id: controller; view: screen.player }
            EditorScreen { id: screen; anchors.fill: parent; project: controller }
        }
    }
    Component {
        id: homeComponent
        HomeScreen { anchors.fill: parent; project: ProjectController {} }
    }

    TestCase {
        id: testCase
        name: "Menus"
        when: windowShown

        readonly property string av: "file://" + samplesDir + "/test_av_1080p30.mp4"

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
        function settled(parent, name) {
            let item = null
            tryVerify(() => {
                item = findChild(parent, name)
                return item !== null && item.visible && item.width > 0
            }, 3000, name + " never showed")
            waitForPolish(item, 1000)
            return item
        }
        function rightClick(parent, name) {
            const item = settled(parent, name)
            mouseClick(item, item.width / 2, item.height / 2, Qt.RightButton)
        }
        // A menu entry by name, in a menu or one of its submenus (their items are made by
        // the menu, outside the object tree findChild searches).
        function entry(f, name) {
            const direct = findChild(f.editor, name)
            if (direct)
                return direct
            for (const menuName of ["clipMenu:speed"]) {
                const menu = findChild(f.editor, menuName)
                for (let i = 0; menu && i < menu.count; ++i)
                    if (menu.itemAt(i) && menu.itemAt(i).objectName === name)
                        return menu.itemAt(i)
            }
            return null
        }
        function trigger(f, name) {
            const entry = testCase.entry(f, name)
            verify(entry, name)
            verify(entry.enabled, name + " is disabled")
            entry.triggered()
        }

        function test_home_quick_actions_are_on_and_say_what_they_do() {
            const home = createTemporaryObject(homeComponent, root)
            waitForRendering(home)
            const record = findChild(home, "homeRecord")
            const instant = findChild(home, "homeInstant")
            verify(record.enabled)
            verify(instant.enabled)
            verify(record.tip.indexOf("Phase") < 0)
            const recordSpy = createTemporaryObject(signalSpy, root, { target: home, signalName: "recordRequested" })
            const instantSpy = createTemporaryObject(signalSpy, root, { target: home, signalName: "instantRequested" })
            // (Pressed through their clicked signal: a click can land on a popup another test
            // file left open when the whole suite runs.)
            record.clicked()
            instant.clicked()
            tryCompare(recordSpy, "count", 1)
            tryCompare(instantSpy, "count", 1)

            // In the editor they open the recorder, and the instant-video panel (with an
            // Import button while there is no media).
            const f = createEditor()
            f.editor.openRecorder()
            tryVerify(() => findChild(f.editor, "recordPanel").opened)
            findChild(f.editor, "recordPanel").close()
            f.editor.openInstantMode()
            tryVerify(() => findChild(f.editor, "autoEditPanel") !== null && findChild(f.editor, "autoEditPanel").opened)
            compare(findChild(f.editor, "autoEditPanel").mode, "instant")
            verify(findChild(f.editor, "autoEditImport").visible)
        }
        Component { id: signalSpy; SignalSpy {} }

        function test_track_menu_adds_renames_and_deletes_tracks() {
            const f = createEditor()
            const p = f.project
            verify(p.importAndAppend(av))
            const before = p.tracks.length
            rightClick(f.editor, "trackHeader:Video 1")
            tryVerify(() => findChild(f.editor, "trackMenu").opened)
            trigger(f, "trackMenu:addVideo")
            compare(p.tracks.length, before + 1)
            compare(p.undoText, "Add Track")
            // A video track goes above the one clicked; an audio one at the bottom.
            const main = p.tracks.findIndex((t) => t.main)
            compare(p.tracks[main - 1].kind, "video")
            trigger(f, "trackMenu:addAudio")
            compare(p.tracks[p.tracks.length - 1].kind, "audio")

            // Renamed through the box.
            const added = p.tracks[main - 1]
            rightClick(f.editor, "trackHeader:" + added.name)
            trigger(f, "trackMenu:rename")
            const box = findChild(f.editor, "renameTrack")
            tryVerify(() => box.opened)
            findChild(f.editor, "renameTrackField").text = "B-roll"
            findChild(f.editor, "renameTrackAccept").clicked()
            tryVerify(() => p.tracks.some((t) => t.name === "B-roll"))

            // Deleting a track with clips asks first; an empty one just goes.
            verify(p.addClip(p.media[0].id, p.tracks.find((t) => t.name === "B-roll").id, 0))
            rightClick(f.editor, "trackHeader:B-roll")
            trigger(f, "trackMenu:delete")
            const confirm = findChild(f.editor, "timelineConfirm")
            tryVerify(() => confirm.opened)
            verify(p.tracks.some((t) => t.name === "B-roll"))
            findChild(confirm.contentItem, "confirmAccept").clicked()
            tryVerify(() => !p.tracks.some((t) => t.name === "B-roll"))
            compare(p.undoText, "Remove Track")
            // An empty track just goes; then the main track cannot: it is the last video track.
            rightClick(f.editor, "trackHeader:Video 2")
            trigger(f, "trackMenu:delete")
            verify(!p.tracks.some((t) => t.name === "Video 2"))
            rightClick(f.editor, "trackHeader:Video 1")
            verify(!findChild(f.editor, "trackMenu:delete").enabled)
            findChild(f.editor, "trackMenu").close()
            compare(p.removeEmptyTracks(), 2) // Audio 1 and the audio track added above
        }

        function test_clip_menu_edits_the_clip() {
            const f = createEditor()
            const p = f.project
            verify(p.importAndAppend(av))
            const clip = mainTrack(p).clips[0]
            rightClick(f.editor, "clip:" + clip.id)
            tryVerify(() => findChild(f.editor, "clipMenu").opened)
            compare(p.selectedClip, clip.id) // right-clicking selects it

            // Detach audio: the sound goes to an audio track.
            verify(findChild(f.editor, "clipMenu:detachAudio").visible)
            trigger(f, "clipMenu:detachAudio")
            compare(p.undoText, "Detach Audio")
            verify(p.tracks.some((t) => t.kind === "audio" && t.clips.length === 1))

            // Speed from the menu: twice as fast, half as long.
            rightClick(f.editor, "clip:" + clip.id)
            trigger(f, "clipMenu:speed:2")
            fuzzyCompare(mainTrack(p).clips[0].duration, 1.0, 0.01)
            p.undo()

            // Copy, then paste at the playhead (at the timeline's end, 2 s); duplicate the
            // pasted clip right after itself; ripple-delete the pasted one: the gap closes.
            p.selectedClip = clip.id
            rightClick(f.editor, "clip:" + clip.id)
            trigger(f, "clipMenu:copy")
            verify(p.canPaste)
            p.seek(2)
            rightClick(f.editor, "clip:" + clip.id)
            trigger(f, "clipMenu:paste")
            compare(p.undoText, "Paste")
            compare(mainTrack(p).clips.length, 2)
            fuzzyCompare(mainTrack(p).clips[1].start, 2, 0.01)
            const pasted = mainTrack(p).clips[1].id
            compare(p.selectedClip, pasted) // the pasted clip is selected
            rightClick(f.editor, "clip:" + pasted)
            trigger(f, "clipMenu:duplicate")
            compare(mainTrack(p).clips.length, 3)
            fuzzyCompare(mainTrack(p).clips[2].start, 4, 0.01) // right after it
            rightClick(f.editor, "clip:" + pasted)
            trigger(f, "clipMenu:rippleDelete")
            compare(p.undoText, "Ripple Delete")
            compare(mainTrack(p).clips.length, 2)
            fuzzyCompare(mainTrack(p).clips[1].start, 2, 0.01) // the duplicate moved up

            // Split, and trim to the playhead, need the playhead in the clip.
            p.seek(1)
            rightClick(f.editor, "clip:" + clip.id)
            verify(findChild(f.editor, "clipMenu:trimEnd").enabled)
            trigger(f, "clipMenu:trimEnd")
            fuzzyCompare(mainTrack(p).clips[0].duration, 1.0, 0.01)
            p.seek(5)
            rightClick(f.editor, "clip:" + clip.id)
            verify(!findChild(f.editor, "clipMenu:split").enabled)
            findChild(f.editor, "clipMenu").close()
        }

        function test_lane_menu_and_clipboard_shortcuts() {
            const f = createEditor()
            const p = f.project
            verify(p.importAndAppend(av))
            const clip = mainTrack(p).clips[0]
            p.selectedClip = clip.id
            verify(p.copyClips())
            // Right-click empty space on the main track at 6 s: paste there.
            const lanes = settled(f.editor, "laneSpace")
            const header = settled(f.editor, "trackHeader:Video 1")
            const y = header.mapToItem(lanes, 0, header.height / 2).y
            mouseClick(lanes, 6 * 60 + 2, y, Qt.RightButton)
            tryVerify(() => findChild(f.editor, "laneMenu").opened)
            trigger(f, "laneMenu:paste")
            compare(mainTrack(p).clips.length, 2)
            fuzzyCompare(mainTrack(p).clips[1].start, 6, 0.05)
            trigger(f, "laneMenu:selectAll")
            compare(p.selectedClips.length, 2)

            // ⌘D duplicates, ⌘X cuts, ⌘V pastes (the actions the shortcuts run).
            p.selectedClip = clip.id
            verify(findChild(f.editor, "shortcut:duplicate").enabled)
            findChild(f.editor, "shortcut:duplicate").activated()
            compare(mainTrack(p).clips.length, 3)
            findChild(f.editor, "shortcut:cut").activated()
            compare(mainTrack(p).clips.length, 2)
            compare(p.undoText, "Cut")
            p.seek(10)
            findChild(f.editor, "shortcut:paste").activated()
            compare(mainTrack(p).clips.length, 3)
            findChild(f.editor, "shortcut:selectAll").activated()
            compare(p.selectedClips.length, 3)
        }

        function test_media_menu_places_and_removes_media() {
            const f = createEditor()
            const p = f.project
            compare(p.importMedia([av]), 1)
            const media = p.media[0]
            verify(p.addClip(media.id)) // 0–2 s on the main track
            // At the playhead (1 s) the main track is taken: the track above has room.
            rightClick(f.editor, "media:" + media.name)
            tryVerify(() => findChild(f.editor, "mediaMenu").opened)
            p.seek(1)
            trigger(f, "mediaMenu:atPlayhead")
            const main = p.tracks.findIndex((t) => t.main)
            compare(p.tracks[main - 1].clips.length, 1)
            fuzzyCompare(p.tracks[main - 1].clips[0].start, 1, 0.01)
            compare(p.undoText, "Add Clip")
            // Again there: no track has room, so a new video track on top.
            const before = p.tracks.length
            verify(p.addMediaAt(media.id, 1) > 0)
            compare(p.tracks.length, before + 1)
            compare(p.tracks[0].clips.length, 1)

            // Removing media the timeline uses asks first, then takes its clips.
            rightClick(f.editor, "media:" + media.name)
            trigger(f, "mediaMenu:remove")
            const confirm = findChild(f.editor, "mediaConfirm")
            tryVerify(() => confirm.opened)
            verify(confirm.message.indexOf("3") >= 0)
            findChild(confirm.contentItem, "confirmAccept").clicked()
            compare(p.media.length, 0)
            compare(mainTrack(p).clips.length, 0)
            p.undo()
            compare(p.media.length, 1)
            compare(mainTrack(p).clips.length, 1)
        }
    }
}
