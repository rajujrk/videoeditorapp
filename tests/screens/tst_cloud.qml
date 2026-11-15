import QtQuick
import QtTest
import TechieVideoEditor.Theme
import TechieVideoEditor.Player
import TechieVideoEditor.Shell
import "screens.js" as Screens

// AI with your own keys, against the test runner's mock services (TVE_CLOUD_MOCK; test keys
// in a scratch key store, never the Keychain): settings, writing, a voice, the copilot,
// Generate, and copying a voice with consent.
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
        name: "ScreensCloud"
        when: windowShown
        readonly property string media: "file://" + screensMediaDir + "/"

        function editor() {
            const f = createTemporaryObject(editorComponent, root)
            waitForRendering(f)
            const cloud = f.project.cloud
            verify(cloud.setKey("anthropic", "test-claude-key"))
            verify(cloud.setKey("elevenlabs", "test-voice-key"))
            verify(cloud.setKey("openai", "test-openai-key"))
            verify(cloud.setKey("google", "test-google-key"))
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
        function cleanUp(f) {
            for (const provider of ["anthropic", "elevenlabs", "openai", "google"])
                f.project.cloud.removeKey(provider)
        }

        function test_scenes() {
            for (const dark of [true, false]) {
                Theme.dark = dark
                EditingPreferences.reset()
                Screens.palette(root.Window.window, Theme)

                // Write with AI: titles from the captions.
                let f = editor()
                let p = f.project
                verify(p.importAndAppend(media + "night-street.mp4"))
                compare(p.importSubtitles("file://" + captionFixturesDir + "/talk.srt"), true)
                p.cloud.resetUsage()
                settled(f.editor, "libraryTab:Transcript").clicked()
                settled(f.editor, "writeWithAi").clicked()
                const write = findChild(f.editor, "writePanel")
                tryVerify(() => write.opened)
                mouseClick(settled(write.contentItem, "writeButton"))
                tryVerify(() => p.cloud.job === "", 10000)
                shot("ai-write", dark)
                write.close()

                // A cloud voice at the playhead, with captions timed to its words.
                tryVerify(() => p.cloud.voices.some((v) => v.id === "elevenlabs:mockvoice2"), 5000)
                p.seek(0.5)
                verify(p.speakText("Welcome back. Today we look at the launch.", "elevenlabs:mockvoice2", {}), "" + p.lastError)
                tryVerify(() => p.cloud.job === "", 10000)
                p.seek(1.2)
                settled(f.editor, "libraryTab:Audio").clicked()
                shot("ai-voice", dark)

                // AI settings: providers, models and what this session used.
                mouseClick(settled(f.editor, "cloudSettingsButton"))
                const settings = findChild(f.editor, "cloudSettings")
                tryVerify(() => settings.opened)
                shot("ai-settings", dark)
                settings.close()
                cleanUp(f)
                f.destroy(); wait(200)

                // The copilot: a request in words, and the changes it proposes.
                f = editor()
                p = f.project
                verify(p.importAndAppend(media + "night-street.mp4"))
                verify(p.importAndAppend(media + "portrait-wide.mp4"))
                settled(f.editor, "libraryTab:Copilot").clicked()
                verify(p.copilot.ask("Add a welcome title at the start and a thank-you at the end."))
                tryVerify(() => !p.copilot.busy, 10000)
                verify(p.copilot.proposal.length === 2, JSON.stringify(p.copilot.proposal))
                p.seek(1)
                shot("copilot", dark)
                cleanUp(f)
                f.destroy(); wait(200)

                // Generate: a sticker placed over the picture, then a video from a first and a last picture.
                f = editor()
                p = f.project
                verify(p.importAndAppend(media + "night-street.mp4"))
                compare(p.importMedia([media + "photo.png", "file://" + stillsDir + "/disc.png"]), 2)
                p.seek(1)
                settled(f.editor, "libraryTab:Generate").clicked()
                const page = settled(f.editor, "generatePage")
                mouseClick(settled(page, "generateKind:sticker"))
                settled(page, "generatePrompt").text = "A red balloon"
                mouseClick(settled(page, "generateButton"))
                tryVerify(() => p.cloud.job === "", 10000)
                page.kind = "video"
                page.videoMode = "frames"
                settled(page, "generatePrompt").text = "The astronaut turns towards the camera and smiles"
                page.picked = [p.media.find((m) => m.name === "photo.png").id, p.media.find((m) => m.name === "disc.png").id]
                for (let a = page; a; a = a.parent)
                    if (a.contentY !== undefined) { a.contentY = 0; break }
                p.selectedClip = 0
                shot("generate", dark)
                cleanUp(f)
                f.destroy(); wait(200)

                // Copying a voice with consent: the statement to read, before recording.
                f = editor()
                p = f.project
                verify(p.importAndAppend(media + "night-street.mp4"))
                settled(f.editor, "libraryTab:Audio").clicked()
                settled(f.editor, "audioSection:voice").clicked()
                settled(f.editor, "copyVoice").clicked()
                const clone = findChild(f.editor, "voiceCloneDialog")
                tryVerify(() => clone.opened)
                const content = clone.contentItem
                findChild(content, "cloneSpeaker").text = "Ada Okafor"
                findChild(content, "cloneVoiceName").text = "Ada narrating"
                findChild(content, "cloneIsSpeaker").checked = true
                findChild(content, "cloneAdult").checked = true
                findChild(content, "cloneAgrees").checked = true
                findChild(content, "cloneContinue").clicked()
                tryCompare(p.voiceCloning, "stage", "ready")
                settled(content, "cloneStatement")
                shot("voice-clone", dark)
                clone.close()
                cleanUp(f)
                f.destroy(); wait(200)
            }
            EditingPreferences.reset()
            Theme.dark = true
        }
    }
}
