import QtQuick
import QtTest
import TechieVideoEditor.Theme
import TechieVideoEditor.Player
import TechieVideoEditor.Shell

// Autosave and recovery: what is kept, what is not, and getting it back.
Item {
    id: root
    width: 1400
    height: 780

    Component {
        id: controllerComponent
        ProjectController {}
    }
    Component {
        id: homeComponent
        HomeScreen { anchors.fill: parent }
    }
    SignalSpy { id: recoverSpy; signalName: "recoverRequested" }

    TestCase {
        id: testCase
        name: "Recovery"
        when: windowShown

        readonly property string av: "file://" + samplesDir + "/test_av_1080p30.mp4"
        property string folder

        function init() {
            // A folder of its own per test (the harness points the default at a scratch dir).
            folder = tempDir + "/tve-recovery-" + Date.now() + "-" + Math.floor(Math.random() * 1e6)
        }

        // A controller using this test's folder. Destroy with destroy() to "quit".
        function controller() {
            const c = controllerComponent.createObject(testCase)
            c.autosaveDirectory = folder
            return c
        }

        function test_edits_are_autosaved_after_a_pause() {
            const a = controller()
            compare(a.lastAutosave, 0)
            a.importAndAppend(av)
            verify(a.modified)
            tryVerify(() => a.lastAutosave > 0, 5000, "never autosaved")
            a.destroy()
        }

        function test_work_left_unsaved_is_offered_back() {
            const a = controller()
            a.importAndAppend(av)
            a.autosaveNow()
            const b = controller()
            compare(b.recoverableProjects.length, 0) // still open in this process
            a.destroy() // quits with unsaved changes
            wait(0) // destroy() is deferred
            compare(b.recoverableProjects.length, 1)
            const entry = b.recoverableProjects[0]
            compare(entry.name, "Untitled project")
            compare(entry.clipCount, 1)
            verify(entry.savedAt > 0)

            verify(b.recoverProject(entry.id))
            compare(b.tracks.find((t) => t.main).clips.length, 1)
            verify(b.modified) // until saved
            compare(b.recoverableProjects.length, 0)
            verify(b.lastAutosave > 0) // now kept under b's own session
            b.destroy()
        }

        function test_saved_or_undone_work_leaves_nothing() {
            const a = controller()
            a.importAndAppend(av)
            a.autosaveNow()
            verify(a.save("file://" + folder + "-saved.tve"))
            compare(a.lastAutosave, 0)
            a.destroy()
            wait(0)

            const b = controller()
            b.importAndAppend(av)
            b.autosaveNow()
            while (b.canUndo)
                b.undo() // back to the untouched project
            verify(!b.modified)
            b.destroy()
            wait(0) // destroy() is deferred

            const c = controller()
            compare(c.recoverableProjects.length, 0)
            c.destroy()
        }

        function test_switching_projects_keeps_unsaved_work() {
            const a = controller()
            a.importAndAppend(av)
            a.newProject(1920, 1080)
            // The replaced work is recoverable at once, even from the same controller.
            compare(a.recoverableProjects.length, 1)
            a.discardRecovery(a.recoverableProjects[0].id)
            compare(a.recoverableProjects.length, 0)
            a.destroy()
        }

        function test_recovered_work_saves_to_its_original_file() {
            const file = "file://" + folder + "-original.tve"
            const a = controller()
            a.importAndAppend(av)
            verify(a.save(file))
            a.importAndAppend(av) // an unsaved change on top
            const path = a.filePath
            a.destroy()
            wait(0)

            const b = controller()
            compare(b.recoverableProjects.length, 1)
            compare(b.recoverableProjects[0].originalPath, path)
            compare(b.recoverableProjects[0].name, "tve-recovery-" + folder.split("tve-recovery-")[1] + "-original")
            verify(b.recoverProject(b.recoverableProjects[0].id))
            compare(b.filePath, path)
            compare(b.tracks.find((t) => t.main).clips.length, 2)
            verify(b.save(""))
            verify(!b.modified)
            b.destroy()
        }

        function test_turning_autosave_off_removes_its_files() {
            const a = controller()
            a.importAndAppend(av)
            a.autosaveNow()
            a.autosaveEnabled = false
            compare(a.lastAutosave, 0)
            a.destroy()
            wait(0)
            const b = controller()
            compare(b.recoverableProjects.length, 0)
            b.destroy()
        }

        function test_home_screen_lists_unsaved_work() {
            const a = controller()
            a.importAndAppend(av)
            a.destroy()
            wait(0)
            const b = controller()
            const home = createTemporaryObject(homeComponent, root, { project: b })
            recoverSpy.target = home
            recoverSpy.clear()
            const section = findChild(home, "recoverySection")
            tryVerify(() => section.visible)
            const id = b.recoverableProjects[0].id
            let button = null
            tryVerify(() => (button = findChild(home, "recover:" + id)) !== null)
            waitForRendering(home)
            mouseClick(button)
            compare(recoverSpy.count, 1)
            compare(recoverSpy.signalArguments[0][0], id)
            mouseClick(findChild(home, "discardRecovery:" + id))
            tryVerify(() => !section.visible)
            b.destroy()
        }
    }
}
