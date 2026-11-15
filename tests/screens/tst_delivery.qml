import QtQuick
import QtTest
import TechieVideoEditor.Theme
import TechieVideoEditor.Player
import TechieVideoEditor.Shell
import "screens.js" as Screens

// Delivery: the export dialog (a file, a preset for phones with a marker range, sound only as
// MP3), the render queue, render files, a timeline exported for Final Cut Pro, an HDR project,
// and an OpenFX plug-in. Exports go to the test run's temporary folder.
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

    TestCase {
        name: "ScreensDelivery"
        when: windowShown
        readonly property string media: "file://" + screensMediaDir + "/"
        readonly property string av: "file://" + samplesDir + "/test_av_1080p30.mp4"

        function editor() {
            const f = createTemporaryObject(editorComponent, root)
            waitForRendering(f)
            return f
        }
        function main(p) { return p.tracks.find((t) => t.main) }
        function out(name) { return "file://" + tempDir + "/tve_screens_" + name }
        function shot(name, dark, settle) {
            mouseMove(root, root.width - 4, root.height - 4)
            wait(settle === undefined ? 1500 : settle)
            grabImage(root.Window.window.contentItem).save(Screens.file(screensDir, name, dark))
        }
        function settled(parent, name) {
            tryVerify(() => { const i = findChild(parent, name); return i !== null && i.visible }, 5000, name)
            return findChild(parent, name)
        }
        function pick(combo, id) {
            combo.currentIndex = combo.model.findIndex((m) => m.id === id)
            verify(combo.currentIndex >= 0, id)
            combo.activated(combo.currentIndex)
        }
        function openExport(f) {
            mouseClick(settled(f.editor, "exportButton"))
            const dialog = findChild(f.editor, "exportDialog")
            tryVerify(() => dialog.opened)
            return dialog
        }
        // A short film: two shots, a title over the first, a marked range.
        function film(p) {
            verify(p.importAndAppend(media + "night-street.mp4"))
            verify(p.importAndAppend(media + "portrait-wide.mp4"))
            verify(p.addTitle({ text: "Night walk", size: 0.09, bold: true }, -1, 0.4), "" + p.lastError)
            const range = p.addMarker(1, "Opening")
            verify(p.setMarker(range, { duration: 3, color: "green" }), "" + p.lastError)
        }

        function cleanup() {
            Theme.dark = true
        }

        function test_scenes() {
            for (const dark of [true, false]) {
                Theme.dark = dark
                EditingPreferences.reset()
                Screens.palette(root.Window.window, Theme)

                // The export dialog as it opens: a file, a format, a size.
                let f = editor()
                let p = f.project
                film(p)
                p.seek(1.2)
                let dialog = openExport(f)
                dialog.file = out("night-walk.mp4")
                shot("export", dark)

                // For phones: the vertical preset, louder, only the marked range.
                let content = dialog.contentItem
                findChild(content, "exportNormalize").checked = true
                const ranges = findChild(content, "exportRange")
                ranges.currentIndex = dialog.ranges.findIndex((r) => r.range === "marker")
                ranges.activated(ranges.currentIndex)
                pick(findChild(content, "exportPreset"), "vertical")
                dialog.file = out("night-walk-opening.mp4")
                shot("export-dialog", dark)

                // Sound only, as an MP3.
                pick(findChild(content, "exportPreset"), "audio-mp3")
                ranges.currentIndex = 0
                ranges.activated(0)
                dialog.file = out("night-walk.mp3")
                shot("export-mp3", dark)
                dialog.close()
                tryVerify(() => !dialog.visible)

                // The render queue: a GIF done, a ProRes master paused, two waiting.
                verify(p.exportVideo(out("night-walk.gif"), { format: "gif", width: 480, height: 270, frameRate: 15 }), "" + p.lastError)
                tryVerify(() => p.exportJobs.length === 1 && p.exportJobs[0].state === "done", 60000, "the GIF never finished")
                verify(p.exportVideo(out("night-walk-master.mov"), { format: "mov-prores-hq" }), "" + p.lastError)
                verify(p.exportVideo(out("night-walk-4k.mp4"), { format: "mp4-hevc", width: 3840, height: 2160 }), "" + p.lastError)
                verify(p.exportVideo(out("night-walk.wav"), { format: "wav-24" }), "" + p.lastError)
                tryVerify(() => p.exportProgress > 0.2, 60000)
                p.pauseExport()
                mouseClick(settled(f.editor, "renderQueueButton"))
                const queue = findChild(f.editor, "renderQueuePanel")
                tryVerify(() => queue.opened)
                shot("render-queue", dark)
                queue.close()
                for (const job of p.exportJobs)
                    if (job.state === "waiting")
                        p.cancelExport(job.id)
                p.resumeExport()
                p.cancelExport()
                tryVerify(() => p.exportJobs.every((j) => j.state !== "running" && j.state !== "waiting"), 20000)
                f.destroy(); wait(200)

                // Render files: a stretch too heavy to play live (the limit lowered here), rendered.
                testFiles.setEnv("TVE_REALTIME_LOAD", "0.025")
                f = editor()
                p = f.project
                p.deleteRenderFiles()
                compare(p.importMedia([av]), 1)
                verify(p.addClip(p.media[0].id, -1, 0))
                verify(p.addClip(p.media[0].id, -1, 2))
                const above = p.tracks.find((t) => t.kind === "video" && t.clips.length === 0)
                verify(p.addClip(p.media[0].id, above.id, 1))
                verify(p.addClip(p.media[0].id, above.id, 3.2))
                verify(p.needsRender)
                verify(p.renderHeavy())
                tryVerify(() => !p.rendering, 60000, "the render never finished: " + p.lastError)
                verify(p.setClipVisual(main(p).clips[1].id, { opacity: 0.8 }))
                p.seek(1.5)
                findChild(f.editor, "timelinePanel").pixelsPerSecond = 160
                f.editor.player.halfResolution = true
                mouseClick(settled(f.editor, "renderButton"))
                wait(400)
                shot("render-files", dark)
                keyClick(Qt.Key_Escape)
                f.editor.player.halfResolution = false
                p.deleteRenderFiles()
                testFiles.setEnv("TVE_REALTIME_LOAD", "")
                f.destroy(); wait(200)

                // The timeline exported for Final Cut Pro, with what didn't carry over.
                f = editor()
                p = f.project
                verify(p.importAndAppend(media + "night-street.mp4"))
                verify(p.importAndAppend(media + "portrait-wide.mp4"))
                verify(p.addTitle({ text: "Opening", size: 0.1 }, -1, 0.5))
                verify(p.addMarker(1.0, "Beat") !== 0)
                verify(p.setClipColor(main(p).clips[0].id, { exposure: 0.4 }))
                p.seek(1.2)
                mouseClick(settled(f.editor, "interchangeButton"))
                const interchange = findChild(f.editor, "interchangeDialog")
                tryVerify(() => interchange.opened)
                mouseClick(settled(interchange.contentItem, "interchangeFormat:fcpxml"))
                interchange.file = out("night-walk.fcpxml")
                mouseClick(settled(interchange.contentItem, "interchangeExportButton"))
                tryVerify(() => interchange.stage === "done")
                shot("interchange", dark)
                interchange.close()
                f.destroy(); wait(200)

                // An HDR project: the colour space, the scopes in nits.
                f = editor()
                p = f.project
                verify(p.importAndAppend(av))
                p.projectColour = "hdr-pq"
                p.selectedClip = main(p).clips[0].id
                p.seek(1)
                findChild(f.editor, "inspectorTab:Color").clicked()
                settled(f.editor, "projectColourNote")
                shot("colour-hdr", dark)
                f.destroy(); wait(200)

                // An OpenFX plug-in on a clip, with its own controls.
                f = editor()
                p = f.project
                verify(p.importAndAppend(av))
                const clip = Number(main(p).clips[0].id)
                p.videoPlugins
                tryVerify(() => !p.scanningPlugins && p.videoPlugins.length > 0, 30000, "no plug-ins")
                verify(p.addClipEffect(clip, "ofx:com.techievideoeditor.tests.Invert"), "" + p.lastError)
                verify(p.setClipEffectParam(clip, 0, "amount", 0.85))
                p.selectedClip = clip
                p.seek(0.5)
                findChild(f.editor, "inspectorTab:Effects").clicked()
                settled(f.editor, "effectParam:0:amount")
                shot("video-plugins", dark)
                f.destroy(); wait(200)
            }
            EditingPreferences.reset()
        }
    }
}
