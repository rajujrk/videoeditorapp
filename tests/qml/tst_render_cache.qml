import QtQuick
import QtTest
import TechieVideoEditor.Theme
import TechieVideoEditor.Player
import TechieVideoEditor.Shell

// Render files through the real editor: a stretch too heavy to play in real time (here a
// lower limit, TVE_REALTIME_LOAD, so two 1080p layers are) marked over the ruler, rendered
// from the Render menu, then played from its file; an edit inside it makes it heavy again and
// undo brings the file back without rendering; the whole timeline rendered; the files deleted.
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
        name: "RenderCache"
        when: windowShown
        readonly property string av: "file://" + samplesDir + "/test_av_1080p30.mp4"

        function init() {
            testFiles.setEnv("TVE_REALTIME_LOAD", "0.025")
        }
        function cleanup() {
            testFiles.setEnv("TVE_REALTIME_LOAD", "")
        }
        function createEditor() {
            const f = createTemporaryObject(editorComponent, root)
            verify(f)
            waitForRendering(f)
            f.project.deleteRenderFiles()
            return f
        }
        // Two clips, the second over the first from 1 s to 2 s on the track above.
        function twoLayers(p) {
            compare(p.importMedia([av]), 1)
            const media = p.media[0].id
            verify(p.addClip(media, -1, 0))
            const above = p.tracks.find((t) => t.kind === "video" && t.clips.length === 0)
            verify(above)
            verify(p.addClip(media, above.id, 1))
            const main = p.tracks.find((t) => t.kind === "video" && t.id !== above.id && t.clips.length === 1)
            return { top: p.tracks.find((t) => t.id === above.id).clips[0], bottom: main.clips[0] }
        }
        // The player's picture at a 4 × 3 grid of points, as [r, g, b] 0..255.
        function picture(f) {
            wait(500)
            waitForRendering(root)
            const view = findChild(f.editor, "player")
            const image = grabImage(view)
            const points = []
            for (let y = 1; y <= 3; ++y)
                for (let x = 1; x <= 4; ++x) {
                    const c = image.pixel(Math.round(image.width * x / 5), Math.round(image.height * y / 4))
                    points.push([c.r * 255, c.g * 255, c.b * 255])
                }
            return points
        }
        function difference(a, b) {
            let sum = 0
            for (let i = 0; i < a.length; ++i)
                for (let c = 0; c < 3; ++c)
                    sum += Math.abs(a[i][c] - b[i][c])
            return sum / (a.length * 3)
        }
        function stretches(p) {
            return p.renderStretches.map((s) => [Math.round(s.from * 10) / 10, Math.round(s.to * 10) / 10, s.rendered])
        }

        function test_a_heavy_stretch_rendered_edited_and_undone() {
            const f = createEditor()
            const p = f.project
            const clips = twoLayers(p)
            const top = clips.top
            // Where both play: heavy, not rendered.
            verify(p.needsRender)
            const end = Math.min(clips.bottom.start + clips.bottom.duration, top.start + top.duration)
            compare(stretches(p), [[1, Math.round(end * 10) / 10, false]])
            verify(findChild(f.editor, "renderStretch:0").visible)
            compare(findChild(f.editor, "renderStretch:0").rendered, false)
            verify(findChild(f.editor, "renderButton").tip.indexOf("too heavy") > 0)

            p.seek(1.5)
            const live = picture(f)
            // Rendered from the menu: a ProRes file in the render folder, played from there.
            findChild(f.editor, "renderHeavy").triggered()
            verify(p.rendering)
            tryVerify(() => !p.rendering, 60000, "the render never finished: " + p.lastError)
            compare(stretches(p), [[1, Math.round(end * 10) / 10, true]])
            verify(!p.needsRender)
            verify(p.renderFilesSize > 100000)
            const index = JSON.parse(testFiles.read(renderFilesDir + "/index.json"))
            compare(index.stretches.length, 1)
            verify(testFiles.exists(renderFilesDir + "/" + index.stretches[0].key + ".mov"))
            compare(findChild(f.editor, "renderStretch:0").rendered, true)
            // The same picture as composited.
            p.seek(1.4)
            p.seek(1.5)
            const fromFile = picture(f)
            verify(difference(live, fromFile) < 4, "rendered " + JSON.stringify(fromFile) + " live " + JSON.stringify(live))

            // An edit inside it: heavy again. Undone: the file applies again, unrendered.
            verify(p.setClipVisual(top.id, { opacity: 0.5 }))
            compare(stretches(p), [[1, Math.round(end * 10) / 10, false]])
            p.undo()
            compare(stretches(p), [[1, Math.round(end * 10) / 10, true]])
            compare(JSON.parse(testFiles.read(renderFilesDir + "/index.json")).stretches.length, 1)

            // Deleted: heavy again, no files.
            findChild(f.editor, "deleteRenderFiles").triggered()
            compare(p.renderFilesSize, 0)
            compare(stretches(p), [[1, Math.round(end * 10) / 10, false]])
        }

        function test_the_whole_timeline_rendered_and_stopped() {
            const f = createEditor()
            const p = f.project
            twoLayers(p)
            // Nothing heavy at a normal limit: Render has nothing to do.
            testFiles.setEnv("TVE_REALTIME_LOAD", "")
            // (any change brings the stretches up to date)
            verify(p.setClipVisual(p.tracks.find((t) => t.kind === "video" && t.clips.length > 0).clips[0].id, { opacity: 0.9 }))
            verify(!p.needsRender)
            verify(!p.renderHeavy())
            verify(p.lastError.indexOf("Nothing to render") === 0, p.lastError)

            // The whole timeline: every stretch with something in it, played from files.
            verify(p.renderAll())
            tryVerify(() => !p.rendering, 60000, "the render never finished: " + p.lastError)
            verify(p.renderStretches.length > 0)
            verify(p.renderStretches.every((s) => s.rendered))
            const covered = p.renderStretches.reduce((sum, s) => sum + s.to - s.from, 0)
            fuzzyCompare(covered, p.duration, 0.05)

            // A render stopped part way leaves nothing behind for it.
            p.deleteRenderFiles()
            verify(p.renderAll())
            p.cancelRender()
            tryVerify(() => !p.rendering, 20000)
            const left = testFiles.exists(renderFilesDir + "/index.json")
                ? JSON.parse(testFiles.read(renderFilesDir + "/index.json")).stretches.length : 0
            compare(left, p.renderStretches.filter((s) => s.rendered).length)
        }
    }
}
