import QtQuick
import QtTest
import TechieVideoEditor.Theme
import TechieVideoEditor.Player
import TechieVideoEditor.Shell

// Speed, reverse, freeze frame and thumbnails through the real editor UI.
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
        name: "Speed"
        when: windowShown

        readonly property string av: "file://" + samplesDir + "/test_av_1080p30.mp4"

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
        function mainClips(project) { return project.tracks.find((t) => t.main).clips }

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

        function click(editor, name) {
            scrollIntoView(settled(editor, name))
            const item = settled(editor, name)
            mouseMove(item)
            mouseClick(item)
        }

        function openSpeed(f) {
            const p = f.project
            p.importAndAppend(av)
            p.selectedClip = mainClips(p)[0].id
            click(f.editor, "inspectorTab:Speed")
        }

        function test_preset_changes_length_and_undoes() {
            const f = createEditor()
            const p = f.project
            openSpeed(f)
            const before = mainClips(p)[0].duration
            click(f.editor, "speedPreset:2")
            compare(p.selectedClipInfo.speed, 2)
            fuzzyCompare(mainClips(p)[0].duration, before / 2, 0.001)
            compare(p.undoText, "Change Speed")
            p.undo()
            compare(p.selectedClipInfo.speed, 1)
            fuzzyCompare(mainClips(p)[0].duration, before, 0.001)
        }

        function test_reverse_switch() {
            const f = createEditor()
            const p = f.project
            openSpeed(f)
            click(f.editor, "reverseSwitch")
            compare(p.selectedClipInfo.reversed, true)
            compare(mainClips(p)[0].reversed, true)
            compare(p.undoText, "Change Speed")
        }

        function test_speed_is_clamped() {
            const f = createEditor()
            const p = f.project
            openSpeed(f)
            verify(p.setClipSpeed(p.selectedClip, 50, false))
            compare(p.selectedClipInfo.speed, 10)
        }

        function test_freeze_button_inserts_a_still() {
            const f = createEditor()
            const p = f.project
            openSpeed(f)
            const before = mainClips(p)[0].duration
            p.seek(1)
            click(f.editor, "freezeFrameButton")
            const clips = mainClips(p)
            compare(clips.length, 3)
            compare(clips[1].freeze, true)
            fuzzyCompare(clips[1].duration, 2, 0.001)
            fuzzyCompare(clips[0].duration + clips[2].duration, before, 0.001)
            compare(p.selectedClip, clips[1].id)
            compare(p.undoText, "Freeze Frame")
            // A still has no speed controls.
            tryVerify(() => !findChild(f.editor, "speedSlider").visible)
            p.undo()
            compare(mainClips(p).length, 1)
        }

        function test_freeze_needs_a_clip_under_the_playhead() {
            const f = createEditor()
            const p = f.project
            verify(!p.freezeFrame(2))
            verify(p.lastError.indexOf("playhead") >= 0)
        }

        function test_timeline_and_bin_thumbnails_load() {
            const f = createEditor()
            const p = f.project
            p.importAndAppend(av)
            const id = mainClips(p)[0].id
            let thumb = null
            tryVerify(() => (thumb = findChild(f.editor, "thumb:" + id + ":0")) !== null, 3000)
            tryVerify(() => thumb.status === Image.Ready, 5000, "timeline thumbnail never loaded")
            verify(thumb.sourceSize.height > 0)
            let binThumb = null
            tryVerify(() => (binThumb = findChild(f.editor, "mediaThumb:test_av_1080p30.mp4")) !== null, 3000)
            tryVerify(() => binThumb.status === Image.Ready, 5000, "media bin thumbnail never loaded")
        }

        function test_speed_ramp_from_a_preset() {
            const f = createEditor()
            const p = f.project
            openSpeed(f)
            const id = mainClips(p)[0].id
            const footage = mainClips(p)[0].duration // 2 s at 1×
            verify(!findChild(f.editor, "speedRampEditor").visible)

            click(f.editor, "rampPreset:ramp-up") // 0.5× up to 3×, average 1.75×
            compare(p.undoText, "Speed Ramp")
            const clip = mainClips(p)[0]
            compare(clip.speedRamp.length, 2)
            fuzzyCompare(clip.speed, 1.75, 1e-6)
            fuzzyCompare(clip.duration, footage / 1.75, 1e-3)
            verify(settled(f.editor, "speedRampEditor").visible)
            compare(findChild(f.editor, "rampPreset:ramp-up").variant, "primary")
            compare(findChild(f.editor, "speedPreset:2").variant, "secondary")
            // Thumbnails follow the ramp: slow at first, so halfway shows less than half.
            fuzzyCompare(p.clipSourceTime(id, 0), 0, 1e-6)
            verify(p.clipSourceTime(id, clip.duration / 2) < footage / 2)

            // Turning it around keeps the ramp.
            click(f.editor, "reverseSwitch")
            compare(p.undoText, "Change Speed")
            verify(mainClips(p)[0].reversed)
            compare(mainClips(p)[0].speedRamp.length, 2)

            // Back to constant: the average, same length.
            click(f.editor, "removeRampButton")
            compare(p.undoText, "Remove Speed Ramp")
            compare(mainClips(p)[0].speedRamp.length, 0)
            fuzzyCompare(mainClips(p)[0].duration, footage / 1.75, 1e-3)
            p.undo()
            compare(mainClips(p)[0].speedRamp.length, 2)

            // A constant speed replaces it.
            click(f.editor, "speedPreset:2")
            compare(mainClips(p)[0].speedRamp.length, 0)
            fuzzyCompare(mainClips(p)[0].duration, 1, 1e-3)
        }

        function test_edit_ramp_points() {
            const f = createEditor()
            const p = f.project
            openSpeed(f)
            const id = mainClips(p)[0].id
            verify(p.applySpeedRampPreset(id, "bullet-time"))
            const editor = settled(f.editor, "speedRampEditor")
            scrollIntoView(editor)
            const points = () => mainClips(p)[0].speedRamp

            // Drag the third point (slow, 0.25×) up to 1×.
            const handle = settled(f.editor, "rampPoint:2")
            const from = handle.mapToItem(editor, handle.width / 2, handle.height / 2)
            mousePress(editor, from.x, from.y)
            mouseMove(editor, from.x, (from.y + editor.py(1)) / 2)
            mouseMove(editor, from.x, editor.py(1))
            mouseRelease(editor, from.x, editor.py(1))
            compare(p.undoText, "Speed Ramp")
            fuzzyCompare(points()[2].speed, 1, 0.05)
            fuzzyCompare(points()[2].at, 0.4, 0.02)

            // Double-click the graph to add a point on the line.
            const count = points().length
            const x = editor.px(0.55)
            mouseDoubleClickSequence(editor, x, editor.py(0.15))
            compare(points().length, count + 1)
            const added = points().find((pt) => Math.abs(pt.at - 0.55) < 0.02)
            verify(added)

            // Select it and remove it.
            compare(editor.selected, points().findIndex((pt) => Math.abs(pt.at - 0.55) < 0.02))
            click(f.editor, "removeRampPoint")
            compare(points().length, count)
            // The ends cannot be removed.
            editor.selected = 0
            verify(!findChild(f.editor, "removeRampPoint").enabled)

            verify(!p.setClipSpeedRamp(id, [{ at: 0, speed: 1 }, { at: 1, speed: 99 }]))
            verify(p.lastError.indexOf("speed") >= 0)
            verify(!p.applySpeedRampPreset(id, "warp-9"))
        }
    }
}
