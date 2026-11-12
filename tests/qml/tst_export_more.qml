import QtQuick
import QtTest
import TechieVideoEditor.Theme
import TechieVideoEditor.Player
import TechieVideoEditor.Shell

// More ways to export: aiming under a file size, several formats in one go, and Live Photos.
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
        name: "ExportMore"
        when: windowShown

        function media(name) { return "file://" + analysisMediaDir + "/" + name }
        function out(name) { return scratchDir + "/export-more/" + name }

        function init() {
            verify(testFiles.write(out(".folder"), "")) // (the folder the exports go in)
        }
        function createEditor() {
            const fixture = createTemporaryObject(editorComponent, root)
            verify(fixture)
            waitForRendering(fixture)
            finished.clear()
            finished.target = fixture.project
            return fixture
        }
        function probe(path) {
            const p = Qt.createQmlObject('import TechieVideoEditor.Player; ProjectController {}', testCase)
            const found = p.importMedia(["file://" + path]) === 1 ? p.media[0] : null
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
        function openDialog(f) {
            click(f.editor, "exportButton")
            const dialog = findChild(f.editor, "exportDialog")
            tryVerify(() => dialog.opened)
            return dialog
        }
        // Waits for `count` exports to end, all well.
        function waitForExports(count) {
            tryVerify(() => finished.count >= count, 60000, "exports never finished")
            for (let i = 0; i < count; ++i)
                compare(finished.signalArguments[i][0], true, finished.signalArguments[i][1])
        }

        function test_aiming_under_a_file_size() {
            const f = createEditor()
            const p = f.project
            verify(p.importAndAppend(media("noise.mov")))
            const dialog = openDialog(f)
            const content = dialog.contentItem
            dialog.file = "file://" + out("small.mp4")
            click(content, "exportQuality:size")
            const field = settled(content, "exportTargetSize")
            field.text = "1.5"
            compare(dialog.options.targetSize, 1.5)
            // The estimate is the size asked (less the room left for the file itself).
            const estimate = findChild(content, "exportEstimate").text
            verify(estimate.indexOf("1.5 MB") > 0, estimate)
            click(content, "exportStartButton")
            waitForExports(1)
            // Noise wants more; the encoder overshoots the first time (by a tenth or so on a clip
            // this short), so it is made again a little smaller: under 1.5 MB, using most of it.
            const bytes = testFiles.size(out("small.mp4"))
            verify(bytes > 1.2e6 && bytes <= 1.5e6, "file of " + bytes + " bytes")
            const m = probe(out("small.mp4"))
            verify(m)
            fuzzyCompare(m.duration, 2, 0.05)
            verify(m.hasAudio)

            // Full noise cannot be squeezed under 0.5 MB in 2 seconds: said so, and no file.
            finished.clear()
            verify(p.exportVideo("file://" + out("squeezed.mp4"), { targetSize: 0.5 }))
            finished.wait(60000)
            compare(finished.signalArguments[0][0], false)
            verify(finished.signalArguments[0][1].indexOf("Could not get it under 0.5 MB (it came out at 0.") === 0,
                   finished.signalArguments[0][1])
            verify(!testFiles.exists(out("squeezed.mp4")))

            // Twice the size, twice the bits a second (sound and pictures together: the size
            // over the length, less 3 % for the file itself).
            const at = (mb) => p.exportBitrate({ format: "mp4-h264", targetSize: mb })
            verify(Math.abs(at(2) - 2 * at(1)) < 1000, at(1) + " / " + at(2))
            verify(Math.abs(at(1) - 1e6 * 8 * 0.97 / 2) < 1000, String(at(1)))
            // Too small for the length: refused, saying how much it needs.
            verify(!p.exportVideo("file://" + out("tiny.mp4"), { targetSize: 0.02 }))
            verify(p.lastError.indexOf("0.02 MB is too small for 2.0 seconds of video") === 0, p.lastError)
            // Only H.264 and HEVC aim at a size; ProRes and sound files ignore it.
            compare(p.exportBitrate({ format: "mov-prores-proxy", targetSize: 0.5 }),
                    p.exportBitrate({ format: "mov-prores-proxy" }))
        }

        function test_several_formats_in_one_go() {
            const f = createEditor()
            const p = f.project
            verify(p.importAndAppend(media("scenes.mov")))
            const dialog = openDialog(f)
            const content = dialog.contentItem
            dialog.file = "file://" + out("Clip.mp4")
            // The format chosen is not offered again; a Live Photo is not offered at all.
            verify(!findChild(content, "exportAlso:mp4-h264"))
            verify(!findChild(content, "exportAlso:live-photo"))
            click(content, "exportAlso:mp4-hevc")
            click(content, "exportAlso:gif")
            click(content, "exportAlso:wav-16")
            click(content, "exportAlso:mov-prores-lt")
            click(content, "exportAlso:mov-prores-lt") // and off again
            compare(dialog.alsoFormats.join(","), "mp4-hevc,gif,wav-16")
            click(content, "exportStartButton")
            compare(dialog.alsoQueued, 3)
            compare(p.exportJobs.length, 4)
            waitForExports(4)
            tryCompare(dialog, "stage", "done")
            compare(findChild(content, "exportResult").text, "Saved Clip.mp4. 3 more formats are in the queue.")

            // The same name, each format's extension; the second MP4 named by its codec.
            const mp4 = probe(out("Clip.mp4"))
            const hevc = probe(out("Clip - HEVC.mp4"))
            const gif = probe(out("Clip.gif"))
            const wav = probe(out("Clip.wav"))
            verify(mp4 && hevc && gif && wav)
            compare([mp4.width, mp4.height], [160, 90])
            compare([hevc.width, hevc.height], [160, 90])
            fuzzyCompare(hevc.duration, 3, 0.05)
            fuzzyCompare(gif.duration, 3, 0.15)
            verify(!wav.hasVideo)
            verify(wav.hasAudio)

            // Asked again: names already queued or taken get a number.
            compare(p.exportAlso("file://" + out("Clip.mp4"), { format: "mp4-h264" }, ["mp4-hevc", "mp4-h264", "nope"]), 1)
            compare(p.exportJobs[p.exportJobs.length - 1].path, out("Clip - HEVC.mp4"))
            compare(p.exportAlso("file://" + out("Clip.mp4"), { format: "mp4-h264" }, ["mp4-hevc"]), 1)
            compare(p.exportJobs[p.exportJobs.length - 1].path, out("Clip 2.mp4"))
            waitForExports(6)
        }

        function test_a_live_photo() {
            const f = createEditor()
            const p = f.project
            verify(p.importAndAppend(media("long.mov")))
            p.seek(12)
            const dialog = openDialog(f)
            const content = dialog.contentItem
            const formats = findChild(content, "exportFormat")
            formats.currentIndex = p.exportFormats.findIndex((x) => x.id === "live-photo")
            dialog.formatChosen()
            dialog.file = "file://" + out("Moment.mov")
            // The key photo at the playhead, the movie 3 seconds around it; no range to pick.
            compare(settled(content, "exportLivePhotoKey").text,
                    "At the playhead (00:00:12:00), with the movie from 00:00:10:15 to 00:00:13:15")
            verify(!findChild(content, "exportRange").visible)
            verify(!findChild(content, "exportAlsoFormats").visible)
            click(content, "exportAddToPhotos")
            click(content, "exportStartButton")
            waitForExports(1)
            compare(findChild(content, "exportResult").text, "Saved Moment.mov and its key photo.")
            compare(p.exportJobs[0].format, "Live Photo")

            // A movie of 3 seconds and a key photo beside it, paired as Photos pairs them.
            const movie = probe(out("Moment.mov"))
            verify(movie)
            fuzzyCompare(movie.duration, 3, 0.05)
            verify(movie.hasAudio)
            const still = probe(out("Moment.heic"))
            verify(still && still.still)
            compare([still.width, still.height], [160, 90])
            const live = testFiles.livePhoto(out("Moment.heic"), out("Moment.mov"))
            verify(live.stillId.length === 36, live.stillId)
            compare(live.movieId, live.stillId)
            fuzzyCompare(live.stillTime, 1.5, 0.02)
            verify(live.accepted, "Photos does not take them for a Live Photo")
            // "Added to Photos": here, the test's stand-in library.
            verify(testFiles.exists(livePhotoLibrary + "/Moment.heic"))
            verify(testFiles.exists(livePhotoLibrary + "/Moment.mov"))
            // No leftovers.
            verify(!testFiles.exists(out("Moment.mov.part.mov")))

            // At the start (or end) of the timeline the 3 seconds stay inside it.
            compare(p.exportRange({ format: "live-photo", keyTime: 0.4 }).from, 0)
            compare(p.exportRange({ format: "live-photo", keyTime: 0.4 }).to, 3)
            compare(p.exportRange({ format: "live-photo", keyTime: 39.9 }).from, 37)
            compare(p.exportRange({ format: "live-photo", keyTime: 39.9 }).to, 40)
            verify(p.exportVideo("file://" + out("Start.mov"), { format: "live-photo", keyTime: 0.4 }))
            waitForExports(2)
            fuzzyCompare(testFiles.livePhoto(out("Start.heic"), out("Start.mov")).stillTime, 0.4, 0.02)
            // Not added to Photos unless asked.
            verify(!testFiles.exists(livePhotoLibrary + "/Start.mov"))
        }
    }
}
