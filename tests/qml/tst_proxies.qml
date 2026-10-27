import QtQuick
import QtTest
import TechieVideoEditor.Theme
import TechieVideoEditor.Player
import TechieVideoEditor.Shell

// Proxies through the real editor: making, showing, removing, using, automatic ones.
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
        name: "Proxies"
        when: windowShown

        readonly property string hd: samplesDir + "/test_1080p30_h264.mp4"
        readonly property string uhd: samplesDir + "/4k30_h264.mp4"

        function createEditor() {
            const fixture = createTemporaryObject(editorComponent, root)
            verify(fixture)
            waitForRendering(fixture)
            return fixture
        }
        function stateOf(project, path) { return (project.proxyStates[path] || { state: "none" }).state }

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

        function test_make_and_remove_a_proxy_from_the_media_bin() {
            const f = createEditor()
            const p = f.project
            compare(p.importMedia(["file://" + hd]), 1)
            compare(stateOf(p, hd), "none") // 1080p: not made automatically

            // Hovering the tile offers to make one.
            const tile = settled(f.editor, "media:test_1080p30_h264.mp4")
            mouseMove(tile, tile.width / 2, tile.height / 2)
            const make = settled(f.editor, "proxyAction:test_1080p30_h264.mp4")
            compare(make.contentItem.text, "Make proxy")
            mouseMove(make)
            mouseClick(make)
            verify(["queued", "making", "ready"].indexOf(stateOf(p, hd)) >= 0)
            tryVerify(() => stateOf(p, hd) === "ready", 15000)
            tryCompare(findChild(f.editor, "proxyBadge:test_1080p30_h264.mp4").children[0], "text", "Proxy")

            verify(p.deleteProxy(p.media[0].id))
            compare(stateOf(p, hd), "none")
        }

        function test_player_toggle_switches_proxies_off() {
            const f = createEditor()
            const p = f.project
            verify(p.useProxies)
            const toggle = settled(f.editor, "proxyToggle")
            mouseMove(toggle)
            mouseClick(toggle)
            verify(!p.useProxies)
            mouseClick(toggle)
            verify(p.useProxies)
        }

        function test_large_video_gets_a_proxy_automatically() {
            const f = createEditor()
            const p = f.project
            compare(p.importMedia(["file://" + uhd]), 1)
            verify(["queued", "making"].indexOf(stateOf(p, uhd)) >= 0, stateOf(p, uhd))
            // Removing it while it is being made stops it and leaves nothing.
            tryVerify(() => stateOf(p, uhd) === "making", 5000)
            verify(p.deleteProxy(p.media[0].id))
            compare(stateOf(p, uhd), "none")
            wait(500)
            compare(stateOf(p, uhd), "none")

            p.autoProxies = false
            p.newProject(1920, 1080)
            compare(p.importMedia(["file://" + uhd]), 1)
            compare(stateOf(p, uhd), "none")
        }

        function test_only_videos_have_proxies() {
            const f = createEditor()
            const p = f.project
            compare(p.importMedia(["file://" + samplesDir + "/test_tone_440.wav"]), 1)
            verify(!p.createProxy(p.media[0].id))
            verify(p.lastError.indexOf("Only videos") >= 0)
        }
    }
}
