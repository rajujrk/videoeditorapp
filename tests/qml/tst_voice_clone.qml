import QtQuick
import QtTest
import TechieVideoEditor.Theme
import TechieVideoEditor.Player
import TechieVideoEditor.Shell

// Copying a voice with consent through the real editor, the real speech model and the mock
// ElevenLabs (TVE_CLOUD_MOCK=1): the speaker's go-ahead, the statement written for them (code
// words fixed by TVE_CONSENT_SEED), the take — the Mac's own voice reading it, or reading
// something else (TVE_CONSENT_RECORDING) — transcribed here and checked, the copy made only
// from a take that says it, its consent record kept, and the voice listed and deleted.
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

    TestCase {
        id: testCase
        name: "VoiceClone"
        when: windowShown

        function initTestCase() {
            if (!speechModelAvailable)
                skip("no speech model: run tools/fetch_models.sh")
        }
        function createEditor() {
            const fixture = createTemporaryObject(editorComponent, root)
            verify(fixture)
            waitForRendering(fixture)
            const p = fixture.project
            p.cloud.removeKey("elevenlabs")
            verify(p.cloud.setKey("elevenlabs", "test-voice-key"))
            if (p.voiceCloning.needsModel) {
                verify(p.downloadSpeechModel(p.voiceCloning.speechModel))
                tryVerify(() => !p.voiceCloning.needsModel, 60000, "the speech model never arrived")
            }
            return fixture
        }
        function settled(parentItem, name) {
            let item = null
            let last = null
            tryVerify(() => {
                item = findChild(parentItem, name)
                if (!item || !item.visible)
                    return false
                const p = item.mapToItem(root, 0, 0)
                const stable = last !== null && p.x === last.x && p.y === last.y
                last = p
                return stable
            }, 5000, name + " never settled")
            return item
        }
        function press(item) {
            verify(item.enabled, item.objectName + " is disabled")
            item.clicked()
        }
        function elevenLabsRequests(cloud, path) {
            return cloud.mockRequests().filter((r) => r.path.indexOf(path) === 0)
        }
        function openDialog(f) {
            findChild(f.editor, "libraryTab:Audio").clicked()
            press(settled(f.editor, "audioSection:voice"))
            press(settled(f.editor, "copyVoice"))
            const dialog = findChild(f.editor, "voiceCloneDialog")
            tryVerify(() => dialog.opened)
            return dialog.contentItem
        }
        function fillIn(content, speaker) {
            findChild(content, "cloneSpeaker").text = speaker
            findChild(content, "cloneVoiceName").text = "Ada narrating"
            verify(!findChild(content, "cloneContinue").enabled) // not before the go-ahead
            findChild(content, "cloneIsSpeaker").checked = true
            findChild(content, "cloneAdult").checked = true
            verify(!findChild(content, "cloneContinue").enabled)
            findChild(content, "cloneAgrees").checked = true
            press(findChild(content, "cloneContinue"))
        }

        function test_a_voice_is_copied_only_from_a_reading_of_the_statement() {
            const f = createEditor()
            const p = f.project
            const vc = p.voiceCloning
            // Without the go-ahead: refused.
            verify(!vc.begin("Ada Okafor", "", true, false, true))
            verify(vc.error.indexOf("18 or older") > 0)
            compare(vc.stage, "start")

            const content = openDialog(f)
            fillIn(content, "Ada Okafor")
            compare(vc.stage, "ready")
            const statement = settled(content, "cloneStatement").text
            verify(statement.indexOf("My name is Ada Okafor.") === 0, statement)
            verify(statement.indexOf("ElevenLabs") > 0)

            // Someone reading something else: not copied.
            testFiles.setEnv("TVE_CONSENT_RECORDING", "say-wrong")
            press(settled(content, "cloneRecord"))
            compare(vc.stage, "recording")
            press(settled(content, "cloneStop"))
            tryVerify(() => vc.stage === "checked", 60000, "the recording was never checked")
            compare(vc.check.passed, false)
            verify(settled(content, "cloneCheck").text.indexOf("weren't heard") >= 0, vc.check.why)
            verify(!findChild(content, "cloneMake").visible)
            verify(!vc.makeVoice())
            compare(elevenLabsRequests(p.cloud, "/v1/voices/add").length, 0)

            // The statement read: checked here, then copied.
            testFiles.setEnv("TVE_CONSENT_RECORDING", "say")
            press(settled(content, "cloneRecord"))
            press(settled(content, "cloneStop"))
            tryVerify(() => vc.stage === "checked", 60000, "the recording was never checked")
            verify(vc.check.passed, vc.check.why + " — heard: " + vc.check.heard)
            verify(vc.check.match >= 0.8)
            press(settled(content, "cloneMake"))
            tryVerify(() => vc.stage === "done", 10000, "the voice was never made: " + vc.error)
            const added = elevenLabsRequests(p.cloud, "/v1/voices/add")
            compare(added.length, 1)
            compare(added[0].method, "POST")
            compare(added[0].body.name, "Ada narrating")
            verify(added[0].body.description.indexOf("Ada Okafor") > 0)
            compare(added[0].body.files.length, 1)
            compare(added[0].body.files[0].name, "files")
            compare(added[0].body.files[0].type, "audio/wav")
            verify(added[0].body.files[0].size > 100000)
            compare(added[0].headers["xi-api-key"], "test-voice-key")

            // Kept with its consent, listed, and chosen.
            compare(vc.madeVoice, "elevenlabs:mockclone1")
            tryVerify(() => vc.clones.length === 1, 5000)
            compare(vc.clones[0].name, "Ada narrating")
            compare(vc.clones[0].consent.speaker, "Ada Okafor")
            const record = JSON.parse(testFiles.read(consentDir + "/mockclone1/record.json"))
            compare(record.speaker, "Ada Okafor")
            compare(record.statement, statement)
            verify(record.heard.length > 100)
            verify(testFiles.exists(consentDir + "/mockclone1/consent.wav"))
            press(settled(content, "cloneCancel"))
            tryVerify(() => vc.stage === "start")
            const pane = findChild(f.editor, "voicePane")
            compare(pane.voice, "elevenlabs:mockclone1")
            verify(settled(f.editor, "clonedVoice:0"))

            // Deleted: from the account, with its record.
            press(settled(f.editor, "deleteClone:0"))
            press(settled(findChild(f.editor, "deleteCloneConfirm").contentItem, "deleteCloneConfirmButton"))
            tryVerify(() => vc.clones.length === 0, 5000, "the voice stayed")
            const deletes = p.cloud.mockRequests().filter((r) => r.method === "DELETE")
            compare(deletes.length, 1)
            compare(deletes[0].path, "/v1/voices/mockclone1")
            verify(!testFiles.exists(consentDir + "/mockclone1"))
            testFiles.setEnv("TVE_CONSENT_RECORDING", "")
        }
    }
}
