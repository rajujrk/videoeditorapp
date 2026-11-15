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
        name: "ShotText"
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
                verify(p.addColorClip({ kind: "radial", from: [0.16, 0.18, 0.3, 1], to: [0.04, 0.05, 0.1, 1] }) > 0)
                // A curved 3D gradient title over it.
                verify(p.addTitle({ text: "Summer Trip", size: 0.16, bold: true, color: [1, 0.85, 0.2, 1] }, -1, 0))
                verify(p.setClipText(p.selectedClip, { curve: 0.35, depth: 0.12, depthColor: [0.55, 0.15, 0.1, 1],
                                                      fill: { kind: "linear", to: [1, 0.35, 0.25, 1], angle: 90 },
                                                      underline: false, tiltX: 12 }))
                verify(p.setClipVisual(p.selectedClip, { y: -0.12 }))
                p.seek(1)
                findChild(f.editor, "inspectorTab:Video").clicked()
                wait(300)
                scrollTo(f, "titleFillSection")
                findChild(f.editor, "transformOverlay").opacity = 0
                mouseMove(root, 1, 1)
                wait(900)
                waitForRendering(f)
                grabImage(f).save(screensDir + "/text-styles-" + tag + ".png")
                f.destroy(); wait(50)
            }
            Theme.dark = true
        }
    }
}
