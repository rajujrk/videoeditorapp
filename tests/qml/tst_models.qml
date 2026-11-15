import QtQuick
import QtTest
import TechieVideoEditor.Theme
import TechieVideoEditor.Player
import TechieVideoEditor.Shell

// The on-device models from the Home screen: what is recommended is chosen, downloaded one
// after another (from the test runner's local model folder into an empty one, checked as for
// real), shown as on this Mac; a model that can't be had says why and the rest carry on.
Item {
    id: root
    width: 1400
    height: 860

    Component {
        id: homeComponent
        Item {
            property alias project: controller
            property alias home: screen
            anchors.fill: parent
            ProjectController { id: controller; autosaveEnabled: false }
            HomeScreen { id: screen; anchors.fill: parent; project: controller }
        }
    }

    TestCase {
        name: "Models"
        when: windowShown

        readonly property bool modelsHere: speechModelAvailable

        function settled(parent, name) {
            tryVerify(() => { const i = findChild(parent, name); return i !== null && i.visible }, 5000, name)
            return findChild(parent, name)
        }
        function click(item) {
            mouseMove(item)
            mouseClick(item)
        }
        function open(f) {
            click(settled(f.home, "homeModels"))
            const dialog = findChild(f.home, "modelsDialog")
            verify(dialog)
            tryCompare(dialog, "opened", true)
            return dialog
        }
        function row(dialog, id) { return dialog.rows.find((r) => r.id === id) }

        function test_the_recommended_models_are_chosen_and_downloaded_in_turn() {
            if (!modelsHere)
                skip("models missing: run tools/fetch_models.sh")
            const f = createTemporaryObject(homeComponent, root)
            waitForRendering(f)
            const p = f.project
            const dialog = open(f)
            const content = dialog.contentItem

            // English: the English speech model, speech cleanup and depth; not the multilingual one.
            compare(dialog.rows.slice(0, 4).map((r) => r.id),
                    ["base.en-q5_1", "base-q5_1", "deepfilternet3", "depth-anything-v2-small-f16"])
            verify(dialog.rows.every((r) => r.id !== "upscale" || r.size === 0))
            const recommended = ["base.en-q5_1", "deepfilternet3", "depth-anything-v2-small-f16"]
            for (const id of recommended)
                compare(dialog.chosen[id], !row(dialog, id).downloaded, id)
            verify(!dialog.chosen["base-q5_1"])
            compare(findChild(content, "modelState:deepfilternet3").text,
                    row(dialog, "deepfilternet3").downloaded ? "On this Mac" : Fmt.mb(row(dialog, "deepfilternet3").size))
            const expected = recommended.filter((id) => !row(dialog, id).downloaded)
            if (expected.length > 0)
                verify(settled(content, "modelsSummary").text.indexOf("to download") > 0)

            // Downloaded one after another; each then on this Mac.
            if (expected.length > 0) {
                click(settled(content, "modelsDownload"))
                verify(dialog.busy)
                verify(!findChild(content, "modelsDownload").enabled)
                compare(dialog.downloading, expected[0])
                tryVerify(() => !dialog.busy, 120000, "the models never all came")
            }
            for (const id of recommended) {
                verify(row(dialog, id).downloaded, id)
                compare(findChild(content, "modelState:" + id).text, "On this Mac")
                verify(!findChild(content, "modelChoice:" + id).visible)
            }
            compare(Object.keys(dialog.errors).length, 0)
            verify(p.enhanceModel.downloaded)
            verify(p.depthModel.downloaded)

            // One that can't be had (the test folder doesn't hold it): it says why.
            const choice = settled(content, "modelChoice:base-q5_1")
            click(choice)
            verify(dialog.chosen["base-q5_1"])
            click(settled(content, "modelsDownload"))
            tryVerify(() => !dialog.busy, 20000)
            verify(!row(dialog, "base-q5_1").downloaded)
            verify(settled(content, "modelError:base-q5_1").text.length > 0)

            // Closed and opened again: what is here stays shown as here.
            click(settled(content, "modelsClose"))
            tryCompare(dialog, "opened", false)
            open(f)
            for (const id of recommended)
                compare(findChild(dialog.contentItem, "modelState:" + id).text, "On this Mac")
            verify(!dialog.chosen["base-q5_1"])
        }

        function test_nothing_chosen_downloads_nothing() {
            const f = createTemporaryObject(homeComponent, root)
            waitForRendering(f)
            const dialog = open(f)
            for (const r of dialog.rows)
                dialog.choose(r.id, false)
            compare(dialog.chosenCount, 0)
            const download = findChild(dialog.contentItem, "modelsDownload")
            verify(!download.visible || !download.enabled)
            click(settled(dialog.contentItem, "modelsClose"))
            tryCompare(dialog, "opened", false)
            compare(f.project.modelDownload.id, undefined)
        }
    }
}
