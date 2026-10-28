import QtQuick
import QtTest
import TechieVideoEditor.Theme
import TechieVideoEditor.Player
import TechieVideoEditor.Shell

// Colour grading through the real editor: the Color tab, the player, LUTs, copy and paste,
// presets.
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
        name: "Color"
        when: windowShown

        readonly property string av: "file://" + samplesDir + "/test_av_1080p30.mp4"

        function createEditor() {
            const fixture = createTemporaryObject(editorComponent, root)
            verify(fixture)
            waitForRendering(fixture)
            return fixture
        }
        // An editor with two clips of the same video, the first selected, on the Color tab.
        function gradable() {
            const f = createEditor()
            const p = f.project
            compare(p.importMedia([av]), 1)
            verify(p.addClip(p.media[0].id))
            verify(p.addClip(p.media[0].id))
            p.selectedClip = mainClips(p)[0].id
            click(f.editor, "inspectorTab:Color")
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
        function grade(p) { return p.selectedClipInfo.color }

        function test_basic_corrections_reach_the_player() {
            const f = gradable()
            const p = f.project
            const id = p.selectedClip
            verify(grade(p).neutral)
            verify(!findChild(f.editor, "resetColorButton").enabled)
            p.seek(0.5)
            const player = settled(f.editor, "player")

            // Previews don't touch the history.
            const before = p.undoText
            p.previewClipColor(id, { saturation: -1 })
            compare(p.undoText, before)

            verify(p.setClipColor(id, { saturation: -1 }))
            compare(p.undoText, "Change Color")
            compare(grade(p).saturation, -1)
            verify(!grade(p).neutral)
            compare(findChild(f.editor, "grade:saturation").value, -1)
            // Without colour the picture is grey everywhere.
            tryVerify(() => {
                for (const fx of [0.3, 0.5, 0.7]) {
                    const c = colorAt(player, fx, 0.5)
                    if (Math.abs(c.r - c.g) > 0.03 || Math.abs(c.g - c.b) > 0.03)
                        return false
                }
                return true
            }, 5000, "the grade never reached the player")

            verify(!p.setClipColor(id, { exposure: 9 }))
            verify(p.lastError.indexOf("exposure") >= 0)

            click(f.editor, "resetColorButton")
            compare(p.undoText, "Reset Color")
            verify(grade(p).neutral)
            p.undo()
            compare(grade(p).saturation, -1)
        }

        function test_wheels() {
            const f = gradable()
            const p = f.project
            const disk = settled(f.editor, "wheel:gain:disk")
            scrollIntoView(disk)
            const d = settled(f.editor, "wheel:gain:disk")
            // Drag the puck to the right edge: towards red.
            mousePress(d, d.width / 2, d.height / 2)
            mouseMove(d, d.width * 0.75, d.height / 2)
            mouseMove(d, d.width + 20, d.height / 2)
            mouseRelease(d, d.width + 20, d.height / 2)
            compare(p.undoText, "Change Color")
            fuzzyCompare(grade(p).gain.x, 1, 1e-6)
            fuzzyCompare(grade(p).gain.y, 0, 1e-6)
            // Double-click centres it again.
            mouseDoubleClickSequence(d, d.width / 2, d.height / 4)
            compare(grade(p).gain.x, 0)
            compare(grade(p).gain.y, 0)
            verify(p.setClipColor(p.selectedClip, { lift: { x: 0, y: 0, master: 0.3 } }))
            fuzzyCompare(findChild(f.editor, "wheel:lift:master").value, 0.3, 1e-6)
        }

        function test_tone_curves() {
            const f = gradable()
            const p = f.project
            click(f.editor, "curveChannel:red")
            const editor = settled(f.editor, "toneCurve")
            scrollIntoView(editor)
            const e = settled(f.editor, "toneCurve")
            // Double-click adds a point; the curve gets its two ends too.
            mouseDoubleClickSequence(e, e.px(0.5), e.py(0.7))
            compare(p.undoText, "Change Color")
            compare(grade(p).red.length, 3)
            fuzzyCompare(grade(p).red[1].x, 0.5, 0.02)
            fuzzyCompare(grade(p).red[1].y, 0.7, 0.02)
            compare(grade(p).master.length, 0)
            // Drag it down.
            const handle = settled(f.editor, "curvePoint:1")
            const at = handle.mapToItem(e, handle.width / 2, handle.height / 2)
            mousePress(e, at.x, at.y)
            mouseMove(e, at.x, (at.y + e.py(0.3)) / 2)
            mouseMove(e, at.x, e.py(0.3))
            mouseRelease(e, at.x, e.py(0.3))
            fuzzyCompare(grade(p).red[1].y, 0.3, 0.02)
            // The controller samples it as the grade reads it.
            const samples = p.sampleColorCurve(grade(p).red, 3)
            fuzzyCompare(samples[1], grade(p).red[1].y, 0.02)
            click(f.editor, "resetCurveButton")
            compare(grade(p).red.length, 0)
            verify(!p.setClipColor(p.selectedClip, { blue: [{ x: 0.2, y: 0 }, { x: 1, y: 1 }] }))
        }

        function test_lut() {
            const f = gradable()
            const p = f.project
            const id = p.selectedClip
            compare(findChild(f.editor, "lutName").text, "No LUT. Choose a .cube or .3dl file.")
            verify(!p.chooseLut(id, "file://" + fixturesDir + "/broken.cube"))
            verify(p.lastError.indexOf("the table has 1 rows") >= 0)
            verify(p.chooseLut(id, "file://" + fixturesDir + "/swap.cube"))
            compare(p.undoText, "Apply LUT")
            compare(grade(p).lutName, "swap.cube")
            verify(!grade(p).lutMissing)
            compare(grade(p).lutIntensity, 1)
            verify(settled(f.editor, "grade:lutIntensity").visible)
            verify(p.setClipColor(id, { lutIntensity: 0.4 }))
            compare(grade(p).lutIntensity, 0.4)
            click(f.editor, "removeLutButton")
            compare(p.undoText, "Remove LUT")
            compare(grade(p).lutName, "")
            verify(!p.removeLut(id))
        }

        function test_copy_and_paste_a_grade() {
            const f = gradable()
            const p = f.project
            const [first, second] = mainClips(p).map((c) => c.id)
            verify(!findChild(f.editor, "pasteGradeButton").enabled)
            verify(p.setClipColor(first, { temperature: 0.6, red: [{ x: 0, y: 0 }, { x: 0.5, y: 0.6 }, { x: 1, y: 1 }] }))
            click(f.editor, "copyGradeButton")
            verify(p.hasCopiedGrade)
            p.selectedClip = second
            verify(grade(p).neutral)
            click(f.editor, "pasteGradeButton")
            compare(p.undoText, "Paste Grade")
            compare(grade(p).temperature, 0.6)
            compare(grade(p).red.length, 3)
        }

        function test_presets() {
            const f = gradable()
            const p = f.project
            const [first, second] = mainClips(p).map((c) => c.id)
            verify(p.setClipColor(first, { contrast: 0.25, gamma: { x: 0.2, y: 0.1, master: -0.1 } }))
            const name = settled(f.editor, "colorPresetName")
            scrollIntoView(name)
            name.text = "Teal & warm"
            click(f.editor, "saveColorPreset")
            verify(p.colorPresets.indexOf("Teal & warm") >= 0)
            compare(name.text, "")
            compare(findChild(f.editor, "colorPresetPicker").currentText, "Teal & warm")

            p.selectedClip = second
            click(f.editor, "applyColorPreset")
            compare(p.undoText, "Apply Color Preset")
            compare(grade(p).contrast, 0.25)
            fuzzyCompare(grade(p).gamma.x, 0.2, 1e-9)

            verify(!p.applyColorPreset(second, "Nothing like this"))
            verify(!p.saveColorPreset(second, "   "))
            click(f.editor, "deleteColorPreset")
            verify(p.colorPresets.indexOf("Teal & warm") < 0)
        }

        function test_titles_and_sound_have_no_grade() {
            const f = createEditor()
            const p = f.project
            verify(p.addTitle({ text: "Hi" }, -1, 0))
            click(f.editor, "inspectorTab:Color")
            verify(!findChild(f.editor, "copyGradeButton").visible)
            verify(!p.setClipColor(p.selectedClip, { exposure: 1 }))
        }
    }
}
