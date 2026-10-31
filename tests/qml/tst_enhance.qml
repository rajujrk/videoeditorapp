import QtQuick
import QtTest
import TechieVideoEditor.Theme
import TechieVideoEditor.Player
import TechieVideoEditor.Shell

// Cleaning up speech: the live noise-reduction effects (RNNoise) and Enhance speech
// (DeepFilterNet3, downloaded from the test's local model folder), through the real editor.
Item {
    id: root
    width: 1400
    height: 900

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

    SignalSpy { id: finished; signalName: "analysisFinished" }
    SignalSpy { id: downloaded; signalName: "modelDownloadFinished" }

    TestCase {
        id: testCase
        name: "Enhance"
        when: windowShown

        function createEditor() {
            const fixture = createTemporaryObject(editorComponent, root)
            verify(fixture)
            waitForRendering(fixture)
            finished.clear()
            finished.target = fixture.project
            downloaded.clear()
            downloaded.target = fixture.project
            return fixture
        }
        function settled(parent, name) {
            let item = null
            let last = null
            tryVerify(() => {
                item = findChild(parent, name)
                if (!item || !item.visible)
                    return false
                const p = item.mapToItem(root, 0, 0)
                const stable = last !== null && p.x === last.x && p.y === last.y
                last = p
                return stable
            }, 3000, name + " never settled")
            for (let a = item; a && a !== root; a = a.parent)
                waitForPolish(a, 1000)
            return item
        }
        function scrollIntoView(item) {
            for (let a = item.parent; a; a = a.parent) {
                if (a.contentY === undefined || a.contentHeight === undefined || a.height === undefined)
                    continue
                const top = item.mapToItem(a.contentItem, 0, 0).y
                if (top < a.contentY || top + item.height > a.contentY + a.height)
                    a.contentY = Math.max(0, Math.min(top - 20, a.contentHeight - a.height))
                return
            }
        }
        function click(parent, name) {
            scrollIntoView(settled(parent, name))
            const item = settled(parent, name)
            mouseMove(item)
            mouseClick(item)
        }
        // The talking sample on the timeline, selected.
        function withSpeech(f) {
            const p = f.project
            if (p.importMedia(["file://" + samplesDir + "/speech_en.wav"]) !== 1)
                skip("speech_en.wav missing: run tools/make_test_media.sh")
            verify(p.addClip(p.media[0].id))
            const id = p.tracks.find((t) => t.kind === "audio").clips[0].id
            p.selectedClip = id
            return id
        }

        function test_noise_reduction_presets_are_audio_effects() {
            const f = createEditor()
            const p = f.project
            const cleanup = p.audioEffects.filter((e) => e.categories[0] === "cleanup").map((e) => e.id)
            compare(cleanup.length, 5)
            for (const id of ["tve.audio.noise_reduction", "tve.audio.light_noise_reduction", "tve.audio.quiet_between_words",
                              "tve.audio.voice_cleanup", "tve.audio.podcast_voice"])
                verify(cleanup.indexOf(id) >= 0, id)
            const id = withSpeech(f)
            click(f.editor, "inspectorTab:Audio")
            const picker = settled(f.editor, "audioEffectPicker")
            picker.currentIndex = p.audioEffects.findIndex((e) => e.id === "tve.audio.noise_reduction")
            compare(picker.currentText, "Clean-up · Noise Reduction")
            click(f.editor, "addAudioEffectButton")
            compare(p.selectedClipInfo.audioEffects[0].effectId, "tve.audio.noise_reduction")
            verify(settled(f.editor, "audioEffectParam:0:amount"))
            verify(settled(f.editor, "audioEffectParam:0:quiet"))
            verify(p.setAudioEffectParam(id, 0, "quiet", -12))
            compare(p.selectedClipInfo.audioEffects[0].values.quiet, -12)
        }

        function test_enhance_speech_downloads_the_model_then_cleans() {
            const f = createEditor()
            const p = f.project
            const id = withSpeech(f)
            click(f.editor, "inspectorTab:Analyze")
            verify(settled(f.editor, "enhanceSection"))
            compare(p.enhanceModel.id, "deepfilternet3")
            compare(p.enhanceModel.size, 7983136)
            // Not here yet: enhancing says so, and the button waits for the model.
            verify(!p.enhanceModel.downloaded)
            verify(!findChild(f.editor, "enhanceSpeech").enabled)
            verify(!p.enhanceSpeech(id))
            verify(p.lastError.indexOf("Download the speech enhancement model") === 0, p.lastError)
            // The captions page lists only recognition models.
            verify(!p.speechModels.some((m) => m.id === "deepfilternet3"))

            verify(settled(f.editor, "downloadEnhanceModel").text.indexOf("8 MB") > 0)
            click(f.editor, "downloadEnhanceModel")
            tryVerify(() => downloaded.count === 1, 20000)
            if (!downloaded.signalArguments[0][1])
                skip("DeepFilterNet3 missing: run tools/fetch_models.sh (" + downloaded.signalArguments[0][2] + ")")
            verify(p.enhanceModel.downloaded)
            verify(!findChild(f.editor, "downloadEnhanceModel").visible)

            // Fully cleaned: one undo step, the clip plays its cleaned sound.
            click(f.editor, "enhanceSpeech")
            tryVerify(() => finished.count === 1, 30000, "enhancing never finished")
            compare(finished.signalArguments[0][0], true, finished.signalArguments[0][1])
            compare(p.undoText, "Enhance Speech")
            compare(p.selectedClipInfo.enhanced.limitDb, 100)
            compare(p.selectedClipInfo.enhanced.missing, false)
            compare(settled(f.editor, "enhanceState").text, "Enhanced: the clip plays its cleaned sound.")
            compare(settled(f.editor, "enhanceSpeech").text, "Apply strength")
            // The timeline draws the cleaned sound's waveform.
            const wave = findChild(f.editor, "waveform:" + id)
            verify(String(wave.source).indexOf("-dfn3.wav") > 0, String(wave.source))

            // A gentler strength mixes some of the original back (quick: no model run).
            const strength = settled(f.editor, "enhanceStrength")
            strength.value = 12
            click(f.editor, "enhanceSpeech")
            tryVerify(() => finished.count === 2, 10000)
            compare(finished.signalArguments[1][0], true, finished.signalArguments[1][1])
            compare(p.selectedClipInfo.enhanced.limitDb, 12)
            compare(settled(f.editor, "enhanceState").text, "Enhanced, noise lowered by up to 12 dB.")
            p.undo()
            compare(p.selectedClipInfo.enhanced.limitDb, 100)
            // The second time it is instant: the cleaned sound is kept.
            verify(p.enhanceSpeech(id, { limitDb: 12 }))
            verify(!p.analyzing)
            compare(p.selectedClipInfo.enhanced.limitDb, 12)

            // Back to the original sound, as one undo step.
            click(f.editor, "removeEnhancement")
            compare(p.undoText, "Original Sound")
            compare(p.selectedClipInfo.enhanced, undefined)
            verify(!findChild(f.editor, "enhanceState").visible)
            p.undo()
            compare(p.selectedClipInfo.enhanced.limitDb, 12)
        }

        function test_what_cannot_be_enhanced_says_why() {
            const f = createEditor()
            const p = f.project
            verify(!p.enhanceSpeech(12345))
            verify(p.lastError.indexOf("Select a clip with sound") === 0, p.lastError)
            const id = withSpeech(f)
            verify(!p.removeEnhancement(id))
            verify(p.lastError.indexOf("not enhanced") >= 0, p.lastError)
            // A title has no sound: no Enhance speech section.
            verify(p.addTitle({ text: "Hello" }, -1, 0)) // selected
            verify(p.selectedClipInfo.isTitle)
            click(f.editor, "inspectorTab:Analyze")
            verify(!findChild(f.editor, "enhanceSection").visible)
        }
    }
}
