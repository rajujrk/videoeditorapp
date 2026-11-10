import QtQuick
import QtTest
import TechieVideoEditor.Theme
import TechieVideoEditor.Player
import TechieVideoEditor.Shell

// Colour management and HDR through the real editor: the project's colour (one undo step),
// SDR media looking the same in an HDR project, the scopes in nits, an HDR export (10-bit
// HEVC PQ) that imports back as HDR and plays tone-mapped in an SDR project, the export
// dialog's colour note, and the HDR viewer following the player in a window of its own.
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

    SignalSpy { id: finished; signalName: "exportFinished" }

    TestCase {
        id: testCase
        name: "Hdr"
        when: windowShown

        readonly property string av: "file://" + samplesDir + "/test_av_1080p30.mp4"
        readonly property var points: [[0.2, 0.3], [0.5, 0.5], [0.8, 0.3], [0.3, 0.8], [0.7, 0.7]]

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
            finished.clear()
            finished.target = fixture.project
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
        // The player's colours at the test points, as [[r, g, b] 0..255].
        function colours(f) {
            wait(400)
            waitForRendering(root)
            const overlay = findChild(f.editor, "maskOverlay")
            const r = overlay.canvasRect
            const image = grabImage(root)
            const scale = image.width / root.width
            return points.map(([u, v]) => {
                const p = overlay.mapToItem(root, r.x + u * r.width, r.y + v * r.height)
                const c = image.pixel(Math.round(p.x * scale), Math.round(p.y * scale))
                return [c.r * 255, c.g * 255, c.b * 255]
            })
        }
        // Mean difference of two colours() over every point and channel.
        function difference(a, b) {
            let sum = 0
            for (let i = 0; i < a.length; ++i)
                for (let c = 0; c < 3; ++c)
                    sum += Math.abs(a[i][c] - b[i][c])
            return sum / (a.length * 3)
        }
        // What a fresh import finds in a file, or null.
        function probe(url) {
            const p = Qt.createQmlObject('import TechieVideoEditor.Player; ProjectController {}', testCase)
            const found = p.importMedia([url]) === 1 ? p.media[0] : null
            p.destroy()
            return found
        }

        function test_the_project_colour_is_one_undo_step_and_scales_the_scopes() {
            const f = createEditor()
            const p = f.project
            verify(p.importAndAppend(av))
            compare(p.projectColour, "sdr")
            click(f.editor, "inspectorTab:Color")
            verify(settled(f.editor, "projectColourNote").text.indexOf("Rec.709") === 0)
            const scope = settled(f.editor, "scopeView")
            compare(scope.colour, "sdr")
            compare(scope.scale.map((m) => m.label), ["0", "25", "50", "75", "100"])

            click(f.editor, "projectColour:hdr-pq")
            compare(p.projectColour, "hdr-pq")
            compare(p.undoText, "Change Project Colour")
            verify(findChild(f.editor, "projectColourNote").text.indexOf("Rec.2020 PQ") === 0)
            // Scopes in nits, where PQ puts them: 203 nits a little above the middle.
            compare(scope.colour, "hdr-pq")
            compare(scope.scale.map((m) => m.label), ["0", "10", "100", "203", "1000", "4000", "10k"])
            fuzzyCompare(scope.scale.find((m) => m.label === "203").level, 0.58, 0.01)
            fuzzyCompare(scope.scale.find((m) => m.label === "1000").level, 0.75, 0.01)
            const frames = scope.frames
            p.seek(0.6)
            tryVerify(() => scope.frames > frames, 5000, "the scopes never saw the HDR picture")

            click(f.editor, "projectColour:hdr-hlg")
            compare(p.projectColour, "hdr-hlg")
            fuzzyCompare(scope.scale.find((m) => m.label === "203").level, 0.75, 0.01) // HLG's graphics white
            compare(scope.scale[scope.scale.length - 1].label, "1000")
            p.undo()
            compare(p.projectColour, "hdr-pq")
            p.undo()
            compare(p.projectColour, "sdr")
        }

        function test_sdr_media_looks_the_same_in_an_hdr_project() {
            const f = createEditor()
            const p = f.project
            verify(p.importAndAppend(av))
            p.seek(0.5)
            const sdr = colours(f)
            for (const colour of ["hdr-pq", "hdr-hlg"]) {
                p.projectColour = colour
                p.seek(0.5)
                // Into Rec.2020 PQ or HLG at graphics white, and back for the SDR player.
                let now = colours(f)
                for (let i = 0; i < 8 && difference(now, sdr) > 8; ++i)
                    now = colours(f)
                verify(difference(now, sdr) <= 8, colour + ": " + JSON.stringify(now) + " vs " + JSON.stringify(sdr))
            }
        }

        function test_an_hdr_export_is_hdr_and_plays_tone_mapped_in_an_sdr_project() {
            const f = createEditor()
            const p = f.project
            verify(p.importAndAppend(av))
            p.seek(0.5)
            const sdr = colours(f)
            p.projectColour = "hdr-pq"

            // The dialog says what HEVC and H.264 do with it.
            click(f.editor, "exportButton")
            const dialog = findChild(f.editor, "exportDialog")
            tryVerify(() => dialog.opened)
            const content = dialog.contentItem
            const formats = p.exportFormats
            const picker = findChild(content, "exportFormat")
            picker.currentIndex = formats.findIndex((x) => x.id === "mp4-hevc")
            tryVerify(() => findChild(content, "exportColourNote").text.indexOf("10-bit HEVC, Rec.2020 PQ") >= 0)
            verify(findChild(content, "exportHdr").visible)
            compare(dialog.options.hdr, true)
            picker.currentIndex = formats.findIndex((x) => x.id === "mp4-h264")
            tryVerify(() => findChild(content, "exportColourNote").text.indexOf("tone-mapped") >= 0)
            verify(!findChild(content, "exportHdr").visible)
            dialog.close()

            const file = "file://" + tempDir + "/tve_hdr_export.mp4"
            // (Full size and quality: the test pattern's saturated bars, squeezed small, would
            // compare its codec's chroma, not the colour management.)
            verify(p.exportVideo(file, { format: "mp4-hevc", quality: "high", range: "custom", from: 0, to: 1 }))
            finished.wait(60000)
            compare(finished.signalArguments[0][0], true, finished.signalArguments[0][1])
            const media = probe(file)
            verify(media)
            compare(media.hdr, true)
            compare(media.colour, "HDR · PQ")

            // Back in an SDR project: tone-mapped, about as it was (8-bit SDR → 10-bit PQ → SDR).
            const g = createEditor()
            verify(g.project.importAndAppend(file))
            compare(g.project.media[0].hdr, true)
            g.project.seek(0.5)
            let back = colours(g)
            for (let i = 0; i < 8 && difference(back, sdr) > 12; ++i)
                back = colours(g)
            verify(difference(back, sdr) <= 12, JSON.stringify(back) + " vs " + JSON.stringify(sdr))
            verify(settled(g.editor, "hdrBadge:" + g.project.media[0].name).visible)
        }

        function test_the_hdr_viewer_follows_the_player_in_a_window_of_its_own() {
            const f = createEditor()
            const p = f.project
            verify(p.importAndAppend(av))
            p.projectColour = "hdr-pq"
            click(f.editor, "hdrViewerButton")
            const button = findChild(f.editor, "hdrViewerButton")
            verify(button.checked)
            let viewer = null
            tryVerify(() => {
                viewer = f.editor.hdrViewer || null
                return viewer !== null && viewer.visible
            }, 5000, "the HDR viewer never opened")
            const view = findChild(viewer.contentItem, "hdrView")
            verify(view)
            compare(view.follows, f.editor.player)
            compare(view.hdrOutput, true)
            verify(view.displayHeadroom >= 1)
            const status = findChild(viewer.contentItem, "hdrViewerStatus")
            verify(view.hdrActive ? status.text.indexOf("HDR · PQ") === 0 : status.text.indexOf("This display shows SDR") === 0,
                   status.text)

            // The player's frames, by its clock.
            p.togglePlay()
            tryVerify(() => view.presentFps > 5, 8000, "the viewer never showed the playing frames")
            p.togglePlay()

            // Closed with the button (or the window's own close).
            click(f.editor, "hdrViewerButton")
            tryVerify(() => !f.editor.hdrViewer, 3000, "the viewer stayed open")
        }
    }
}
