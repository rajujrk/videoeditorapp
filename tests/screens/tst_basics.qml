import QtQuick
import QtTest
import TechieVideoEditor.Theme
import TechieVideoEditor.Player
import TechieVideoEditor.Shell
import "screens.js" as Screens

// Home, recovery, the editor, proxies, stills, transparent video, markers and compound clips,
// keyframes, a compound clip's speed, and the app in Tamil.
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
    Component { id: homeComponent; HomeScreen { anchors.fill: parent } }
    Component { id: controllerComponent; ProjectController {} }

    TestCase {
        name: "ScreensBasics"
        when: windowShown
        readonly property string media: "file://" + screensMediaDir + "/"
        readonly property string samples: "file://" + samplesDir + "/"

        function editor() {
            const f = createTemporaryObject(editorComponent, root)
            waitForRendering(f)
            return f
        }
        function main(p) { return p.tracks.find((t) => t.main) }
        function park() { mouseMove(root, root.width - 4, root.height - 4) }
        function shot(name, dark, settle) {
            park()
            wait(settle === undefined ? 1500 : settle)
            grabImage(root.Window.window.contentItem).save(Screens.file(screensDir, name, dark))
        }
        function scrollTo(f, name) {
            const item = findChild(f.editor, name)
            verify(item, name)
            for (let a = item.parent; a; a = a.parent) {
                if (a.contentY === undefined || a.contentHeight === undefined)
                    continue
                a.contentY = Math.max(0, Math.min(item.mapToItem(a.contentItem, 0, 0).y - 12, a.contentHeight - a.height))
                return
            }
        }

        function test_scenes() {
            for (const dark of [true, false]) {
                Theme.dark = dark
                EditingPreferences.reset()
                Screens.palette(root.Window.window, Theme)

                // Home, nothing to recover.
                const folder = tempDir + "/tve-screens-recovery-" + Date.now()
                let c = controllerComponent.createObject(root)
                c.autosaveDirectory = folder + "-empty"
                let home = createTemporaryObject(homeComponent, root, { project: c })
                waitForRendering(home)
                shot("home", dark)
                // The on-device models, as offered on the first start.
                home.openModels()
                const models = findChild(home, "modelsDialog")
                tryVerify(() => models && models.opened)
                shot("models", dark)
                models.close()
                home.destroy(); c.destroy(); wait(100)

                // Home after a crash: unsaved work offered back.
                const lost = controllerComponent.createObject(root)
                lost.autosaveDirectory = folder
                verify(lost.importAndAppend(media + "night-street.mp4"))
                lost.autosaveNow()
                lost.destroy(); wait(50)
                c = controllerComponent.createObject(root)
                c.autosaveDirectory = folder
                home = createTemporaryObject(homeComponent, root, { project: c })
                waitForRendering(home)
                shot("home-recovery", dark)
                home.destroy(); c.destroy(); wait(100)

                // The editor: 4K footage with two effects.
                let f = editor()
                let p = f.project
                verify(p.importAndAppend(samples + "4k30_h264.mp4"), "" + p.lastError)
                let clip = Number(main(p).clips[0].id)
                verify(p.addClipEffect(clip, "tve.stylize.vignette"), "" + p.lastError)
                verify(p.addClipEffect(clip, "tve.glitch.rgb_split"), "" + p.lastError)
                p.selectedClip = clip
                findChild(f.editor, "inspectorTab:Effects").clicked()
                p.seek(2)
                shot("editor", dark, 2500)
                f.destroy(); wait(200)

                // Proxies for the 4K files, played with the proxy toggle on.
                f = editor()
                p = f.project
                compare(p.importMedia([samples + "4k30_h264.mp4", samples + "4k60_h264.mp4",
                                       samples + "test_av_1080p30.mp4"]), 3)
                verify(p.createProxy(p.media[0].id), "" + p.lastError)
                verify(p.createProxy(p.media[1].id), "" + p.lastError)
                tryVerify(() => Object.values(p.proxyStates).filter((s) => s.state === "ready").length >= 2, 120000)
                verify(p.addClip(p.media[0].id), "" + p.lastError)
                p.useProxies = true
                p.seek(1)
                p.selectedClip = 0
                shot("editor-proxies", dark, 2000)
                f.destroy(); wait(200)

                // Stills: a photo pushing in, then a HEIC.
                f = editor()
                p = f.project
                compare(p.importMedia([media + "photo.png", media + "photo.heic"]), 2)
                verify(p.addClip(p.media[0].id), "" + p.lastError)
                verify(p.addClip(p.media[1].id, main(p).id, 5), "" + p.lastError)
                clip = Number(main(p).clips[0].id)
                verify(p.panZoom(clip, { fromScale: 1, fromX: 0, fromY: 0, toScale: 1.35, toX: 0.05, toY: -0.08 }), "" + p.lastError)
                p.selectedClip = clip
                p.seek(2.5)
                findChild(f.editor, "inspectorTab:Video").clicked()
                shot("stills", dark)
                f.destroy(); wait(200)

                // Transparent video: a VP9 with alpha over a clip; the HEVC one in the bin too.
                f = editor()
                p = f.project
                verify(p.importAndAppend(media + "night-street.mp4"), "" + p.lastError)
                compare(p.importMedia([samples + "test_alpha_vp9.webm", samples + "test_alpha_hevc.mov"]), 2)
                verify(p.addClip(p.media[1].id, p.tracks.find((t) => t.name === "Video 2").id, 0))
                p.seek(1)
                p.selectedClip = 0
                shot("alpha-video", dark)
                f.destroy(); wait(200)

                // Markers, a group and a compound clip.
                f = editor()
                p = f.project
                verify(p.importAndAppend(media + "night-street.mp4"), "" + p.lastError)
                verify(p.importAndAppend(media + "portrait-wide.mp4"), "" + p.lastError)
                verify(p.importAndAppend(media + "night-street.mp4"), "" + p.lastError)
                const video2 = p.tracks.find((t) => t.name === "Video 2").id
                verify(p.addSticker("star_spin", video2, 0.5), "" + p.lastError)
                verify(p.addSticker("heart_pulse", video2, 5.5), "" + p.lastError)
                // The first two clips made one compound clip; the last sticker grouped with the last clip.
                let line = main(p).clips
                p.selectedClip = Number(line[0].id)
                p.toggleClipSelection(Number(line[1].id))
                verify(p.createCompound() > 0, "" + p.lastError)
                const stickers = p.tracks.find((t) => t.name === "Video 2").clips
                line = main(p).clips
                p.selectedClip = Number(stickers[stickers.length - 1].id)
                p.toggleClipSelection(Number(line[line.length - 1].id))
                verify(p.groupSelected(), "" + p.lastError)
                const one = p.addMarker(0.2, "Part one")
                verify(p.setMarker(one, { chapter: true }), "" + p.lastError)
                const two = p.addMarker(3.1, "Part two")
                verify(p.setMarker(two, { chapter: true, color: "blue" }), "" + p.lastError)
                const range = p.addMarker(5.4, "Best take")
                verify(p.setMarker(range, { duration: 1.5, color: "green" }), "" + p.lastError)
                p.selectedClip = Number(main(p).clips[0].id)
                p.seek(1)
                const flag = findChild(f.editor, "marker:" + two)
                verify(flag)
                mouseClick(flag)
                tryVerify(() => findChild(f.editor, "markerEditor").opened)
                shot("structure", dark)
                findChild(f.editor, "markerEditor").close()
                f.destroy(); wait(200)

                // Keyframes: grown from 1 s to 4 s (eased), faded out by 7 s.
                f = editor()
                p = f.project
                verify(p.importAndAppend(media + "night-street.mp4"), "" + p.lastError)
                verify(p.importAndAppend(media + "portrait-wide.mp4"), "" + p.lastError)
                verify(p.importAndAppend(media + "night-street.mp4"), "" + p.lastError)
                verify(p.addTitle({ text: "Keyframes" }, -1, 0), "" + p.lastError)
                clip = Number(main(p).clips[0].id)
                p.selectedClip = clip
                p.seek(1)
                verify(p.setClipVisual(clip, { scale: 0.6, x: -0.2 }), "" + p.lastError)
                verify(p.addKeyframes(clip, ["scale", "x"]), "" + p.lastError)
                p.seek(2.5)
                verify(p.setClipVisual(clip, { scale: 0.85, x: 0 }), "" + p.lastError) // (keyed: animated)
                verify(p.setKeyframeEasing(clip, "easeInOut"), "" + p.lastError)
                p.seek(1.5)
                findChild(f.editor, "inspectorTab:Video").clicked()
                wait(300)
                scrollTo(f, "keyframesSection")
                shot("keyframes", dark)
                f.destroy(); wait(200)

                // A compound clip at 2×, with dissolves on its cuts.
                f = editor()
                p = f.project
                verify(p.importAndAppend(samples + "test_av_1080p30.mp4"), "" + p.lastError)
                verify(p.importAndAppend(media + "night-street.mp4"), "" + p.lastError)
                verify(p.importAndAppend(media + "portrait-wide.mp4"), "" + p.lastError)
                verify(p.importAndAppend(samples + "gradients_1080p30_h264.mp4"), "" + p.lastError)
                const clips = main(p).clips
                p.selectedClip = Number(clips[1].id)
                p.toggleClipSelection(Number(clips[2].id))
                const compound = p.createCompound()
                verify(compound > 0)
                verify(p.setClipSpeed(compound, 2, false), "" + p.lastError)
                verify(p.addTransition("tve.transition.dissolve", compound), "" + p.lastError)
                const line2 = main(p).clips
                const at = line2.findIndex((c2) => Number(c2.id) === compound)
                const after = line2[at]
                verify(p.addTransition("tve.transition.dissolve", Number(line2[at + 1].id)), "" + p.lastError)
                p.selectedClip = compound
                findChild(f.editor, "timelinePanel").pixelsPerSecond = 110
                findChild(f.editor, "inspectorTab:Speed").clicked()
                p.seek(after.start + 0.4)
                shot("compound-speed", dark)
                f.destroy(); wait(200)

                // The app in Tamil: Home, and the editor with a title and the Audio library.
                Language.language = "ta"
                c = controllerComponent.createObject(root)
                c.autosaveDirectory = folder + "-empty"
                home = createTemporaryObject(homeComponent, root, { project: c })
                waitForRendering(home)
                shot("tamil-home", dark)
                home.destroy(); c.destroy(); wait(100)
                f = editor()
                p = f.project
                verify(p.importAndAppend(media + "night-street.mp4"), "" + p.lastError)
                verify(p.addTitle({ text: "வணக்கம்", size: 0.12 }, -1, 0), "" + p.lastError)
                findChild(f.editor, "libraryTab:Audio").clicked()
                findChild(f.editor, "inspectorTab:Video").clicked()
                p.seek(1)
                shot("tamil-editor", dark)
                f.destroy(); wait(200)
                Language.language = "en"
            }
            EditingPreferences.reset()
            Theme.dark = true
        }
    }
}
