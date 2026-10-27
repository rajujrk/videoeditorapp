import QtQuick
import QtTest
import TechieVideoEditor.Theme
import TechieVideoEditor.Player
import TechieVideoEditor.Shell

// The Home and Editor screens load cleanly and their live parts are wired up.
// Screens are created under `root`, not the TestCase: TestCase items are invisible, and
// invisible items receive no mouse events. The size fits a 13" laptop screen, which
// caps the test window.
Item {
    id: root
    width: 1400
    height: 780

    Component { id: homeComponent; HomeScreen { anchors.fill: parent } }
    Component {
        id: editorComponent
        EditorScreen {
            anchors.fill: parent
            project: ProjectController { view: player }
        }
    }
    Component { id: spyComponent; SignalSpy {} }

    TestCase {
        id: testCase
        name: "Screens"
        when: windowShown

        function createScreen(component) {
            const screen = createTemporaryObject(component, root)
            verify(screen)
            waitForRendering(screen)
            return screen
        }

        function test_home_new_project_reports_canvas_size() {
            const home = createScreen(homeComponent)
            const spy = createTemporaryObject(spyComponent, testCase, { target: home, signalName: "newProject" })

            const card = findChild(home, "newProject:9:16")
            verify(card, "9:16 card exists")
            mouseClick(card)
            compare(spy.count, 1)
            compare(spy.signalArguments[0][1], 1080)
            compare(spy.signalArguments[0][2], 1920)
        }

        function test_editor_exposes_all_built_in_effects() {
            const editor = createScreen(editorComponent)
            verify(editor.player)
            compare(editor.player.availableEffects.length, 5)
            compare(editor.player.enabledEffectCount, 0)
        }

        function test_inspector_switch_enables_effect() {
            const editor = createScreen(editorComponent)
            const toggle = findChild(editor, "effect:tve.color.adjust")
            verify(toggle, "Color Adjust switch exists in the inspector")
            mouseClick(toggle)
            verify(editor.player.isEffectEnabled("tve.color.adjust"))
            compare(editor.player.enabledEffectCount, 1)
        }

        function test_library_tabs_switch() {
            const editor = createScreen(editorComponent)
            const effectsTab = findChild(editor, "libraryTab:Effects")
            verify(effectsTab)
            verify(!effectsTab.checked)
            mouseClick(effectsTab)
            verify(effectsTab.checked)
        }

        function test_theme_switch_changes_tokens() {
            // Copy the value: a color read from a property stays bound to that property.
            const darkCanvas = Theme.color.canvas.toString()
            Theme.dark = false
            verify(Theme.color.canvas.toString() !== darkCanvas)
            Theme.dark = true
            compare(Theme.color.canvas.toString(), darkCanvas)
        }
    }
}
