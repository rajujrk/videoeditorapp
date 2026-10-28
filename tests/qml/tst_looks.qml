import QtQuick
import QtTest
import TechieVideoEditor.Theme
import TechieVideoEditor.Player
import TechieVideoEditor.Shell

// Looks, the LUT library, overlays and adjustment layers through the real editor.
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

    TestCase {
        id: testCase
        name: "Looks"
        when: windowShown

        readonly property string av: "file://" + samplesDir + "/test_av_1080p30.mp4"

        function createEditor() {
            const fixture = createTemporaryObject(editorComponent, root)
            verify(fixture)
            waitForRendering(fixture)
            return fixture
        }
        function withClip() {
            const f = createEditor()
            compare(f.project.importMedia([av]), 1)
            verify(f.project.addClip(f.project.media[0].id))
            return f
        }
        function mainClips(project) { return project.tracks.find((t) => t.main).clips }

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
        function click(editor, name) {
            scrollIntoView(settled(editor, name))
            const item = settled(editor, name)
            mouseMove(item)
            mouseClick(item)
        }
        function colorAt(item, fx, fy) {
            const image = grabImage(root)
            const scale = image.width / root.width
            const p = item.mapToItem(root, item.width * fx, item.height * fy)
            return image.pixel(Math.round(p.x * scale), Math.round(p.y * scale))
        }

        function test_filters_tab_lists_the_looks_with_previews() {
            const f = createEditor()
            const p = f.project
            compare(p.looks.length, 12)
            compare(p.lutLibrary.length, 10)
            click(f.editor, "libraryTab:Filters")
            verify(settled(f.editor, "libraryAdjustmentLayer"))
            scrollIntoView(settled(f.editor, "lookPreview:sepia"))
            const preview = settled(f.editor, "lookPreview:sepia")
            tryCompare(preview, "status", Image.Ready)
            compare(preview.sourceSize.width, 192)
            // Sepia's preview is warm where black & white's is neutral.
            tryVerify(() => {
                const c = colorAt(preview, 0.5, 0.5) // sepia warms everything
                return c.r > c.b + 0.05
            }, 3000)
            scrollIntoView(settled(f.editor, "lookPreview:black_white"))
            const mono = settled(f.editor, "lookPreview:black_white")
            tryCompare(mono, "status", Image.Ready)
            const m = colorAt(mono, 0.5, 0.2)
            verify(Math.abs(m.r - m.b) < 0.03)
        }

        function test_apply_a_look_and_set_its_intensity() {
            const f = withClip()
            const p = f.project
            click(f.editor, "libraryTab:Filters")
            click(f.editor, "libraryLook:teal_orange")
            compare(p.undoText, "Apply Look")
            compare(p.selectedClipInfo.look.id, "teal_orange")
            compare(p.selectedClipInfo.look.title, "Teal & Orange")
            compare(p.selectedClipInfo.look.intensity, 1)
            verify(findChild(f.editor, "libraryLook:teal_orange").parent.active)
            compare(mainClips(p)[0].lookTitle, "Teal & Orange")

            click(f.editor, "inspectorTab:Color")
            compare(settled(f.editor, "lookName").text, "Teal & Orange")
            p.previewLookIntensity(p.selectedClip, 0.3)
            compare(p.undoText, "Apply Look") // previews leave the history alone
            verify(p.setLookIntensity(p.selectedClip, 0.4))
            compare(p.undoText, "Change Look")
            compare(p.selectedClipInfo.look.intensity, 0.4)
            compare(findChild(f.editor, "lookIntensity").value, 0.4)
            click(f.editor, "removeLookButton")
            compare(p.undoText, "Remove Look")
            verify(p.selectedClipInfo.look === undefined)
            verify(!p.applyLook(p.selectedClip, "nonsense"))
        }

        function test_built_in_luts() {
            const f = withClip()
            const p = f.project
            verify(p.chooseLut(p.selectedClip, "qrc" + p.lutLibrary[0].path))
            compare(p.selectedClipInfo.color.lutPath, p.lutLibrary[0].path)
            compare(p.selectedClipInfo.color.lutName, p.lutLibrary[0].name)
            verify(!p.selectedClipInfo.color.lutMissing)
            verify(!p.chooseLut(p.selectedClip, "qrc:/effects/looks/luts/missing.cube"))
        }

        function test_overlays_are_effects() {
            const f = withClip()
            const p = f.project
            click(f.editor, "libraryTab:Effects")
            click(f.editor, "libraryEffect:tve.overlay.light_leak")
            compare(p.undoText, "Add Effect")
            compare(p.selectedClipInfo.effects[0].effectId, "tve.overlay.light_leak")
        }

        function test_adjustment_layer_changes_what_is_below() {
            const f = withClip()
            const p = f.project
            const player = settled(f.editor, "player")
            p.seek(0.5)
            click(f.editor, "libraryTab:Filters")
            click(f.editor, "libraryAdjustmentLayer")
            compare(p.undoText, "Add Adjustment Layer")
            const info = p.selectedClipInfo
            verify(info.isAdjustment)
            compare(info.label, "Adjustment layer")
            const track = p.tracks[0] // a new track on top
            compare(track.clips[0].isAdjustment, true)
            compare(track.clips[0].label, "Adjustment layer")
            compare(track.clips[0].start, 0.5)
            compare(track.clips[0].duration, 5)

            click(f.editor, "inspectorTab:Video")
            verify(settled(f.editor, "adjustmentNote").visible)
            // Grey it: the clip under it turns grey in the player.
            verify(p.setClipColor(p.selectedClip, { saturation: -1 }))
            p.seek(1.0)
            tryVerify(() => {
                for (const fx of [0.3, 0.5, 0.7]) {
                    const c = colorAt(player, fx, 0.5)
                    if (Math.abs(c.r - c.g) > 0.03 || Math.abs(c.g - c.b) > 0.03)
                        return false
                }
                return true
            }, 5000, "the adjustment layer never greyed the picture")
            // Outside it, colour is back.
            p.seek(0.2)
            tryVerify(() => {
                for (const fx of [0.2, 0.4, 0.6, 0.8]) {
                    const c = colorAt(player, fx, 0.5)
                    if (Math.abs(c.r - c.b) > 0.1 || Math.abs(c.g - c.b) > 0.1)
                        return true
                }
                return false
            }, 5000, "colour never came back outside the adjustment layer")

            verify(!p.setClipSpeed(p.selectedClip, 2, false))
            click(f.editor, "inspectorTab:Speed")
            verify(!findChild(f.editor, "speedSlider").visible)
        }
    }
}
