import QtQuick
import QtTest
import TechieVideoEditor.Theme
import TechieVideoEditor.Player
import TechieVideoEditor.Shell

// OpenFX video plug-ins through the real editor, with the test plug-in (TVE Test Invert:
// inverts the picture, tinted, by an amount; CPU and Metal) found by the real scanner
// (main.cpp): picked in the Effects tab, its parameters as controls (a 2D one a number each),
// keyframed, the player showing its picture inverted, and one that isn't installed.
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
        name: "VideoPlugins"
        when: windowShown

        readonly property string av: "file://" + samplesDir + "/test_av_1080p30.mp4"
        readonly property string invert: "ofx:com.techievideoeditor.tests.Invert"

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
        // The player's colour at canvas fraction (u, v), as [r, g, b] 0..255.
        function colour(f, u, v) {
            const overlay = findChild(f.editor, "maskOverlay")
            const r = overlay.canvasRect
            const image = grabImage(root)
            const scale = image.width / root.width
            const p = overlay.mapToItem(root, r.x + u * r.width, r.y + v * r.height)
            const c = image.pixel(Math.round(p.x * scale), Math.round(p.y * scale))
            return [c.r * 255, c.g * 255, c.b * 255]
        }
        function look(f) {
            wait(400)
            waitForRendering(root)
            return colour(f, 0.3, 0.3)
        }

        function test_an_openfx_plugin_on_a_clip() {
            const f = createEditor()
            const p = f.project
            verify(p.importAndAppend(av))
            const clip = p.tracks.find((t) => t.main).clips[0].id
            p.selectedClip = clip
            p.videoPlugins
            tryVerify(() => !p.scanningPlugins && p.videoPlugins.length > 0, 30000, "the plug-ins were never found")
            const found = p.videoPlugins.find((x) => x.id === invert)
            verify(found)
            compare(found.format, "OpenFX")
            verify(found.metal)
            // Its controls: a number, a colour, a choice, and a 2D one as two numbers.
            compare(found.params.map((x) => x.id), ["amount", "tint", "mode", "offset#0", "offset#1"])
            compare(found.params[1].type, "color")
            compare(found.params[2].values, ["Invert", "Keep"])
            compare(found.params[3].title, "Offset X")

            p.seek(0.5)
            const before = look(f)
            click(f.editor, "inspectorTab:Effects")
            const picker = settled(f.editor, "effectPicker")
            picker.currentIndex = picker.model.findIndex((e) => e.id === invert)
            compare(picker.currentText, "Plug-in · TVE Test Invert (TVE/Tests)")
            click(f.editor, "addEffectButton")
            compare(p.undoText, "Add Plug-in")
            compare(p.selectedClipInfo.effects[0].effectId, invert)
            verify(settled(f.editor, "effectParam:0:amount"))
            verify(settled(f.editor, "effectParam:0:offset#1"))
            verify(settled(f.editor, "clipEffectPluginNote:0").text.indexOf("the clip's own picture") >= 0)

            // The player shows it inverted.
            const inverted = (now) => [0, 1, 2].every((c) => Math.abs(now[c] + before[c] - 255) <= 8)
            let after = look(f)
            for (let i = 0; i < 10 && !inverted(after); ++i)
                after = look(f)
            verify(inverted(after), "the picture never looked inverted: " + before + " / " + after)

            // Kept as it was (the choice), then a settled amount; keyframed.
            verify(p.setClipEffectParam(clip, 0, "mode", 1))
            compare(p.selectedClipInfo.effects[0].values.mode, 1)
            const same = (now) => [0, 1, 2].every((c) => Math.abs(now[c] - before[c]) <= 8)
            let kept = look(f)
            for (let i = 0; i < 10 && !same(kept); ++i)
                kept = look(f)
            verify(same(kept), "the picture never came back: " + before + " / " + kept)
            verify(p.setClipEffectParam(clip, 0, "mode", 0))
            verify(p.setClipEffectParam(clip, 0, "amount", 0.25))
            compare(p.undoText, "Change Effect")
            verify(p.addKeyframes(clip, ["effect:0:amount"]))
            compare(p.selectedClipInfo.keyframeStates["effect:0:amount"], "key")
            verify(p.addKeyframes(clip, ["effect:0:offset#0"]))
            verify(!p.addKeyframes(clip, ["effect:0:tint"])) // a colour isn't keyframed

            // One that isn't on this Mac: kept, passed over, said so.
            verify(p.addClipEffect(clip, "ofx:com.example.none"))
            const note = settled(f.editor, "clipEffectPluginNote:1")
            verify(note.text.indexOf("Not available") === 0, note.text)
            click(f.editor, "removeEffect:1")
            compare(p.selectedClipInfo.effects.length, 1)
        }
    }
}
