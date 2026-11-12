import QtQuick
import QtTest
import TechieVideoEditor.Theme
import TechieVideoEditor.Player
import TechieVideoEditor.Shell

// Auto colour, colour match, HDR wheels and lens correction through the real editor.
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

    TestCase {
        id: testCase
        name: "ColourTools"
        when: windowShown

        readonly property string av: "file://" + samplesDir + "/test_av_1080p30.mp4"
        function still(name) { return "file://" + stillsDir + "/" + name }

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
        function mainTrack(p) { return p.tracks.find((t) => t.main) }
        function colourTab(f) {
            findChild(f.editor, "inspectorTab:Color").clicked()
            tryVerify(() => findChild(f.editor, "autoColourSection").visible)
        }

        function test_auto_colour_and_match() {
            const f = createEditor()
            const p = f.project
            verify(p.importAndAppend(av))
            compare(p.importMedia([still("halves.png")]), 1)
            verify(p.addClip(p.media[1].id, mainTrack(p).id, -1))
            const video = mainTrack(p).clips[0].id, picture = mainTrack(p).clips[1].id
            p.selectedClip = video
            p.seek(1)
            colourTab(f)
            findChild(f.editor, "autoColourButton").clicked()
            compare(p.undoText, "Auto Colour")
            const grade = p.selectedClipInfo.color
            verify(grade.red.length >= 2 && grade.green.length >= 2 && grade.blue.length >= 2)
            compare(grade.master.length, 0)
            p.undo()
            compare(p.selectedClipInfo.color.red.length, 0)
            // Match the bars to the red and blue picture: other curves than auto's.
            const reference = findChild(f.editor, "matchReference")
            compare(reference.count, 1)
            findChild(f.editor, "matchColourButton").clicked()
            compare(p.undoText, "Match Colour")
            verify(p.selectedClipInfo.color.green.length >= 2)
            // A clip cannot be matched to itself; titles have no picture to measure.
            verify(!p.matchColour(video, video))
            verify(p.addTitle({}))
            verify(!p.autoColour(p.selectedClip))
            void picture
        }

        function test_hdr_wheels() {
            const f = createEditor()
            const p = f.project
            verify(p.importAndAppend(av))
            p.selectedClip = mainTrack(p).clips[0].id
            colourTab(f)
            for (const key of ["black", "dark", "shadow", "light", "highlight", "specular"])
                verify(findChild(f.editor, "wheel:" + key), key)
            verify(p.setClipColor(p.selectedClip, { specular: { x: 0.2, y: -0.1, master: -0.5 }, black: { x: 0, y: 0, master: 0.2 } }))
            const grade = p.selectedClipInfo.color
            fuzzyCompare(grade.specular.master, -0.5, 1e-6)
            fuzzyCompare(grade.black.master, 0.2, 1e-6)
            compare(grade.shadow.master, 0)
            verify(!p.setClipColor(p.selectedClip, { dark: { x: 0, y: 0, master: 4 } }))
        }

        function test_lens_correction() {
            const f = createEditor()
            const p = f.project
            verify(p.importAndAppend(av))
            p.selectedClip = mainTrack(p).clips[0].id
            findChild(f.editor, "inspectorTab:Video").clicked()
            verify(p.lensPresets().length >= 5)
            const combo = findChild(f.editor, "lensPreset")
            combo.activated(1) // action camera, wide
            compare(p.undoText, "Lens Correction")
            compare(combo.currentIndex, 0)
            const effects = p.selectedClipInfo.effects
            compare(effects[0].effectId, "tve.distort.lens")
            fuzzyCompare(effects[0].values.amount, -0.42, 1e-3)
            fuzzyCompare(effects[0].values.fringe, 0, 1e-6)
            // Another lens: the same effect, changed.
            verify(p.correctLens(p.selectedClip, "fisheye"))
            compare(p.selectedClipInfo.effects.length, 1)
            fuzzyCompare(p.selectedClipInfo.effects[0].values.amount, -0.85, 1e-3)
            tryVerify(() => findChild(f.editor, "removeLensCorrection").visible)
            findChild(f.editor, "removeLensCorrection").clicked()
            compare(p.selectedClipInfo.effects.length, 0)
            verify(!p.correctLens(p.selectedClip, "telescope"))
        }
    }
}
