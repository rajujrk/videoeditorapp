import QtQuick
import QtTest
import TechieVideoEditor.Theme
import TechieVideoEditor.Player
import TechieVideoEditor.Shell

// Export through the real editor: the dialog, a finished MP4, cancelling.
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
        name: "Export"
        when: windowShown

        readonly property string av: "file://" + samplesDir + "/test_av_1080p30.mp4"
        readonly property string longClip: "file://" + samplesDir + "/4k60_h264.mp4"

        function createEditor() {
            const fixture = createTemporaryObject(editorComponent, root)
            verify(fixture)
            waitForRendering(fixture)
            finished.clear()
            finished.target = fixture.project
            return fixture
        }

        // What a fresh import finds in a file: {width, height, duration, hasAudio}, or null.
        function probe(url) {
            const p = Qt.createQmlObject('import TechieVideoEditor.Player; ProjectController {}', testCase)
            const found = p.importMedia([url]) === 1 ? p.media[0] : null
            p.destroy()
            return found
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
            return item
        }

        function click(editor, name) {
            const item = settled(editor, name)
            mouseMove(item)
            mouseClick(item)
        }

        function test_nothing_to_export_in_an_empty_project() {
            const f = createEditor()
            verify(!findChild(f.editor, "exportButton").enabled)
            verify(!f.project.exportVideo("file://" + tempDir + "/tve_empty.mp4", {}))
            verify(f.project.lastError.indexOf("timeline") >= 0)
        }

        function test_export_through_the_dialog() {
            const f = createEditor()
            const p = f.project
            p.importAndAppend(av)
            verify(p.addTitle({ text: "Hello", size: 0.1 }, -1, 0))
            click(f.editor, "exportButton")
            const dialog = findChild(f.editor, "exportDialog")
            tryVerify(() => dialog.opened)
            // The dialog's controls live in the window's overlay, not under the editor.
            const content = dialog.contentItem
            verify(findChild(content, "exportEstimate").text.indexOf("MB") > 0)

            // 720p, H.264, small, into the temp folder.
            dialog.file = "file://" + tempDir + "/tve_dialog_export.mp4"
            findChild(content, "exportSize").currentIndex = 1
            click(content, "exportQuality:small")
            compare(dialog.options.width, 1280)
            compare(dialog.options.height, 720)
            compare(dialog.options.quality, "small")
            click(content, "exportStartButton")
            compare(dialog.stage, "exporting")
            verify(p.exporting)

            finished.wait(30000)
            compare(finished.signalArguments[0][0], true, finished.signalArguments[0][1])
            compare(dialog.stage, "done")
            verify(!p.exporting)
            fuzzyCompare(p.exportProgress, 1.0, 1e-6)

            const media = probe(dialog.file)
            verify(media, "the exported file does not open")
            compare(media.width, 1280)
            compare(media.height, 720)
            compare(media.hasAudio, true)
            fuzzyCompare(media.duration, 5.0, 0.05) // the title runs to 5 s, past the 2 s clip
        }

        function test_cancelling_leaves_no_file() {
            const f = createEditor()
            const p = f.project
            p.importAndAppend(longClip) // 20 s of 4K60
            const file = "file://" + tempDir + "/tve_cancelled_export.mp4"
            verify(p.exportVideo(file, {}))
            verify(!p.exportVideo(file, {})) // one at a time
            tryVerify(() => p.exportProgress > 0.02, 20000)
            p.cancelExport()
            finished.wait(20000)
            compare(finished.signalArguments[0][0], false)
            compare(finished.signalArguments[0][1], "")
            verify(probe(file) === null, "a cancelled export left a file")
        }
    }
}
