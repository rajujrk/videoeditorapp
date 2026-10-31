import QtQuick
import QtTest
import TechieVideoEditor.Theme
import TechieVideoEditor.Player
import TechieVideoEditor.Shell

// Importing from Photos, Music and a device through the real editor, from test folders in
// their place (TVE_PHOTOS_SOURCE, TVE_MUSIC_SOURCE, TVE_DEVICE_SOURCE; written by main.cpp),
// fetched into a scratch Imports folder (TVE_IMPORT_DIR).
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
        name: "Imports"
        when: windowShown

        function createEditor() {
            const fixture = createTemporaryObject(editorComponent, root)
            verify(fixture)
            waitForRendering(fixture)
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
        function click(item) {
            mouseMove(item)
            mouseClick(item)
        }
        // The panel, opened from the Media tab's menu.
        function openPanel(f, source) {
            click(settled(f.editor, "importFromButton"))
            const item = findChild(f.editor, "importFrom:" + source)
            verify(item)
            tryCompare(item, "visible", true)
            click(item)
            const panel = findChild(f.editor, "importPanel")
            tryCompare(panel, "opened", true)
            compare(panel.source, source)
            return panel
        }
        function inPanel(panel, name) { return settled(panel.contentItem, name) }

        function test_photos_albums_and_import() {
            const f = createEditor()
            const p = f.project
            const panel = openPanel(f, "photos")
            compare(panel.access, "granted")
            compare(panel.collections.map((c) => c.title), ["Holiday", "Pets"])
            compare(panel.collections[0].count, 2)
            compare(panel.collection, "Holiday")
            compare(panel.items.map((i) => i.kind), ["photo", "video"])
            const video = panel.items[1]
            compare(video.width, 160)
            verify(video.duration > 2.9 && video.duration < 3.1, video.duration)
            verify(video.thumbnail.indexOf("image://tvethumb/") === 0)
            click(inPanel(panel, "importCollection:Pets"))
            compare(panel.items.length, 1)
            click(inPanel(panel, "importCollection:Holiday"))
            compare(panel.chosenCount, 0)
            verify(!inPanel(panel, "importChosen").enabled)

            // Choose both (and one off and on again), then import: copied into the Imports
            // folder, into the media bin, and the panel closes.
            click(inPanel(panel, "importItem:Holiday/IMG_0001.png"))
            click(inPanel(panel, "importItem:Holiday/IMG_0002.mov"))
            click(inPanel(panel, "importItem:Holiday/IMG_0001.png"))
            compare(panel.chosenCount, 1)
            click(inPanel(panel, "chooseAllImports"))
            compare(panel.chosenCount, 2)
            compare(inPanel(panel, "importStatus").text, "2 items chosen")
            click(inPanel(panel, "importChosen"))
            tryCompare(p.imports, "fetching", false, 5000)
            tryCompare(panel, "opened", false)
            compare(p.media.length, 2)
            // (Other tests import into the same folder: a taken name gets a number.)
            verify(/^IMG_0001( \d+)?\.png$/.test(p.media[0].path.substring(importsDir.length + 1)), p.media[0].path)
            verify(p.media[0].still)
            verify(/^IMG_0002( \d+)?\.mov$/.test(p.media[1].path.substring(importsDir.length + 1)), p.media[1].path)
            verify(p.media[1].hasVideo)

            // Again: new names rather than overwriting.
            const again = openPanel(f, "photos")
            click(inPanel(again, "importItem:Holiday/IMG_0001.png"))
            click(inPanel(again, "importChosen"))
            tryCompare(again, "opened", false, 5000)
            compare(p.media.length, 3)
            verify(/^IMG_0001 \d+\.png$/.test(p.media[2].path.substring(importsDir.length + 1)), p.media[2].path)
            verify(p.media[2].path !== p.media[0].path)
        }

        function test_music_uses_the_songs_own_files() {
            const f = createEditor()
            const p = f.project
            const panel = openPanel(f, "music")
            compare(panel.collections.map((c) => c.title), ["Road trip"])
            compare(panel.items.map((i) => i.title), ["Hum along", "On the beat"])
            verify(Math.abs(panel.items[0].duration - 7) < 0.05)
            compare(inPanel(panel, "importChosen").text, "Add to media")
            click(inPanel(panel, "importItem:Road trip/On the beat.wav"))
            click(inPanel(panel, "importChosen"))
            tryCompare(panel, "opened", false, 5000)
            compare(p.media.length, 1)
            compare(p.media[0].path, importFixturesDir + "/music/Road trip/On the beat.wav")
            verify(!p.media[0].hasVideo)
        }

        function test_devices_and_switching_sources() {
            const f = createEditor()
            const p = f.project
            const panel = openPanel(f, "devices")
            compare(panel.collections.map((c) => c.title), ["Test camera"])
            compare(panel.items.map((i) => i.title), ["CLIP0001", "DSC0002"])
            click(inPanel(panel, "importItem:Test camera/DSC0002.jpg"))
            // Switching source starts afresh.
            click(inPanel(panel, "importSource:photos"))
            compare(panel.source, "photos")
            compare(panel.collection, "Holiday")
            compare(panel.chosenCount, 0)
            click(inPanel(panel, "importSource:devices"))
            click(inPanel(panel, "chooseAllImports"))
            click(inPanel(panel, "importChosen"))
            tryCompare(panel, "opened", false, 5000)
            compare(p.media.map((m) => m.path.substring(m.path.lastIndexOf("/") + 1)).join(","), "CLIP0001.mov,DSC0002.jpg")
            verify(p.media.every((m) => m.path.indexOf(importsDir) === 0))
        }

        function test_empty_bin_offers_the_sources() {
            const f = createEditor()
            click(settled(f.editor, "emptyImportFrom:music"))
            const panel = findChild(f.editor, "importPanel")
            tryCompare(panel, "opened", true)
            compare(panel.source, "music")
            click(inPanel(panel, "closeImport"))
            tryCompare(panel, "opened", false)
        }

        function test_a_failed_fetch_is_reported() {
            const f = createEditor()
            const p = f.project
            let errors = []
            p.errorOccurred.connect((message) => errors.push(message))
            verify(p.importFrom("photos", ["Holiday/IMG_0001.png", "Holiday/missing.png"]))
            tryCompare(p.imports, "fetching", false, 5000)
            compare(p.media.length, 1)
            compare(errors.length, 1)
            verify(errors[0].indexOf("could not be imported") >= 0, errors[0])
            verify(!p.importFrom("photos", []))
        }
    }
}
