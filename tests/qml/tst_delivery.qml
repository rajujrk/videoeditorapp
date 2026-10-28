import QtQuick
import QtTest
import TechieVideoEditor.Theme
import TechieVideoEditor.Player
import TechieVideoEditor.Shell

// Delivery: export formats and presets, ranges, and the render queue, through the real editor.
Item {
    id: root
    width: 1400
    height: 900

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

    SignalSpy { id: finished; signalName: "exportFinished" }

    TestCase {
        id: testCase
        name: "Delivery"
        when: windowShown

        readonly property string av: "file://" + samplesDir + "/test_av_1080p30.mp4" // 2 s with sound
        readonly property string longClip: "file://" + samplesDir + "/4k60_h264.mp4"  // 20 s of 4K60
        function out(name) { return "file://" + tempDir + "/tve_delivery_" + name }

        function createEditor() {
            const fixture = createTemporaryObject(editorComponent, root)
            verify(fixture)
            waitForRendering(fixture)
            finished.clear()
            finished.target = fixture.project
            return fixture
        }
        // What a fresh import finds in a file: {width, height, duration, hasVideo, hasAudio}, or null.
        function probe(url) {
            const p = Qt.createQmlObject('import TechieVideoEditor.Player; ProjectController {}', testCase)
            const found = p.importMedia([url]) === 1 ? p.media[0] : null
            p.destroy()
            return found
        }
        function settled(parent, name) {
            let item = null
            let last = null
            tryVerify(() => {
                item = findChild(parent, name)
                if (!item || !item.visible)
                    return false
                const p = item.mapToItem(root, 0, 0)
                const stable = last !== null && p.x === last.x && p.y === last.y
                last = p
                return stable
            }, 3000, name + " never settled")
            return item
        }
        function click(parent, name) {
            const item = settled(parent, name)
            mouseMove(item)
            mouseClick(item)
        }
        function jobNamed(p, name) { return p.exportJobs.find((j) => j.name === "tve_delivery_" + name) }
        function waitForJobs(p, count) {
            tryVerify(() => finished.count >= count, 60000, "exports did not finish")
            for (let i = 0; i < finished.count; ++i)
                compare(finished.signalArguments[i][0], true, finished.signalArguments[i][1])
        }

        function test_presets_and_formats() {
            const f = createEditor()
            const p = f.project
            compare(p.exportFormats.length, 13)
            compare(p.exportPresets.length, 11)
            for (const preset of p.exportPresets) {
                verify(preset.title.length > 0, preset.id)
                verify(p.exportFormats.some((fmt) => fmt.id === preset.format), preset.id + " names a known format")
            }
            p.importAndAppend(av)
            // A preset's values come first; what is given overrides them.
            const gif = p.exportRange({ preset: "gif" })
            fuzzyCompare(gif.duration, 2, 0.05)
            verify(p.exportBitrate({ preset: "master" }) > 100e6) // ProRes HQ at 1080p
            verify(p.exportBitrate({ preset: "small" }) < p.exportBitrate({ preset: "web-1080" }))
            fuzzyCompare(p.exportBitrate({ format: "wav-24" }), 2304000, 1)
            verify(p.exportBitrate({ format: "mp4-h264", audio: false }) < p.exportBitrate({ format: "mp4-h264" }))
            verify(!p.exportVideo(out("bad.mp4"), { preset: "nope" }))
            verify(p.lastError.indexOf("preset") >= 0)
            verify(!p.exportVideo(out("bad.gif"), { format: "gif", width: 5000, height: 100 }))
            verify(p.lastError.indexOf("4096") >= 0)

            // In the dialog: picking a preset sets the format, size, frame rate and file type.
            click(f.editor, "exportButton")
            const dialog = findChild(f.editor, "exportDialog")
            tryVerify(() => dialog.opened)
            const content = dialog.contentItem
            const presets = findChild(content, "exportPreset")
            presets.currentIndex = presets.model.findIndex((m) => m.id === "gif")
            presets.activated(presets.currentIndex)
            compare(dialog.options.format, "gif")
            compare(dialog.options.height, 360)
            compare(dialog.options.frameRate, 15)
            verify(String(dialog.file).endsWith(".gif"))
            verify(!findChild(content, "exportQuality:high").visible) // GIFs have no bitrate
            verify(findChild(content, "exportPresetDescription").text.indexOf("looping") >= 0)
            presets.currentIndex = presets.model.findIndex((m) => m.id === "vertical")
            presets.activated(presets.currentIndex)
            compare(dialog.options.width, 1080)
            compare(dialog.options.height, 1920)
            compare(dialog.options.fill, true)
            verify(String(dialog.file).endsWith(".mp4"))
            presets.currentIndex = presets.model.findIndex((m) => m.id === "audio-wav")
            presets.activated(presets.currentIndex)
            verify(!findChild(content, "exportSize").visible) // sound only
            verify(String(dialog.file).endsWith(".wav"))
            verify(findChild(content, "exportEstimate").text.indexOf("MB") > 0)
            // Picking a format by hand turns the preset back to Custom.
            const formats = findChild(content, "exportFormat")
            formats.currentIndex = formats.model.findIndex((m) => m.id === "mov-prores-422")
            formats.activated(formats.currentIndex)
            compare(presets.currentIndex, 0)
            verify(String(dialog.file).endsWith(".mov"))
            dialog.close()
        }

        function test_every_format_exports_and_reads_back() {
            const f = createEditor()
            const p = f.project
            p.importAndAppend(av)
            const formats = [["mp4-hevc", "hevc.mp4", true, true], ["mov-h264", "h264.mov", true, true],
                             ["mov-prores-proxy", "proxy.mov", true, true], ["mov-prores-4444", "4444.mov", true, true],
                             ["gif", "anim.gif", true, false], ["m4a", "sound.m4a", false, true],
                             ["wav-16", "sound16.wav", false, true], ["flac", "sound.flac", false, true]]
            for (const [format, name] of formats)
                verify(p.exportVideo(out(name), { format: format, width: 320, height: 180 }), format + ": " + p.lastError)
            waitForJobs(p, formats.length)
            for (const [format, name, video, sound] of formats) {
                const media = probe(out(name))
                verify(media, name + " does not open")
                compare(media.hasVideo, video, name)
                compare(media.hasAudio, sound, name)
                if (video) {
                    compare(media.width, 320, name)
                    compare(media.height, 180, name)
                }
                fuzzyCompare(media.duration, 2.0, 0.1)
            }
            // Without sound, a video has none.
            verify(p.exportVideo(out("silent.mp4"), { width: 320, height: 180, audio: false }))
            waitForJobs(p, formats.length + 1)
            compare(probe(out("silent.mp4")).hasAudio, false)
            compare(p.exportJobs.filter((j) => j.state === "done").length, formats.length + 1)
        }

        function test_ranges_and_marker_files() {
            const f = createEditor()
            const p = f.project
            p.importAndAppend(av)
            // Part of the timeline: custom, a marker's range, the selected clips.
            verify(p.exportVideo(out("custom.mp4"), { width: 320, height: 180, range: "custom", from: 0.5, to: 1.25 }))
            const intro = p.addMarker(0.2, "Intro")
            verify(p.setMarker(intro, { duration: 0.6 }))
            const outro = p.addMarker(1.2, "Outro: the end")
            verify(p.setMarker(outro, { duration: 0.5 }))
            p.addMarker(1.0, "no length") // not a range
            verify(p.exportVideo(out("marker.mp4"), { width: 320, height: 180, range: "marker", markerId: intro }))
            fuzzyCompare(p.exportRange({ range: "marker", markerId: outro }).duration, 0.5, 0.01)
            p.selectedClip = 0
            verify(!p.exportVideo(out("none.mp4"), { range: "selection" }))
            verify(p.lastError.indexOf("Select") === 0, p.lastError)
            p.selectedClip = p.tracks.find((t) => t.main).clips[0].id
            fuzzyCompare(p.exportRange({ range: "selection" }).duration, 2.0, 0.01)
            verify(!p.exportVideo(out("backwards.mp4"), { range: "custom", from: 1.5, to: 1.0 }))
            verify(p.lastError.indexOf("nothing to export") >= 0, p.lastError)
            // One file per marker range, named by the notes.
            compare(p.exportMarkerRanges("file://" + tempDir, { format: "m4a" }), 2)
            waitForJobs(p, 4)
            fuzzyCompare(probe(out("custom.mp4")).duration, 0.75, 0.07)
            fuzzyCompare(probe(out("marker.mp4")).duration, 0.6, 0.07)
            const names = p.exportJobs.map((j) => j.name)
            verify(names.indexOf(p.name + " - Intro.m4a") >= 0, names.join(", "))
            verify(names.indexOf(p.name + " - Outro- the end.m4a") >= 0, names.join(", "))
            const outroFile = "file://" + tempDir + "/" + p.name + " - Outro- the end.m4a"
            fuzzyCompare(probe(outroFile).duration, 0.5, 0.05)
        }

        function test_the_queue_runs_in_order_pauses_and_cancels() {
            const f = createEditor()
            const p = f.project
            if (probe(longClip) === null)
                skip("4k60_h264.mp4 missing: run tools/make_test_media.sh")
            p.importAndAppend(longClip)
            verify(p.exportVideo(out("first.mp4"), {}))
            verify(p.exportVideo(out("second.mp4"), { width: 320, height: 180, range: "custom", from: 0, to: 1 }))
            verify(p.exportVideo(out("third.mp4"), { width: 320, height: 180, range: "custom", from: 0, to: 1 }))
            verify(!p.exportVideo(out("second.mp4"), {})) // already queued
            compare(jobNamed(p, "first.mp4").state, "running")
            compare(jobNamed(p, "second.mp4").state, "waiting")
            compare(jobNamed(p, "third.mp4").state, "waiting")

            // The top bar's queue button counts what is left; its panel lists the jobs.
            const queueButton = settled(f.editor, "renderQueueButton")
            compare(queueButton.pending, 3)
            click(f.editor, "renderQueueButton")
            const panel = findChild(f.editor, "renderQueuePanel")
            tryVerify(() => panel.opened)
            tryVerify(() => findChild(panel.contentItem, "exportJob:" + jobNamed(p, "third.mp4").id) !== null)

            // Pause holds the running export where it is.
            tryVerify(() => p.exportProgress > 0.01, 20000)
            p.pauseExport()
            verify(p.exportPaused)
            wait(300)
            const held = p.exportProgress
            wait(700)
            compare(p.exportProgress, held)
            verify(findChild(f.editor, "exportButton").text.indexOf("Paused") === 0)
            p.resumeExport()
            tryVerify(() => p.exportProgress > held, 10000)

            // A waiting job leaves the queue; cancelling the running one removes its file and
            // the next starts.
            p.cancelExport(jobNamed(p, "third.mp4").id)
            compare(jobNamed(p, "third.mp4").state, "cancelled")
            p.cancelExport()
            finished.wait(20000)
            compare(finished.signalArguments[0][0], false)
            compare(finished.signalArguments[0][1], "")
            compare(finished.signalArguments[0][2], jobNamed(p, "first.mp4").id)
            compare(jobNamed(p, "first.mp4").state, "cancelled")
            verify(probe(out("first.mp4")) === null, "a cancelled export left a file")
            tryVerify(() => jobNamed(p, "second.mp4").state === "done", 20000)
            compare(finished.count, 2)
            verify(probe(out("third.mp4")) === null, "a cancelled job ran")
            compare(queueButton.pending, 0)

            // Finished jobs can be taken off the list.
            verify(p.removeExportJob(jobNamed(p, "first.mp4").id))
            p.clearFinishedExports()
            compare(p.exportJobs.length, 0)
        }

        function test_add_to_queue_from_the_dialog() {
            const f = createEditor()
            const p = f.project
            p.importAndAppend(av)
            click(f.editor, "exportButton")
            const dialog = findChild(f.editor, "exportDialog")
            tryVerify(() => dialog.opened)
            const content = dialog.contentItem
            dialog.file = out("queued.mov")
            const formats = findChild(content, "exportFormat")
            formats.currentIndex = formats.model.findIndex((m) => m.id === "mov-prores-lt")
            formats.activated(formats.currentIndex)
            findChild(content, "exportSize").currentIndex = 1
            click(content, "exportQueueButton")
            tryVerify(() => !dialog.visible) // queued: back to editing
            compare(p.exportJobs.length, 1)
            compare(p.exportJobs[0].format, "MOV · ProRes LT")
            finished.wait(30000)
            compare(finished.signalArguments[0][0], true, finished.signalArguments[0][1])
            const media = probe(out("queued.mov"))
            verify(media)
            compare(media.height, 720)

            // Several marker ranges queue one file each.
            verify(p.setMarker(p.addMarker(0, "A"), { duration: 0.5 }))
            verify(p.setMarker(p.addMarker(1, "B"), { duration: 0.5 }))
            click(f.editor, "exportButton")
            tryVerify(() => dialog.opened)
            const ranges = findChild(content, "exportRange")
            ranges.currentIndex = dialog.ranges.findIndex((r) => r.range === "each")
            verify(!findChild(content, "exportStartButton").visible)
            compare(findChild(content, "exportQueueButton").text, "Queue 2 files")
            click(content, "exportQueueButton")
            tryVerify(() => !dialog.visible)
            compare(p.exportJobs.length, 3)
            tryVerify(() => finished.count === 3, 30000)
        }
    }
}
