import QtQuick
import QtTest
import TechieVideoEditor.Theme
import TechieVideoEditor.Player
import TechieVideoEditor.Shell

// Timelines with other editors through the real editor: the Export timeline dialog writing
// Final Cut Pro XML, OpenTimelineIO and an EDL with its report of what didn't carry over, and
// opening those timelines back as new projects (their media probed, the report shown).
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
        name: "Interchange"
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
        function settled(parentItem, name) {
            let item = null
            let last = null
            tryVerify(() => {
                item = findChild(parentItem, name)
                if (!item || !item.visible)
                    return false
                const p = item.mapToItem(root, 0, 0)
                const stable = last !== null && p.x === last.x && p.y === last.y
                last = p
                return stable
            }, 3000, name + " never settled")
            return item
        }
        function click(parentItem, name) {
            const item = settled(parentItem, name)
            mouseMove(item)
            mouseClick(item)
        }
        // A short edit: the clip, a title over it, a marker, and a grade (which stays here).
        function edit(p) {
            verify(p.importAndAppend(av))
            const clip = p.tracks.find((t) => t.main).clips[0].id
            verify(p.addTitle({ text: "Opening", size: 0.1 }, -1, 0.5))
            p.seek(1.0)
            verify(p.addMarker(1.0, "Beat") !== 0)
            verify(p.setClipColor(clip, { exposure: 0.4 }))
            return clip
        }
        function notesOf(dialog) {
            const out = []
            for (let i = 0; i < dialog.notes.length; ++i)
                out.push(dialog.notes[i])
            return out.join("\n")
        }

        function test_a_timeline_goes_to_final_cut_and_comes_back() {
            const f = createEditor()
            const p = f.project
            edit(p)
            click(f.editor, "interchangeButton")
            const dialog = findChild(f.editor, "interchangeDialog")
            tryVerify(() => dialog.opened)
            const content = dialog.contentItem
            compare(dialog.format, "otio")
            click(content, "interchangeFormat:fcpxml")
            compare(dialog.format, "fcpxml")
            verify(String(dialog.file).endsWith(".fcpxml"))
            verify(findChild(content, "interchangeFormatNote").text.indexOf("Final Cut Pro") >= 0)
            dialog.file = "file://" + tempDir + "/tve_interchange.fcpxml"
            click(content, "interchangeExportButton")
            compare(dialog.stage, "done")
            compare(findChild(content, "interchangeTitle").text, "Timeline exported")
            const report = notesOf(dialog)
            verify(report.indexOf("Colour grades and looks — 1 clip") >= 0, report)
            verify(report.indexOf("Titles' look and animation") >= 0, report)
            verify(settled(content, "interchangeNote:0").text.indexOf("• ") === 0)
            click(content, "interchangeClose")
            tryVerify(() => !dialog.opened)

            // Opened back: a new project, its media probed, the report for it.
            verify(p.open("file://" + tempDir + "/tve_interchange.fcpxml"))
            compare(p.name, "Untitled project")
            compare(p.modified, false)
            tryVerify(() => dialog.opened)
            compare(dialog.stage, "opened")
            compare(findChild(dialog.contentItem, "interchangeTitle").text, "Opened tve_interchange.fcpxml")
            compare(p.media.length, 1)
            compare(p.media[0].width, 1920)
            fuzzyCompare(p.media[0].duration, 2.0, 0.05)
            const main = p.tracks.find((t) => t.main)
            compare(main.clips.length, 1)
            fuzzyCompare(main.clips[0].duration, 2.0, 0.05)
            const titles = []
            for (const t of p.tracks.filter((t) => t.kind === "video" && !t.main))
                for (const c of t.clips)
                    titles.push(c)
            compare(titles.length, 1)
            fuzzyCompare(titles[0].start, 0.5, 0.05)
            compare(p.markers.length, 1)
            fuzzyCompare(p.markers[0].time, 1.0, 0.05)
            compare(p.markers[0].note, "Beat")
            compare([p.canvasWidth, p.canvasHeight], [1920, 1080])
        }

        function test_opentimelineio_and_an_edl() {
            const f = createEditor()
            const p = f.project
            edit(p)
            const otio = "file://" + tempDir + "/tve_interchange.otio"
            const result = p.exportTimeline(otio)
            verify(result.ok)
            verify(result.notes.length > 0)
            verify(p.open(otio))
            const main = p.tracks.find((t) => t.main)
            compare(main.clips.length, 1)
            compare(main.clips[0].audioEnabled !== false, true) // its own sound, not doubled on an audio track
            verify(p.tracks.filter((t) => t.kind === "audio").every((t) => t.clips.length === 0))
            compare(p.media[0].hasAudio, true)

            // An EDL goes out, but doesn't come back in.
            const edl = "file://" + tempDir + "/tve_interchange.edl"
            const g = createEditor()
            edit(g.project)
            verify(g.project.exportTimeline(edl, "edl").ok)
            verify(!g.project.open(edl))
            verify(g.project.lastError.indexOf("isn't a timeline this app can open") >= 0, g.project.lastError)
            // Nothing to export from an empty timeline.
            const h = createEditor()
            verify(!h.project.exportTimeline("file://" + tempDir + "/tve_empty.otio").ok)
            verify(h.project.lastError.indexOf("Add something") === 0)
        }
    }
}
