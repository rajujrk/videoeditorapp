import QtQuick
import QtTest
import TechieVideoEditor.Theme
import TechieVideoEditor.Player
import TechieVideoEditor.Shell

// Keyframes through the real editor: inspector, timeline marks, toolbar.
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
        name: "Keyframes"
        when: windowShown

        readonly property string av: "file://" + samplesDir + "/test_av_1080p30.mp4" // 2 s
        readonly property string tone: "file://" + samplesDir + "/test_tone_440.wav"

        function createEditor() {
            const fixture = createTemporaryObject(editorComponent, root)
            verify(fixture)
            waitForRendering(fixture)
            return fixture
        }
        // An editor with one 2 s clip, selected, and the inspector on its Video tab.
        function animatable() {
            const f = createEditor()
            const p = f.project
            compare(p.importMedia([av]), 1)
            verify(p.addClip(p.media[0].id))
            compare(p.selectedClipInfo.trackKind, "video")
            click(f.editor, "inspectorTab:Video")
            return f
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
            // A position can look stable while a layout change is still waiting for its
            // polish pass (e.g. a row above was just hidden); let it run, then look again.
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
        function keys(p, property) { return (p.selectedClipInfo.keyframes || {})[property] || [] }
        function stateOf(p, property) { return (p.selectedClipInfo.keyframeStates || {})[property] || "none" }

        function test_animate_position_from_the_inspector() {
            const f = animatable()
            const p = f.project
            const id = p.selectedClip
            compare(findChild(f.editor, "keyframeButton").text, "Add keyframe")
            verify(!findChild(f.editor, "previousKeyframe").enabled)

            // A keyframe at the start keeps placement and opacity as they are.
            p.seek(0)
            click(f.editor, "keyframeButton")
            compare(p.undoText, "Add Keyframe")
            for (const property of ["x", "y", "scale", "rotation", "opacity"]) {
                compare(keys(p, property).length, 1)
                compare(stateOf(p, property), "key")
            }
            compare(stateOf(p, "cropLeft"), "none")
            compare(findChild(f.editor, "keyframe:x").keyState, "key")
            compare(findChild(f.editor, "keyframeButton").text, "Remove")
            // The section fits the inspector (nothing pushed past its right edge).
            const section = findChild(f.editor, "keyframesSection")
            const page = section.parent
            verify(section.width <= page.width + 0.5, section.width + " > " + page.width)
            const clear = findChild(f.editor, "resetVisualButton")
            verify(clear.mapToItem(page, clear.width, 0).x <= page.width + 0.5)

            // Later, changing an animated value sets a keyframe there.
            p.seek(1.0)
            compare(stateOf(p, "x"), "animated")
            verify(p.setClipVisual(id, { x: 0.5 }))
            compare(p.undoText, "Change Picture")
            compare(keys(p, "x").length, 2)
            compare(keys(p, "x")[1].time, 1.0)
            compare(keys(p, "x")[1].value, 0.5)
            compare(keys(p, "y").length, 1) // only what changed
            compare(stateOf(p, "x"), "key")

            // The inspector shows values as they are at the playhead.
            p.seek(0.5)
            fuzzyCompare(p.selectedClipInfo.visual.x, 0.25, 1e-6)
            p.seek(1.5)
            fuzzyCompare(p.selectedClipInfo.visual.x, 0.5, 1e-6)

            // The timeline marks both moments on the clip.
            settled(f.editor, "keyframeMark:" + id + ":0")
            settled(f.editor, "keyframeMark:" + id + ":1")
            compare(p.tracks.find((t) => t.main).clips[0].keyframeTimes, [0, 1])

            // Step between them.
            click(f.editor, "previousKeyframe")
            compare(p.playhead, 1.0)
            click(f.editor, "previousKeyframe")
            compare(p.playhead, 0)
            verify(!p.seekKeyframe(id, -1))
            click(f.editor, "nextKeyframe")
            compare(p.playhead, 1.0)

            // Undo takes the second keyframe away again.
            p.undo()
            compare(keys(p, "x").length, 1)
        }

        function test_easing_and_hold() {
            const f = animatable()
            const p = f.project
            const id = p.selectedClip
            p.seek(0)
            verify(p.addKeyframes(id, ["opacity"]))
            p.seek(1.0)
            verify(p.setClipVisual(id, { opacity: 0 }))
            p.seek(0.5)
            fuzzyCompare(p.selectedClipInfo.visual.opacity, 0.5, 1e-6)

            p.seek(0)
            const easing = settled(f.editor, "keyframeEasing")
            compare(easing.currentText, "Linear")
            verify(p.setKeyframeEasing(id, "easeIn"))
            compare(p.undoText, "Change Keyframe")
            compare(easing.currentText, "Ease in")
            p.seek(0.5)
            fuzzyCompare(p.selectedClipInfo.visual.opacity, 0.875, 1e-6)
            p.seek(0)
            verify(p.setKeyframeEasing(id, "hold"))
            p.seek(0.9)
            compare(p.selectedClipInfo.visual.opacity, 1)
            verify(!p.setKeyframeEasing(id, "bouncy"))
        }

        function test_one_property_from_its_diamond() {
            const f = animatable()
            const p = f.project
            p.seek(0.5)
            click(f.editor, "keyframe:rotation")
            compare(stateOf(p, "rotation"), "key")
            compare(stateOf(p, "x"), "none")
            compare(keys(p, "rotation")[0].time, 0.5)
            click(f.editor, "keyframe:rotation")
            compare(stateOf(p, "rotation"), "none")
            compare(p.undoText, "Remove Keyframe")
            // Still unanimated values change the static picture.
            verify(p.setClipVisual(p.selectedClip, { rotation: 30 }))
            compare(keys(p, "rotation").length, 0)
            p.seek(1.5)
            compare(p.selectedClipInfo.visual.rotation, 30)
        }

        function test_removing_the_last_keyframe_keeps_its_value() {
            const f = animatable()
            const p = f.project
            const id = p.selectedClip
            p.seek(1.0)
            verify(p.addKeyframes(id, ["scale"]))
            verify(p.setClipVisual(id, { scale: 2 }))
            verify(p.removeKeyframes(id, ["scale"]))
            compare(keys(p, "scale").length, 0)
            p.seek(0)
            compare(p.selectedClipInfo.visual.scale, 2)
            verify(!p.removeKeyframes(id))
            verify(p.lastError.indexOf("No keyframe") >= 0)
        }

        function test_clear_keeps_values_at_the_playhead() {
            const f = animatable()
            const p = f.project
            const id = p.selectedClip
            p.seek(0)
            verify(p.addKeyframes(id))
            p.seek(1.0)
            verify(p.setClipVisual(id, { x: 0.4, opacity: 0.2 }))
            p.seek(0.5)
            click(f.editor, "clearKeyframesButton")
            compare(p.undoText, "Clear Keyframes")
            compare(Object.keys(p.selectedClipInfo.keyframes).length, 0)
            fuzzyCompare(p.selectedClipInfo.visual.x, 0.2, 1e-6)
            fuzzyCompare(p.selectedClipInfo.visual.opacity, 0.6, 1e-6)
            verify(!findChild(f.editor, "clearKeyframesButton").visible)
        }

        function test_drag_a_keyframe_along_the_clip() {
            const f = animatable()
            const p = f.project
            const id = p.selectedClip
            p.seek(0.5)
            verify(p.addKeyframes(id, ["x"]))
            const mark = settled(f.editor, "keyframeMark:" + id + ":0")
            // In the clip's coordinates: the mark follows the mouse while dragged.
            const clipItem = findChild(f.editor, "clip:" + id)
            const at = mark.mapToItem(clipItem, mark.width / 2, mark.height / 2)
            mousePress(clipItem, at.x, at.y)
            mouseMove(clipItem, at.x + 15, at.y)
            mouseMove(clipItem, at.x + 30, at.y)
            mouseRelease(clipItem, at.x + 30, at.y)
            compare(p.undoText, "Move Keyframe")
            fuzzyCompare(keys(p, "x")[0].time, 1.0, 1 / 30 + 1e-6) // half a second at 60 px/s, on a frame
            fuzzyCompare(p.playhead, keys(p, "x")[0].time, 1e-6)

            // A click goes to the keyframe.
            p.seek(0)
            const moved = settled(f.editor, "keyframeMark:" + id + ":0")
            mouseClick(moved)
            fuzzyCompare(p.playhead, keys(p, "x")[0].time, 1e-6)
        }

        function test_keyframe_tool_button() {
            const f = createEditor()
            const p = f.project
            const button = findChild(f.editor, "keyframeToolButton")
            verify(!button.enabled)
            compare(p.importMedia([av]), 1)
            verify(p.addClip(p.media[0].id))
            verify(button.enabled)
            p.seek(0.25)
            mouseMove(button)
            mouseClick(button)
            verify(button.checked)
            compare(keys(p, "x").length, 1)
            mouseClick(button)
            verify(!button.checked)
            compare(keys(p, "x").length, 0)
        }

        function test_animate_volume() {
            const f = createEditor()
            const p = f.project
            compare(p.importMedia([tone]), 1)
            verify(p.addClip(p.media[0].id))
            compare(p.selectedClipInfo.trackKind, "audio")
            click(f.editor, "inspectorTab:Audio")
            p.seek(0)
            click(f.editor, "keyframe:volume")
            compare(stateOf(p, "volume"), "key")
            p.seek(2.0)
            verify(p.setClipAudio(p.selectedClip, -20, 0, 0))
            compare(p.undoText, "Change Audio")
            compare(keys(p, "volume").length, 2)
            p.seek(1.0)
            fuzzyCompare(p.selectedClipInfo.gainDb, -10, 1e-4)
            // Picture properties do not apply to sound.
            verify(!p.addKeyframes(p.selectedClip, ["opacity"]))
        }
    }
}
