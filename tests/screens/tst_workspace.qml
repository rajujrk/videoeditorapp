import QtQuick
import QtTest
import TechieVideoEditor.Theme
import TechieVideoEditor.Player
import TechieVideoEditor.Shell
import "screens.js" as Screens

// Around the edit: importing from Photos, media that moved and finding it again, keyboard
// shortcuts, the pack store and recording the screen. All against the test runner's stand-ins
// (test Photos, display and camera sources; a pack site on this machine; a scratch folder).
Item {
    id: root
    width: 1470
    height: 790
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
        name: "ScreensWorkspace"
        when: windowShown
        readonly property string media: "file://" + screensMediaDir + "/"

        function editor() {
            const f = createTemporaryObject(editorComponent, root)
            waitForRendering(f)
            return f
        }
        function main(p) { return p.tracks.find((t) => t.main) }
        function shot(name, dark, settle) {
            mouseMove(root, root.width - 4, root.height - 4)
            wait(settle === undefined ? 1500 : settle)
            grabImage(root.Window.window.contentItem).save(Screens.file(screensDir, name, dark))
        }
        function settled(parent, name) {
            tryVerify(() => { const i = findChild(parent, name); return i !== null && i.visible }, 5000, name)
            return findChild(parent, name)
        }
        function click(parent, name) {
            const target = settled(parent, name)
            for (let a = target.parent; a; a = a.parent) {
                if (a.contentY === undefined || a.contentHeight === undefined || a.height === undefined)
                    continue
                const top = target.mapToItem(a.contentItem, 0, 0).y
                if (top < a.contentY || top + target.height > a.contentY + a.height)
                    a.contentY = Math.max(0, Math.min(top - 20, a.contentHeight - a.height))
                break
            }
            const item = settled(parent, name)
            mouseMove(item)
            mouseClick(item)
        }
        function cleanup() {
            KeyMap.resetAll()
            Theme.dark = true
        }

        function test_scenes() {
            for (const dark of [true, false]) {
                Theme.dark = dark
                EditingPreferences.reset()
                Screens.palette(root.Window.window, Theme)

                // Import from Photos: an album, both items chosen.
                let f = editor()
                let p = f.project
                verify(p.importAndAppend(media + "night-street.mp4"))
                click(f.editor, "importFromButton")
                const source = findChild(f.editor, "importFrom:photos")
                tryCompare(source, "visible", true)
                mouseClick(source)
                const imports = findChild(f.editor, "importPanel")
                tryCompare(imports, "opened", true)
                click(imports.contentItem, "chooseAllImports")
                compare(imports.chosenCount, 2)
                shot("imports", dark)
                imports.close()
                f.destroy(); wait(200)

                // A project whose media moved: the bar, the clips marked, then the dialog.
                const dir = scratchDir + "/screens-" + (dark ? "dark" : "light")
                verify(testFiles.copy(screensMediaDir + "/night-street.mp4", dir + "/Wedding film/Footage/Ceremony.mp4"))
                verify(testFiles.copy(screensMediaDir + "/portrait-wide.mp4", dir + "/Wedding film/Footage/Speeches.mp4"))
                verify(testFiles.copy(screensMediaDir + "/photo.png", dir + "/Wedding film/Footage/Garden.png"))
                f = editor()
                p = f.project
                compare(p.importMedia(["file://" + dir + "/Wedding film/Footage/Ceremony.mp4",
                                       "file://" + dir + "/Wedding film/Footage/Speeches.mp4",
                                       "file://" + dir + "/Wedding film/Footage/Garden.png"]), 3)
                for (const m of p.media)
                    verify(p.addClip(m.id))
                verify(p.save("file://" + dir + "/Wedding film/Wedding.tve"))
                wait(1500) // the thumbnails, before the files move away
                f.destroy(); wait(200)
                verify(testFiles.move(dir + "/Wedding film/Footage/Speeches.mp4", dir + "/Backup/Speeches.mp4"))
                verify(testFiles.move(dir + "/Wedding film/Footage/Garden.png", dir + "/Backup/Garden.png"))
                f = editor()
                p = f.project
                verify(p.open("file://" + dir + "/Wedding film/Wedding.tve"))
                compare(p.missingMedia.length, 2)
                settled(f.editor, "missingMediaBar")
                p.seek(4)
                shot("missing-media", dark)
                click(f.editor, "relinkButton")
                const relink = findChild(f.editor, "relinkDialog")
                tryVerify(() => relink.opened)
                tryVerify(() => findChild(relink.contentItem, "missing:Garden.png") !== null)
                shot("relink", dark)
                relink.close()

                // Keyboard shortcuts: a key already in use asks before it moves.
                click(f.editor, "shortcutsButton")
                const keys = findChild(f.editor, "shortcutsDialog")
                tryVerify(() => keys.opened)
                click(keys.contentItem, "shortcutKey:split")
                compare(keys.recording, "split")
                keyClick(Qt.Key_M)
                settled(keys.contentItem, "shortcutConflict")
                shot("shortcuts", dark)
                keyClick(Qt.Key_Escape)
                keys.close()
                KeyMap.resetAll()
                f.destroy(); wait(200)

                // The store: packs from the catalogue, one installed.
                packSite.reset()
                compare(packSite.publish({ "sounds": soundFixturesDir + "/packs/good" }, "test.sounds", 1,
                                         "Pop and ding", "Two short sound effects for taps and reveals."), "")
                const list = packSite.text("stickers.json", JSON.stringify({ stickers: [
                    { id: "dot", title: "Pack dot", category: "shapes", file: "dot.json" }] }))
                compare(packSite.publish({ "stickers/stickers.json": list, "stickers/dot.json": ":/effects/stickers/star_pulse.json" },
                                         "test.stickers", 1, "Little stars", "A star that pulses, for titles and thumbnails.",
                                         stillsDir + "/disc.png"), "")
                packSite.sign()
                f = editor()
                p = f.project
                verify(p.importAndAppend(media + "night-street.mp4"))
                const rail = findChild(f.editor, "libraryTabRail")
                rail.contentY = rail.contentHeight - rail.height
                settled(f.editor, "libraryTab:Store").clicked()
                settled(f.editor, "storePage")
                tryVerify(() => p.store.checked, 10000, p.store.lastError)
                let installed = false
                p.store.finished.connect(() => installed = true)
                settled(f.editor, "packInstall:test.sounds").clicked()
                tryVerify(() => installed, 10000)
                p.seek(1)
                shot("store", dark)
                for (const id of ["test.sounds", "test.stickers"])
                    p.store.remove(id)
                f.destroy(); wait(200)

                // Recording the screen and the camera, then the cursor's options on the screen clip.
                f = editor()
                p = f.project
                click(f.editor, "screenRecordButton")
                const panel = findChild(f.editor, "recordPanel")
                tryVerify(() => panel.opened)
                const camera = findChild(panel.contentItem, "cameraChoice")
                camera.currentIndex = 1
                camera.activated(1)
                click(panel.contentItem, "startScreenRecording")
                tryVerify(() => p.screenRecordingTime > 2.6, 8000)
                click(panel.contentItem, "stopScreenRecording")
                tryVerify(() => !p.screenRecording)
                panel.close()
                const screen = p.tracks.find((t) => t.name === "Screen").clips[0]
                p.selectedClip = screen.id
                p.seek(1.3)
                findChild(f.editor, "inspectorTab:Analyze").clicked()
                settled(f.editor, "cursorSection")
                verify(p.setCursorEffects(screen.id, { spotlight: true }), "" + p.lastError)
                tryVerify(() => findChild(f.editor, "cursorSpotlight").checked)
                verify(p.selectedClipInfo.cursor.spotlight)
                shot("recording", dark)
                f.destroy(); wait(200)
            }
            EditingPreferences.reset()
        }
    }
}
