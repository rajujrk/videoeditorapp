import QtQuick
import QtTest
import TechieVideoEditor.Theme
import TechieVideoEditor.Player
import TechieVideoEditor.Shell

// Audio plug-ins through the real editor: Apple's own Audio Units (on every Mac) and the test
// VST3 effect (TVE Test Gain, found by the real out-of-process scanner; main.cpp). Adding
// one to a clip from the inspector, its parameters in the plug-in's own words, keyframing
// one, its own window (a change there is one undo step and becomes its saved state), and a
// track's own effects from the mixer.
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
        name: "Plugins"
        when: windowShown

        readonly property string av: "file://" + samplesDir + "/test_av_1080p30.mp4"
        readonly property string delay: "au:aufx/dely/appl"
        readonly property string testGain: "vst3:5A3C1E427B9D4F608E21C3A4D05B6F17"

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
        function settled(editor, name) {
            let item = null
            let last = null
            tryVerify(() => {
                item = findChild(editor, name)
                if (!item || !item.visible)
                    return false
                const p = item.mapToItem(root, 0, 0)
                const stable = last !== null && p.x === last.x && p.y === last.y
                last = p
                return stable
            }, 3000, name + " never settled")
            for (let a = item; a && a !== root; a = a.parent)
                verify(waitForPolish(a), "layout never ran for " + name)
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
        function click(editor, name) {
            scrollIntoView(settled(editor, name))
            const item = settled(editor, name)
            mouseMove(item)
            mouseClick(item)
        }
        function withClip() {
            const f = createEditor()
            const p = f.project
            verify(p.importAndAppend(av))
            p.selectedClip = p.tracks.find((t) => t.main).clips[0].id
            return f
        }
        // The plug-ins, once the background look has finished.
        function plugins(p) {
            p.audioPlugins // starts it
            tryVerify(() => !p.scanningPlugins && p.audioPlugins.length > 0, 30000, "the plug-ins were never found")
            return p.audioPlugins
        }

        function test_plugins_on_a_clip() {
            const f = withClip()
            const p = f.project
            const found = plugins(p)
            const au = found.find((x) => x.id === delay)
            verify(au)
            compare(au.format, "AU")
            const vst3 = found.find((x) => x.id === testGain)
            verify(vst3)
            compare(vst3.name, "TVE Test Gain")
            compare(vst3.format, "VST3")

            // Picked like any audio effect.
            click(f.editor, "inspectorTab:Audio")
            const picker = settled(f.editor, "audioEffectPicker")
            const at = picker.model.findIndex((e) => e.id === delay)
            verify(at >= 0)
            picker.currentIndex = at
            compare(picker.currentText, "Plug-in · AUDelay (Apple, AU)")
            click(f.editor, "addAudioEffectButton")
            compare(p.undoText, "Add Plug-in")
            const clip = p.selectedClip
            compare(p.selectedClipInfo.audioEffects[0].plugin, true)

            // Its parameters, in its own words.
            const params = p.pluginParameters(clip, -1, 0)
            const time = params.find((x) => x.title === "Delay Time")
            verify(time)
            compare(p.pluginValueText(clip, -1, 0, time.id, 0.05), "0.1 Secs")
            verify(settled(f.editor, "audioEffectParam:0:" + time.id))
            verify(p.setAudioEffectParam(clip, 0, time.id, 0.05))
            compare(p.undoText, "Change Plug-in")
            compare(p.selectedClipInfo.audioEffects[0].values[time.id], 0.05)
            verify(p.setAudioEffectParam(clip, 0, time.id, 7)) // 0..1
            compare(p.selectedClipInfo.audioEffects[0].values[time.id], 1)

            // Keyframed: a key at the playhead, then a change there moves the key.
            const channel = "audioEffect:0:" + time.id
            verify(p.addKeyframes(clip, [channel]))
            compare(p.selectedClipInfo.keyframeStates[channel], "key")
            verify(p.setAudioEffectParam(clip, 0, time.id, 0.25))
            compare(p.undoText, "Change Plug-in")
            compare(p.selectedClipInfo.keyframeStates[channel], "key")
            verify(p.removeKeyframes(clip, [channel]))

            // Its own window: a change there becomes its saved state, one undo step.
            click(f.editor, "audioEffectWindow:0")
            tryVerify(() => p.isPluginWindowOpen(clip, -1, 0))
            verify(p.editPluginLive(clip, -1, 0, time.id, 0.4))
            compare(p.undoText, "Adjust Plug-in")
            compare(p.selectedClipInfo.audioEffects[0].values[time.id], undefined)
            compare(p.pluginParameters(clip, -1, 0).find((x) => x.id === time.id)["default"].toFixed(3), "0.400")
            p.closePluginWindow(clip, -1, 0)
            verify(!p.isPluginWindowOpen(clip, -1, 0))
            // Undone: the value before (left when its keyframes went) comes back to the plug-in shown.
            p.undo()
            compare(p.selectedClipInfo.audioEffects[0].values[time.id], 0.25)
            compare(p.pluginParameters(clip, -1, 0).find((x) => x.id === time.id)["default"].toFixed(3), "0.250")

            // A VST3 too; one that isn't installed is shown as such.
            verify(p.addAudioEffect(clip, testGain))
            const gain = p.pluginParameters(clip, -1, 1)
            compare(gain.length, 1)
            compare(gain[0].title, "Gain")
            verify(p.pluginValueText(clip, -1, 1, gain[0].id, 1).indexOf("x") >= 0)
            verify(p.addAudioEffect(clip, "au:aufx/none/nope"))
            compare(p.pluginParameters(clip, -1, 2).length, 0)
            verify(!p.openPluginWindow(clip, -1, 2))
        }

        function test_track_effects_from_the_mixer() {
            const f = withClip()
            const p = f.project
            plugins(p)
            const main = p.tracks.find((t) => t.main)
            click(f.editor, "mixerButton")
            const panel = findChild(f.editor, "mixerPanel")
            tryVerify(() => panel.opened)
            const strip = settled(panel.contentItem, "trackEffects:" + main.id)
            compare(strip.text, "FX")
            mouseClick(strip)
            const effects = findChild(f.editor, "trackEffectsPanel")
            tryVerify(() => effects.opened)
            const picker = settled(effects.contentItem, "trackEffectPicker")
            picker.currentIndex = picker.model.findIndex((e) => e.id === delay)
            mouseClick(settled(effects.contentItem, "addTrackEffect"))
            compare(p.undoText, "Add Track Plug-in")
            const track = p.tracks.find((t) => t.id === main.id)
            compare(track.audioEffects.length, 1)
            tryVerify(() => findChild(panel.contentItem, "trackEffects:" + main.id).text === "FX 1")
            // Its card works like a clip's.
            verify(settled(effects.contentItem, "trackEffectWindow:0"))
            const time = p.pluginParameters(-1, main.id, 0).find((x) => x.title === "Delay Time")
            verify(p.setTrackAudioEffectParam(main.id, 0, time.id, 0.1))
            compare(p.undoText, "Change Plug-in")
            compare(p.tracks.find((t) => t.id === main.id).audioEffects[0].values[time.id], 0.1)
            verify(p.openPluginWindow(-1, main.id, 0), p.lastError)
            tryVerify(() => p.isPluginWindowOpen(-1, main.id, 0))
            verify(p.editPluginLive(-1, main.id, 0, time.id, 0.3))
            compare(p.undoText, "Adjust Plug-in")
            p.closePluginWindow(-1, main.id, 0)
            // Built-in effects go there too; removing one.
            verify(p.addTrackAudioEffect(main.id, "tve.audio.hall"))
            mouseClick(settled(effects.contentItem, "removeTrackEffect:0"))
            compare(p.tracks.find((t) => t.id === main.id).audioEffects.length, 1)
            compare(p.tracks.find((t) => t.id === main.id).audioEffects[0].effectId, "tve.audio.hall")
        }
    }
}
