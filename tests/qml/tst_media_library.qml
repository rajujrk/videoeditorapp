import QtQuick
import QtTest
import TechieVideoEditor.Theme
import TechieVideoEditor.Player
import TechieVideoEditor.Shell

// The media library and the project kept in order (slice 31b) through the real editor:
// folders, sorting, grouping, list and sizes, colour labels, removing unused media, importing
// a folder, the canvas and frame rate changed, archiving with the media, export presets of
// your own, and a clip's sound channels.
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
        name: "MediaLibrary"
        when: windowShown

        readonly property string av: samplesDir + "/test_av_1080p30.mp4"
        readonly property string tone: samplesDir + "/test_tone_440.wav"

        function initTestCase() {
            const probe = Qt.createQmlObject('import TechieVideoEditor.Player; ProjectController {}', testCase)
            if (probe.importMedia(["file://" + av]) !== 1)
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
        function settled(parent, name) {
            let item = null
            tryVerify(() => {
                item = findChild(parent, name)
                return item !== null && item.visible && item.width > 0
            }, 3000, name + " never showed")
            waitForPolish(item, 1000)
            return item
        }
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
        }
        // Three pieces of media: a video, a photo and a sound.
        function threeMedia() {
            const f = createEditor()
            const p = f.project
            compare(p.importMedia(["file://" + av, "file://" + stillsDir + "/halves.png", "file://" + tone]), 3)
            return f
        }
        function names(list) { return list.filter((e) => e.entryKind === undefined).map((m) => m.name) }

        function test_folders_hold_media() {
            const f = threeMedia()
            const p = f.project
            const panel = findLibrary(f)
            // A new folder, named in the box.
            settled(f.editor, "newBin").clicked()
            const box = findChild(f.editor, "binNameBox")
            tryVerify(() => box.opened)
            findChild(box.contentItem, "binNameField").text = "Shoot"
            findChild(box.contentItem, "binNameAccept").clicked()
            tryVerify(() => !box.visible)
            compare(p.undoText, "New Folder")
            compare(p.bins.length, 1)
            const shoot = p.bins[0].id
            // Shown as a tile before the media.
            tryCompare(panel.binEntries[0], "name", "Shoot")
            compare(panel.binEntries[0].entryKind, "bin")
            verify(settled(f.editor, "binTile:Shoot"))
            // The video moved in from its menu.
            const video = p.media.find((m) => m.hasVideo && !m.still)
            panel.openMediaMenu(video, f.editor, 10, 10)
            trigger(f, "moveTo:" + shoot, "mediaMenu:move")
            findChild(f.editor, "mediaMenu").dismiss()
            compare(p.undoText, "Move to Folder")
            compare(p.media.find((m) => m.id === video.id).binId, shoot)
            compare(names(panel.binEntries).length, 2) // the others stay at the top
            // Opened: only what is in it, and the trail back.
            panel.currentBin = shoot
            compare(names(panel.binEntries), [video.name])
            verify(settled(f.editor, "binCrumb:" + shoot))
            settled(f.editor, "binCrumb:0").clicked()
            compare(panel.currentBin, 0)
            // A folder inside, renamed, then the outer one deleted: what was in it moves up.
            const day = p.addBin("Day 2", shoot)
            verify(day > 0)
            verify(p.renameBin(day, "Day 1"))
            compare(p.bins.find((b) => b.id === day).path, "Shoot › Day 1")
            verify(!p.moveToBin([], day, [shoot])) // not inside itself
            panel.openBinMenu(panel.binEntries[0], f.editor, 10, 10)
            trigger(f, "binMenu:delete")
            compare(p.undoText, "Delete Folder")
            compare(p.bins.length, 1)
            compare(p.media.find((m) => m.id === video.id).binId, 0)
            p.undo()
            compare(p.bins.length, 2)
            // Searching looks in every folder.
            panel.currentBin = 0
            panel.searchText = "test_av"
            tryCompare(findChild(f.editor, "mediaGrid"), "count", 1)
            panel.searchText = ""
        }
        function findLibrary(f) {
            let item = findChild(f.editor, "mediaGrid")
            while (item && item.binEntries === undefined)
                item = item.parent
            verify(item, "the library panel")
            return item
        }

        function test_sorting_grouping_list_and_sizes() {
            const f = threeMedia()
            const p = f.project
            const panel = findLibrary(f)
            const grid = findChild(f.editor, "mediaGrid")
            // By when imported, then by name, backwards, by length.
            compare(names(panel.binEntries), ["test_av_1080p30.mp4", "halves.png", "test_tone_440.wav"])
            findChild(f.editor, "binView").clicked()
            trigger(f, "binSort:name")
            findChild(f.editor, "binViewMenu").dismiss()
            compare(EditingPreferences.mediaSort, "name")
            compare(names(panel.binEntries), ["halves.png", "test_av_1080p30.mp4", "test_tone_440.wav"])
            EditingPreferences.mediaDescending = true
            compare(names(panel.binEntries), ["test_tone_440.wav", "test_av_1080p30.mp4", "halves.png"])
            EditingPreferences.mediaDescending = false
            // Grouped by kind: a heading over each, each group starting its own row.
            EditingPreferences.mediaGroups = true
            const kinds = panel.binEntries.filter((e) => e.entryKind === "header").map((e) => e.kind)
            compare(kinds, ["video", "photo", "audio"])
            compare(panel.binEntries.length % 2, 1) // header + gap + item per group, the last unpadded
            compare(panel.binEntries[1].entryKind, "gap")
            tryVerify(() => findChild(f.editor, "header:video") !== null)
            // Sizes: columns and heights.
            EditingPreferences.mediaGroups = false
            EditingPreferences.mediaSize = "small"
            compare(panel.mediaColumns, 3)
            compare(grid.cellHeight, 96)
            EditingPreferences.mediaSize = "large"
            compare(panel.mediaColumns, 1)
            verify(grid.cellHeight > 200)
            // A list: a row each, the grid gone.
            EditingPreferences.mediaView = "list"
            verify(!grid.visible)
            const list = findChild(f.editor, "mediaList")
            tryVerify(() => list.visible && list.count === 3)
            verify(settled(f.editor, "mediaRow:halves.png"))
            EditingPreferences.mediaGroups = true
            tryCompare(list, "count", 6) // three headings, no gaps
        }

        function test_labels_and_unused_media() {
            const f = threeMedia()
            const p = f.project
            const panel = findLibrary(f)
            const photo = p.media.find((m) => m.still)
            panel.openMediaMenu(photo, f.editor, 10, 10)
            trigger(f, "mediaLabel:green", "mediaMenu:label")
            findChild(f.editor, "mediaMenu").dismiss()
            compare(p.undoText, "Label Media")
            compare(p.media.find((m) => m.id === photo.id).label, "green")
            tryVerify(() => findChild(f.editor, "mediaLabel:halves.png").visible)
            // Showing that label only.
            panel.labelFilter = "green"
            compare(names(panel.binEntries), ["halves.png"])
            verify(settled(f.editor, "labelFilterChip"))
            findChild(f.editor, "labelFilterChip").clicked()
            compare(panel.labelFilter, "")

            // The video on the timeline; the photo and sound unused, removed after asking.
            verify(p.addClip(p.media[0].id))
            compare(p.unusedMedia().length, 2)
            findChild(f.editor, "binView").clicked()
            trigger(f, "removeUnused")
            findChild(f.editor, "binViewMenu").dismiss()
            const confirm = findChild(f.editor, "mediaConfirm")
            tryVerify(() => confirm.opened)
            findChild(confirm.contentItem, "confirmAccept").clicked()
            tryCompare(p.media, "length", 1)
            compare(p.undoText, "Remove Unused Media")
            compare(p.removeUnusedMedia(), 0)
            p.undo()
            compare(p.media.length, 3)
        }

        function test_importing_a_folder_keeps_its_folders() {
            const f = createEditor()
            const p = f.project
            const dir = scratchDir + "/import-folder/Trip"
            verify(testFiles.copy(av, dir + "/clip.mp4"))
            verify(testFiles.copy(stillsDir + "/halves.png", dir + "/Day 1/photo.png"))
            verify(testFiles.copy(tone, dir + "/Day 1/Sound/tone.wav"))
            verify(testFiles.write(dir + "/notes.txt", "not media"))
            compare(p.importFolder("file://" + dir), 3)
            compare(p.undoText, "Import Folder")
            const paths = p.bins.map((b) => b.path).sort()
            compare(paths, ["Trip", "Trip › Day 1", "Trip › Day 1 › Sound"])
            const binNamed = (path) => p.bins.find((b) => b.path === path).id
            compare(p.media.find((m) => m.name === "clip.mp4").binId, binNamed("Trip"))
            compare(p.media.find((m) => m.name === "tone.wav").binId, binNamed("Trip › Day 1 › Sound"))
            p.undo()
            compare(p.media.length, 0)
            compare(p.bins.length, 0)
            // Flat, into the folder open.
            const kept = p.addBin("Kept")
            compare(p.importFolder("file://" + dir, false, kept), 3)
            verify(p.media.every((m) => m.binId === kept))
            compare(p.bins.length, 1)
            verify(!p.importFolder("file://" + dir + "/nowhere"))
        }

        function test_the_canvas_and_frame_rate_change() {
            const f = createEditor()
            const p = f.project
            verify(p.importAndAppend("file://" + av))
            compare(p.canvasWidth, 1920)
            settled(f.editor, "canvasButton").clicked()
            const dialog = findChild(f.editor, "projectSettingsDialog")
            tryVerify(() => dialog.opened)
            const size = findChild(dialog.contentItem, "canvasSize")
            compare(size.currentText, "Landscape 16:9 · 1920 × 1080")
            verify(!findChild(dialog.contentItem, "applyCanvas").enabled) // nothing changed
            size.currentIndex = size.model.findIndex((s) => s.id === "vertical")
            findChild(dialog.contentItem, "canvasRate").currentIndex = 2 // 25 fps
            findChild(dialog.contentItem, "applyCanvas").clicked()
            tryVerify(() => !dialog.visible)
            compare(p.canvasWidth, 1080)
            compare(p.canvasHeight, 1920)
            fuzzyCompare(p.frameRate, 25, 1e-6)
            compare(p.undoText, "Change Canvas")
            fuzzyCompare(p.duration, 2, 0.04) // the clip keeps its time
            // A custom size and NTSC rates.
            verify(p.setCanvas(1000, 500, 29.97))
            fuzzyCompare(p.frameRate, 29.97, 0.001)
            verify(!p.setCanvas(8, 500, 30))
            verify(!p.setCanvas(1000, 500, 0))
            p.undo()
            p.undo()
            compare(p.canvasWidth, 1920)
        }

        function test_archiving_copies_the_project_and_its_media() {
            const f = createEditor()
            const p = f.project
            const dir = scratchDir + "/archive-source"
            verify(testFiles.copy(av, dir + "/clip.mp4"))
            verify(testFiles.copy(stillsDir + "/halves.png", dir + "/unused.png"))
            compare(p.importMedia(["file://" + dir + "/clip.mp4", "file://" + dir + "/unused.png"]), 2)
            verify(p.addClip(p.media[0].id))
            verify(p.save("file://" + scratchDir + "/archive-source/Trip.tve"))
            const out = scratchDir + "/archives"
            verify(testFiles.write(out + "/.keep", ""))
            const spy = createTemporaryObject(spyComponent, testCase, { target: p, signalName: "archiveFinished" })
            settled(f.editor, "canvasButton").clicked()
            const dialog = findChild(f.editor, "projectSettingsDialog")
            tryVerify(() => dialog.opened)
            findChild(dialog.contentItem, "archiveUsedOnly").checked = true
            verify(dialog.archiveInto("file://" + out))
            tryCompare(spy, "count", 1, 10000)
            compare(spy.signalArguments[0][0], true)
            const archived = spy.signalArguments[0][1]
            compare(archived, out + "/Trip Archive/Trip.tve")
            verify(testFiles.exists(out + "/Trip Archive/Media/clip.mp4"))
            verify(!testFiles.exists(out + "/Trip Archive/Media/unused.png")) // only what is used
            verify(findChild(dialog.contentItem, "archiveResult").text.indexOf("Trip Archive") >= 0)
            // The archived project points at its copies; the open one is unchanged.
            const text = testFiles.read(archived)
            verify(text.indexOf("Trip Archive/Media/clip.mp4") >= 0)
            verify(text.indexOf("unused.png") < 0)
            compare(p.media.length, 2)
            verify(p.media[0].path.indexOf("archive-source") >= 0)
            // A second archive gets a folder of its own.
            verify(dialog.archiveInto("file://" + out))
            tryCompare(spy, "count", 2, 10000)
            compare(spy.signalArguments[1][1], out + "/Trip Archive 2/Trip.tve")
            dialog.close()
        }
        Component { id: spyComponent; SignalSpy {} }

        function test_export_presets_of_your_own() {
            const f = createEditor()
            const p = f.project
            verify(p.importAndAppend("file://" + av))
            const before = p.exportPresets.length
            const id = p.saveExportPreset("Client review", { format: "mp4-h264", width: 1280, height: 720, frameRate: 25,
                                                               quality: "small", audio: false })
            verify(id !== "")
            compare(p.exportPresets.length, before + 1)
            const mine = p.exportPresets.find((x) => x.id === id)
            verify(mine.custom)
            compare(mine.title, "Client review")
            verify(mine.description.indexOf("1280 × 720") >= 0)
            verify(mine.description.indexOf("no sound") >= 0)
            // The same name replaces it; another format refused.
            compare(p.saveExportPreset("client review", { format: "mov-prores-hq" }), id)
            compare(p.exportPresets.length, before + 1)
            compare(p.saveExportPreset("Odd", { format: "nothing" }), "")
            // From the dialog: the settings saved, then chosen again.
            findChild(f.editor, "exportButton").clicked()
            const dialog = findChild(f.editor, "exportDialog")
            tryVerify(() => dialog.opened)
            const picker = findChild(dialog.contentItem, "exportPreset")
            picker.currentIndex = picker.model.findIndex((x) => x.id === "small")
            picker.activated(picker.currentIndex)
            findChild(dialog.contentItem, "saveExportPreset").clicked()
            const name = findChild(dialog.contentItem, "exportPresetName")
            tryVerify(() => name.visible)
            name.text = "Quick share"
            findChild(dialog.contentItem, "exportPresetSave").clicked()
            const quick = p.exportPresets.find((x) => x.title === "Quick share")
            verify(quick)
            compare(quick.format, "mp4-h264")
            compare(picker.model[picker.currentIndex].id, quick.id)
            verify(findChild(dialog.contentItem, "deleteExportPreset").visible)
            findChild(dialog.contentItem, "deleteExportPreset").clicked()
            verify(!p.exportPresets.some((x) => x.title === "Quick share"))
            verify(!p.removeExportPreset("web-1080")) // built-in ones stay
            verify(p.removeExportPreset(id))
            compare(p.exportPresets.length, before)
            dialog.close()
        }

        function test_a_clips_sound_channels() {
            const f = createEditor()
            const p = f.project
            compare(p.importMedia(["file://" + tone]), 1)
            verify(p.addClip(p.media[0].id))
            const track = p.tracks.find((t) => t.kind === "audio" && t.clips.length > 0)
            p.selectedClip = track.clips[0].id
            findChild(f.editor, "inspectorTab:Audio").clicked()
            compare(p.selectedClipInfo.channels, "stereo")
            settled(f.editor, "channels:left").clicked()
            compare(p.undoText, "Sound Channels")
            compare(p.selectedClipInfo.channels, "left")
            verify(p.setClipChannels(p.selectedClip, "mono"))
            compare(p.selectedClipInfo.channels, "mono")
            verify(!p.setClipChannels(p.selectedClip, "surround"))
            p.undo()
            compare(p.selectedClipInfo.channels, "left")
        }
    }
}
