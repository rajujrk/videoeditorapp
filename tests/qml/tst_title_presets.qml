import QtQuick
import QtTest
import TechieVideoEditor.Theme
import TechieVideoEditor.Player
import TechieVideoEditor.Shell

// Title presets of the user's own, titles that move with the music, charts and logo reveals:
// through the library and inspector, and seen in the player's pixels.
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
            ProjectController { id: controller; view: screen.player; autosaveEnabled: false }
            EditorScreen { id: screen; anchors.fill: parent; project: controller }
        }
    }

    SignalSpy { id: finished; signalName: "analysisFinished" }

    TestCase {
        id: testCase
        name: "TitlePresets"
        when: windowShown

        function createEditor() {
            const fixture = createTemporaryObject(editorComponent, root)
            verify(fixture)
            waitForRendering(fixture)
            finished.clear()
            finished.target = fixture.project
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
        function click(parent, name, button) {
            scrollIntoView(settled(parent, name))
            const item = settled(parent, name)
            mouseMove(item)
            mouseClick(item, item.width / 2, item.height / 2, button === undefined ? Qt.LeftButton : button)
        }
        function clips(p) {
            let all = []
            for (const track of p.tracks)
                all = all.concat(track.clips)
            return all
        }
        // The player's pixels over the canvas matching `test`: their bounds and count (canvas
        // fractions).
        function scan(f, test) {
            const overlays = ["transformOverlay", "guidesOverlay"].map((n) => findChild(f.editor, n))
            overlays.forEach((o) => o.opacity = 0)
            const image = grabImage(root)
            overlays.forEach((o) => o.opacity = 1)
            const o = overlays[0], r = o.canvasRect
            const scale = image.width / root.width
            const result = { minX: 1, maxX: 0, minY: 1, maxY: 0, count: 0, left: 0, right: 0 }
            for (let v = 0.01; v < 1; v += 0.01) {
                for (let u = 0.005; u < 1; u += 0.005) {
                    const p = o.mapToItem(root, r.x + u * r.width, r.y + v * r.height)
                    const c = image.pixel(Math.round(p.x * scale), Math.round(p.y * scale))
                    if (!test(c))
                        continue
                    result.minX = Math.min(result.minX, u); result.maxX = Math.max(result.maxX, u)
                    result.minY = Math.min(result.minY, v); result.maxY = Math.max(result.maxY, v)
                    result.count++
                    if (u < 0.5) result.left++; else result.right++
                }
            }
            return result
        }
        function lit(c) { return c.r + c.g + c.b > 1.2 }
        function green(c) { return c.g > 0.5 && c.r < 0.35 && c.b < 0.35 }
        function red(c) { return c.r > 0.6 && c.g < 0.3 && c.b < 0.3 }
        function blue(c) { return c.b > 0.6 && c.r < 0.3 && c.g < 0.35 }
        function at(f, seconds) {
            f.project.seek(seconds)
            waitForRendering(f.editor)
            wait(150)
        }

        function test_presets_are_saved_applied_and_in_the_library() {
            const f = createEditor()
            const p = f.project
            verify(p.addTitle({ text: "Saved Look", size: 0.12, bold: true, color: [1, 0.2, 0.2, 1], curve: 0.3, shadow: true,
                                y: -0.2 }, -1, 0))
            const first = p.selectedClip
            click(f.editor, "inspectorTab:Video")
            const name = settled(f.editor, "titlePresetName")
            scrollIntoView(name)
            name.text = "Red Arch"
            click(f.editor, "saveTitlePreset")
            compare(p.titlePresets.length, 1)
            const saved = p.titlePresets[0]
            compare(saved.name, "Red Arch")
            compare(saved.style.text, "Saved Look")
            fuzzyCompare(saved.style.curve, 0.3, 1e-6)
            fuzzyCompare(saved.style.y, -0.2, 1e-6)
            compare(saved.style.duration, 5)
            verify(testFiles.exists(p.titlePresetDirectory + "/Red Arch.json"))

            // In the library under My presets: a click adds it, looking the same.
            p.seek(8)
            click(f.editor, "libraryTab:Titles")
            click(f.editor, "titleCategory:mine")
            click(f.editor, "titlePreset:Red Arch")
            verify(p.selectedClip !== first)
            compare(p.selectedClipInfo.text.text, "Saved Look")
            fuzzyCompare(p.selectedClipInfo.text.curve, 0.3, 1e-6)
            fuzzyCompare(p.selectedClipInfo.text.color[1], 0.2, 1e-6)

            // Put on another title, which keeps its own words; one undo step.
            verify(p.addTitle({ text: "Plain" }, -1, 15))
            const plain = p.selectedClip
            click(f.editor, "inspectorTab:Video")
            click(f.editor, "applyTitlePreset")
            compare(p.selectedClipInfo.text.text, "Plain")
            verify(p.selectedClipInfo.text.shadow)
            fuzzyCompare(p.selectedClipInfo.text.curve, 0.3, 1e-6)
            p.undo()
            compare(p.selectedClipInfo.text.curve, 0)
            verify(!p.applyTitlePreset(plain, "Nothing Saved"))

            // Deleted from the tile's menu.
            click(f.editor, "titlePreset:Red Arch", Qt.RightButton)
            click(f.editor, "titlePresetMenu:delete")
            compare(p.titlePresets.length, 0)
            tryVerify(() => !findChild(f.editor, "titlePreset:Red Arch"), 2000, "the deleted preset's tile stayed")
            verify(!testFiles.exists(p.titlePresetDirectory + "/Red Arch.json"))
        }

        function test_charts_grow_draw_and_load_values() {
            const f = createEditor()
            const p = f.project
            verify(p.addTitle({ text: "", size: 0.05, color: [1, 1, 1, 1],
                                chart: { kind: "bar", data: "A, 1\nB, 2\nC, 4", color: [0, 0.9, 0, 1], grow: 2, values: false } }, -1, 0))
            const id = p.selectedClip
            compare(p.selectedClipInfo.text.chart.points.length, 3)
            compare(p.selectedClipInfo.text.chart.points[2].label, "C")
            // Growing: hardly any bar at first, all of them later, the last the tallest.
            at(f, 0.1)
            const early = scan(f, green)
            at(f, 3)
            let grown = null
            tryVerify(() => { grown = scan(f, green); return grown.count > early.count * 3 + 50 }, 5000, "the bars never grew")
            verify(grown.right > grown.left)
            // A line: drawn from the left, reaching the right later.
            verify(p.setClipText(id, { chart: { kind: "line" } }))
            at(f, 0.5)
            const part = scan(f, green)
            at(f, 3)
            let whole = null
            tryVerify(() => { whole = scan(f, green); return whole.count > 20 && whole.maxX > part.maxX + 0.1 }, 5000, "the line never drew on")

            // Values from a file (a heading line skipped, quotes taken off).
            const csv = scratchDir + "/values.csv"
            verify(testFiles.write(csv, "month,views\nJan,5\nFeb,10\n\"Mar\",20\n"))
            verify(p.loadChartData(id, "file://" + csv))
            const points = p.selectedClipInfo.text.chart.points
            compare(points.length, 3)
            compare(points[2].label, "Mar")
            compare(points[2].value, 20)
            compare(p.selectedClipInfo.text.chart.kind, "line") // kept
            verify(testFiles.write(csv, "nothing to chart\n"))
            verify(!p.loadChartData(id, "file://" + csv))
            verify(!p.setClipText(id, { chart: { grow: 40 } }))

            // Made from a plain title in the inspector, and back.
            verify(p.addTitle({ text: "Plain" }, -1, 10))
            click(f.editor, "inspectorTab:Video")
            findChild(f.editor, "titleChartSection").expanded = true // (collapsed without a chart)
            click(f.editor, "titleMakeChart")
            compare(p.selectedClipInfo.text.chart.points.length, 3)
            compare(settled(f.editor, "titleChartData").text.split("\n").length, 3)
            click(f.editor, "titleChartKind:line")
            compare(p.selectedClipInfo.text.chart.kind, "line")
            click(f.editor, "titleRemoveChart")
            verify(!p.selectedClipInfo.text.chart)
        }

        function test_titles_move_with_the_music() {
            const f = createEditor()
            const p = f.project
            // Clicks every half second from 0.25 s.
            compare(p.importMedia(["file://" + analysisMediaDir + "/beat.wav"]), 1)
            verify(p.addClip(p.media[0].id))
            // A pulsing title over it listens when added.
            verify(p.addTitle({ text: "HHHH", size: 0.15, fontFamily: "Menlo", bold: true,
                                react: { kind: "pulse", amount: 1 } }, -1, 0))
            const id = p.selectedClip
            tryVerify(() => finished.count >= 1, 20000, "the music was never measured")
            compare(finished.signalArguments[0][0], true)
            tryVerify(() => p.selectedClipInfo.text.react.measured === true, 2000, "no levels")
            compare(p.selectedClipInfo.text.react.kind, "pulse")
            // Bigger just after a beat than between beats.
            at(f, 2.3)
            const hit = scan(f, lit)
            at(f, 2.67)
            const between = scan(f, lit)
            verify(hit.count > 50 && between.count > 50)
            verify(hit.maxX - hit.minX > (between.maxX - between.minX) * 1.08)

            // Another way to move: the levels stay; then none (they go).
            click(f.editor, "inspectorTab:Video")
            verify(settled(f.editor, "titleReactKind"))
            verify(p.setClipText(id, { react: { kind: "bounce" } }))
            verify(p.selectedClipInfo.text.react.measured)
            verify(p.reactToMusic(id, { kind: "none" }))
            verify(!p.selectedClipInfo.text.react.measured)
            compare(p.selectedClipInfo.text.react.kind, "none")
            verify(!p.setClipText(id, { react: { amount: 2 } }))

            // A flash to the highlight colour where the music hits (given levels).
            verify(p.setClipText(id, { highlightColor: [0, 1, 0, 1],
                                       react: { kind: "flash", amount: 1, levels: [1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
                                                                                      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0] } }))
            at(f, 0.3)
            tryVerify(() => scan(f, green).count > 50, 5000, "no flash")
            at(f, 1.1)
            tryVerify(() => scan(f, green).count === 0 && scan(f, lit).count > 50, 5000, "the flash never ended")

            // Without music under it there is nothing to follow.
            verify(p.addTitle({ text: "Alone" }, -1, 30))
            verify(!p.reactToMusic(p.selectedClip, { kind: "pulse" }))
            compare(p.lastError, "Put music under the title first")
        }

        function test_logo_reveals() {
            const f = createEditor()
            const p = f.project
            click(f.editor, "libraryTab:Titles")
            click(f.editor, "titleCategory:logos")
            compare(p.logoReveals.length, 6)
            for (const reveal of p.logoReveals)
                verify(settled(f.editor, "logoReveal:" + reveal.id))
            verify(!p.addLogoReveal("nope", "file://" + stillsDir + "/halves.png", "x"))

            p.seek(0)
            verify(p.addLogoReveal("spin", "file://" + stillsDir + "/halves.png", "Studio North"))
            const logo = p.selectedClipInfo
            verify(logo.groupId > 0)
            compare(logo.motionBlur, 0.5)
            verify(logo.keyframes.opacity.length > 0)
            const group = clips(p).filter((c) => c.groupId === logo.groupId)
            compare(group.length, 2)
            const tagline = group.find((c) => c.isTitle)
            verify(tagline)
            verify(tagline.start > 0.5) // a moment after the logo
            fuzzyCompare(group.find((c) => !c.isTitle).duration, 4, 0.01)

            // Not there at the start, there once it has arrived.
            at(f, 0)
            compare(scan(f, red).count + scan(f, blue).count, 0)
            at(f, 2.5)
            tryVerify(() => scan(f, red).count > 50 && scan(f, blue).count > 50, 5000, "the logo never arrived")
            tryVerify(() => scan(f, lit).maxY > scan(f, blue).maxY, 5000, "no tagline under the logo")

            // One undo step takes both.
            p.undo()
            compare(clips(p).length, 0)
        }
    }
}
