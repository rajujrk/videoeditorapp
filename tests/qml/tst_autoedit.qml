import QtQuick
import QtTest
import TechieVideoEditor.Theme
import TechieVideoEditor.Player
import TechieVideoEditor.Shell

// Made for you, through the real editor: an instant video from pictures and videos, a
// highlight montage of a long video (its loud stretch in it; Claude's order when asked, from
// the mock), and smart shorts (tall, each a range marker, following the subject). The media
// is measured for real; long.mov is 40 s in four shots, loud from 22 to 30 s (main.cpp).
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

    TestCase {
        id: testCase
        name: "AutoEdit"
        when: windowShown

        function createEditor() {
            const fixture = createTemporaryObject(editorComponent, root)
            verify(fixture)
            waitForRendering(fixture)
            return fixture
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
            for (let a = item; a && a !== root; a = a.parent)
                waitForPolish(a, 1000)
            return item
        }
        function click(item) {
            for (let a = item.parent; a; a = a.parent) {
                if (a.contentY === undefined || a.contentHeight === undefined || a.contentItem === undefined)
                    continue
                const top = item.mapToItem(a.contentItem, 0, 0).y
                if (top < a.contentY || top + item.height > a.contentY + a.height) {
                    a.contentY = Math.max(0, Math.min(top - 20, a.contentHeight - a.height))
                    waitForPolish(a, 1000)
                }
                break
            }
            mouseMove(item)
            mouseClick(item)
        }
        function press(item) {
            verify(item.enabled, item.objectName + " is disabled")
            item.clicked()
        }
        function mediaNamed(p, name) { return p.media.find((m) => m.name === name) }
        function track(p, name) { return p.tracks.find((t) => t.name === name) }
        function finished(p) {
            let result = null
            p.autoEditFinished.connect((kind, ok, message) => result = { kind: kind, ok: ok, message: message })
            return () => result
        }
        function init() {
            const f = createEditor()
            f.project.cloud.removeKey("anthropic")
        }

        function test_instant_video_from_pictures_and_videos() {
            const f = createEditor()
            const p = f.project
            compare(p.importMedia(["file://" + analysisMediaDir + "/scenes.mov", "file://" + analysisMediaDir + "/talk.mov",
                                   "file://" + stillsDir + "/halves.png", "file://" + stillsDir + "/disc.png"]), 4)
            press(settled(f.editor, "libraryTab:Templates"))
            const page = settled(f.editor, "templatesPage")
            click(settled(page, "makeFor:instant"))
            const panel = findChild(f.editor, "autoEditPanel")
            tryCompare(panel, "opened", true)
            compare(panel.chosen.length, 4)
            click(settled(panel.contentItem, "autoStyle:upbeat"))
            click(settled(panel.contentItem, "autoSeconds:15"))
            settled(panel.contentItem, "autoTitle").text = "Weekend"
            const result = finished(p)
            click(settled(panel.contentItem, "makeAutoEdit"))
            tryVerify(() => result() !== null, 30000)
            verify(result().ok, result().message)
            compare(result().kind, "instant")
            compare(p.templateInfo.id, "auto.upbeat")
            compare(p.canvasWidth, 1920)
            compare(p.canvasHeight, 1080)
            // Cut on the beat of 124 BPM, about 15 s in all, pictures and videos both in it.
            const beat = 60 / 124
            const clips = track(p, "Video 1").clips
            let total = 0
            for (const clip of clips) {
                const beats = clip.duration / beat
                verify(Math.abs(beats - Math.round(beats)) < 0.02, clip.duration)
                total += clip.duration
            }
            // (Short of material at this style's cuts, they run longer.)
            verify(total >= 15 && total < 15 + 8, total)
            verify(clips.some((c) => c.path.endsWith("halves.png")))
            verify(clips.some((c) => c.path.endsWith(".mov")))
            // The style: flashes between cuts, its look on each, its music, the title.
            compare(clips.filter((c) => c.transition && c.transition.transitionId === "tve.transition.flash").length, clips.length - 1)
            p.selectedClip = clips[0].id
            compare(p.selectedClipInfo.look.id, "vivid")
            verify(track(p, "Music").clips.length === 1)
            verify(track(p, "Titles").clips.some((c) => c.label === "Weekend"))
            // A template project: the storyboard can change any shot.
            compare(p.templateInfo.slots.length, clips.length)
            verify(p.templateInfo.slots.every((s) => s.filled))
            compare(p.templateInfo.texts[0].text, "Weekend")
        }

        function test_highlight_montage_keeps_the_loud_part() {
            const f = createEditor()
            const p = f.project
            compare(p.importMedia(["file://" + analysisMediaDir + "/long.mov", "file://" + stillsDir + "/halves.png"]), 2)
            const result = finished(p)
            // Pictures do not count for a montage.
            verify(!p.makeAutoEdit({ kind: "montage", media: [mediaNamed(p, "halves.png").id] }))
            verify(p.makeAutoEdit({ kind: "montage", media: [mediaNamed(p, "long.mov").id, mediaNamed(p, "halves.png").id],
                                    style: "cinematic", shape: "9:16", seconds: 15 }))
            tryVerify(() => result() !== null, 60000)
            verify(result().ok, result().message)
            compare(p.canvasWidth, 1080)
            compare(p.canvasHeight, 1920)
            const clips = track(p, "Video 1").clips
            verify(clips.length >= 2)
            verify(clips.every((c) => c.path.endsWith("long.mov")))
            // In the order they happen, and the loud stretch among them.
            for (let i = 1; i < clips.length; ++i)
                verify(clips[i].sourceIn > clips[i - 1].sourceIn)
            verify(clips.some((c) => c.sourceIn >= 21.5 && c.sourceIn < 30), JSON.stringify(clips.map((c) => c.sourceIn)))
            // A slow style: cuts of at most eight beats at 72 BPM.
            verify(clips.every((c) => c.duration <= 8 * 60 / 72 + 0.01))

            // With Claude choosing (the mock turns the order round): another set of moments.
            const firstPick = clips.map((c) => c.sourceIn.toFixed(2)).join(",")
            verify(p.cloud.setKey("anthropic", "test-claude-key"))
            compare(p.importMedia(["file://" + analysisMediaDir + "/long.mov"]), 1)
            const again = finished(p)
            verify(p.makeAutoEdit({ kind: "montage", media: [mediaNamed(p, "long.mov").id], style: "cinematic", shape: "9:16",
                                    seconds: 15, rank: true }))
            tryVerify(() => again() !== null, 60000)
            verify(again().ok)
            compare(again().message, "Moments ranked by Claude")
            const request = p.cloud.mockRequests().filter((r) => r.path.indexOf("/v1/messages") === 0).pop()
            verify(request.body.messages[0].content.indexOf("<moments>") >= 0)
            compare(request.body.output_config.format.type, "json_schema")
            verify(track(p, "Video 1").clips.map((c) => c.sourceIn.toFixed(2)).join(",") !== firstPick)
        }

        function test_smart_shorts_are_tall_ranges_following_the_subject() {
            const f = createEditor()
            const p = f.project
            compare(p.importMedia(["file://" + analysisMediaDir + "/long.mov", "file://" + analysisMediaDir + "/talk.mov",
                                   "file://" + stillsDir + "/halves.png"]), 3)
            // Not from a picture, nor a video too short for them.
            verify(!p.makeShorts({ media: mediaNamed(p, "halves.png").id }))
            verify(!p.makeShorts({ media: mediaNamed(p, "talk.mov").id, seconds: 30 }))
            press(settled(f.editor, "libraryTab:Templates"))
            const page = settled(f.editor, "templatesPage")
            click(settled(page, "makeFor:shorts"))
            const panel = findChild(f.editor, "autoEditPanel")
            tryCompare(panel, "opened", true)
            compare(panel.mode, "shorts")
            click(settled(panel.contentItem, "autoMedia:long.mov"))
            compare(panel.chosen, [mediaNamed(p, "long.mov").id])
            click(settled(panel.contentItem, "autoSeconds:15"))
            panel.count = 2
            // No transcript: captions can't be had.
            verify(!settled(panel.contentItem, "shortsCaptions").enabled)
            const result = finished(p)
            click(settled(panel.contentItem, "makeAutoEdit"))
            tryVerify(() => result() !== null, 90000)
            verify(result().ok, result().message)
            verify(result().message.indexOf("2 shorts") === 0, result().message)
            verify(result().message.indexOf("No captions") >= 0, result().message)
            compare(p.canvasWidth, 1080)
            compare(p.canvasHeight, 1920)
            // Each short a range marker over its clip.
            const clips = track(p, "Video 1").clips
            compare(clips.length, 2)
            compare(p.markers.length, 2)
            for (let i = 0; i < 2; ++i) {
                compare(p.markers[i].time, clips[i].start)
                compare(p.markers[i].duration, clips[i].duration)
                compare(p.markers[i].note, "Short " + (i + 1))
                verify(clips[i].duration >= 9 - 0.01 && clips[i].duration <= 21 + 0.01, clips[i].duration)
            }
            // The best first: the loud stretch.
            verify(clips[0].sourceIn + clips[0].duration > 22 && clips[0].sourceIn < 30, clips[0].sourceIn)
            // Following the subject (position keyframes), or filling the frame from the middle.
            p.selectedClip = clips[0].id
            verify(clips[0].keyframeTimes.length > 0 || p.selectedClipInfo.visual.cropLeft > 0.3)
            verify(settled(panel.contentItem, "exportShorts"))
            // A short moved later in the video still follows the subject, from there.
            if (clips[1].keyframeTimes.length > 0) {
                verify(p.setTemplateSlotStart(1, 2))
                compare(p.undoText, "Fill Slot")
                const moved = track(p, "Video 1").clips[1]
                verify(Math.abs(moved.sourceIn - 2) < 0.01)
                verify(moved.keyframeTimes.length > 0)
            }

            // Claude's order and titles (the mock: the other way round, "Moment N").
            verify(p.cloud.setKey("anthropic", "test-claude-key"))
            compare(p.importMedia(["file://" + analysisMediaDir + "/long.mov"]), 1)
            const again = finished(p)
            verify(p.makeShorts({ media: mediaNamed(p, "long.mov").id, count: 2, seconds: 15, rank: true }))
            tryVerify(() => again() !== null, 90000)
            verify(again().ok)
            verify(p.markers.every((m) => /^Short \d: Moment \d+$/.test(m.note)), JSON.stringify(p.markers.map((m) => m.note)))
        }
    }
}
