import QtQuick
import QtTest
import TechieVideoEditor.Theme
import TechieVideoEditor.Player
import TechieVideoEditor.Shell
import "screens.js" as Screens

Item {
    id: root
    width: 1280
    height: 720
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
        id: compositeComponent
        Item {
            width: 1280; height: 720
            property alias back: back
            property alias ink: ink
            property alias bar: bar
            Image { id: back; anchors.fill: parent }
            Image { id: ink; anchors.fill: parent }
            Image { id: bar }
        }
    }
    TestCase {
        name: "ShotDrawing"
        when: windowShown
        function stroke(layer, pts) {
            const area = findChild(layer.contentItem, "liveDrawingArea")
            mousePress(area, pts[0][0], pts[0][1])
            for (const [x, y] of pts.slice(1))
                mouseMove(area, x, y)
            const last = pts[pts.length - 1]
            mouseRelease(area, last[0], last[1])
        }
        function test_shots() {
            const av = "file://" + samplesDir + "/test_av_1080p30.mp4"
            for (const dark of [true, false]) {
                Theme.dark = dark
                const tag = dark ? "dark" : "light"
                Screens.palette(root.Window.window, Theme)
                const f = createTemporaryObject(editorComponent, root)
                waitForRendering(f)
                const p = f.project
                verify(p.importAndAppend(av))
                p.seek(1)
                wait(600)
                grabImage(f).save(tempDir + "/tve-dr-back.png")
                f.editor.openLiveDrawing({ x: 0, y: 0, width: 1280, height: 720 })
                const layer = f.editor.liveDrawing
                layer.fades = false
                wait(300)
                // Circle the player's picture, an arrow to the timeline, highlight the inspector.
                const circle = []
                for (let a = 0; a <= 2 * Math.PI + 0.2; a += 0.15)
                    circle.push([640 + 230 * Math.cos(a), 230 + 120 * Math.sin(a)])
                stroke(layer, circle)
                layer.tool = "arrow"; layer.ink = "#FFCC00"; layer.thickness = 6
                stroke(layer, [[880, 420], [700, 470], [520, 540]])
                layer.tool = "highlighter"; layer.ink = "#34C759"
                stroke(layer, [[1000, 92], [1060, 92], [1120, 92], [1180, 92]])
                wait(400)
                grabImage(layer.contentItem).save(tempDir + "/tve-dr-ink.png")
                grabImage(layer.controls.contentItem).save(tempDir + "/tve-dr-bar.png")
                const c = createTemporaryObject(compositeComponent, root)
                c.back.source = "file://" + tempDir + "/tve-dr-back.png"
                c.ink.source = "file://" + tempDir + "/tve-dr-ink.png"
                c.bar.source = "file://" + tempDir + "/tve-dr-bar.png"
                tryVerify(() => c.back.status === Image.Ready && c.ink.status === Image.Ready && c.bar.status === Image.Ready)
                c.bar.width = c.bar.implicitWidth / 2
                c.bar.height = c.bar.implicitHeight / 2
                c.bar.x = (1280 - c.bar.width) / 2
                c.bar.y = 720 - c.bar.height - 48
                wait(200)
                grabImage(c).save(screensDir + "/live-drawing-" + tag + ".png")
                c.destroy()
                f.destroy(); wait(100)
            }
            Theme.dark = true
        }
    }
}
