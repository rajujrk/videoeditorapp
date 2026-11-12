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
            compare(editor.player.availableEffects.length, 61) // 55 effects, 6 overlays
            compare(editor.player.availableEffects.filter((e) => e.categories.indexOf("overlay") >= 0).length, 6)
            compare(editor.player.enabledEffectCount, 0)
        }

        function test_effects_tab_asks_for_a_clip() {
            const editor = createScreen(editorComponent)
            mouseClick(findChild(editor, "inspectorTab:Effects"))
            // No clip selected: no effect controls, so nothing can be applied by accident.
            const picker = findChild(editor, "effectPicker")
            verify(!picker || !picker.visible)
        }

        function test_library_tabs_switch() {
            const editor = createScreen(editorComponent)
            const effectsTab = findChild(editor, "libraryTab:Effects")
            verify(effectsTab)
            verify(!effectsTab.checked)
            // Clicked where it has come to rest: the split views lay the panels out after the
            // first frame, and under load a click can land where the tab was a moment before.
            let last = null
            tryVerify(() => {
                const p = effectsTab.mapToItem(root, 0, 0)
                const stable = last !== null && p.x === last.x && p.y === last.y && effectsTab.width > 0
                last = p
                return stable
            }, 3000, "the tab never came to rest")
            for (let a = effectsTab; a && a !== root; a = a.parent)
                waitForPolish(a, 1000)
            mouseClick(effectsTab)
            tryVerify(() => effectsTab.checked, 2000, "the click did not switch the tab")
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
