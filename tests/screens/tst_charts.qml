import QtQuick
import QtTest
import TechieVideoEditor.Theme
import TechieVideoEditor.Player
import TechieVideoEditor.Shell
import "screens.js" as Screens

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
        name: "ShotCharts"
        when: windowShown
        function scrollTo(f, name) {
            const section = findChild(f.editor, name)
            let flick = section.parent
            while (flick && flick.contentY === undefined)
                flick = flick.parent
            if (flick)
                flick.contentY = Math.max(0, Math.min(section.mapToItem(flick.contentItem, 0, 0).y - 20, flick.contentHeight - flick.height))
        }
        function test_shots() {
            for (const dark of [true, false]) {
                Theme.dark = dark
                const tag = dark ? "dark" : "light"
                Screens.palette(root.Window.window, Theme)
                const f = createTemporaryObject(editorComponent, root)
                waitForRendering(f)
                const p = f.project
                verify(p.addColorClip({ kind: "linear", from: [0.1, 0.12, 0.2, 1], to: [0.05, 0.06, 0.1, 1] }) > 0)
                verify(p.addTitle({ text: "Views each quarter", size: 0.06, bold: true, duration: 6,
                                    chart: { kind: "bar", data: "Q1, 120\nQ2, 180\nQ3, 150\nQ4, 240", grow: 1.5 } }, -1, 0))
                p.seek(3)
                findChild(f.editor, "inspectorTab:Video").clicked()
                wait(300)
                scrollTo(f, "titleChartSection")
                findChild(f.editor, "transformOverlay").opacity = 0
                mouseMove(root, 1, 1)
                wait(900)
                waitForRendering(f)
                grabImage(f).save(screensDir + "/charts-" + tag + ".png")
                verify(p.setClipText(p.selectedClip, { chart: { kind: "line" } }))
                p.seek(0.8)
                wait(700)
                grabImage(f).save(tempDir + "/tve-shot-line-mid-" + tag + ".png")
                p.selectedClip = -1
                verify(p.addLogoReveal("spin", "file://" + stillsDir + "/halves.png", "Studio North"))
                p.seek(0.5)
                wait(700)
                grabImage(f).save(tempDir + "/tve-shot-logo-mid-" + tag + ".png")
                p.seek(2.5)
                wait(700)
                grabImage(f).save(tempDir + "/tve-shot-logo-" + tag + ".png")
                f.destroy(); wait(50)
            }
            Theme.dark = true
        }
    }
}
