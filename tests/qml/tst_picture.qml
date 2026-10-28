import QtQuick
import QtTest
import TechieVideoEditor.Theme
import TechieVideoEditor.Player
import TechieVideoEditor.Shell

// Clip picture settings and per-clip effects through the real editor UI.
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
        name: "Picture"
        when: windowShown

        readonly property string av: "file://" + samplesDir + "/test_av_1080p30.mp4"
        readonly property string mjpeg: "file://" + samplesDir + "/test_720p30_mjpeg.mov"

        function initTestCase() {
            const probe = Qt.createQmlObject('import TechieVideoEditor.Player; ProjectController {}', testCase)
            if (probe.importMedia([av, mjpeg]) !== 2)
                skip("samples missing: run tools/make_test_media.sh")
            probe.destroy()
        }

        function createEditor() {
            const fixture = createTemporaryObject(editorComponent, root)
            verify(fixture)
            waitForRendering(fixture)
            return fixture
        }
        function mainClip(project) { return project.tracks.find((t) => t.main).clips[0] }

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
            // A layout that has not run yet leaves positions stale but "stable"; wait for
            // the layout (polish) pass of the item and everything above it.
            for (let a = item; a && a !== root; a = a.parent)
                verify(waitForPolish(a), "layout never ran for " + name)
            return item
        }

        // Scrolls the nearest scrolling ancestor so `item` is fully in view, like a user would.
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

        // Move there first, like a real cursor: leaving the previous control closes its
        // tooltip, which would otherwise take this click.
        function click(editor, name) {
            scrollIntoView(settled(editor, name))
            const item = settled(editor, name)
            mouseMove(item)
            mouseClick(item)
        }

        function selectFirstClipAndOpen(f, tab) {
            const p = f.project
            p.importAndAppend(av)
            p.selectedClip = mainClip(p).id
            click(f.editor, "inspectorTab:" + tab)
        }

        function test_scale_slider_commits_one_step() {
            const f = createEditor()
            const p = f.project
            selectFirstClipAndOpen(f, "Video")
            scrollIntoView(settled(f.editor, "visual:scale"))
            const slider = settled(f.editor, "visual:scale")
            mouseMove(slider, slider.width * 0.1, slider.height - 8)
            mouseClick(slider, slider.width * 0.1, slider.height - 8)
            const scale = p.selectedClipInfo.visual.scale
            verify(scale < 1 && scale > 0.05)
            compare(p.undoText, "Change Picture")
            p.undo()
            compare(p.selectedClipInfo.visual.scale, 1)
        }

        function test_preview_does_not_touch_history() {
            const f = createEditor()
            const p = f.project
            selectFirstClipAndOpen(f, "Video")
            const before = p.undoText
            p.previewClipVisual(p.selectedClip, { rotation: 45, opacity: 0.3 })
            compare(p.undoText, before)
            compare(p.selectedClipInfo.visual.rotation, 0)
            p.cancelPreview(p.selectedClip)
        }

        function test_values_are_validated() {
            const f = createEditor()
            const p = f.project
            selectFirstClipAndOpen(f, "Video")
            verify(!p.setClipVisual(p.selectedClip, { cropLeft: 0.6, cropRight: 0.6 }))
            verify(p.lastError.indexOf("crop") >= 0)
            verify(p.setClipVisual(p.selectedClip, { x: 0.25, y: -0.1, rotation: 15, blend: "screen" }))
            const v = p.selectedClipInfo.visual
            compare(v.x, 0.25)
            compare(v.blend, "screen")
            verify(p.resetClipVisual(p.selectedClip))
            compare(p.selectedClipInfo.visual.x, 0)
        }

        function test_library_effect_goes_on_the_selected_clip() {
            const f = createEditor()
            const p = f.project
            selectFirstClipAndOpen(f, "Effects")
            click(f.editor, "libraryTab:Effects")
            click(f.editor, "libraryEffect:tve.stylize.vignette")
            compare(p.selectedClipInfo.effects.length, 1)
            compare(p.selectedClipInfo.effects[0].effectId, "tve.stylize.vignette")
            compare(p.undoText, "Add Effect")

            // The inspector shows it; switch it off, then remove it.
            click(f.editor, "clipEffect:0")
            compare(p.selectedClipInfo.effects[0].enabled, false)
            click(f.editor, "removeEffect:0")
            compare(p.selectedClipInfo.effects.length, 0)
        }

        function test_effect_params_commit_on_release() {
            const f = createEditor()
            const p = f.project
            selectFirstClipAndOpen(f, "Effects")
            verify(p.addClipEffect(p.selectedClip, "tve.color.adjust"))
            p.previewClipEffectParam(p.selectedClip, 0, "brightness", 0.4)
            compare(p.undoText, "Add Effect") // preview only
            verify(p.setClipEffectParam(p.selectedClip, 0, "brightness", 0.4))
            compare(p.undoText, "Change Effect")
            fuzzyCompare(p.selectedClipInfo.effects[0].values.brightness, 0.4, 1e-6)
        }

        function test_effects_need_a_video_clip() {
            const f = createEditor()
            const p = f.project
            verify(!p.addClipEffect(0, "tve.color.adjust"))
            p.importAndAppend(mjpeg)
            verify(!p.addClipEffect(mainClip(p).id, "tve.no.such.effect"))
            verify(p.lastError.indexOf("Unknown effect") >= 0)
        }

        function test_first_clip_sets_an_untouched_canvas() {
            const f = createEditor()
            const p = f.project
            p.importAndAppend(mjpeg) // 1280×720 @ 30
            compare(p.canvasWidth, 1280)
            compare(p.canvasHeight, 720)

            // A canvas picked on Home is kept.
            p.newProject(1080, 1920)
            p.importAndAppend(mjpeg)
            compare(p.canvasWidth, 1080)
            compare(p.canvasHeight, 1920)
        }
    }
}
