import QtQuick
import QtTest
import TechieVideoEditor.Theme
import TechieVideoEditor.Player
import TechieVideoEditor.Shell
import "screens.js" as Screens

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
    readonly property string scratch: screensMediaDir + "/"
    TestCase {
        name: "ShotLibrary"
        when: windowShown
        function findLibrary(f) {
            let item = findChild(f.editor, "mediaGrid")
            while (item && item.binEntries === undefined)
                item = item.parent
            return item
        }
        function test_shots() {
            const window = root.Window.window
            for (const dark of [true, false]) {
                Theme.dark = dark
                EditingPreferences.reset()
                const tag = dark ? "dark" : "light"
                Screens.palette(root.Window.window, Theme)
                let f = createTemporaryObject(editorComponent, root)
                waitForRendering(f)
                let p = f.project
                verify(p.importMedia(["file://" + root.scratch + "portrait-wide.mp4", "file://" + root.scratch + "night-street.mp4",
                                      "file://" + samplesDir + "/test_av_1080p30.mp4", "file://" + stillsDir + "/halves.png",
                                      "file://" + stillsDir + "/disc.png", "file://" + samplesDir + "/test_tone_440.wav"]) === 6)
                const shoot = p.addBin("Interviews")
                p.addBin("B-roll")
                p.addBin("Music")
                verify(p.moveToBin([p.media[2].id], shoot))
                verify(p.setMediaLabel([p.media[0].id], "green"))
                verify(p.setMediaLabel([p.media[1].id], "orange"))
                verify(p.setMediaLabel([p.media[3].id], "blue"))
                verify(p.addClip(p.media[0].id))
                EditingPreferences.mediaGroups = true
                EditingPreferences.mediaSize = "small"
                p.selectedClip = 0
                wait(1200)
                grabImage(window.contentItem).save(screensDir + "/media-library-" + tag + ".png")
                EditingPreferences.mediaView = "list"
                wait(800)
                grabImage(window.contentItem).save(screensDir + "/media-list-" + tag + ".png")
                EditingPreferences.reset()

                // Project settings, the archive under way.
                findChild(f.editor, "canvasButton").clicked()
                const dialog = findChild(f.editor, "projectSettingsDialog")
                tryVerify(() => dialog.opened)
                const size = findChild(dialog.contentItem, "canvasSize")
                size.currentIndex = size.model.findIndex((s) => s.id === "vertical")
                findChild(dialog.contentItem, "archiveUsedOnly").checked = true
                dialog.result = "Archived to ~/Movies/Untitled project Archive/Untitled project.tve"
                wait(700)
                grabImage(window.contentItem).save(screensDir + "/project-settings-" + tag + ".png")
                dialog.close()
                wait(300)

                // The Audio tab's channels, on the music.
                verify(p.addClip(p.media[5].id))
                const music = p.tracks.find((t) => t.kind === "audio" && t.clips.length > 0)
                p.selectedClip = Number(music.clips[0].id)
                verify(p.setClipChannels(p.selectedClip, "left"))
                findChild(f.editor, "inspectorTab:Audio").clicked()
                wait(500)
                const section = findChild(f.editor, "channelsSection")
                for (let a = section.parent; a; a = a.parent) {
                    if (a.contentY === undefined || a.contentHeight === undefined)
                        continue
                    a.contentY = Math.max(0, Math.min(section.mapToItem(a.contentItem, 0, 0).y - 12, a.contentHeight - a.height))
                    break
                }
                wait(700)
                grabImage(window.contentItem).save(screensDir + "/sound-channels-" + tag + ".png")
                f.destroy(); wait(150)
            }
            EditingPreferences.reset()
            Theme.dark = true
        }
    }
}
