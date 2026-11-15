import QtQuick
import QtTest
import TechieVideoEditor.Theme
import TechieVideoEditor.Player
import TechieVideoEditor.Shell
import "screens.js" as Screens

Item {
    id: root
    width: 1470
    height: 790
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
    Component {
        id: pickerComponent
        ScreenAreaPicker { display: ({ x: 0, y: 0, width: 1470, height: 790 }) }
    }
    // The picker's grab laid over the editor's.
    Component {
        id: layered
        Item {
            property alias under: below.source
            property alias over: above.source
            width: 1470
            height: 790
            z: 100
            Image { id: below; anchors.fill: parent; cache: false }
            Image { id: above; anchors.fill: parent; cache: false }
        }
    }
    readonly property string portrait: "file://" + screensMediaDir + "/portrait-wide.mp4"
    TestCase {
        name: "ShotHandling"
        when: windowShown
        function test_shots() {
            const window = root.Window.window
            const av = "file://" + samplesDir + "/test_av_1080p30.mp4"
            for (const dark of [true, false]) {
                Theme.dark = dark
                EditingPreferences.reset()
                const tag = dark ? "dark" : "light"
                Screens.palette(root.Window.window, Theme)

                // The timeline: a clip turned off, a placeholder, skimming, the clip menu.
                let f = createTemporaryObject(editorComponent, root)
                waitForRendering(f)
                let p = f.project
                for (let i = 0; i < 3; ++i)
                    verify(p.importAndAppend(av))
                const main = () => p.tracks.find((t) => t.main)
                p.seek(6)
                verify(p.addPlaceholder(4) > 0)
                verify(p.setClipEnabled(Number(main().clips[1].id), false))
                EditingPreferences.skimming = true
                EditingPreferences.autoRipple = true
                p.seek(0.8)
                p.skim(4.6)
                const third = main().clips[2]
                p.selectedClip = Number(third.id)
                wait(600)
                const menus = findChild(f.editor, "timelineMenus")
                const clipItem = findChild(f.editor, "clip:" + third.id)
                menus.openClipMenu(third, main(), f.editor, 1010, 40)
                wait(700)
                grabImage(window.contentItem).save(screensDir + "/timeline-handling-" + tag + ".png")
                findChild(f.editor, "clipMenu").dismiss()
                p.endSkim()
                f.destroy(); wait(150)

                // The frame border: a picture moved onto the left edge, the edge lit.
                f = createTemporaryObject(editorComponent, root)
                waitForRendering(f)
                p = f.project
                verify(p.importMedia([root.portrait]) === 1)
                verify(p.addClip(p.media[0].id))
                const id = Number(p.tracks.find((t) => t.main).clips[0].id)
                verify(p.setClipVisual(id, { scale: 0.55 }))
                p.selectedClip = id
                p.seek(1)
                const overlay = findChild(f.editor, "transformOverlay")
                tryVerify(() => overlay.frame !== null, 3000)
                wait(500)
                overlay.preview({ x: -0.225 })
                wait(120) // (mid-shake)
                overlay.preview({ x: -0.225 })
                wait(700)
                grabImage(window.contentItem).save(screensDir + "/frame-border-" + tag + ".png")
                overlay.draft = null
                f.destroy(); wait(150)

                // The recorder: An area chosen, and the picker over the screen.
                f = createTemporaryObject(editorComponent, root)
                waitForRendering(f)
                p = f.project
                verify(p.importAndAppend(av))
                p.selectedClip = 0
                findChild(f.editor, "screenRecordButton").clicked()
                const panel = findChild(f.editor, "recordPanel")
                tryVerify(() => panel.opened)
                findChild(panel.contentItem, "recordMode:area").clicked()
                tryVerify(() => panel.areaPicker !== null)
                findChild(panel.areaPicker.contentItem, "cancelArea").clicked()
                panel.area = { x: 400, y: 53, width: 690, height: 388 }
                wait(700)
                grabImage(window.contentItem).save(screensDir + "/record-panel-" + tag + ".png")
                panel.close()
                wait(400)
                const under = tempDir + "/tve-shot-record-under-" + tag + ".png"
                grabImage(window.contentItem).save(under)
                const picker = createTemporaryObject(pickerComponent, root)
                picker.setArea(400, 53, 690, 388)
                tryVerify(() => picker.visible)
                wait(700)
                const over = tempDir + "/tve-shot-record-over-" + tag + ".png"
                grabImage(picker.contentItem).save(over)
                picker.destroy(); wait(200)
                const stack = createTemporaryObject(layered, root, { under: "file://" + under, over: "file://" + over })
                wait(500)
                grabImage(stack).save(screensDir + "/record-area-" + tag + ".png")
                stack.destroy()
                f.destroy(); wait(150)
            }
            EditingPreferences.reset()
            Theme.dark = true
        }
    }
}
