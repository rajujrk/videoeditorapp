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
    Rectangle {
        id: sign
        x: root.width + 100
        width: 1280
        height: 720
        color: "#2B5D78"
        Column {
            anchors.centerIn: parent
            spacing: 40
            Text { text: "HARBOUR CAFE"; font.pixelSize: 130; font.bold: true; color: "#F4EFE3" }
            Text { text: "Fresh fish · Open every day"; font.pixelSize: 60; color: "#F4EFE3"; anchors.horizontalCenter: parent.horizontalCenter }
        }
    }
    SignalSpy { id: finished; signalName: "analysisFinished" }
    TestCase {
        name: "ShotSearch"
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
        function signPicture() {
            let saved = ""
            sign.grabToImage((result) => { const path = stillsDir + "/Harbour sign.png"; if (result.saveToFile(path)) saved = path })
            tryVerify(() => saved !== "", 5000)
            return saved
        }
        function test_shots() {
            const window = root.Window.window
            const picture = signPicture()
            for (const dark of [true, false]) {
                Theme.dark = dark
                const tag = dark ? "dark" : "light"
                Screens.palette(root.Window.window, Theme)
                const f = createTemporaryObject(editorComponent, root)
                waitForRendering(f)
                const p = f.project
                finished.clear(); finished.target = p
                verify(p.cloud.setKey("anthropic", "test-claude-key"))
                verify(p.importMedia(["file://" + samplesDir + "/people.mp4", "file://" + samplesDir + "/shaky.mp4", "file://" + picture, "file://" + samplesDir + "/portrait.jpg"]) === 4)
                verify(p.addClip(p.media[0].id))
                verify(p.analyzeForSearch())
                tryVerify(() => finished.count >= 1, 60000)
                console.log("labels", JSON.stringify(p.mediaLabels(p.media[0].id)), JSON.stringify(p.mediaLabels(p.media[3].id)))
                findChild(f.editor, "libraryTab:Media").clicked()
                const field = findChild(f.editor, "librarySearch")
                field.text = "harbour"
                wait(900)
                grabImage(window.contentItem).save(screensDir + "/smart-search-" + tag + ".png")
                const word = p.mediaLabels(p.media[0].id)[0].label
                field.text = word
                wait(900)
                grabImage(window.contentItem).save(screensDir + "/smart-search-labels-" + tag + ".png")
                field.text = ""

                // Proofreading.
                verify(p.importSubtitles("file://" + captionFixturesDir + "/talk.srt"))
                const captions = p.tracks.find((t) => t.captions).clips
                verify(p.setClipText(captions[1].id, { text: "Teh tide had left shells everywhere." }))
                verify(p.addTitle({ text: "Its a good day to recieve guests" }, -1, 0))
                findChild(f.editor, "libraryTab:Captions").clicked()
                verify(p.checkText())
                tryVerify(() => p.cloud.job === "", 5000)
                wait(400)
                scrollTo(findChild(f.editor, "checkText"))
                wait(700)
                grabImage(window.contentItem).save(screensDir + "/proofread-" + tag + ".png")

                // A prompt from the frame.
                verify(p.applyTextFixes(p.textFixes.map((x) => x.index)))
                verify(p.cloud.setKey("openai", "test-openai-key"))
                p.seek(1.5)
                findChild(f.editor, "libraryTab:Generate").clicked()
                verify(p.promptFromPicture("picture"))
                tryVerify(() => p.cloud.job === "", 10000)
                wait(700)
                grabImage(window.contentItem).save(screensDir + "/image-prompt-" + tag + ".png")
                p.cloud.removeKey("anthropic")
                p.cloud.removeKey("openai")
                f.destroy(); wait(100)
            }
            Theme.dark = true
        }
    }
}
