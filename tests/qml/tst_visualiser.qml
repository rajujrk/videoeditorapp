import QtQuick
import QtTest
import TechieVideoEditor.Theme
import TechieVideoEditor.Player
import TechieVideoEditor.Shell

// Audio visualisers: added from the Titles library over the music at the playhead, measured from
// its spectrum, and seen in the player's pixels.
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
        name: "Visualiser"
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
            }, 5000, name + " never settled")
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
        // The player's pixels over the canvas matching `test`: their bounds and count.
        function scan(f, test) {
            const overlays = ["transformOverlay", "guidesOverlay"].map((n) => findChild(f.editor, n))
            overlays.forEach((o) => o.opacity = 0)
            const image = grabImage(root)
            overlays.forEach((o) => o.opacity = 1)
            const o = overlays[0], r = o.canvasRect
            const scale = image.width / root.width
            const result = { minX: 1, maxX: 0, minY: 1, maxY: 0, count: 0 }
            for (let v = 0.01; v < 1; v += 0.01) {
                for (let u = 0.005; u < 1; u += 0.005) {
                    const p = o.mapToItem(root, r.x + u * r.width, r.y + v * r.height)
                    const c = image.pixel(Math.round(p.x * scale), Math.round(p.y * scale))
                    if (!test(c))
                        continue
                    result.minX = Math.min(result.minX, u); result.maxX = Math.max(result.maxX, u)
                    result.minY = Math.min(result.minY, v); result.maxY = Math.max(result.maxY, v)
                    result.count++
                }
            }
            return result
        }
        function red(c) { return c.r > 0.6 && c.g < 0.3 && c.b < 0.4 }
        function blue(c) { return c.b > 0.6 && c.r < 0.4 && c.g < 0.3 }
        function at(f, seconds) {
            f.project.seek(seconds)
            waitForRendering(f.editor)
            wait(150)
        }
        // A 220 Hz hum for 7 s on the audio track, from 0.
        function withMusic(f) {
            const p = f.project
            compare(p.importMedia(["file://" + analysisMediaDir + "/hum.wav"]), 1)
            const audio = p.tracks.find((t) => t.kind === "audio")
            verify(p.addClip(p.media[0].id, audio.id, 0))
            return audio.clips[0]
        }

        function test_added_over_the_music_and_moving_with_it() {
            const f = createEditor()
            const p = f.project
            withMusic(f)
            p.seek(1)
            click(f.editor, "libraryTab:Titles")
            click(f.editor, "titleCategory:visualisers")
            for (const name of ["Spectrum Bars", "Mirror Bars", "Sound Wave", "Ring"])
                verify(settled(f.editor, "libraryTitle:" + name))
            click(f.editor, "libraryTitle:Spectrum Bars")
            tryVerify(() => finished.count === 1, 20000, "the music was never measured")
            compare(finished.signalArguments[0][0], true)
            // As long as the music, measured all through: one step.
            const info = p.selectedClipInfo
            verify(info.isTitle)
            compare(info.start, 0)
            fuzzyCompare(info.duration, 7, 0.05)
            verify(info.text.visualiser.measured)
            fuzzyCompare(info.text.visualiser.seconds, 7, 0.05)
            compare(p.undoText, "Add Title")
            const id = p.selectedClip

            // Blue at rest, red at the loudest: the hum's band (low, so on the left) is red.
            verify(p.setClipText(id, { visualiser: { color: [0, 0, 1, 1], peak: [1, 0, 0, 1] } }))
            at(f, 2)
            let hit = null
            tryVerify(() => { hit = scan(f, red); return hit.count > 20 }, 5000, "no loud band")
            verify(hit.maxX < 0.5)
            verify(scan(f, blue).count > 20) // the quiet bands
            // Round a ring: on both sides of the middle.
            verify(p.setClipText(id, { visualiser: { style: "ring" } }))
            tryVerify(() => { hit = scan(f, red); return hit.count > 10 && hit.minX < 0.5 && hit.maxX > 0.5 }, 5000, "no ring")

            // The inspector: style, listen again, and back to plain text.
            click(f.editor, "inspectorTab:Video")
            click(f.editor, "titleVisualiserStyle:wave")
            compare(p.selectedClipInfo.text.visualiser.style, "wave")
            click(f.editor, "titleVisualiserListen")
            tryVerify(() => finished.count === 2, 20000, "never measured again")
            compare(p.undoText, "Measure the Music")
            click(f.editor, "titleRemoveVisualiser")
            verify(!p.selectedClipInfo.text.visualiser)
            verify(!p.setClipText(id, { visualiser: { size: 2 } }))
        }

        function test_without_music() {
            const f = createEditor()
            const p = f.project
            withMusic(f)
            // No music at the playhead: nothing to follow.
            p.seek(20)
            verify(!p.addVisualiser({ visualiser: { style: "bars" } }))
            compare(p.lastError, "Put the playhead on music first")
            // Dropped where there is none: a flat line, waiting for music.
            verify(p.addTitle({ text: "", visualiser: { style: "bars", color: [0, 0, 1, 1], peak: [1, 0, 0, 1] } }, -1, 20))
            compare(finished.count, 0)
            verify(!p.selectedClipInfo.text.visualiser.measured)
            at(f, 21)
            tryVerify(() => scan(f, blue).count > 10, 5000, "no line")
            compare(scan(f, red).count, 0)
            verify(!p.measureVisualiser(p.selectedClip))
            compare(p.lastError, "Put music under the title first")
            // Dropped over music: it listens.
            verify(p.addTitle({ text: "", visualiser: { style: "mirror" } }, -1, 1))
            tryVerify(() => finished.count === 1, 20000, "the music was never measured")
            verify(p.selectedClipInfo.text.visualiser.measured)
        }
    }
}
