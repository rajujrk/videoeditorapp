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
    SignalSpy { id: finished; signalName: "analysisFinished" }
    TestCase {
        name: "ShotFit"
        when: windowShown
        function scrollTo(item) {
            for (let a = item.parent; a; a = a.parent) {
                if (a.contentY === undefined || a.contentHeight === undefined)
                    continue
                const top = item.mapToItem(a.contentItem, 0, 0).y
                a.contentY = Math.max(0, Math.min(top - 12, a.contentHeight - a.height))
                return
            }
        }
        function test_shots() {
            const window = root.Window.window
            for (const dark of [true, false]) {
                Theme.dark = dark
                const tag = dark ? "dark" : "light"
                Screens.palette(root.Window.window, Theme)
                const f = createTemporaryObject(editorComponent, root)
                waitForRendering(f)
                const p = f.project
                finished.clear(); finished.target = p
                verify(p.cloud.setKey("anthropic", "test-claude-key"))
                verify(p.cloud.setKey("google", "test-google-key"))
                verify(p.importMedia(["file://" + samplesDir + "/people.mp4", "file://" + samplesDir + "/shaky.mp4", "file://" + analysisMediaDir + "/song.wav"]) === 3)
                verify(p.addClip(p.media[0].id))
                verify(p.addClip(p.media[1].id))
                verify(p.addClip(p.media[0].id))
                verify(p.addClip(p.media[2].id))
                const song = p.tracks.find((t) => t.kind === "audio" && t.clips.length > 0).clips[0]
                const songId = Number(song.id)
                p.selectedClip = songId
                findChild(f.editor, "inspectorTab:Audio").clicked()
                findChild(f.editor, "fitLength").text = p.picturesEnd().toFixed(1)
                verify(p.fitMusic(songId, Number(findChild(f.editor, "fitLength").text)))
                tryVerify(() => finished.count >= 1, 30000)
                wait(500)
                scrollTo(findChild(f.editor, "fitMusicSection"))
                wait(700)
                grabImage(window.contentItem).save(screensDir + "/fit-music-" + tag + ".png")

                // An AI transition at the first cut.
                p.selectedClip = Number(p.tracks.find((t) => t.main).clips[1].id)
                p.seek(2.9)
                findChild(f.editor, "libraryTab:Generate").clicked()
                const page = findChild(f.editor, "generatePage")
                findChild(page, "generateKind:video").clicked()
                findChild(page, "videoMode:transition").clicked()
                findChild(page, "generatePrompt").text = "a whip pan"
                wait(700)
                grabImage(window.contentItem).save(screensDir + "/ai-transition-" + tag + ".png")

                // A video from an idea: the plan to review.
                findChild(page, "generateKind:idea").clicked()
                findChild(page, "generatePrompt").text = "A paper boat sails down a rainy street"
                findChild(page, "ideaShots:3").clicked()
                verify(p.planIdea("A paper boat sails down a rainy street", { shots: 3 }))
                tryVerify(() => p.cloud.job === "", 5000)
                wait(700)
                grabImage(window.contentItem).save(screensDir + "/idea-video-" + tag + ".png")
                p.clearIdeaPlan()
                p.cloud.removeKey("anthropic"); p.cloud.removeKey("google")
                f.destroy(); wait(100)
            }
            Theme.dark = true
        }
    }
}
